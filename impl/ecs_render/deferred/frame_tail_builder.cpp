// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_tail_builder.h"

#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/ecs_render/kernel/task_graph_frame_recovery_task.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredFrameTailBuilder::DeferredFrameTailBuilder(Core::GpuTaskGraph& graph)noexcept
    : m_graph(graph){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<DeferredFrameTailResult> DeferredFrameTailBuilder::declare(const DeferredFrameTailInputs& inputs){
    using namespace RendererTaskGraphDetail;
    DeferredFrameTailResult result{};
    if(!inputs.terminalPresentationTask.valid())
        return MakeUnexpected(Failure{});

    if(inputs.capturesLaggedLightingHistory){
        // The core built-in derives whole-resource CopySource/CopyDest declarations for these regions and retains the imports itself. The array slices stay explicit only in the native copy body.
        // Caustic and surfel irradiance copy regions follow the shadow visibility slots.
        constexpr u32 s_IrradianceHistoryCopyRegionCount = 2u;
        Core::GpuCopyTextureTaskRegion historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + s_IrradianceHistoryCopyRegionCount] = {};
        for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
            Core::GpuCopyTextureTaskRegion& region = historyCopyRegions[shadowSlot];
            region.source = inputs.historyCopyShadowVisibility;
            region.destination = inputs.historyCopyDestinationShadowVisibility;
            region.sourceSlice.setArraySlice(shadowSlot);
            region.destinationSlice.setArraySlice(shadowSlot);
        }
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].source = inputs.historyCopyCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT].destination = inputs.historyCopyDestinationCausticIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].source = inputs.historyCopySurfelIrradiance;
        historyCopyRegions[NWB_SCENE_SHADOW_SLOT_COUNT + 1u].destination = inputs.historyCopyDestinationSurfelIrradiance;
        Core::GpuTaskSchedulingHint historyCopyScheduling;
        historyCopyScheduling.cost = Core::GpuTaskCostHint::Medium;
        historyCopyScheduling.forceSubmissionBoundary = true;
        historyCopyScheduling.allowPacketMerge = false;
        const Core::GpuTaskId historyCopyDependencies[] = { inputs.terminalPresentationTask };
        Core::GpuTaskDesc historyCopyDesc;
        historyCopyDesc.setIdentity(Name("render.lagged_history_copy")).setMarkerLabel("Lagged Lighting History Copy").setScheduling(historyCopyScheduling).setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies));
        const auto resultHistoryCopyTaskResult = m_graph.addCopyTextureTask(historyCopyDesc, Core::GpuCopyTextureTaskDesc{ .regions = historyCopyRegions, .regionCount = LengthOf(historyCopyRegions), .acceptedToken = &inputs.historyCopySubmissionToken, });
        if(!resultHistoryCopyTaskResult){
            result.historyCopyTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred lagged-lighting history-copy task"));
            return MakeUnexpected(Failure{});
        }
        result.historyCopyTask = *resultHistoryCopyTaskResult;
    }

    // Recovery is a late independent Graphics tail.
    // It deliberately has no packet dependency on normal work: a rejected suffix must not prevent it from retiring the accepted frame prefix.
    // Its compiled packet asks the graph transaction to join every accepted non-Graphics physical queue at submit time.
    const auto recoveryDomainResult = m_graph.importHazardDomain(HazardDomainDesc(Name("render.frame_recovery.timing"), "Frame Recovery Timing"));
    if(!recoveryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred frame-recovery graph resources"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuGraphResourceId recoveryDomain = *recoveryDomainResult;

    const Core::GpuTaskResourceUse recoveryResourceUses[] = { ReadWriteUse(recoveryDomain, Core::ResourceStates::Common), };
    Core::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.cost = Core::GpuTaskCostHint::Tiny;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    Core::GpuTaskDesc recoveryDesc;
    recoveryDesc.setIdentity(Name("render.frame_recovery")).setMarkerLabel("Frame Recovery").setScheduling(recoveryScheduling).setResourceUses(recoveryResourceUses, LengthOf(recoveryResourceUses));
    const auto resultRecoveryTaskResult = m_graph.addTask<ECSRenderDetail::FrameRecoveryGraphTask>(
        recoveryDesc,
        ECSRenderDetail::FrameRecoveryGraphTask::Payload{ .frameTimingTransaction = inputs.frameTimingTransaction, .armed = inputs.recoveryArmed, .retiresFrameTiming = inputs.recoveryRetiresFrameTiming, }
    );
    if(!resultRecoveryTaskResult){
        result.recoveryTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred frame-recovery graph task"));
        return MakeUnexpected(Failure{});
    }
    result.recoveryTask = *resultRecoveryTaskResult;

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

