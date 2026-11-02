// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_occupancy_tasks.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitPreGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    // A CSG upload without its frozen stream would detach clears and consumers.
    if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    if(payload.hasTransparentRenderers && payload.transparentCsgStreamsUploaded){
        payload.avboitSystem.renderAvboitTransparentCsgIntervals(
            commandList,
            payload.targets,
            payload.transparentCsgSnapshot.receiverSurfaceDrawItemsView(),
            payload.transparentCsgSnapshot.csgFrameDataView(),
            payload.csgResources,
            payload.frameBindings,
            payload.transparentCsgSnapshot.instanceCount,
            payload.transparentCsgSnapshot.materialTypedByteCount,
            payload.transparentCsgMaterialGeometryStatesGraphOwned,
            payload.transparentCsgIntervalsTiming
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitOccupancyComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitOccupancySharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitOccupancy, MaterialPipelinePass::AvboitOccupancy, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitSharedComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitOccupancyGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    return RecordAvboitRasterPassFromPayload(payload, commandList, &RendererAvboitSystem::renderAvboitOccupancyPass);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitDepthWarpGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    return RecordAvboitComputeEffect(payload, commandList, context, &RendererAvboitSystem::dispatchAvboitDepthWarp);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

