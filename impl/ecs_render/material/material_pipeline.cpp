// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"
#include "material_pipeline_builder.h"
#include <impl/ecs_render/material/csg_shader_variant_builder.adhd.h>

#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/material/material_shader_variants_private.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_material/types.adhd.h>
#include <impl/ecs_csg/shape_registry.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GraphicsString> ResolveCsgProjectEvaluatorModuleInclude(Core::GraphicsArena& arena, const CsgShapeRegistry& shapeRegistry, const Name& evaluatorVariant){
    if(!evaluatorVariant)
        return Core::GraphicsString(arena);
    auto moduleInclude = shapeRegistry.findShaderModuleInclude(evaluatorVariant, arena);
    if(!moduleInclude || moduleInclude->empty())
        return MakeUnexpected(Failure{});
    return moduleInclude;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNull<MaterialPipelineResources*>> RendererMaterialSystem::createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer& framebuffer){
    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    NWB_ASSERT(materialKey);

    auto [it, inserted] = m_materialState.m_pipelines.try_emplace(pipelineKey);
    MaterialPipelineResources& resources = it.value();
    switch(resources.renderPath){
    case RenderPath::MeshShader:
        if(resources.meshletPipeline)
            return MakeNotNull(&resources);
        break;
    case RenderPath::VertexIndexed:
        if(resources.indexedPipeline && resources.objectGeometryDecodePipeline)
            return MakeNotNull(&resources);
        break;
    case RenderPath::ComputeEmulation:
        if(resources.computePipeline && resources.emulationPipeline)
            return MakeNotNull(&resources);
        break;
    default:
        break;
    }

    auto removeFailedEntry = [&]()noexcept{
        if(inserted)
            m_materialState.m_pipelines.erase(it);
    };
    auto failMaterialPipeline = [&]()noexcept{
        removeFailedEntry();
        return MakeUnexpected(Failure{});
    };

    NWB_ASSERT(!materialInfo.shaderVariant.empty());
    const AStringView shaderVariant(materialInfo.shaderVariant.data(), materialInfo.shaderVariant.size());
    Core::GraphicsString csgShaderVariant(m_arena);
    Core::GraphicsString avboitCsgShaderVariant(m_arena);
    const MaterialPipelineCsgBindingUse csgBindingUse = MaterialPipelineResolveCsgBindingUse(pipelineKey.csgMode, pass);
    const b8 csgClipPipeline = csgBindingUse.clip;
    const b8 avboitCsgClipPipeline = csgBindingUse.avboitClip;
    Core::GraphicsString csgProjectEvaluatorModuleAssignment(m_arena);
    AStringView materialProjectEvaluatorModuleAssignmentToAdd;
    AStringView avboitProjectEvaluatorModuleAssignmentToAdd;
    if(csgClipPipeline){
        const auto csgProjectEvaluatorModuleInclude = __hidden_material_pipeline::ResolveCsgProjectEvaluatorModuleInclude(m_arena, m_csgShapeRegistry, pipelineKey.csgEvaluatorVariant);
        if(!csgProjectEvaluatorModuleInclude){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to resolve CSG evaluator module for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgProjectEvaluatorModuleAssignment = CsgShaderVariantBuilder().buildEvaluatorAssignment(AStringView(*csgProjectEvaluatorModuleInclude));
        if(!csgProjectEvaluatorModuleAssignment.empty()){
            const Expected<AStringView> existingEvaluatorModuleAssignment = CsgShaderVariantBuilder().findDefineAssignment(shaderVariant, s_CsgProjectEvaluatorModuleDefineName, Core::ShaderArchive::s_DefaultVariant);
            if(existingEvaluatorModuleAssignment && *existingEvaluatorModuleAssignment != AStringView(csgProjectEvaluatorModuleAssignment)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' uses a different CSG evaluator module than its active cutters"), ConvertString(materialKey.resolvedText()));
                return failMaterialPipeline();
            }
            if(!existingEvaluatorModuleAssignment)
                materialProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
            avboitProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
        }
    }
    if(csgClipPipeline && !avboitCsgClipPipeline){
        auto variant = CsgShaderVariantBuilder().buildVariant(shaderVariant, Core::ShaderArchive::s_DefaultVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = CsgShaderVariantBuilder().buildVariant(shaderVariant, Core::ShaderArchive::s_DefaultVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG mesh shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = CsgShaderVariantBuilder().buildVariant(Core::ShaderArchive::s_DefaultVariant, Core::ShaderArchive::s_DefaultVariant, avboitProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG pixel shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        avboitCsgShaderVariant = Move(*variant);
    }
    const AStringView pixelShaderVariant = csgClipPipeline && !avboitCsgClipPipeline ? AStringView(csgShaderVariant) : shaderVariant;
    const AStringView meshShaderVariant = csgClipPipeline ? AStringView(csgShaderVariant) : shaderVariant;

    const Name sharedMeshShaderAssetName = m_shaderSystem.resolveSubsystemAsset<MeshSubsystem>(&MeshSubsystem::sharedMs);
    const Name objectVertexShaderAssetName = m_shaderSystem.resolveSubsystemAsset<MeshSubsystem>(&MeshSubsystem::objectVs);
    const b8 sharedObjectGeometry = sharedMeshShaderAssetName && materialInfo.meshShader.name() == sharedMeshShaderAssetName && shaderVariant == Core::ShaderArchive::s_DefaultVariant;

    Name passPixelShaderName = s_NameNone;
    Name passPixelShaderDebugName = s_InvalidPassPixelShaderDebugName;
    MaterialPipelineAvboitPixelShaderSelection avboitPixelShaderSelection;
    if(MaterialPipelinePassUsesRendererAvboit(pass)){
        avboitPixelShaderSelection = SelectAvboitPixelShader(
            pass,
            materialInfo.avboitOccupancyPixelShader,
            materialInfo.avboitExtinctionPixelShader,
            materialInfo.avboitAccumulatePixelShader
        );
        NWB_ASSERT(!materialInfo.transparent || avboitPixelShaderSelection.materialDriven());
    }
    switch(pass){
    case MaterialPipelinePass::Opaque:
    case MaterialPipelinePass::CsgReceiverSurface:
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

    const MaterialPipelineBuildInputs buildInputs{
        .system = *this,
        .shaderSystem = m_shaderSystem,
        .materialState = m_materialState,
        .resources = resources,
        .materialInfo = materialInfo,
        .pipelineKey = pipelineKey,
        .framebuffer = framebuffer,
        .device = device,
        .heap = heap,
        .renderState = renderState,
        .materialPassBindingLayout = *materialPassBindingLayout,
        .materialKey = materialKey,
        .sharedMeshShaderAssetName = sharedMeshShaderAssetName,
        .passPixelShaderName = passPixelShaderName,
        .passPixelShaderDebugName = passPixelShaderDebugName,
        .pixelShaderVariant = pixelShaderVariant,
        .meshShaderVariant = meshShaderVariant,
        .avboitCsgShaderVariant = AStringView(avboitCsgShaderVariant),
        .pass = pass,
        .avboitCsgClipPipeline = avboitCsgClipPipeline,
    };
    MaterialPipelineBuilder pipelineBuilder(buildInputs);

    const b8 meshSupported = m_graphics.queryFeatureSupport(Core::Feature::Meshlets);
    // Surface info admission already requires the mesh shader and the opaque pixel shader.
    NWB_ASSERT(pass != MaterialPipelinePass::Opaque || materialInfo.pixelShader.valid());
    NWB_ASSERT(materialInfo.meshShader.valid());

    if(meshSupported){
        if(!pipelineBuilder.buildMesh()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create the required mesh rendering path for material '{}' on a mesh-capable device"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }

        logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
        return MakeNotNull(&resources);
    }

    const b8 indexedAvailable = sharedObjectGeometry
        && objectVertexShaderAssetName
        && m_shaderSystem.hasShaderArchiveStage(objectVertexShaderAssetName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::VertexStage))
    ;
    if(indexedAvailable){
        if(!pipelineBuilder.buildIndexed())
            return failMaterialPipeline();
    }
    else if(!pipelineBuilder.buildCompute()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation rendering path for material '{}' from its mesh shader"), ConvertString(materialKey.resolvedText()));
        return failMaterialPipeline();
    }

    logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
    return MakeNotNull(&resources);
}

Expected<NotNull<MaterialPipelineResources*>> RendererMaterialSystem::findRendererPipeline(const MaterialPipelineKey& pipelineKey)noexcept{
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

    return MakeNotNull(&resources);
}

void RendererMaterialSystem::invalidateRendererPipelines()noexcept{
    m_materialState.m_pipelines.clear();
    m_materialState.m_objectGeometryDecodeShader.reset();
    m_materialState.m_objectGeometryDecodePipeline.reset();
    m_materialState.m_objectGeometryInputLayout.reset();
}

void RendererMaterialSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath::Enum renderPath, const b8 meshSupported){
    auto [it, inserted] = m_materialState.m_loggedMaterialPaths.try_emplace(materialKey, renderPath);
    if(!inserted){
        if(it.value() == renderPath)
            return;
        it.value() = renderPath;
    }

    LogMaterialRenderPathDecision(materialKey, renderPath, meshSupported);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

