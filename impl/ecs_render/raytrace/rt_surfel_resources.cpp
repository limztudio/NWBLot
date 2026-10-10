// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/gi/quality_settings.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::createSurfelTargets(DeferredFrameTargets& targets){
    const u32 factor = targets.surfelResolveFactor;
    const SurfelGiResolveSize resolveSize = MakeSurfelGiResolveSize(targets.width, targets.height, factor);
    if(resolveSize.width == 0u || resolveSize.height == 0u)
        return false;

    // Deferred lighting samples resolved surfel GI, never the writable pool.
    targets.surfelIrradianceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc surfelIrradianceDesc;
    surfelIrradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.surfelIrradianceFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/gi/surfel_irradiance")
    ;
    targets.surfelIrradiance = m_graphics.createTexture(surfelIrradianceDesc);
    if(!targets.surfelIrradiance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel irradiance target"));
        return false;
    }

    // Transient reduced-resolution surfel resolve input.
    Core::TextureDesc surfelIrradianceHalfDesc;
    surfelIrradianceHalfDesc
        .setWidth(resolveSize.width)
        .setHeight(resolveSize.height)
        .setFormat(targets.surfelIrradianceFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/gi/surfel_irradiance_half")
    ;
    targets.surfelIrradianceHalf = m_graphics.createTexture(surfelIrradianceHalfDesc);
    if(!targets.surfelIrradianceHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel reduced-resolution irradiance target"));
        return false;
    }

    m_rayTracingState.m_surfelResolveDispatchLogged = false;
    return true;
}

void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelConstantsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelGuidePoolHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCounterHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelFreeListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolSnapshotHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle);
        return;
    }

    m_rayTracingState.m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelGuidePoolHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::Invalid();
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

    m_rayTracingState.m_surfelUseCsgTrace = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    if(!ensureSurfelResources())
        return false;

    // GI consumes the same selector descriptor as the other current ray effects.
    if(
        !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !ensureRayTraceMaterialContextSlotsHeapHandle()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI trace heap context is incomplete"));
        return false;
    }

    return true;
}

Expected<Core::GpuUploadBlobId> RendererRayTracingSystem::retainPreparedSurfelFrameConstantsUpload(
    Core::GpuTaskGraph& graph,
    const DeferredFrameTargets& targets
)const{
    Core::GpuUploadBlobId blob;
    if(!hasSurfelWork())
        return blob;
    if(!m_rayTracingState.m_surfelConstants){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI has no preflighted constant buffer"));
        return MakeUnexpected(Failure{});
    }

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(m_rayTracingState, targets);
    blob = graph.copyUploadData(
        &params,
        sizeof(params),
        alignof(NwbSurfelConstantsGpu)
    );
    if(!(blob.valid()))
        return MakeUnexpected(Failure{});
    return blob;
}

void RendererRayTracingSystem::finalizeSurfelResourceInitialization()noexcept{
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

void RendererRayTracingSystem::discardSurfelResourceInitialization()noexcept{
    // Keep the clear pending until a producer succeeds.
    m_rayTracingState.m_surfelResourcesClearPending = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

