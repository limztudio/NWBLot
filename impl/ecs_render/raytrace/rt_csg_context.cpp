// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Optional<Core::GpuUploadBlobId>> RendererRayTracingSystem::retainPreparedCsgTraceContextUpload(Core::GpuTaskGraph& graph)const{
    if(!m_shadowVisibilityTraceResourcesPreflighted || !m_lightSpaceShadow.m_csg.snapshot.hasCsg)
        return Optional<Core::GpuUploadBlobId>{};
    const auto& bytes = m_lightSpaceShadow.m_csg.bytes;
    if(bytes.empty() || !m_rayTracingState.m_csgTraceContextBuffer || !m_rayTracingState.m_csgTraceContextHeapHandle.valid())
        return MakeUnexpected(Failure{});

    const auto blobResult = graph.copyUploadData(bytes.data(), bytes.size(), alignof(u32));
    if(!blobResult)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blobResult};
}

bool RendererRayTracingSystem::prepareCausticOpticalReadTracking(const ECSRenderDetail::MeshViewBufferSnapshot& meshView){
    const bool admitted = m_shadowVisibilityHardwareSupported ? hasHwCausticWork(meshView) : hasCausticWork(meshView);
    if(!ensureRayTraceContextReadTracking(admitted))
        return false;
    m_opticalCausticReaderAdmitted = admitted;
    return true;
}

void RendererRayTracingSystem::confirmRayTraceContextReadSubmission(const Core::QueueSubmissionToken& token)noexcept{
    if(!token.valid() || (!m_lightSpaceShadow.m_csg.snapshot.hasCsg && !m_opticalCausticReaderAdmitted))
        return;
    // Every physical reader protects the next upload, including accepted prefixes of partially rejected graphs.
    auto& readTokens = m_rayTracingState.m_rayTraceContextReadSubmissionTokens;
    NWB_ASSERT(token.hasPhysicalQueueIdentity() && token.physicalQueueIndex < readTokens.size());
    if(!token.hasPhysicalQueueIdentity() || token.physicalQueueIndex >= readTokens.size())
        return;
    auto& previous = readTokens[token.physicalQueueIndex];
    if(!previous.valid() || !previous.matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration) || token.value > previous.value)
        previous = token;
}

bool RendererRayTracingSystem::ensureRayTraceContextReadTracking(const bool opticalCausticReader){
    auto& readTokens = m_rayTracingState.m_rayTraceContextReadSubmissionTokens;
    const bool readerAdmitted = m_lightSpaceShadow.m_csg.snapshot.hasCsg || opticalCausticReader;
    if(!readerAdmitted && readTokens.empty())
        return true;

    auto& device = m_graphics.getDevice();
    // Keep pending readers when a later frame stops using their optical or CSG inputs.
    bool readersPending = false;
    for(auto& token : readTokens){
        if(!token.valid())
            continue;
        const Core::GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
        if(!device.matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration)){
            token = {};
            continue;
        }
        const Expected<u64> completedValue = device.queueGetCompletedInstance(queue);
        if(completedValue && *completedValue >= token.value)
            token = {};
        else
            readersPending = true;
    }
    if(!readerAdmitted){
        if(!readersPending)
            readTokens.clear();
        return true;
    }

    const Core::GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u)
        return false;
    // Acceptance only updates these preallocated physical queue slots.
    readTokens.resize(topology.queueCount);
    return true;
}

bool RendererRayTracingSystem::ensureCsgTraceContextResources(){
    if(!ensureRayTraceContextReadTracking(false))
        return false;
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
    desc.setByteSize(capacity).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(Name("raytrace_csg_context")).enableAutomaticStateTracking(Core::ResourceStates::Common);
    auto replacementResult = m_graphics.createBuffer(desc);
    if(!replacementResult)
        return false;
    Core::BufferHandle replacement = Move(*replacementResult);
    const auto replacementHandle = RayTracingDetail::RegisterHeapBuffer(heap, *replacement, Core::GpuDescriptorClass::StorageBuffer, false);
    if(!replacementHandle)
        return false;
    Core::GpuDescriptorRetirementGuard retireReplacement(heap, *replacementHandle);

    RayTracingDetail::RetireHeapHandle(heap, handle);
    buffer = Move(replacement);
    handle = *replacementHandle;
    // Prior readers retain the old buffer, but also read the persistent selector; its next upload must drain them.
    retireReplacement.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

