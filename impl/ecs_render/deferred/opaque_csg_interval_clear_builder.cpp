// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_csg_interval_clear_builder.h"

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>

#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpaqueCsgIntervalClearBuilder::OpaqueCsgIntervalClearBuilder(
    Core::GpuTaskGraph& graph
)
    : m_graph(graph){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool OpaqueCsgIntervalClearBuilder::declare(
    const OpaqueCsgIntervalClearInputs& inputs,
    GraphClearTimingRecordState& clearTimingState,
    OpaqueCsgIntervalClearResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = OpaqueCsgIntervalClearResult{};
    if(!inputs.targets || !inputs.csgFrameData || !inputs.dependencyTask.valid())
        return false;
    if(!inputs.hasOpaqueCsgFrameWork)
        return true;
    if(!inputs.csgIntervalId.valid() || !inputs.csgReceiverEventCount.valid())
        return false;

    Core::GpuTaskSchedulingHint csgIntervalClearScheduling;
    csgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    csgIntervalClearScheduling.forceSubmissionBoundary = false;
    csgIntervalClearScheduling.allowPacketMerge = true;
    csgIntervalClearScheduling.mergeWithPrevious = true;
    const auto makeCsgIntervalClearTaskDesc = [&csgIntervalClearScheduling](
        const Name identity,
        const AStringView markerLabel,
        const Core::GpuTaskId& dependency
    ){
        Core::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(identity)
            .setMarkerLabel(markerLabel)
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(csgIntervalClearScheduling)
            .setDependencies(&dependency, 1u)
        ;
        return clearDesc;
    };
    const Core::Rect csgClearRect = inputs.csgFrameData->workRegion.resolveRect(
        inputs.targets->width,
        inputs.targets->height
    );
    const Core::GpuClearTextureTaskRecordHooks csgIntervalClearBeginHooks{
        .context = &clearTimingState,
        .beforeClear = &BeginGraphClearTimingRecord,
        .discarded = &DiscardGraphClearTimingRecord,
    };
    const Core::GpuClearTextureTaskRecordHooks csgIntervalClearEndHooks{
        .context = &clearTimingState,
        .afterClear = &EndGraphClearTimingRecord,
        .discarded = &DiscardGraphClearTimingRecord,
    };
    Core::GpuTaskId clearTask = m_graph.addClearTextureRectUIntTask(
        makeCsgIntervalClearTaskDesc(
            Name("render.graphics_prefix.csg_interval_clear"),
            "CSG Interval Id Clear",
            inputs.dependencyTask
        ),
        Core::GpuClearTextureRectUIntTaskDesc{
            .destination = inputs.csgIntervalId,
            .subresources = inputs.csgPeelSubresources,
            .rect = csgClearRect,
            .uintValue = Core::UIntColor(0u),
            .recordHooks = csgIntervalClearBeginHooks,
        }
    );
    if(!clearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval-id clear"));
        return false;
    }
    outResult.clearFirstTask = clearTask;
    Core::GpuTaskSchedulingHint csgIntervalClearTailScheduling = csgIntervalClearScheduling;
    // The timing endpoint must remain in the first clear's Graphics packet even when another queue observes the
    // interval id. The explicit immediate dependency satisfies FrontierSafe's consumer-frontier override.
    csgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc csgIntervalClearTailDesc;
    csgIntervalClearTailDesc
        .setIdentity(Name("render.graphics_prefix.csg_receiver_event_count_clear"))
        .setMarkerLabel("CSG Receiver Event Count Clear")
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(csgIntervalClearTailScheduling)
        .setDependencies(&clearTask, 1u)
    ;
    clearTask = m_graph.addClearTextureRectUIntTask(
        csgIntervalClearTailDesc,
        Core::GpuClearTextureRectUIntTaskDesc{
            .destination = inputs.csgReceiverEventCount,
            .subresources = inputs.csgReceiverEventCountSubresources,
            .rect = csgClearRect,
            .uintValue = Core::UIntColor(0u),
            .recordHooks = csgIntervalClearEndHooks,
        }
    );
    if(!clearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver-event clear"));
        return false;
    }
    outResult.clearTask = clearTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

