#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/avboit/avboit_private.h>
#include <impl/ecs_render/material/material_shader_variants_private.h>

#include <impl/assets/graphics/csg/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ResolveCsgProjectEvaluatorModuleInclude(
    const CsgShapeRegistry& shapeRegistry,
    const Name& evaluatorVariant,
    ACompactString& outModuleInclude
){
    outModuleInclude.clear();
    if(!evaluatorVariant)
        return true;

    return shapeRegistry.findShaderModuleInclude(evaluatorVariant, outModuleInclude) && !outModuleInclude.empty();
}

[[nodiscard]] bool BuildCsgProjectEvaluatorModuleAssignment(const AStringView moduleInclude, Core::GraphicsString& outAssignment){
    outAssignment.clear();
    if(moduleInclude.empty())
        return true;

    outAssignment.reserve(
        ECSRenderMaterialShaderVariants::s_CsgProjectEvaluatorModuleDefineName.size()
        + moduleInclude.size()
        + 3u
    );
    outAssignment += ECSRenderMaterialShaderVariants::s_CsgProjectEvaluatorModuleDefineName;
    outAssignment += "=\"";
    outAssignment += moduleInclude;
    outAssignment += '"';
    return true;
}

[[nodiscard]] bool BuildCsgShaderVariantName(
    const AStringView baseVariant,
    const bool avboitClipSet,
    const AStringView projectEvaluatorModuleAssignment,
    Core::GraphicsString& outVariant
){
    ECSRenderMaterialShaderVariants::ShaderVariantDefineAssignment defineAssignments[
        ECSRenderMaterialShaderVariants::s_MaxCsgClipShaderVariantDefineAssignments
    ];
    usize defineAssignmentCount = 0u;

    if(avboitClipSet){
        defineAssignments[defineAssignmentCount++] = {
            ECSRenderMaterialShaderVariants::s_CsgClipSetDefineName,
            ECSRenderMaterialShaderVariants::s_CsgAvboitClipSetDefineAssignment
        };
    }
    defineAssignments[defineAssignmentCount++] = {
        ECSRenderMaterialShaderVariants::s_CsgEnabledDefineName,
        ECSRenderMaterialShaderVariants::s_CsgEnabledDefineAssignment
    };
    defineAssignments[defineAssignmentCount++] = {
        ECSRenderMaterialShaderVariants::s_CsgIntervalSampleEnabledDefineName,
        ECSRenderMaterialShaderVariants::s_CsgIntervalSampleEnabledDefineAssignment
    };
    if(avboitClipSet){
        defineAssignments[defineAssignmentCount++] = {
            ECSRenderMaterialShaderVariants::s_CsgIntervalSampleSetDefineName,
            ECSRenderMaterialShaderVariants::s_CsgAvboitIntervalSampleSetDefineAssignment
        };
    }
    if(!projectEvaluatorModuleAssignment.empty()){
        defineAssignments[defineAssignmentCount++] = {
            ECSRenderMaterialShaderVariants::s_CsgProjectEvaluatorModuleDefineName,
            projectEvaluatorModuleAssignment
        };
    }

    return ECSRenderMaterialShaderVariants::BuildCsgClipShaderVariantName(
        baseVariant,
        defineAssignments,
        defineAssignmentCount,
        outVariant
    );
}

struct MaterialPipelineCsgBindingLayouts{
    const Core::BindingLayoutHandle& clip;
    const Core::BindingLayoutHandle& receiverSurface;
    const Core::BindingLayoutHandle& intervalSample;
    const Core::BindingLayoutHandle& avboitEmpty;
};

struct MaterialPipelineAvboitPixelShaderSelection{
    const Core::Assets::AssetRef<Shader>* materialShader = nullptr;
    const char* debugName = nullptr;

    [[nodiscard]] bool materialDriven()const{ return materialShader != nullptr && materialShader->valid(); }
    [[nodiscard]] Name shaderName()const{ return materialDriven() ? materialShader->name() : NAME_NONE; }
};

[[nodiscard]] MaterialPipelineAvboitPixelShaderSelection SelectAvboitPixelShader(
    const MaterialPipelinePass::Enum pass,
    const MaterialSurfaceInfo& materialInfo
){
    MaterialPipelineAvboitPixelShaderSelection selection;
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
        selection.materialShader = &materialInfo.avboitOccupancyPixelShader;
        selection.debugName = "ECSRender_AvboitOccupancyPS";
        break;
    case MaterialPipelinePass::AvboitExtinction:
        selection.materialShader = &materialInfo.avboitExtinctionPixelShader;
        selection.debugName = "ECSRender_AvboitExtinctionPS";
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        selection.materialShader = &materialInfo.avboitAccumulatePixelShader;
        selection.debugName = "ECSRender_AvboitAccumulatePS";
        break;
    default:
        break;
    }
    return selection;
}

struct MaterialPipelineAvboitBindingLayouts{
    const Core::BindingLayoutHandle& occupancy;
    const Core::BindingLayoutHandle& extinction;
    const Core::BindingLayoutHandle& accumulate;
};

template<typename PipelineDesc>
[[nodiscard]] bool AddAvboitBindingLayout(
    PipelineDesc& pipelineDesc,
    const MaterialPipelinePass::Enum pass,
    const MaterialPipelineAvboitBindingLayouts& bindingLayouts
){
    const Core::BindingLayoutHandle* bindingLayout = nullptr;
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
        bindingLayout = &bindingLayouts.occupancy;
        break;
    case MaterialPipelinePass::AvboitExtinction:
        bindingLayout = &bindingLayouts.extinction;
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        bindingLayout = &bindingLayouts.accumulate;
        break;
    default:
        return true;
    }

    if(!*bindingLayout)
        return false;

    pipelineDesc.addBindingLayout(*bindingLayout);
    return true;
}

template<typename PipelineDesc>
[[nodiscard]] bool AddCsgGraphicsBindingLayouts(
    PipelineDesc& pipelineDesc,
    const MaterialPipelineCsgBindingUse& csgBindingUse,
    const MaterialPipelineCsgBindingLayouts& bindingLayouts
){
    if(!csgBindingUse.clip)
        return true;
    if(!bindingLayouts.clip)
        return false;
    if(csgBindingUse.receiverSurface && !bindingLayouts.receiverSurface)
        return false;
    if(csgBindingUse.intervalSample && !bindingLayouts.intervalSample)
        return false;

    pipelineDesc.addBindingLayout(bindingLayouts.clip);
    if(csgBindingUse.receiverSurface)
        pipelineDesc.addBindingLayout(bindingLayouts.receiverSurface);
    if(csgBindingUse.intervalSample)
        pipelineDesc.addBindingLayout(bindingLayouts.intervalSample);
    return true;
}

[[nodiscard]] bool AddCsgComputeBindingLayouts(
    Core::ComputePipelineDesc& pipelineDesc,
    const MaterialPipelineCsgBindingUse& csgBindingUse,
    const MaterialPipelineCsgBindingLayouts& bindingLayouts
){
    if(!csgBindingUse.clip)
        return true;
    if(!bindingLayouts.clip)
        return false;
    if(csgBindingUse.avboitClip){
        if(!bindingLayouts.avboitEmpty)
            return false;
        pipelineDesc.addBindingLayout(bindingLayouts.avboitEmpty);
    }
    pipelineDesc.addBindingLayout(bindingLayouts.clip);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::createRendererPipeline(
    const MaterialSurfaceInfo& materialInfo,
    const MaterialPipelineKey& pipelineKey,
    Core::Framebuffer* framebuffer,
    MaterialPipelineResources*& outResources
){
    outResources = nullptr;

    NWB_ASSERT(framebuffer);

    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    NWB_ASSERT(materialKey);

    auto [it, inserted] = materialState().m_pipelines.try_emplace(pipelineKey);
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
            materialState().m_pipelines.erase(it);
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
    Core::GraphicsString csgShaderVariant(arena());
    Core::GraphicsString avboitCsgShaderVariant(arena());
    const MaterialPipelineCsgBindingUse csgBindingUse =
        MaterialPipelineResolveCsgBindingUse(pipelineKey, pass);
    const bool csgClipPipeline = csgBindingUse.clip;
    const bool avboitCsgClipPipeline = csgBindingUse.avboitClip;
    ACompactString csgProjectEvaluatorModuleInclude;
    Core::GraphicsString csgProjectEvaluatorModuleAssignment(arena());
    AStringView materialProjectEvaluatorModuleAssignmentToAdd;
    AStringView avboitProjectEvaluatorModuleAssignmentToAdd;
    if(csgClipPipeline){
        if(!__hidden_material_pipeline::ResolveCsgProjectEvaluatorModuleInclude(
            csgShapeRegistry(),
            pipelineKey.csgEvaluatorVariant,
            csgProjectEvaluatorModuleInclude
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to resolve CSG evaluator module for material '{}'"), StringConvert(materialKey.c_str()));
            return failMaterialPipeline();
        }
        if(!__hidden_material_pipeline::BuildCsgProjectEvaluatorModuleAssignment(csgProjectEvaluatorModuleInclude.view(), csgProjectEvaluatorModuleAssignment)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG evaluator module define for material '{}'"), StringConvert(materialKey.c_str()));
            return failMaterialPipeline();
        }
        if(!csgProjectEvaluatorModuleAssignment.empty()){
            AStringView existingEvaluatorModuleAssignment;
            const bool materialVariantHasEvaluatorModule = ECSRenderMaterialShaderVariants::FindVariantDefineAssignment(
                shaderVariant,
                ECSRenderMaterialShaderVariants::s_CsgProjectEvaluatorModuleDefineName,
                existingEvaluatorModuleAssignment
            );
            if(materialVariantHasEvaluatorModule && existingEvaluatorModuleAssignment != AStringView(csgProjectEvaluatorModuleAssignment)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' uses a different CSG evaluator module than its active cutters")
                    , StringConvert(materialKey.c_str())
                );
                return failMaterialPipeline();
            }
            if(!materialVariantHasEvaluatorModule)
                materialProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
            avboitProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
        }
    }
    if(
        csgClipPipeline
        && !avboitCsgClipPipeline
        && !__hidden_material_pipeline::BuildCsgShaderVariantName(
            shaderVariant,
            false,
            materialProjectEvaluatorModuleAssignmentToAdd,
            csgShaderVariant
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG shader variant for material '{}'"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }
    if(
        avboitCsgClipPipeline
        && !__hidden_material_pipeline::BuildCsgShaderVariantName(
            shaderVariant,
            true,
            materialProjectEvaluatorModuleAssignmentToAdd,
            csgShaderVariant
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG mesh shader variant for material '{}'"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }
    if(
        avboitCsgClipPipeline
        && !__hidden_material_pipeline::BuildCsgShaderVariantName(
            Core::ShaderArchive::s_DefaultVariant,
            true,
            avboitProjectEvaluatorModuleAssignmentToAdd,
            avboitCsgShaderVariant
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG pixel shader variant for material '{}'"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }
    const AStringView pixelShaderVariant = csgClipPipeline && !avboitCsgClipPipeline
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
    Name passPixelShaderName = NAME_NONE;
    const char* passPixelShaderDebugName = nullptr;
    __hidden_material_pipeline::MaterialPipelineAvboitPixelShaderSelection avboitPixelShaderSelection;
    if(MaterialPipelinePassUsesRendererAvboit(pass)){
        if(
            !avboitState().m_emptyBindingLayout
            || !avboitState().m_occupancyBindingLayout
            || !avboitState().m_extinctionBindingLayout
            || !avboitState().m_accumulateBindingLayout
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT resources were not validated before material pipeline creation"));
            return failMaterialPipeline();
        }
        avboitPixelShaderSelection = __hidden_material_pipeline::SelectAvboitPixelShader(
            pass,
            materialInfo
        );
        if(materialInfo.transparent && !avboitPixelShaderSelection.materialDriven()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: transparent material '{}' is missing its cook-generated AVBOIT pass pixel shader"), StringConvert(materialKey.c_str()));
            return failMaterialPipeline();
        }
    }
    const bool materialDrivenAvboitPass = avboitPixelShaderSelection.materialDriven();

    switch(pass){
    case MaterialPipelinePass::Opaque:
        break;
    case MaterialPipelinePass::CsgReceiverSurface:
        passPixelShaderName = AssetsGraphicsCsg::s_ReceiverSurfacePixelShaderName;
        passPixelShaderDebugName = "ECSRender_CsgReceiverSurfacePS";
        break;
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
    case MaterialPipelinePass::AvboitAccumulate:
        passPixelShaderName = avboitPixelShaderSelection.shaderName();
        passPixelShaderDebugName = avboitPixelShaderSelection.debugName;
        break;
    default:
        break;
    }

    auto* device = graphics().getDevice();
    const Core::RenderState renderState = ECSRenderDetail::BuildRenderStateForPass(pass, pipelineKey.twoSided);
    const __hidden_material_pipeline::MaterialPipelineCsgBindingLayouts csgBindingLayouts{
        csgState().m_clipBindingLayout,
        csgState().m_receiverSurfaceBindingLayout,
        csgState().m_intervalSampleBindingLayout,
        avboitState().m_emptyBindingLayout
    };
    const __hidden_material_pipeline::MaterialPipelineAvboitBindingLayouts avboitBindingLayouts{
        avboitState().m_occupancyBindingLayout,
        avboitState().m_extinctionBindingLayout,
        avboitState().m_accumulateBindingLayout
    };

    auto loadPassPixelShader = [&]() -> bool{
        if(pass == MaterialPipelinePass::Opaque){
            return m_renderer.shaderSystem().loadShader(
                resources.pixelShader,
                materialInfo.pixelShader.name(),
                pixelShaderVariant,
                Core::ShaderType::Pixel,
                "ECSRender_RendererPS"
            );
        }
        if(pass == MaterialPipelinePass::CsgReceiverSurface){
            return m_renderer.shaderSystem().loadShader(
                resources.pixelShader,
                passPixelShaderName,
                Core::ShaderArchive::s_DefaultVariant,
                Core::ShaderType::Pixel,
                passPixelShaderDebugName
            );
        }
        if(avboitCsgClipPipeline){
            return m_renderer.shaderSystem().loadShader(
                resources.pixelShader,
                passPixelShaderName,
                AStringView(avboitCsgShaderVariant),
                Core::ShaderType::Pixel,
                passPixelShaderDebugName
            );
        }
        if(!passPixelShader){
            // AVBOIT pixel shaders are generated for the selected material and use its typed binding and project
            // surface/BXDF contract, so load the resolved per-material pass shader at its default variant.
            return m_renderer.shaderSystem().loadShader(
                resources.pixelShader,
                passPixelShaderName,
                Core::ShaderArchive::s_DefaultVariant,
                Core::ShaderType::Pixel,
                passPixelShaderDebugName
            );
        }
        resources.pixelShader = passPixelShader;
        return true;
    };

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!drawState().m_meshBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh shader resources were not validated before material pipeline creation"));
            return false;
        }
        if(!m_renderer.shaderSystem().loadShader(resources.meshShader, materialInfo.meshShader.name(), meshShaderVariant, Core::ShaderType::Mesh, "ECSRender_RendererMesh"))
            return false;
        if(!loadPassPixelShader())
            return false;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        pipelineDesc.addBindingLayout(drawState().m_meshBindingLayout);
        if(!__hidden_material_pipeline::AddAvboitBindingLayout(pipelineDesc, pass, avboitBindingLayouts))
            return false;
        if(!__hidden_material_pipeline::AddCsgGraphicsBindingLayouts(pipelineDesc, csgBindingUse, csgBindingLayouts))
            return false;

        resources.meshletPipeline = device->createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
        if(!resources.meshletPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        resources.renderPath = RenderPath::MeshShader;
        return true;
    };

    auto tryBuildComputePipeline = [&]() -> bool{
        if(
            !drawState().m_computeBindingLayout
            || !drawState().m_emulationViewBindingLayout
            || !drawState().m_emulationVertexShader
            || !drawState().m_emulationInputLayout
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation resources were not validated before material pipeline creation"));
            return false;
        }
        const Name& meshComputeArchiveStageName = MaterialShaderStageNames::s_MeshComputeArchiveStageName;
        if(!m_renderer.shaderSystem().loadShader(
            resources.computeShader,
            materialInfo.meshShader.name(),
            meshShaderVariant,
            Core::ShaderType::Compute,
            "ECSRender_RendererCS",
            &meshComputeArchiveStageName
        ))
            return false;
        if(!loadPassPixelShader())
            return false;
        Core::ComputePipelineDesc computeDesc;
        computeDesc.setComputeShader(resources.computeShader);
        computeDesc.addBindingLayout(drawState().m_computeBindingLayout);
        if(!__hidden_material_pipeline::AddCsgComputeBindingLayouts(computeDesc, csgBindingUse, csgBindingLayouts))
            return false;
        resources.computePipeline = device->createComputePipeline(computeDesc);
        if(!resources.computePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(drawState().m_emulationInputLayout);
        emulationDesc.setVertexShader(drawState().m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        const bool emulationGraphicsUsesMeshFrameSet =
            !MaterialPipelinePassUsesRendererAvboit(pass)
            || csgClipPipeline
            || materialDrivenAvboitPass
        ;
        const Core::BindingLayoutHandle& avboitViewBindingLayout = emulationGraphicsUsesMeshFrameSet
            ? drawState().m_emulationViewBindingLayout
            : avboitState().m_emptyBindingLayout
        ;
        if(MaterialPipelinePassUsesRendererAvboit(pass)){
            emulationDesc.addBindingLayout(avboitViewBindingLayout);
            if(!__hidden_material_pipeline::AddAvboitBindingLayout(emulationDesc, pass, avboitBindingLayouts))
                return false;
        }
        else{
            emulationDesc.addBindingLayout(drawState().m_emulationViewBindingLayout);
        }
        if(!__hidden_material_pipeline::AddCsgGraphicsBindingLayouts(emulationDesc, csgBindingUse, csgBindingLayouts))
            return false;
        resources.emulationPipeline = device->createGraphicsPipeline(emulationDesc, framebuffer->getFramebufferInfo());
        if(!resources.emulationPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        resources.emulationGraphicsUsesMeshFrameSet = emulationGraphicsUsesMeshFrameSet;
        return true;
    };

    const bool meshSupported = graphics().queryFeatureSupport(Core::Feature::Meshlets);
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

bool RendererMaterialSystem::findRendererPipeline(const MaterialPipelineKey& pipelineKey, MaterialPipelineResources*& outResources){
    outResources = nullptr;

    const auto foundPipeline = materialState().m_pipelines.find(pipelineKey);
    if(foundPipeline == materialState().m_pipelines.end())
        return false;

    MaterialPipelineResources& resources = foundPipeline.value();
    switch(resources.renderPath){
    case RenderPath::MeshShader:
        if(!resources.meshletPipeline)
            return false;
        break;
    case RenderPath::ComputeEmulation:
        if(!resources.computePipeline || !resources.emulationPipeline)
            return false;
        break;
    default:
        return false;
    }

    outResources = &resources;
    return true;
}

void RendererMaterialSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath::Enum renderPath, const bool meshSupported){
    auto [it, inserted] = materialState().m_loggedMaterialPaths.try_emplace(materialKey, renderPath);
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

