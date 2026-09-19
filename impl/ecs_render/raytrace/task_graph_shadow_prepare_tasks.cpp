// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>

#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShadowPrepareGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(
        !payload.graphics
        || !payload.raytracingSystem
        || !payload.outcome
        || !payload.targets
        || !payload.frameTimingTransaction
        || !payload.timingTicket
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(!payload.frameTimingTransaction->begin(
        RendererGpuTimingScope::s_Frame,
        payload.graphics->getDevice(),
        commandList
    ))
        return false;
    payload.outcome->ready = false;
    // Selector state is ConstantBuffer; normal graph frames need no native bridge.
    const bool shadowResourcesPrepared = payload.targets->bindless.valid()
        && payload.raytracingSystem->recordPreflightShadowVisibilityResources(
            commandList,
            *payload.targets,
            payload.outcome->ready,
            payload.shadowMaterialContextBatchGraphOwned,
            payload.sceneTlasBuildGraphOwned,
            payload.meshBlasBuildsGraphOwned,
            payload.meshBlasGeometryBuildInputStatesGraphOwned,
            payload.meshSwBvhBuildsGraphOwned,
            payload.preparedMeshSwBvhBuildsRecordedByGraph
        )
    ;
    // Selector upload precedes this task; compiler establishes ConstantBuffer state first.
    if(!shadowResourcesPrepared)
        return false;

    // These declarations export BLAS/SW-BVH boundary states before the Prefix packet seeds.
    return true;
}


void ShadowPrepareGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    static_cast<void>(token);
    if(payload.targets && payload.currentBindlessSlotsGraphOwned)
        payload.targets->bindless.slotsUploaded = true;
    if(payload.raytracingSystem)
        payload.raytracingSystem->confirmPreparedShadowTraceGeometryNormalization();
    if(payload.raytracingSystem && payload.shadowMaterialContextBatchGraphOwned)
        payload.raytracingSystem->confirmPreparedShadowMaterialContextUploads();
    if(payload.raytracingSystem && payload.sceneBvhBatchGraphOwned)
        payload.raytracingSystem->confirmPreparedSceneBvhUploads();
}


void ShadowPrepareGraphTask::discarded(Payload& payload){
    if(payload.timingTicket)
        payload.timingTicket->discard();
    if(!payload.raytracingSystem || !payload.outcome)
        return;

    // Failed preparation keeps storage but invalidates the frame plan and caches.
    payload.outcome->ready = false;
    payload.outcome->resourcesValid = false;
    if(payload.targets)
        payload.targets->bindless.slotsUploaded = payload.deferredBindlessSlotsWereUploaded;
    payload.raytracingSystem->discardPreflightShadowVisibilityResources();
}


bool ShadowPrepareSoftwareBvhBuildGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    if(!payload.raytracingSystem || !payload.timingTicket)
        return false;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    return payload.raytracingSystem->recordPreparedMeshSwBvhBuildAfterGraphClears(
        commandList,
        payload.build
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

