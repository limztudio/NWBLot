// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_extinction_integration_tasks.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitExtinctionComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitExtinction, MaterialPipelinePass::AvboitExtinction, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitExtinctionSharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    const AvboitComputeEmulationRecordTrait trait{ RendererGpuTimingScope::s_AvboitExtinction, MaterialPipelinePass::AvboitExtinction, &AvboitFrameTargets::lowFramebuffer, };

    return RecordAvboitSharedComputeEmulation(payload, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitExtinctionGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    return RecordAvboitRasterPassFromPayload(
        payload,
        commandList,
        [&](Core::CommandList& dispatchCommandList, const MaterialPassDrawItemPartitionsView& dispatchDrawItems, const CsgFrameGpuDataView& dispatchCsgFrameData, const usize dispatchInstanceCount, const usize dispatchMaterialTypedByteCount){
            payload.avboitSystem.renderAvboitExtinctionPass(
                dispatchCommandList,
                payload.targets,
                dispatchDrawItems,
                dispatchCsgFrameData,
                payload.csgResources,
                payload.frameBindings,
                dispatchInstanceCount,
                dispatchMaterialTypedByteCount,
                payload.materialFrameStatesGraphOwned,
                payload.materialGeometryStatesGraphOwned,
                payload.computeEmulationOutputStatesGraphOwned,
                payload.csgComputeEmulationOutputStatesGraphOwned,
                payload.generatedGeometryReused,
                payload.computeEmulationTiming
            );
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitIntegrationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    return RecordAvboitComputeEffect(
        payload,
        commandList,
        context,
        [&](Core::CommandList& dispatchCommandList, AvboitFrameTargets& targets, const Core::GpuTimingSampleAttribution attribution, b8* const timingRecorded){
            payload.avboitSystem.dispatchAvboitIntegration(dispatchCommandList, targets, attribution, timingRecorded);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

