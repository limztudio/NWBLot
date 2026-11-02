// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

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

[[nodiscard]] Core::GraphicsString BuildCsgProjectEvaluatorModuleAssignment(Core::GraphicsArena& arena, const AStringView moduleInclude){
    Core::GraphicsString assignment(arena);
    if(moduleInclude.empty())
        return assignment;

    assignment.reserve(s_CsgProjectEvaluatorModuleDefineName.size() + moduleInclude.size() + s_ModuleAssignmentPunctuationSize);
    assignment += s_CsgProjectEvaluatorModuleDefineName;
    assignment += "=\"";
    assignment += moduleInclude;
    assignment += '"';
    return assignment;
}

[[nodiscard]] Expected<Core::GraphicsString> BuildCsgShaderVariantName(Core::GraphicsArena& arena, const AStringView baseVariant, const AStringView projectEvaluatorModuleAssignment){
    Array<ShaderVariantDefineAssignment, s_MaxCsgClipShaderVariantDefineAssignments> defineAssignments;
    usize defineAssignmentCount = 0u;

    defineAssignments[defineAssignmentCount++] = { s_CsgEnabledDefineName, s_CsgEnabledDefineAssignment };
    defineAssignments[defineAssignmentCount++] = { s_CsgIntervalSampleEnabledDefineName, s_CsgIntervalSampleEnabledDefineAssignment };
    if(!projectEvaluatorModuleAssignment.empty())
        defineAssignments[defineAssignmentCount++] = { s_CsgProjectEvaluatorModuleDefineName, projectEvaluatorModuleAssignment };

    return ECSRenderMaterialShaderVariants::BuildCsgClipShaderVariantName(arena, baseVariant, Span<const ShaderVariantDefineAssignment>(defineAssignments.data(), defineAssignmentCount));
}

struct MaterialPipelineAvboitPixelShaderSelection{
    const Core::Assets::AssetRef<PixelShader>* materialShader = nullptr;
    Name debugName = s_InvalidAvboitPixelShaderDebugName;


public:
    [[nodiscard]] b8 materialDriven()const noexcept{ return materialShader != nullptr && materialShader->valid(); }

    [[nodiscard]] Name shaderName()const noexcept{ return materialDriven() ? materialShader->name() : s_NameNone; }
};

[[nodiscard]] MaterialPipelineAvboitPixelShaderSelection SelectAvboitPixelShader(const MaterialPipelinePass::Enum pass, const MaterialSurfaceInfo& materialInfo)noexcept{
    MaterialPipelineAvboitPixelShaderSelection selection;
    selection.debugName = ResolveAvboitPixelShaderDebugName(pass);
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
        selection.materialShader = &materialInfo.avboitOccupancyPixelShader;
        break;
    case MaterialPipelinePass::AvboitExtinction:
        selection.materialShader = &materialInfo.avboitExtinctionPixelShader;
        break;
    case MaterialPipelinePass::AvboitAccumulate:
    case MaterialPipelinePass::AvboitRefractionCapture:
        // The draw mode selects optical capture in the existing material-authored accumulation shader.
        selection.materialShader = &materialInfo.avboitAccumulatePixelShader;
        break;
    default:
        break;
    }
    return selection;
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
        csgProjectEvaluatorModuleAssignment = __hidden_material_pipeline::BuildCsgProjectEvaluatorModuleAssignment(m_arena, AStringView(*csgProjectEvaluatorModuleInclude));
        if(!csgProjectEvaluatorModuleAssignment.empty()){
            AStringView existingEvaluatorModuleAssignment;
            const b8 hasExistingEvaluatorModuleAssignment = TryFindVariantDefineAssignment(shaderVariant, s_CsgProjectEvaluatorModuleDefineName, Core::ShaderArchive::s_DefaultVariant, existingEvaluatorModuleAssignment);
            if(hasExistingEvaluatorModuleAssignment && existingEvaluatorModuleAssignment != AStringView(csgProjectEvaluatorModuleAssignment)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' uses a different CSG evaluator module than its active cutters"), ConvertString(materialKey.resolvedText()));
                return failMaterialPipeline();
            }
            if(!hasExistingEvaluatorModuleAssignment)
                materialProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
            avboitProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
        }
    }
    if(csgClipPipeline && !avboitCsgClipPipeline){
        auto variant = __hidden_material_pipeline::BuildCsgShaderVariantName(m_arena, shaderVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = __hidden_material_pipeline::BuildCsgShaderVariantName(m_arena, shaderVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG mesh shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = __hidden_material_pipeline::BuildCsgShaderVariantName(m_arena, Core::ShaderArchive::s_DefaultVariant, avboitProjectEvaluatorModuleAssignmentToAdd);
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
    __hidden_material_pipeline::MaterialPipelineAvboitPixelShaderSelection avboitPixelShaderSelection;
    if(MaterialPipelinePassUsesRendererAvboit(pass)){
        avboitPixelShaderSelection = __hidden_material_pipeline::SelectAvboitPixelShader(pass, materialInfo);
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

    auto loadPassPixelShader = [&]() -> b8{
        if(pass == MaterialPipelinePass::Opaque)
            return m_shaderSystem.loadShader<PixelShader>(materialInfo.pixelShader.name(), pixelShaderVariant, s_RendererPsDebugName, resources.pixelShader);
        if(pass == MaterialPipelinePass::CsgReceiverSurface)
            return m_shaderSystem.loadShader<PixelShader>(&CsgSubsystem::receiverSurfacePs, Core::ShaderArchive::s_DefaultVariant, s_CsgReceiverSurfacePsDebugName, resources.pixelShader);
        if(avboitCsgClipPipeline)
            return m_shaderSystem.loadShader<PixelShader>(passPixelShaderName, AStringView(avboitCsgShaderVariant), passPixelShaderDebugName, resources.pixelShader);
        // AVBOIT pixel shaders are generated per material with its binding/surface contract; load the resolved pass shader at its default variant.
        return m_shaderSystem.loadShader<PixelShader>(passPixelShaderName, Core::ShaderArchive::s_DefaultVariant, passPixelShaderDebugName, resources.pixelShader);
    };

    auto tryBuildMeshPipeline = [&]() -> b8{
        if(!m_shaderSystem.loadShader<MeshShader>(materialInfo.meshShader.name(), meshShaderVariant, s_RendererMeshDebugName, resources.meshShader))
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
        pipelineDesc.addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());

        auto meshletPipelineResult = device.createMeshletPipeline(pipelineDesc, framebuffer.getFramebufferInfo());
        if(meshletPipelineResult)
            resources.meshletPipeline = Move(*meshletPipelineResult);
        else
            resources.meshletPipeline.reset();
        if(!meshletPipelineResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), ConvertString(materialKey.resolvedText()));
            return false;
        }

        resources.renderPath = RenderPath::MeshShader;
        return true;
    };

    auto tryBuildIndexedPipeline = [&]() -> b8{
        if(!createObjectGeometryPipelineResources(resources))
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
        auto indexedPipelineResult = device.createGraphicsPipeline(desc, framebuffer.getFramebufferInfo());
        if(indexedPipelineResult)
            resources.indexedPipeline = Move(*indexedPipelineResult);
        else
            resources.indexedPipeline.reset();
        if(!indexedPipelineResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create indexed object geometry pipeline for material '{}'"), ConvertString(materialKey.resolvedText()));
            return false;
        }
        resources.renderPath = RenderPath::VertexIndexed;
        return true;
    };

    auto tryBuildComputePipeline = [&]() -> b8{
        if(!createComputeEmulationResources())
            return false;
        NWB_ASSERT(m_materialState.m_computeBindingLayout);
        NWB_ASSERT(m_materialState.m_emulationVertexShader);
        NWB_ASSERT(m_materialState.m_emulationInputLayout);
        const Name& meshComputeArchiveStageName = ShaderBinaryPayload::s_MeshComputeArchiveStageName;
        if(!m_shaderSystem.loadShader<ComputeShader>(materialInfo.meshShader.name(), meshShaderVariant, s_RendererCsDebugName, &meshComputeArchiveStageName, resources.computeShader))
            return false;
        if(!loadPassPixelShader())
            return false;
        // The compute-emulation mesh stage shares the mesh-shader source-stream runtime, so it needs the persistent tables too.
        if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, resources.computeShader, m_materialState.m_computeBindingLayout, false, resources.computePipeline)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), ConvertString(materialKey.resolvedText()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_materialState.m_emulationInputLayout);
        emulationDesc.setVertexShader(m_materialState.m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        emulationDesc.addBindingLayout(*materialPassBindingLayout);
        emulationDesc.addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        auto emulationPipelineResult = device.createGraphicsPipeline(emulationDesc, framebuffer.getFramebufferInfo());
        if(emulationPipelineResult)
            resources.emulationPipeline = Move(*emulationPipelineResult);
        else
            resources.emulationPipeline.reset();
        if(!emulationPipelineResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), ConvertString(materialKey.resolvedText()));
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        // The shared mesh_compute program is independent of authored materials.
        resources.sharedGeometryComputeProgram = pipelineKey.csgMode == MaterialPipelineCsgMode::None
            && sharedMeshShaderAssetName
            && materialInfo.meshShader.name() == sharedMeshShaderAssetName
            && meshShaderVariant == Core::ShaderArchive::s_DefaultVariant
        ;
        resources.indexedGeometryOutput = resources.sharedGeometryComputeProgram;
        return true;
    };

    const b8 meshSupported = m_graphics.queryFeatureSupport(Core::Feature::Meshlets);
    // Surface info admission already requires the mesh shader and the opaque pixel shader.
    NWB_ASSERT(pass != MaterialPipelinePass::Opaque || materialInfo.pixelShader.valid());
    NWB_ASSERT(materialInfo.meshShader.valid());

    if(meshSupported){
        if(!tryBuildMeshPipeline()){
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
        if(!tryBuildIndexedPipeline())
            return failMaterialPipeline();
    }
    else if(!tryBuildComputePipeline()){
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

    switch(renderPath){
    case RenderPath::MeshShader:{
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: material '{}' selected MeshShader + PS on this device"), ConvertString(materialKey.resolvedText()));
        break;
    }
    case RenderPath::VertexIndexed:{
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: material '{}' selected VertexIndexed + PS from persistent object-space geometry"), ConvertString(materialKey.resolvedText()));
        break;
    }
    case RenderPath::ComputeEmulation:{
        if(!meshSupported){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: material '{}' selected CS + PS by compiling its mesh shader for compute emulation because native mesh shaders are unavailable in the current graphics configuration")
                , ConvertString(materialKey.resolvedText())
            );
        }
        else
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: material '{}' selected CS + PS through compute emulation"), ConvertString(materialKey.resolvedText()));
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

