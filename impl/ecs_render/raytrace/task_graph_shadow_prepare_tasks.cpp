// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>

#include <impl/ecs_render/kernel/renderer_private.h>

#include <core/graphics/gpu_timing.h>


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
    if(!payload.renderer || !payload.targets || !payload.frameTimingTransaction || !payload.timingTicket)
        return false;

    RendererSystem& renderer = *payload.renderer;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    if(!payload.frameTimingTransaction->begin(
        RendererGpuTimingScope::s_Frame,
        renderer.m_graphics.getDevice(),
        commandList
    ))
        return false;
    renderer.m_preparedShadowVisibilityReady = false;
    // The compiled ConstantBuffer use established this packet's selector state before this thunk records. The
    // retained descriptor-visible state is also ConstantBuffer, so normal graph frames need no native bridge.
    const bool shadowResourcesPrepared = payload.targets->bindless.valid()
        && renderer.m_raytracingSystem.recordPreflightShadowVisibilityResources(
            commandList,
            *payload.targets,
            renderer.m_preparedShadowVisibilityReady,
            payload.shadowMaterialContextBatchGraphOwned,
            payload.sceneBvhBatchGraphOwned,
            payload.sceneTlasBuildGraphOwned,
            payload.meshBlasBuildsGraphOwned,
            payload.meshBlasGeometryBuildInputStatesGraphOwned,
            payload.meshSwBvhBuildsGraphOwned,
            payload.preparedMeshSwBvhBuildsRecordedByGraph,
            payload.deferHybridSoftwareTail
        )
    ;
    // The immutable material-context selector upload precedes this task and the compiler establishes its
    // ConstantBuffer state before native preparation records.
    if(!shadowResourcesPrepared)
        return false;

    // These declarations, and the adjacent hybrid-tail ShaderResource reads when present, export every selected
    // BLAS/SW-BVH input's exact graph-visible boundary state before the following Prefix packet is seeded.
    // Route-local build work remains inside this callback.
    return true;
}


void ShadowPrepareGraphTask::accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    static_cast<void>(token);
    if(payload.targets && payload.currentBindlessSlotsGraphOwned)
        payload.targets->bindless.slotsUploaded = true;
    if(payload.renderer)
        payload.renderer->m_raytracingSystem.confirmPreparedShadowTraceGeometryNormalization();
    if(payload.renderer && payload.shadowMaterialContextBatchGraphOwned)
        payload.renderer->m_raytracingSystem.confirmPreparedShadowMaterialContextUploads();
    if(payload.renderer && payload.sceneBvhBatchGraphOwned)
        payload.renderer->m_raytracingSystem.confirmPreparedSceneBvhUploads();
}


void ShadowPrepareGraphTask::discarded(Payload& payload){
    if(payload.timingTicket)
        payload.timingTicket->discard();
    if(!payload.renderer)
        return;

    RendererSystem& renderer = *payload.renderer;
    // Failed preparation keeps resource storage but invalidates the selected frame plan and every semantic cache.
    renderer.m_preparedShadowVisibilityReady = false;
    renderer.m_preparedShadowVisibilityResourcesValid = false;
    if(payload.targets)
        payload.targets->bindless.slotsUploaded = payload.deferredBindlessSlotsWereUploaded;
    renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();
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


bool ShadowPrepareHybridSoftwareTailGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    if(!payload.raytracingSystem || !payload.targets || !payload.hardwarePreparationReady || !payload.timingTicket)
        return false;

    const void* hybridHardwareFallbackInstanceMaterialData = nullptr;
    const void* hybridHardwareFallbackInstanceData = nullptr;
    const void* hybridHardwareFallbackMaterialTypedData = nullptr;
    usize hybridHardwareFallbackInstanceMaterialByteCount = 0u;
    usize hybridHardwareFallbackInstanceByteCount = 0u;
    usize hybridHardwareFallbackMaterialTypedByteCount = 0u;
    hybridHardwareFallbackInstanceMaterialData = context.taskGraph.uploadBlobData(
        payload.hybridHardwareFallbackInstanceMaterialBlob,
        hybridHardwareFallbackInstanceMaterialByteCount
    );
    hybridHardwareFallbackInstanceData = context.taskGraph.uploadBlobData(
        payload.hybridHardwareFallbackInstanceBlob,
        hybridHardwareFallbackInstanceByteCount
    );
    hybridHardwareFallbackMaterialTypedData = context.taskGraph.uploadBlobData(
        payload.hybridHardwareFallbackMaterialTypedBlob,
        hybridHardwareFallbackMaterialTypedByteCount
    );
    if(
        !hybridHardwareFallbackInstanceMaterialData
        || !hybridHardwareFallbackInstanceData
        || !hybridHardwareFallbackMaterialTypedData
        || hybridHardwareFallbackInstanceMaterialByteCount == 0u
        || hybridHardwareFallbackInstanceByteCount == 0u
        || hybridHardwareFallbackMaterialTypedByteCount == 0u
    )
        return false;

    // The tail may record SW-BVH timing scopes, so it shares the accepting packet's timing ticket even though
    // its callback begins after the hardware preparation callback closed its own recording scope.
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);

    return payload.raytracingSystem->recordPreflightHybridSoftwareTail(
        commandList,
        *payload.targets,
        *payload.hardwarePreparationReady,
        payload.shadowMaterialContextBatchGraphOwned,
        payload.sceneBvhBatchGraphOwned,
        payload.meshSwBvhBuildsGraphOwned,
        payload.meshSwBvhInputStatesGraphOwned,
        hybridHardwareFallbackInstanceMaterialData,
        hybridHardwareFallbackInstanceMaterialByteCount,
        hybridHardwareFallbackInstanceData,
        hybridHardwareFallbackInstanceByteCount,
        hybridHardwareFallbackMaterialTypedData,
        hybridHardwareFallbackMaterialTypedByteCount
    );
}


void ShadowPrepareHybridSoftwareTailGraphTask::discarded(Payload& payload){
    if(payload.hardwarePreparationReady)
        *payload.hardwarePreparationReady = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

