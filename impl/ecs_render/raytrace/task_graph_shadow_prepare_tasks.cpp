// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShadowPrepareGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    payload.outcome.ready = false;
    // The graph establishes the selector ConstantBuffer state before recording.
    if(!payload.targets.bindless.valid())
        return false;
    const auto shadowResourcesPrepared = payload.raytracingSystem.recordPreflightShadowVisibilityResources(
            commandList,
            payload.targets,
            payload.sceneTlasBuildGraphOwned,
            payload.meshBlasBuildsGraphOwned,
            payload.meshBlasGeometryBuildInputStatesGraphOwned,
            payload.meshSwBvhBuildsGraphOwned,
            payload.preparedMeshSwBvhBuildsRecordedByGraph
        );

    // Selector upload precedes this task; compiler establishes ConstantBuffer state first.
    if(!shadowResourcesPrepared)
        return false;
    payload.outcome.ready = *shadowResourcesPrepared;

    // These declarations export BLAS/SW-BVH boundary states before the Prefix packet seeds.
    return true;
}


void ShadowPrepareGraphTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    static_cast<void>(token);
    if(payload.currentBindlessSlotsGraphOwned)
        payload.targets.bindless.slotsUploaded = true;
    payload.raytracingSystem.confirmPreparedShadowTraceGeometryNormalization();
    if(payload.shadowMaterialContextBatchGraphOwned)
        payload.raytracingSystem.confirmPreparedShadowMaterialContextUploads();
    if(payload.sceneBvhBatchGraphOwned)
        payload.raytracingSystem.confirmPreparedSceneBvhUploads();
}


void ShadowPrepareGraphTask::Discarded(Payload& payload){
    payload.timingTicket.discard();

    // Failed preparation keeps storage but invalidates the frame plan and caches.
    payload.outcome.ready = false;
    payload.outcome.resourcesValid = false;
    payload.targets.bindless.slotsUploaded = payload.deferredBindlessSlotsWereUploaded;
    payload.raytracingSystem.discardPreflightShadowVisibilityResources();
}


bool ShadowPrepareSoftwareBvhBuildGraphTask::Record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    return payload.raytracingSystem.recordPreparedMeshSwBvhBuildAfterGraphClears(
        commandList,
        payload.build
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

