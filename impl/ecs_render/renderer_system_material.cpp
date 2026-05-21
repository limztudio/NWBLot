// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    const auto foundInfo = m_materialSurfaceInfos.find(materialPath);
    if(foundInfo != m_materialSurfaceInfos.end()){
        outInfo = &foundInfo.value();
        return outInfo->valid;
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

    MaterialSurfaceInfo createdInfo(m_arena);
    createdInfo.materialName = materialPath;
    if(material.shaderVariant().empty())
        createdInfo.shaderVariant.assign(Core::ShaderArchive::s_DefaultVariant);
    else
        createdInfo.shaderVariant.assign(material.shaderVariant().data(), material.shaderVariant().size());
    createdInfo.valid = true;

    (void)material.findShaderForStage(Core::ShaderType::PixelStage, createdInfo.pixelShader);
    (void)material.findShaderForStage(Core::ShaderType::MeshStage, createdInfo.meshShader);

    createdInfo.parameters.reserve(material.parameters().size());
    createdInfo.parameters.insert(createdInfo.parameters.end(), material.parameters().begin(), material.parameters().end());
    createdInfo.alpha = material.alpha();
    createdInfo.transparent = material.transparent();

    auto result = m_materialSurfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    outInfo = &it.value();
    return outInfo->valid;
}

bool RendererSystem::createRendererPipeline(
    const MaterialSurfaceInfo& materialInfo,
    const MaterialPipelineKey& pipelineKey,
    Core::IFramebuffer* framebuffer,
    MaterialPipelineResources*& outResources
){
    outResources = nullptr;

    if(!framebuffer)
        return false;

    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    if(!materialInfo.valid || !materialKey){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    auto [it, inserted] = m_materialPipelines.try_emplace(pipelineKey);
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
            m_materialPipelines.erase(it);
    };
    auto failMaterialPipeline = [&](){
        removeFailedEntry();
        return false;
    };

    const AStringView shaderVariant = materialInfo.shaderVariant.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : AStringView(materialInfo.shaderVariant)
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
        passPixelShader = m_avboitOccupancyPixelShader;
        break;
    case MaterialPipelinePass::AvboitExtinction:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitExtinctionPixelShader;
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitAccumulatePixelShader;
        break;
    default:
        break;
    }

    Core::IDevice* device = m_graphics.getDevice();
    const Core::RenderState renderState = ECSRenderDetail::BuildRenderStateForPass(pass);

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!hasMeshShader)
            return false;
        if(pass == MaterialPipelinePass::Opaque && !hasPixelShader)
            return false;
        if(!createMeshShaderResources())
            return false;
        if(!loadShader(resources.meshShader, materialInfo.meshShader.name(), shaderVariant, Core::ShaderType::Mesh, "ECSRender_RendererMesh"))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!loadShader(resources.pixelShader, materialInfo.pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSRender_RendererPS"))
                return false;
        }
        else
            resources.pixelShader = passPixelShader;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        pipelineDesc.addBindingLayout(m_meshBindingLayout);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            pipelineDesc.addBindingLayout(m_avboitOccupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            pipelineDesc.addBindingLayout(m_avboitExtinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            pipelineDesc.addBindingLayout(m_avboitAccumulateBindingLayout);
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
        if(!hasMeshShader)
            return false;
        if(pass == MaterialPipelinePass::Opaque && !hasPixelShader)
            return false;
        if(!createComputeEmulationResources())
            return false;
        const Name& meshComputeArchiveStageName = MaterialShaderStageNames::MeshComputeArchiveStageName();
        if(!loadShader(
            resources.computeShader,
            materialInfo.meshShader.name(),
            shaderVariant,
            Core::ShaderType::Compute,
            "ECSRender_RendererCS",
            &meshComputeArchiveStageName
        ))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!loadShader(resources.pixelShader, materialInfo.pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSRender_RendererPS"))
                return false;
        }
        else{
            resources.pixelShader = passPixelShader;
        }
        if(!createEmulationViewResources())
            return false;

        Core::ComputePipelineDesc computeDesc;
        computeDesc.setComputeShader(resources.computeShader);
        computeDesc.addBindingLayout(m_computeBindingLayout);
        resources.computePipeline = device->createComputePipeline(computeDesc);
        if(!resources.computePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_emulationInputLayout);
        emulationDesc.setVertexShader(m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitOccupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitExtinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitAccumulateBindingLayout);
            break;
        case MaterialPipelinePass::Opaque:
        default:
            emulationDesc.addBindingLayout(m_emulationViewBindingLayout);
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

    if(meshSupported && hasMeshShader){
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
        return materialInfo && materialInfo->valid && materialInfo->transparent;
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
    auto [it, inserted] = m_loggedMaterialPaths.try_emplace(materialKey, renderPath);
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

