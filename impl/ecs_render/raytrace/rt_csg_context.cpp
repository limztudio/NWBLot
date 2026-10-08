// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureCsgTraceContextResources(){
    if(!m_lightSpaceShadow.m_csg.snapshot.hasCsg)
        return true;

    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const usize byteCount = m_lightSpaceShadow.m_csg.bytes.size();
    const u64 maximumCapacity = Min<u64>(device.getMaxStorageBufferRange(), Limit<u32>::s_Max);
    if(!heap.isInitialized() || byteCount == 0u || byteCount > maximumCapacity){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: current CSG ray context exceeds storage capacity"));
        return false;
    }

    auto& buffer = m_rayTracingState.m_csgTraceContextBuffer;
    auto& handle = m_rayTracingState.m_csgTraceContextHeapHandle;
    if(buffer && buffer->getCreationDescription().byteSize >= byteCount)
        return RayTracingDetail::EnsureHeapBuffer(heap, *buffer, Core::GpuDescriptorClass::StorageBuffer, false, handle);

    u64 capacity = byteCount;
    if(buffer){
        const u64 previousCapacity = buffer->getCreationDescription().byteSize;
        const u64 growth = Min<u64>(previousCapacity / 2u, maximumCapacity - previousCapacity);
        capacity = Max<u64>(capacity, previousCapacity + growth);
    }
    Core::BufferDesc desc;
    desc
        .setByteSize(capacity).setStructStride(sizeof(u32)).setCanHaveRawViews(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("raytrace_csg_context")).enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle replacement = m_graphics.createBuffer(desc);
    if(!replacement)
        return false;
    const auto replacementHandle = RayTracingDetail::RegisterHeapBuffer(heap, *replacement, Core::GpuDescriptorClass::StorageBuffer, false);
    if(!replacementHandle)
        return false;
    ScopeExit retireReplacement([&]()noexcept{ heap.free(*replacementHandle); });

    RayTracingDetail::RetireHeapHandle(heap, handle);
    buffer = Move(replacement);
    handle = *replacementHandle;
    m_rayTracingState.m_csgTraceContextReadSubmissionToken = {};
    retireReplacement.release();
    return true;
}

Expected<Core::GpuUploadBlobId> RendererRayTracingSystem::retainPreparedCsgTraceContextUpload(Core::GpuTaskGraph& graph)const{
    Core::GpuUploadBlobId blob;
    if(!hasSurfelWork() || !m_lightSpaceShadow.m_csg.snapshot.hasCsg)
        return blob;
    const auto& bytes = m_lightSpaceShadow.m_csg.bytes;
    if(bytes.empty() || !m_rayTracingState.m_csgTraceContextBuffer || !m_rayTracingState.m_csgTraceContextHeapHandle.valid())
        return MakeUnexpected(Failure{});

    blob = graph.copyUploadData(bytes.data(), bytes.size(), alignof(u32));
    if(!blob.valid())
        return MakeUnexpected(Failure{});
    return blob;
}

void RendererRayTracingSystem::confirmCsgTraceContextReadSubmission(const Core::QueueSubmissionToken& token)noexcept{
    if(hasSurfelWork() && m_rayTracingState.m_surfelUseCsgTrace)
        m_rayTracingState.m_csgTraceContextReadSubmissionToken = token;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

