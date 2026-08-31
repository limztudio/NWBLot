// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/ecs_render/avboit/avboit_private.h>
#include <impl/ecs_render/material/material_shader_variants_private.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <impl/assets/graphics/csg/names.h>
#include <impl/assets_material/shader_stage_names.h>

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>
#include <impl/ecs_csg/shape_registry.h>


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
    const AStringView projectEvaluatorModuleAssignment,
    Core::GraphicsString& outVariant
){
    ECSRenderMaterialShaderVariants::ShaderVariantDefineAssignment defineAssignments[
        ECSRenderMaterialShaderVariants::s_MaxCsgClipShaderVariantDefineAssignments
    ];
    usize defineAssignmentCount = 0u;

    defineAssignments[defineAssignmentCount++] = {
        ECSRenderMaterialShaderVariants::s_CsgEnabledDefineName,
        ECSRenderMaterialShaderVariants::s_CsgEnabledDefineAssignment
    };
    defineAssignments[defineAssignmentCount++] = {
        ECSRenderMaterialShaderVariants::s_CsgIntervalSampleEnabledDefineName,
        ECSRenderMaterialShaderVariants::s_CsgIntervalSampleEnabledDefineAssignment
    };
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
    const MaterialPipelineCsgBindingUse csgBindingUse =
        MaterialPipelineResolveCsgBindingUse(pipelineKey, pass);
    const bool csgClipPipeline = csgBindingUse.clip;
    const bool avboitCsgClipPipeline = csgBindingUse.avboitClip;
    ACompactString csgProjectEvaluatorModuleInclude;
    Core::GraphicsString csgProjectEvaluatorModuleAssignment(m_arena);
    AStringView materialProjectEvaluatorModuleAssignmentToAdd;
    AStringView avboitProjectEvaluatorModuleAssignmentToAdd;
    if(csgClipPipeline){
        if(!__hidden_material_pipeline::ResolveCsgProjectEvaluatorModuleInclude(
            m_csgShapeRegistry,
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
        avboitPixelShaderSelection = __hidden_material_pipeline::SelectAvboitPixelShader(
            pass,
            materialInfo
        );
        if(materialInfo.transparent && !avboitPixelShaderSelection.materialDriven()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: transparent material '{}' is missing its cook-generated AVBOIT pass pixel shader"), StringConvert(materialKey.c_str()));
            return failMaterialPipeline();
        }
    }
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

    auto& device = m_graphics.getDevice();
    const Core::RenderState renderState = ECSRenderDetail::BuildRenderStateForPass(pass, pipelineKey.twoSided);
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material geometry pipeline requires the global descriptor heap"));
        return failMaterialPipeline();
    }
    Core::BindingLayoutHandle materialPassBindingLayout;
    if(!prepareMaterialPassBindingLayout(materialPassBindingLayout))
        return failMaterialPipeline();

    auto loadPassPixelShader = [&]() -> bool{
        if(pass == MaterialPipelinePass::Opaque){
            return m_shaderSystem.loadShader(
                resources.pixelShader,
                materialInfo.pixelShader.name(),
                pixelShaderVariant,
                Core::ShaderType::Pixel,
                "ECSRender_RendererPS"
            );
        }
        if(pass == MaterialPipelinePass::CsgReceiverSurface){
            return m_shaderSystem.loadShader(
                resources.pixelShader,
                passPixelShaderName,
                Core::ShaderArchive::s_DefaultVariant,
                Core::ShaderType::Pixel,
                passPixelShaderDebugName
            );
        }
        if(avboitCsgClipPipeline){
            return m_shaderSystem.loadShader(
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
            return m_shaderSystem.loadShader(
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
        if(!m_shaderSystem.loadShader(resources.meshShader, materialInfo.meshShader.name(), meshShaderVariant, Core::ShaderType::Mesh, "ECSRender_RendererMesh"))
            return false;
        if(!loadPassPixelShader())
            return false;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        // Set 0 is the shared push-only range; all CSG and AVBOIT resources are selected through the global heap.
        pipelineDesc.addBindingLayout(materialPassBindingLayout);
        // The mesh stage resolves every immutable geometry stream through the global StorageBuffer heap. Keep both
        // fixed heap layouts in every mesh pipeline. The sampler layout is part of that frozen heap surface even though geometry uses
        // only the resource table today.
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;

        resources.meshletPipeline = device.createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
        if(!resources.meshletPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        resources.renderPath = RenderPath::MeshShader;
        return true;
    };

    auto tryBuildComputePipeline = [&]() -> bool{
        NWB_ASSERT(m_materialState.m_computeBindingLayout);
        NWB_ASSERT(m_materialState.m_emulationVertexShader);
        NWB_ASSERT(m_materialState.m_emulationInputLayout);
        const Name& meshComputeArchiveStageName = MaterialShaderStageNames::s_MeshComputeArchiveStageName;
        if(!m_shaderSystem.loadShader(
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
        computeDesc.addBindingLayout(m_materialState.m_computeBindingLayout);
        // The compute-emulation mesh stage shares the same heap-backed source-stream runtime as the mesh-shader
        // path, so it needs the persistent tables before its dispatch as well.
        computeDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        resources.computePipeline = device.createComputePipeline(computeDesc);
        if(!resources.computePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_materialState.m_emulationInputLayout);
        emulationDesc.setVertexShader(m_materialState.m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        emulationDesc.addBindingLayout(materialPassBindingLayout);
        emulationDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        resources.emulationPipeline = device.createGraphicsPipeline(emulationDesc, framebuffer->getFramebufferInfo());
        if(!resources.emulationPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        return true;
    };

    const bool meshSupported = m_graphics.queryFeatureSupport(Core::Feature::Meshlets);
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

    const auto foundPipeline = m_materialState.m_pipelines.find(pipelineKey);
    if(foundPipeline == m_materialState.m_pipelines.end())
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

void RendererMaterialSystem::invalidateRendererPipelines(){
    m_materialState.m_pipelines.clear();
}

void RendererMaterialSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath::Enum renderPath, const bool meshSupported){
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
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS by compiling its mesh shader for compute emulation because native mesh shaders are unavailable in the current graphics configuration"),
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

