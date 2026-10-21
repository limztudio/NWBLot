// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_shared_compute_emulation_chain.h"

#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/generated_geometry_state.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_opaque_shared_compute_emulation_chain{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_GenerateResourceUseCapacity = 4u;
constexpr usize s_RasterResourceUseCapacity = 9u;
constexpr usize s_ResourceSetUseCapacity = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpaqueSharedComputeEmulationChainResult> OpaqueSharedComputeEmulationChainBuilder::declare(Core::Alloc::ScratchArena& scratchArena, const OpaqueSharedComputeEmulationChainInputs& inputs, Core::GpuTaskId* const tasks, const usize taskCapacity){
    using namespace RendererTaskGraphDetail;
    using namespace __hidden_opaque_shared_compute_emulation_chain;
    using OpaqueSharedPhase = ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Phase::Enum;

    const DeferredGraphResourceImportResult& resources = inputs.resources;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> generateResourceUses{ scratchArena };
    generateResourceUses.reserve(s_GenerateResourceUseCapacity);
    generateResourceUses.push_back(ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer));
    generateResourceUses.push_back(ReadBufferUse(resources.materialInstances, inputs.materialInstanceRange));
    generateResourceUses.push_back(ReadBufferUse(resources.materialTyped, inputs.materialTypedRange));
    generateResourceUses.push_back(WriteUse(inputs.sharedOutput, Core::ResourceStates::UnorderedAccess));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> rasterResourceUses{ scratchArena };
    rasterResourceUses.reserve(s_RasterResourceUseCapacity);
    rasterResourceUses.insert(rasterResourceUses.end(), inputs.objectGeometryReads.begin(), inputs.objectGeometryReads.end());
    rasterResourceUses.push_back(ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer));
    rasterResourceUses.push_back(ReadBufferUse(resources.materialInstances, inputs.materialInstanceRange));
    rasterResourceUses.push_back(ReadBufferUse(resources.materialTyped, inputs.materialTypedRange));
    rasterResourceUses.push_back(ReadUse(inputs.sharedOutput, ECSRenderDetail::s_GeneratedGeometryRasterState));
    rasterResourceUses.push_back(ReadWriteUse(resources.albedo, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.normal, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.worldPosition, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.specularRoughness, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.depth, Core::ResourceStates::DepthWrite));

    Core::GpuTaskResourceSetUse resourceSetUses[s_ResourceSetUseCapacity] = {};
    usize resourceSetUseCount = 0u;
    resourceSetUses[resourceSetUseCount++] = inputs.materialGeometrySetUse;
    if(inputs.materialSampledTextureSetUse.resourceSet.valid())
        resourceSetUses[resourceSetUseCount++] = inputs.materialSampledTextureSetUse;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    // Each phase has an explicit immediate predecessor. Allow that serial chain to remain in the existing primary-Graphics packet when FrontierSafe detects the normal downstream Compute consumer frontier.
    scheduling.allowMergeAcrossConsumerFrontier = true;

    const Name phaseIdentities[] = {
        Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_a"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_a"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_b"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_b"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_c"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_c"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_d"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_d"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_e"),
        Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_e"),
    };
    const AStringView phaseMarkers[] = {
        "Opaque Shared Compute Emulation Generate A",
        "Opaque Shared Compute Emulation Raster A",
        "Opaque Shared Compute Emulation Generate B",
        "Opaque Shared Compute Emulation Raster B",
        "Opaque Shared Compute Emulation Generate C",
        "Opaque Shared Compute Emulation Raster C",
        "Opaque Shared Compute Emulation Generate D",
        "Opaque Shared Compute Emulation Raster D",
        "Opaque Shared Compute Emulation Generate E",
        "Opaque Shared Compute Emulation Raster E",
    };
    const usize phaseCount = ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(inputs.plan.drawCount);
    if(!ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(phaseCount) || phaseCount > taskCapacity){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid shared opaque compute-emulation phase count"));
        return MakeUnexpected(Failure{});
    }
    Core::GpuTaskId dependency = inputs.gbufferTask;
    for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
        const bool isRasterPhase = phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
        const auto phaseTask = declarePhase(
            inputs,
            scheduling,
            phaseIdentities[phaseIndex],
            phaseMarkers[phaseIndex],
            dependency,
            isRasterPhase ? OpaqueSharedPhase::Raster : OpaqueSharedPhase::Generate,
            phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
            phaseIndex + 1u == phaseCount,
            isRasterPhase ? rasterResourceUses : generateResourceUses,
            resourceSetUses,
            resourceSetUseCount
        );
        if(!phaseTask){
            tasks[phaseIndex] = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared opaque compute-emulation phase"));
            return MakeUnexpected(Failure{});
        }
        tasks[phaseIndex] = *phaseTask;
        dependency = *phaseTask;
    }
    return OpaqueSharedComputeEmulationChainResult{ .completionTask = dependency, .taskCount = phaseCount, };
}


Expected<Core::GpuTaskId> OpaqueSharedComputeEmulationChainBuilder::declarePhase(
    const OpaqueSharedComputeEmulationChainInputs& inputs,
    const Core::GpuTaskSchedulingHint& scheduling,
    const Name& identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Phase::Enum phase,
    const usize drawIndex,
    const bool finishTiming,
    const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
    const Core::GpuTaskResourceSetUse* const resourceSetUses,
    const usize resourceSetUseCount
){
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(markerLabel)
        .setScheduling(scheduling)
        .setDependencies(&dependency, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(resourceSetUses, resourceSetUseCount)
    ;
    ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Payload payload{
        m_materialSystem,
        inputs.targets,
        inputs.timingTicket,
        inputs.meshViewSetupReady,
        inputs.sceneShadingSetupReady,
        inputs.sharedComputeEmulationTiming,
        inputs.frameBindings,
        {},
    };
    payload.plan = inputs.plan;
    payload.drawIndex = drawIndex;
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;
    payload.finishTiming = finishTiming;
    payload.phase = phase;
    return m_graph.addTask<ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask>(desc, Move(payload));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

