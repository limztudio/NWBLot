// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_CsgEnabledDefineName = "NWB_CSG_ENABLED";
inline constexpr AStringView s_CsgEnabledDefineAssignment = "NWB_CSG_ENABLED=1";


[[nodiscard]] static bool CsgClipPipeline(const MaterialPipelineKey& pipelineKey){
    return pipelineKey.pass == MaterialPipelinePass::Opaque && pipelineKey.csgMode != MaterialPipelineCsgMode::None;
}

[[nodiscard]] static AStringView VariantSegmentDefineName(const AStringView segment){
    const usize equalPos = segment.find('=');
    return equalPos == AStringView::npos ? AStringView{} : segment.substr(0u, equalPos);
}

[[nodiscard]] static bool BuildCsgClipShaderVariantName(const AStringView baseVariant, Core::GraphicsString& outVariant){
    outVariant.clear();
    if(baseVariant.empty())
        return false;
    if(baseVariant == Core::ShaderArchive::s_DefaultVariant){
        outVariant.assign(s_CsgEnabledDefineAssignment.data(), s_CsgEnabledDefineAssignment.size());
        return true;
    }

    outVariant.reserve(baseVariant.size() + 1u + s_CsgEnabledDefineAssignment.size());
    bool insertedCsgDefine = false;
    usize begin = 0u;
    while(begin < baseVariant.size()){
        usize segmentEnd = baseVariant.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = baseVariant.size();

        const AStringView segment = baseVariant.substr(begin, segmentEnd - begin);
        const AStringView defineName = VariantSegmentDefineName(segment);
        if(defineName.empty())
            return false;
        if(!insertedCsgDefine && s_CsgEnabledDefineName < defineName){
            if(!outVariant.empty())
                outVariant += ';';
            outVariant += s_CsgEnabledDefineAssignment;
            insertedCsgDefine = true;
        }

        if(!outVariant.empty())
            outVariant += ';';
        outVariant += segment;
        begin = segmentEnd + 1u;
    }

    if(!insertedCsgDefine){
        if(!outVariant.empty())
            outVariant += ';';
        outVariant += s_CsgEnabledDefineAssignment;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::splitMaterialTypedBytesByClass(
    const Material& material,
    const Name& materialPath,
    MaterialTypedByteVector& outConstantTypedBytes,
    MaterialTypedByteVector& outMutableDefaultTypedBytes
){
    outConstantTypedBytes.clear();
    outMutableDefaultTypedBytes.clear();

    const auto& packedTypedBytes = material.typedBlockBytes();
    usize sourceByteOffset = 0u;
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        if(!IsValidMaterialBlockClass(block.blockClass)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has invalid typed material block class")
                , StringConvert(materialPath.c_str())
            );
            return false;
        }
        if((block.byteSize & (sizeof(u32) - 1u)) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block size is not u32 aligned")
                , StringConvert(materialPath.c_str())
            );
            return false;
        }
        if(sourceByteOffset > packedTypedBytes.size() || block.byteSize > packedTypedBytes.size() - sourceByteOffset){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block bytes exceed packed data")
                , StringConvert(materialPath.c_str())
            );
            return false;
        }

        MaterialTypedByteVector& targetTypedBytes = block.blockClass == MaterialBlockClass::MaterialConstant
            ? outConstantTypedBytes
            : outMutableDefaultTypedBytes
        ;
        targetTypedBytes.insert(
            targetTypedBytes.end(),
            packedTypedBytes.begin() + sourceByteOffset,
            packedTypedBytes.begin() + sourceByteOffset + block.byteSize
        );
        sourceByteOffset += block.byteSize;
    }
    if(sourceByteOffset != packedTypedBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material layout size does not match packed data")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo != m_materialState.m_surfaceInfos.end()){
        outInfo = &foundInfo.value();
        return true;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Material::AssetTypeName(), materialPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load material '{}'"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Material::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not a material"), StringConvert(materialPath.c_str()));
        return false;
    }

    const Material& material = static_cast<const Material&>(*loadedAsset);
    const auto& typedBlockBytes = material.typedBlockBytes();

    MaterialSurfaceInfo createdInfo(m_arena);
    createdInfo.materialName = materialPath;
    if(material.shaderVariant().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has empty shader variant")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    createdInfo.shaderVariant.assign(material.shaderVariant().data(), material.shaderVariant().size());

    (void)material.findShaderForStage(Core::ShaderType::PixelStage, createdInfo.pixelShader);
    (void)material.findShaderForStage(Core::ShaderType::MeshStage, createdInfo.meshShader);

    if(!material.materialInterface()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing required material interface")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    createdInfo.materialInterface = material.materialInterface();
    if(material.typedLayoutHash() == 0u || typedBlockBytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing typed material data")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(material.typedLayoutBlocks().size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block count exceeds u32 limits")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(material.typedLayoutFields().size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material field count exceeds u32 limits")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(typedBlockBytes.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material data exceeds u32 limits")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }

    createdInfo.typedLayoutHash = material.typedLayoutHash();
    createdInfo.typedLayoutBlocks.assign(material.typedLayoutBlocks().begin(), material.typedLayoutBlocks().end());
    createdInfo.typedLayoutFields.assign(material.typedLayoutFields().begin(), material.typedLayoutFields().end());
    if(!splitMaterialTypedBytesByClass(
        material,
        materialPath,
        createdInfo.constantTypedBytes,
        createdInfo.mutableDefaultTypedBytes
    ))
        return false;

    createdInfo.transparent = material.transparent();
    createdInfo.twoSided = material.twoSided();

    auto result = m_materialState.m_surfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    outInfo = &it.value();
    NWB_ASSERT(outInfo);
    return true;
}

bool RendererSystem::createRendererPipeline(
    const MaterialSurfaceInfo& materialInfo,
    const MaterialPipelineKey& pipelineKey,
    Core::Framebuffer* framebuffer,
    MaterialPipelineResources*& outResources
){
    outResources = nullptr;

    if(!framebuffer)
        return false;

    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    NWB_ASSERT(materialKey);

    auto [it, inserted] = m_materialState.m_pipelines.try_emplace(pipelineKey);
    MaterialPipelineResources& resources = it.value();
    switch(resources.renderPath){
    case RenderPath::MeshShader:
        if(resources.meshletPipeline){
            outResources = &resources;
            return true;
        }
        break;
    case RenderPath::ComputeEmulation:
        if(resources.computePipeline && resources.emulationPipeline){
            outResources = &resources;
            return true;
        }
        break;
    default:
        break;
    }

    auto removeFailedEntry = [&](){
        if(inserted)
            m_materialState.m_pipelines.erase(it);
    };
    auto failMaterialPipeline = [&](){
        removeFailedEntry();
        return false;
    };

    if(materialInfo.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has empty shader variant")
            , StringConvert(materialKey.c_str())
        );
        return false;
    }
    const AStringView shaderVariant(materialInfo.shaderVariant.data(), materialInfo.shaderVariant.size());
    Core::GraphicsString csgShaderVariant(m_arena);
    const bool csgClipPipeline = __hidden_material_resources::CsgClipPipeline(pipelineKey);
    if(csgClipPipeline && !__hidden_material_resources::BuildCsgClipShaderVariantName(shaderVariant, csgShaderVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG shader variant for material '{}'"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }
    const AStringView pixelShaderVariant = csgClipPipeline
        ? AStringView(csgShaderVariant)
        : shaderVariant
    ;
    const AStringView meshShaderVariant = csgClipPipeline
        ? AStringView(csgShaderVariant)
        : shaderVariant
    ;

    const bool hasPixelShader = materialInfo.pixelShader.valid();
    const bool hasMeshShader = materialInfo.meshShader.valid();
    Core::ShaderHandle passPixelShader;

    switch(pass){
    case MaterialPipelinePass::Opaque:
        break;
    case MaterialPipelinePass::AvboitOccupancy:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitState.m_occupancyPixelShader;
        break;
    case MaterialPipelinePass::AvboitExtinction:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitState.m_extinctionPixelShader;
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitState.m_accumulatePixelShader;
        break;
    default:
        break;
    }

    auto* device = m_graphics.getDevice();
    const Core::RenderState renderState = ECSRenderDetail::BuildRenderStateForPass(pass, materialInfo.twoSided);

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!createMeshShaderResources())
            return false;
        if(!loadShader(resources.meshShader, materialInfo.meshShader.name(), meshShaderVariant, Core::ShaderType::Mesh, "ECSRender_RendererMesh"))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!loadShader(resources.pixelShader, materialInfo.pixelShader.name(), pixelShaderVariant, Core::ShaderType::Pixel, "ECSRender_RendererPS"))
                return false;
        }
        else
            resources.pixelShader = passPixelShader;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        pipelineDesc.addBindingLayout(m_drawState.m_meshBindingLayout);
        if(csgClipPipeline){
            if(!createCsgClipResources())
                return false;
            pipelineDesc.addBindingLayout(m_csgState.m_clipBindingLayout);
        }
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            pipelineDesc.addBindingLayout(m_avboitState.m_occupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            pipelineDesc.addBindingLayout(m_avboitState.m_extinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            pipelineDesc.addBindingLayout(m_avboitState.m_accumulateBindingLayout);
            break;
        case MaterialPipelinePass::Opaque:
        default:
            break;
        }

        resources.meshletPipeline = device->createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
        if(!resources.meshletPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        resources.renderPath = RenderPath::MeshShader;
        return true;
    };

    auto tryBuildComputePipeline = [&]() -> bool{
        if(!createComputeEmulationResources())
            return false;
        const Name& meshComputeArchiveStageName = MaterialShaderStageNames::s_MeshComputeArchiveStageName;
        if(!loadShader(
            resources.computeShader,
            materialInfo.meshShader.name(),
            meshShaderVariant,
            Core::ShaderType::Compute,
            "ECSRender_RendererCS",
            &meshComputeArchiveStageName
        ))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!loadShader(resources.pixelShader, materialInfo.pixelShader.name(), pixelShaderVariant, Core::ShaderType::Pixel, "ECSRender_RendererPS"))
                return false;
        }
        else{
            resources.pixelShader = passPixelShader;
        }
        if(!createEmulationViewResources())
            return false;

        Core::ComputePipelineDesc computeDesc;
        computeDesc.setComputeShader(resources.computeShader);
        computeDesc.addBindingLayout(m_drawState.m_computeBindingLayout);
        if(csgClipPipeline){
            if(!createCsgClipResources())
                return false;
            computeDesc.addBindingLayout(m_csgState.m_clipBindingLayout);
        }
        resources.computePipeline = device->createComputePipeline(computeDesc);
        if(!resources.computePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_drawState.m_emulationInputLayout);
        emulationDesc.setVertexShader(m_drawState.m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            emulationDesc.addBindingLayout(m_avboitState.m_emptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitState.m_occupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            emulationDesc.addBindingLayout(m_avboitState.m_emptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitState.m_extinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            emulationDesc.addBindingLayout(m_avboitState.m_emptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitState.m_accumulateBindingLayout);
            break;
        case MaterialPipelinePass::Opaque:
        default:
            emulationDesc.addBindingLayout(m_drawState.m_emulationViewBindingLayout);
            if(csgClipPipeline){
                if(!createCsgClipResources())
                    return false;
                emulationDesc.addBindingLayout(m_csgState.m_clipBindingLayout);
            }
            break;
        }
        resources.emulationPipeline = device->createGraphicsPipeline(emulationDesc, framebuffer->getFramebufferInfo());
        if(!resources.emulationPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        return true;
    };

    const bool meshSupported = device->queryFeatureSupport(Core::Feature::Meshlets);
    if(pass == MaterialPipelinePass::Opaque && !hasPixelShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a pixel shader"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }

    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a mesh shader; compute emulation is derived internally from that mesh shader")
            , StringConvert(materialKey.c_str())
        );
        return failMaterialPipeline();
    }

    if(meshSupported){
        if(!tryBuildMeshPipeline()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create the required mesh rendering path for material '{}' on a mesh-capable device")
                , StringConvert(materialKey.c_str())
            );
            return failMaterialPipeline();
        }

        logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
        outResources = &resources;
        return true;
    }

    if(!tryBuildComputePipeline()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation rendering path for material '{}' from its mesh shader")
            , StringConvert(materialKey.c_str())
        );
        return failMaterialPipeline();
    }

    logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
    outResources = &resources;
    return true;
}

bool RendererSystem::hasTransparentRenderers(){
    auto materialIsTransparent = [&](const Core::Assets::AssetRef<Material>& material) -> bool{
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!createMaterialSurfaceInfo(material, materialInfo))
            return false;
        NWB_ASSERT(materialInfo);
        return materialInfo->transparent;
    };

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        static_cast<void>(entity);
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }
    return false;
}

void RendererSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath::Enum renderPath, const bool meshSupported){
    auto [it, inserted] = m_materialState.m_loggedMaterialPaths.try_emplace(materialKey, renderPath);
    if(!inserted){
        if(it.value() == renderPath)
            return;
        it.value() = renderPath;
    }

    switch(renderPath){
    case RenderPath::MeshShader:{
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: material '{}' selected MeshShader + PS on this device"),
            StringConvert(materialKey.c_str())
        );
        break;
    }
    case RenderPath::ComputeEmulation:{
        if(!meshSupported){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS by compiling its mesh shader for compute emulation because this device does not support mesh shaders"),
                StringConvert(materialKey.c_str())
            );
        }
        else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS through compute emulation"),
                StringConvert(materialKey.c_str())
            );
        }
        break;
    }
    default:{
        break;
    }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

