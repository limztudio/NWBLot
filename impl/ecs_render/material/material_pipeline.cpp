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
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <impl/ecs_csg/shape_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<ACompactString> ResolveCsgProjectEvaluatorModuleInclude(
    const CsgShapeRegistry& shapeRegistry,
    const Name& evaluatorVariant
){
    if(!evaluatorVariant)
        return ACompactString{};
    auto moduleInclude = shapeRegistry.findShaderModuleInclude(evaluatorVariant);
    if(!moduleInclude || moduleInclude->empty())
        return MakeUnexpected(Failure{});
    return moduleInclude;
}

[[nodiscard]] Core::GraphicsString BuildCsgProjectEvaluatorModuleAssignment(Core::GraphicsArena& arena, const AStringView moduleInclude){
    Core::GraphicsString assignment(arena);
    if(moduleInclude.empty())
        return assignment;

    assignment.reserve(
        ECSRenderMaterialShaderVariants::s_CsgProjectEvaluatorModuleDefineName.size()
        + moduleInclude.size()
        + 3u
    );
    assignment += ECSRenderMaterialShaderVariants::s_CsgProjectEvaluatorModuleDefineName;
    assignment += "=\"";
    assignment += moduleInclude;
    assignment += '"';
    return assignment;
}

[[nodiscard]] Expected<Core::GraphicsString> BuildCsgShaderVariantName(
    Core::GraphicsArena& arena,
    const AStringView baseVariant,
    const AStringView projectEvaluatorModuleAssignment
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
        arena,
        baseVariant,
        defineAssignments,
        defineAssignmentCount
    );
}

struct MaterialPipelineAvboitPixelShaderSelection{
    const Core::Assets::AssetRef<PixelShader>* materialShader = nullptr;
    AStringView debugName = "ECSRender_InvalidAvboitPixelShader";

    [[nodiscard]] bool materialDriven()const noexcept{ return materialShader != nullptr && materialShader->valid(); }
    [[nodiscard]] Name shaderName()const noexcept{ return materialDriven() ? materialShader->name() : s_NameNone; }
};

[[nodiscard]] MaterialPipelineAvboitPixelShaderSelection SelectAvboitPixelShader(
    const MaterialPipelinePass::Enum pass,
    const MaterialSurfaceInfo& materialInfo
)noexcept{
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
    case MaterialPipelinePass::AvboitRefractionCapture:
        // The draw mode selects optical capture in the existing material-authored accumulation shader.
        selection.materialShader = &materialInfo.avboitAccumulatePixelShader;
        selection.debugName = "ECSRender_AvboitRefractionCapturePS";
        break;
    default:
        break;
    }
    return selection;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialPipelineResources*> RendererMaterialSystem::createRendererPipeline(
    const MaterialSurfaceInfo& materialInfo,
    const MaterialPipelineKey& pipelineKey,
    Core::Framebuffer& framebuffer
){
    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    NWB_ASSERT(materialKey);

    auto [it, inserted] = m_materialState.m_pipelines.try_emplace(pipelineKey);
    MaterialPipelineResources& resources = it.value();
    switch(resources.renderPath){
    case RenderPath::MeshShader:
        if(resources.meshletPipeline){
            return &resources;
        }
        break;
    case RenderPath::VertexIndexed:
        if(resources.indexedPipeline && resources.objectGeometryDecodePipeline){
            return &resources;
        }
        break;
    case RenderPath::ComputeEmulation:
        if(resources.computePipeline && resources.emulationPipeline){
            return &resources;
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
        return MakeUnexpected(Failure{});
    };

    if(materialInfo.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has empty shader variant")
            , StringConvert(materialKey.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }
    const AStringView shaderVariant(materialInfo.shaderVariant.data(), materialInfo.shaderVariant.size());
    Core::GraphicsString csgShaderVariant(m_arena);
    Core::GraphicsString avboitCsgShaderVariant(m_arena);
    const MaterialPipelineCsgBindingUse csgBindingUse =
        MaterialPipelineResolveCsgBindingUse(pipelineKey, pass);
    const bool csgClipPipeline = csgBindingUse.clip;
    const bool avboitCsgClipPipeline = csgBindingUse.avboitClip;
    Core::GraphicsString csgProjectEvaluatorModuleAssignment(m_arena);
    AStringView materialProjectEvaluatorModuleAssignmentToAdd;
    AStringView avboitProjectEvaluatorModuleAssignmentToAdd;
    if(csgClipPipeline){
        const auto csgProjectEvaluatorModuleInclude = __hidden_material_pipeline::ResolveCsgProjectEvaluatorModuleInclude(
            m_csgShapeRegistry,
            pipelineKey.csgEvaluatorVariant
        );
        if(!csgProjectEvaluatorModuleInclude){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to resolve CSG evaluator module for material '{}'"), StringConvert(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgProjectEvaluatorModuleAssignment = __hidden_material_pipeline::BuildCsgProjectEvaluatorModuleAssignment(m_arena, csgProjectEvaluatorModuleInclude->view());
        if(!csgProjectEvaluatorModuleAssignment.empty()){
            const auto existingEvaluatorModuleAssignment = ECSRenderMaterialShaderVariants::FindVariantDefineAssignment(
                shaderVariant,
                ECSRenderMaterialShaderVariants::s_CsgProjectEvaluatorModuleDefineName
            );
            if(existingEvaluatorModuleAssignment && *existingEvaluatorModuleAssignment != AStringView(csgProjectEvaluatorModuleAssignment)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' uses a different CSG evaluator module than its active cutters")
                    , StringConvert(materialKey.resolvedText())
                );
                return failMaterialPipeline();
            }
            if(!existingEvaluatorModuleAssignment)
                materialProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
            avboitProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
        }
    }
    if(csgClipPipeline && !avboitCsgClipPipeline){
        auto variant = __hidden_material_pipeline::BuildCsgShaderVariantName(m_arena, shaderVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG shader variant for material '{}'"), StringConvert(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = __hidden_material_pipeline::BuildCsgShaderVariantName(m_arena, shaderVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG mesh shader variant for material '{}'"), StringConvert(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = __hidden_material_pipeline::BuildCsgShaderVariantName(m_arena, Core::ShaderArchive::s_DefaultVariant, avboitProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG pixel shader variant for material '{}'"), StringConvert(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        avboitCsgShaderVariant = Move(*variant);
    }
    const AStringView pixelShaderVariant = csgClipPipeline && !avboitCsgClipPipeline
        ? AStringView(csgShaderVariant)
        : shaderVariant
    ;
    const AStringView meshShaderVariant = csgClipPipeline
        ? AStringView(csgShaderVariant)
        : shaderVariant
    ;

    const bool sharedObjectGeometry = materialInfo.meshShader.name() == Name("engine/graphics/mesh/shared_ms")
        && shaderVariant == Core::ShaderArchive::s_DefaultVariant
    ;
    const AStringView objectGeometryShaderVariant = sharedObjectGeometry ? shaderVariant : meshShaderVariant;

    const bool hasPixelShader = materialInfo.pixelShader.valid();
    const bool hasMeshShader = materialInfo.meshShader.valid();
    Core::ShaderHandle passPixelShader;
    Name passPixelShaderName = s_NameNone;
    AStringView passPixelShaderDebugName = "ECSRender_InvalidPassPixelShader";
    __hidden_material_pipeline::MaterialPipelineAvboitPixelShaderSelection avboitPixelShaderSelection;
    if(MaterialPipelinePassUsesRendererAvboit(pass)){
        avboitPixelShaderSelection = __hidden_material_pipeline::SelectAvboitPixelShader(
            pass,
            materialInfo
        );
        if(materialInfo.transparent && !avboitPixelShaderSelection.materialDriven()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: transparent material '{}' is missing its cook-generated AVBOIT pass pixel shader"), StringConvert(materialKey.resolvedText()));
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
    case MaterialPipelinePass::AvboitRefractionCapture:
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
    const auto materialPassBindingLayout = prepareMaterialPassBindingLayout();
    if(!materialPassBindingLayout)
        return failMaterialPipeline();

    auto loadPassPixelShader = [&]() -> bool{
        if(pass == MaterialPipelinePass::Opaque){
            return m_shaderSystem.loadShader<PixelShader>(
                resources.pixelShader,
                materialInfo.pixelShader.name(),
                pixelShaderVariant,
                "ECSRender_RendererPS"
            );
        }
        if(pass == MaterialPipelinePass::CsgReceiverSurface){
            return m_shaderSystem.loadShader<PixelShader>(
                resources.pixelShader,
                passPixelShaderName,
                Core::ShaderArchive::s_DefaultVariant,
                Name(passPixelShaderDebugName)
            );
        }
        if(avboitCsgClipPipeline){
            return m_shaderSystem.loadShader<PixelShader>(
                resources.pixelShader,
                passPixelShaderName,
                AStringView(avboitCsgShaderVariant),
                Name(passPixelShaderDebugName)
            );
        }
        if(!passPixelShader){
            // AVBOIT pixel shaders are generated for the selected material and use its typed binding and project
            // surface/BXDF contract, so load the resolved per-material pass shader at its default variant.
            return m_shaderSystem.loadShader<PixelShader>(
                resources.pixelShader,
                passPixelShaderName,
                Core::ShaderArchive::s_DefaultVariant,
                Name(passPixelShaderDebugName)
            );
        }
        resources.pixelShader = passPixelShader;
        return true;
    };

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!m_shaderSystem.loadShader<MeshShader>(resources.meshShader, materialInfo.meshShader.name(), meshShaderVariant, "ECSRender_RendererMesh"))
            return false;
        if(!loadPassPixelShader())
            return false;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        // Set 0 is the shared push-only range; all CSG and AVBOIT resources are selected through the global heap.
        pipelineDesc.addBindingLayout(*materialPassBindingLayout);
        // Keep both fixed heap layouts in every mesh pipeline; samplers are frozen surface.
        pipelineDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;

        resources.meshletPipeline = device.createMeshletPipeline(pipelineDesc, framebuffer.getFramebufferInfo());
        if(!resources.meshletPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), StringConvert(materialKey.resolvedText()));
            return false;
        }

        resources.renderPath = RenderPath::MeshShader;
        return true;
    };

    auto tryBuildIndexedPipeline = [&]() -> bool{
        if(!createObjectGeometryPipelineResources(materialInfo.meshShader.name(), objectGeometryShaderVariant, resources))
            return false;
        if(!loadPassPixelShader())
            return false;
        Core::GraphicsPipelineDesc desc;
        desc
            .setInputLayout(m_materialState.m_objectGeometryInputLayout)
            .setVertexShader(resources.objectGeometryVertexShader)
            .setPixelShader(resources.pixelShader)
            .setRenderState(renderState)
            .addBindingLayout(*materialPassBindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        resources.indexedPipeline = device.createGraphicsPipeline(desc, framebuffer.getFramebufferInfo());
        if(!resources.indexedPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create indexed object geometry pipeline for material '{}'"), StringConvert(materialKey.resolvedText()));
            return false;
        }
        resources.renderPath = RenderPath::VertexIndexed;
        return true;
    };

    auto tryBuildComputePipeline = [&]() -> bool{
        if(!createComputeEmulationResources())
            return false;
        NWB_ASSERT(m_materialState.m_computeBindingLayout);
        NWB_ASSERT(m_materialState.m_emulationVertexShader);
        NWB_ASSERT(m_materialState.m_emulationInputLayout);
        const Name& meshComputeArchiveStageName = MaterialShaderStageNames::s_MeshComputeArchiveStageName;
        if(!m_shaderSystem.loadShader<ComputeShader>(
            resources.computeShader,
            materialInfo.meshShader.name(),
            meshShaderVariant,
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
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), StringConvert(materialKey.resolvedText()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_materialState.m_emulationInputLayout);
        emulationDesc.setVertexShader(m_materialState.m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        emulationDesc.addBindingLayout(*materialPassBindingLayout);
        emulationDesc
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        resources.emulationPipeline = device.createGraphicsPipeline(emulationDesc, framebuffer.getFramebufferInfo());
        if(!resources.emulationPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), StringConvert(materialKey.resolvedText()));
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        // The shared mesh_compute program is independent of authored materials.
        resources.sharedGeometryComputeProgram = pipelineKey.csgMode == MaterialPipelineCsgMode::None
            && materialInfo.meshShader.name() == Name("engine/graphics/mesh/shared_ms")
            && meshShaderVariant == Core::ShaderArchive::s_DefaultVariant
        ;
        resources.indexedGeometryOutput = resources.sharedGeometryComputeProgram;
        return true;
    };

    const bool meshSupported = m_graphics.queryFeatureSupport(Core::Feature::Meshlets);
    if(pass == MaterialPipelinePass::Opaque && !hasPixelShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a pixel shader"), StringConvert(materialKey.resolvedText()));
        return failMaterialPipeline();
    }

    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a mesh shader; compute emulation is derived internally from that mesh shader")
            , StringConvert(materialKey.resolvedText())
        );
        return failMaterialPipeline();
    }

    if(meshSupported){
        if(!tryBuildMeshPipeline()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create the required mesh rendering path for material '{}' on a mesh-capable device")
                , StringConvert(materialKey.resolvedText())
            );
            return failMaterialPipeline();
        }

        logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
        return &resources;
    }

    const bool indexedAvailable = (pipelineKey.csgMode == MaterialPipelineCsgMode::None || sharedObjectGeometry)
        && m_shaderSystem.hasShaderArchiveStage(
            materialInfo.meshShader.name(), objectGeometryShaderVariant, MaterialShaderStageNames::s_MeshObjectVertexArchiveStageName
        )
    ;
    if(indexedAvailable){
        if(!tryBuildIndexedPipeline())
            return failMaterialPipeline();
    }
    else if(!tryBuildComputePipeline()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation rendering path for material '{}' from its mesh shader")
            , StringConvert(materialKey.resolvedText())
        );
        return failMaterialPipeline();
    }

    logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
    return &resources;
}

Expected<MaterialPipelineResources*> RendererMaterialSystem::findRendererPipeline(const MaterialPipelineKey& pipelineKey){
    const auto foundPipeline = m_materialState.m_pipelines.find(pipelineKey);
    if(foundPipeline == m_materialState.m_pipelines.end())
        return MakeUnexpected(Failure{});

    MaterialPipelineResources& resources = foundPipeline.value();
    switch(resources.renderPath){
    case RenderPath::MeshShader:
        if(!resources.meshletPipeline)
            return MakeUnexpected(Failure{});
        break;
    case RenderPath::VertexIndexed:
        if(!resources.indexedPipeline || !resources.objectGeometryDecodePipeline)
            return MakeUnexpected(Failure{});
        break;
    case RenderPath::ComputeEmulation:
        if(!resources.computePipeline || !resources.emulationPipeline)
            return MakeUnexpected(Failure{});
        break;
    default:
        return MakeUnexpected(Failure{});
    }

    return &resources;
}

void RendererMaterialSystem::invalidateRendererPipelines(){
    m_materialState.m_pipelines.clear();
    m_materialState.m_objectGeometryDecodeShader.reset();
    m_materialState.m_objectGeometryDecodePipeline.reset();
    m_materialState.m_objectGeometryInputLayout.reset();
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
            StringConvert(materialKey.resolvedText())
        );
        break;
    }
    case RenderPath::VertexIndexed:{
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: material '{}' selected VertexIndexed + PS from persistent object-space geometry"), StringConvert(materialKey.resolvedText()));
        break;
    }
    case RenderPath::ComputeEmulation:{
        if(!meshSupported){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS by compiling its mesh shader for compute emulation because native mesh shaders are unavailable in the current graphics configuration"),
                StringConvert(materialKey.resolvedText())
            );
        }
        else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS through compute emulation"),
                StringConvert(materialKey.resolvedText())
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

