// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_scheduling.h"

#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/kernel/task_graph_float_clear_tasks.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/task/gpu/capture/command_ir.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FloatTextureClearGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    Core::Texture* const destination = context.declarations.textureForResource(payload.destination);
    if(!destination || commandList.isRenderPassActive())
        return false;

    const Core::GpuClearTextureTaskDesc clearDesc{ .destination = payload.destination, .subresources = payload.subresources, .valueType = Core::GpuClearTextureTaskValueType::Float, .floatValue = payload.floatValue, };
    if(context.commandIrCapture && !context.commandIrCapture->captureClearTexture(context.task, context.packet, context.queue, payload.destination, clearDesc))
        return false;
    commandList.clearTextureFloat(*destination, clearDesc.subresources, clearDesc.floatValue);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BeginGraphClearTiming(
    Core::GraphicsRuntime* const graphics,
    Optional<Core::GpuTimingMeasure>* const timing,
    Core::GpuTimingSubmissionTicket* const timingTicket,
    const Core::GpuTimingScopeDefinition& scope,
    Core::CommandList& commandList
){
    if(!graphics || !timing || !timingTicket || *timing)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    timing->emplace(graphics->gpuTiming(), scope, graphics->getDevice(), commandList);
    return Core::FinishSplitGpuTimingMarker(timing);
}

b8 EndGraphClearTiming(Optional<Core::GpuTimingMeasure>* const timing, Core::GpuTimingSubmissionTicket* const timingTicket, Core::CommandList& commandList){
    if(!timing || !*timing || !timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    timing->value().finishTiming(commandList);
    timing->reset();
    return true;
}

b8 BeginGraphClearTimingRecord(void* const rawState, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return false;
    return BeginGraphClearTiming(state->graphics, state->timing, ResolveGraphClearTimingTicket(*state), state->scope, commandList);
}

b8 EndGraphClearTimingRecord(void* const rawState, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return false;
    return EndGraphClearTiming(state->timing, ResolveGraphClearTimingTicket(*state), commandList);
}

void DiscardGraphClearTimingRecord(void* const rawState){
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return;
    Core::DiscardGpuTimingMeasure(state->timing);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererGpuTimingScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Optional<Name>, NameTextError::Enum> DeferredGraphQueueInternalIdle(const Core::GpuPhysicalQueueId& queue, Core::Alloc::ScratchArena& scratchArena){
    if(!queue.valid())
        return Optional<Name>{};

    const AString<Core::Alloc::ScratchArena> scopeName = StringFormat(scratchArena, "render.deferred_graph.queue_{}.internal_idle", queue.index);
    return ToName(scopeName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EnableSameFamilyComputeEffectRouting(Core::GpuTaskSchedulingHint& scheduling, const b8 preserveDirectDependency)noexcept{
    scheduling.allowSameClassQueueRouting = true;
    scheduling.preferNonPrimarySameClassQueue = true;
    scheduling.preserveSameClassQueueWithDirectDependency = preserveDirectDependency;
}

Core::GpuTaskSchedulingHint SharedComputeEmulationChainScheduling()noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    return scheduling;
}

void MakeSharedComputeEmulationPhaseTaskDesc(
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUses,
    Core::GpuTaskDesc& desc
)noexcept{
    desc
        .setIdentity(identity)
        .setMarkerLabel(markerLabel)
        .setScheduling(SharedComputeEmulationChainScheduling())
        .setDependencies({ &dependency, 1u })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ resourceSetUses.empty() ? nullptr : resourceSetUses.data(), resourceSetUses.size() })
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

