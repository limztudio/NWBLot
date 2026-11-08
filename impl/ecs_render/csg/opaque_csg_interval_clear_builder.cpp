// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_csg_interval_clear_builder.h"

#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/csg/graph_types.adhd.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeclareCsgIntervalRectClearTasks(
    const CsgIntervalRectClearDesc& desc,
    Core::GpuTaskGraph& graph,
    GraphClearTimingRecordState& clearTimingState,
    Core::GpuTaskId& outFirstTask,
    Core::GpuTaskId& outTailTask
){
    Core::GpuTaskSchedulingHint csgIntervalClearScheduling;
    csgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    csgIntervalClearScheduling.forceSubmissionBoundary = false;
    csgIntervalClearScheduling.allowPacketMerge = true;
    csgIntervalClearScheduling.mergeWithPrevious = true;

    Core::GpuTaskDesc firstDesc;
    firstDesc.setIdentity(desc.intervalClearIdentity).setMarkerLabel(desc.intervalClearMarkerLabel).setScheduling(csgIntervalClearScheduling).setDependencies({ &desc.dependencyTask, 1u });
    const Core::GpuClearTextureTaskRecordHooks csgIntervalClearBeginHooks{ .context = &clearTimingState, .beforeClear = &BeginGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
    const Core::GpuClearTextureTaskRecordHooks csgIntervalClearEndHooks{ .context = &clearTimingState, .afterClear = &EndGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
    const auto firstTaskResult = graph.addClearTextureRectUIntTask(
        firstDesc,
        Core::GpuClearTextureRectUIntTaskDesc{ .destination = desc.csgIntervalId, .subresources = desc.csgPeelSubresources, .rect = desc.clearRect, .uintValue = Core::UIntColor(0u), .recordHooks = csgIntervalClearBeginHooks, });
    if(!firstTaskResult){
        outFirstTask = {};
        NWB_LOGGER_WARNING(desc.intervalClearWarningMessage);
        return false;
    }
    outFirstTask = *firstTaskResult;

    Core::GpuTaskSchedulingHint csgIntervalClearTailScheduling = csgIntervalClearScheduling;
    // The endpoint stays in the first clear's Graphics packet even when another queue observes the interval id.
    csgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc tailDesc;
    tailDesc.setIdentity(desc.receiverEventClearIdentity).setMarkerLabel(desc.receiverEventClearMarkerLabel).setScheduling(csgIntervalClearTailScheduling).setDependencies({ &outFirstTask, 1u });
    const auto tailTaskResult = graph.addClearTextureRectUIntTask(
        tailDesc,
        Core::GpuClearTextureRectUIntTaskDesc{ .destination = desc.csgReceiverEventCount, .subresources = desc.csgReceiverEventCountSubresources, .rect = desc.clearRect, .uintValue = Core::UIntColor(0u), .recordHooks = csgIntervalClearEndHooks, });
    if(!tailTaskResult){
        outTailTask = {};
        NWB_LOGGER_WARNING(desc.receiverEventClearWarningMessage);
        return false;
    }
    outTailTask = *tailTaskResult;
    return true;
}

Expected<OpaqueCsgIntervalClearResult> OpaqueCsgIntervalClearBuilder::declare(const OpaqueCsgIntervalClearInputs& inputs, GraphClearTimingRecordState& clearTimingState){
    OpaqueCsgIntervalClearResult result{};
    if(!inputs.dependencyTask.valid())
        return MakeUnexpected(Failure{});
    if(!inputs.hasOpaqueCsgFrameWork)
        return result;
    if(!inputs.csgIntervalId.valid() || !inputs.csgReceiverEventCount.valid())
        return MakeUnexpected(Failure{});

    const CsgIntervalRectClearDesc clearDesc{
        .intervalClearIdentity = Name(s_RenderNameRenderGraphicsPrefixCsgIntervalClear),
        .intervalClearMarkerLabel = "CSG Interval Id Clear",
        .intervalClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval-id clear"),
        .receiverEventClearIdentity = Name(s_RenderNameRenderGraphicsPrefixCsgReceiverEventCountClear),
        .receiverEventClearMarkerLabel = "CSG Receiver Event Count Clear",
        .receiverEventClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver-event clear"),
        .csgIntervalId = inputs.csgIntervalId,
        .csgReceiverEventCount = inputs.csgReceiverEventCount,
        .csgPeelSubresources = inputs.csgPeelSubresources,
        .csgReceiverEventCountSubresources = inputs.csgReceiverEventCountSubresources,
        .clearRect = ToCoreRect(inputs.csgFrameData.workRegion.resolveRect(inputs.targets.width, inputs.targets.height)),
        .dependencyTask = inputs.dependencyTask,
    };
    if(!DeclareCsgIntervalRectClearTasks(clearDesc, m_graph, clearTimingState, result.clearFirstTask, result.clearTask))
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

