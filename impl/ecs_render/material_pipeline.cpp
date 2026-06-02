// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"
#include "avboit_private.h"
#include "material_shader_variants_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    Core::GraphicsString avboitCsgShaderVariant(m_arena);
    const bool csgClipPipeline = ECSRenderMaterialShaderVariants::CsgClipPipeline(pipelineKey);
    const bool avboitCsgClipPipeline = ECSRenderMaterialShaderVariants::AvboitCsgClipPipeline(pipelineKey);
    if(csgClipPipeline && !avboitCsgClipPipeline && !ECSRenderMaterialShaderVariants::BuildCsgClipShaderVariantName(shaderVariant, csgShaderVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG shader variant for material '{}'"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }
    if(avboitCsgClipPipeline && !ECSRenderMaterialShaderVariants::BuildAvboitCsgClipShaderVariantName(shaderVariant, csgShaderVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG mesh shader variant for material '{}'"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }
    if(avboitCsgClipPipeline && !ECSRenderMaterialShaderVariants::BuildAvboitCsgClipShaderVariantName(Core::ShaderArchive::s_DefaultVariant, avboitCsgShaderVariant)){
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

    switch(pass){
    case MaterialPipelinePass::Opaque:
        break;
    case MaterialPipelinePass::AvboitOccupancy:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitState.m_occupancyPixelShader;
        passPixelShaderName = ECSRenderAvboitDetail::s_AvboitOccupancyPixelShaderName;
        passPixelShaderDebugName = "ECSRender_AvboitOccupancyPS";
        break;
    case MaterialPipelinePass::AvboitExtinction:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitState.m_extinctionPixelShader;
        passPixelShaderName = ECSRenderAvboitDetail::s_AvboitExtinctionPixelShaderName;
        passPixelShaderDebugName = "ECSRender_AvboitExtinctionPS";
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        if(!createAvboitResources())
            return failMaterialPipeline();
        passPixelShader = m_avboitState.m_accumulatePixelShader;
        passPixelShaderName = ECSRenderAvboitDetail::s_AvboitAccumulatePixelShaderName;
        passPixelShaderDebugName = "ECSRender_AvboitAccumulatePS";
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
        else if(avboitCsgClipPipeline){
            if(!loadShader(resources.pixelShader, passPixelShaderName, AStringView(avboitCsgShaderVariant), Core::ShaderType::Pixel, passPixelShaderDebugName))
                return false;
        }
        else
            resources.pixelShader = passPixelShader;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        pipelineDesc.addBindingLayout(m_drawState.m_meshBindingLayout);
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
        if(csgClipPipeline){
            if(!createCsgClipResources())
                return false;
            pipelineDesc.addBindingLayout(m_csgState.m_clipBindingLayout);
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
        else if(avboitCsgClipPipeline){
            if(!loadShader(resources.pixelShader, passPixelShaderName, AStringView(avboitCsgShaderVariant), Core::ShaderType::Pixel, passPixelShaderDebugName))
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
            if(avboitCsgClipPipeline)
                computeDesc.addBindingLayout(m_avboitState.m_emptyBindingLayout);
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
            break;
        }
        if(csgClipPipeline){
            if(!createCsgClipResources())
                return false;
            emulationDesc.addBindingLayout(m_csgState.m_clipBindingLayout);
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

