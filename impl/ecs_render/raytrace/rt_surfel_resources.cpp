// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelConstantsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCounterHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelFreeListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolSnapshotHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle);
        return;
    }

    m_rayTracingState.m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
}

bool RendererRayTracingSystem::hasSurfelWork()const noexcept{
    return m_rayTracingState.m_surfelEnabled;
}

bool RendererRayTracingSystem::shouldCaptureSurfelCountReadback()const noexcept{
    return hasSurfelWork()
        && m_rayTracingState.m_surfelCounterBuffer
        && m_rayTracingState.m_surfelCounterReadback
        && !m_rayTracingState.m_surfelCountReadbackSubmissionToken.valid()
        && (m_rayTracingState.m_surfelFrameIndex % s_SurfelCountLogInterval) == 0u
    ;
}

void RendererRayTracingSystem::markSurfelCountReadbackScheduled()noexcept{
    m_rayTracingState.m_surfelCountReadbackFrame = m_rayTracingState.m_surfelFrameIndex;
}

bool RendererRayTracingSystem::needsSurfelResourceInitialization()const noexcept{
    return hasSurfelWork() && m_rayTracingState.m_surfelResourcesNeedClear;
}

bool RendererRayTracingSystem::prepareSurfelResources(DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    if(!ensureSurfelResources())
        return false;

    // Register the remaining heap-selected trace context.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        || !RayTracingDetail::EnsureHeapBuffer(
            heap,
            *m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            false,
            m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle
        )
    )
    {
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI trace heap context is incomplete"));
        return false;
    }

    return true;
}

bool RendererRayTracingSystem::retainPreparedSurfelFrameConstantsUpload(
    Core::GpuTaskGraph& graph,
    const DeferredFrameTargets& targets,
    Core::GpuUploadBlobId& outBlob
)const{
    outBlob = {};
    if(!hasSurfelWork())
        return true;
    if(!m_rayTracingState.m_surfelConstants){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI has no preflighted constant buffer"));
        return false;
    }

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(m_rayTracingState, targets);
    outBlob = graph.copyUploadData(
        &params,
        sizeof(params),
        alignof(NwbSurfelConstantsGpu)
    );
    return outBlob.valid();
}

void RendererRayTracingSystem::finalizeSurfelResourceInitialization(){
    if(!m_rayTracingState.m_surfelResourcesClearPending)
        return;

    m_rayTracingState.m_surfelResourcesClearPending = false;
    m_rayTracingState.m_surfelResourcesNeedClear = false;
}

bool RendererRayTracingSystem::recordSurfelResourceInitializationLifecycle()noexcept{
    if(!m_rayTracingState.m_surfelResourcesNeedClear)
        return false;

    m_rayTracingState.m_surfelResourcesClearPending = true;
    return true;
}

void RendererRayTracingSystem::discardSurfelResourceInitialization(){
    // Keep the clear pending until a producer succeeds.
    m_rayTracingState.m_surfelResourcesClearPending = false;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

