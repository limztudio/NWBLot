// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_scene_resources.h"

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalSceneResources::RayTracingOpticalSceneResources(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, const Name identity)noexcept
    : m_arena(arena)
    , m_graphics(graphics)
    , m_identity(identity)
    , m_uploads(arena)
    , m_runtimeBounds(arena, graphics)
{}

void RayTracingOpticalSceneResources::invalidate(){
    m_runtimeBoundsSnapshot.reset();
    m_runtimeBounds.invalidate();
    if(m_resources.uploadState)
        m_resources.uploadState->invalidate();
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized() && m_resources.descriptor.valid() && m_resources.buffer && m_resources.buffer->getDeviceGeneration() == device.getDeviceGeneration())
        heap.free(m_resources.descriptor);
    m_resources = {};
    m_uploads.clear();
    m_capacity = 0u;
    m_prepared = false;
    m_runtimeBoundsRequired = false;
}

bool RayTracingOpticalSceneResources::prepare(const RayTracingOpticalSceneGather& gather){
    m_prepared = false;
    m_runtimeBoundsRequired = !gather.runtimeBounds.empty();
    m_runtimeBoundsSnapshot.reset();
    if(gather.instances.size() > (Limit<u32>::s_Max - sizeof(gather.header)) / sizeof(RayTracingOpticalInstanceGpu)){
        NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: instance table exceeds the shader address range"));
        return false;
    }
    const usize instanceBytes = gather.instances.size() * sizeof(RayTracingOpticalInstanceGpu);
    const usize byteCount = sizeof(gather.header) + instanceBytes;
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: descriptor heap is unavailable during preflight"));
        return false;
    }
    if(m_resources.buffer && m_resources.buffer->getDeviceGeneration() != device.getDeviceGeneration()){
        NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: stale device resources require owner invalidation"));
        return false;
    }
    if(!m_resources.buffer || !m_resources.uploadState || m_capacity < byteCount){
        Core::BufferDesc desc;
        desc.setByteSize(byteCount).setCanHaveRawViews(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(m_identity).enableAutomaticStateTracking(Core::ResourceStates::Common);
        auto bufferResult = m_graphics.createBuffer(desc);
        if(!bufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: failed to create {}-byte metadata buffer"), byteCount);
            return false;
        }
        Core::BufferHandle buffer = Move(*bufferResult);
        const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!descriptor.valid() || !heap.write(descriptor, Core::DescriptorWriteItem::RawBufferSrv(0u, buffer.get()))){
            if(descriptor.valid())
                heap.free(descriptor);
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: failed to register metadata buffer"));
            return false;
        }
        auto uploadStateResult = CreateRayTracingOpticalUploadControl(m_arena, buffer, device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics));
        if(!uploadStateResult){
            heap.free(descriptor);
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: failed to create accepted upload control"));
            return false;
        }
        if(m_resources.uploadState)
            m_resources.uploadState->invalidate();
        if(m_resources.descriptor.valid())
            heap.free(m_resources.descriptor);
        m_resources.buffer = Move(buffer);
        m_resources.uploadState = Move(*uploadStateResult);
        m_resources.descriptor = descriptor;
        m_capacity = byteCount;
    }
    const bool samePayload =
        m_resources.upload
        && m_resources.upload->bytes.size() == byteCount
        && NWB_MEMCMP(m_resources.upload->bytes.data(), &gather.header, sizeof(gather.header)) == 0
        && (instanceBytes == 0u || NWB_MEMCMP(m_resources.upload->bytes.data() + sizeof(gather.header), gather.instances.data(), instanceBytes) == 0)
    ;
    if(!samePayload){
        RayTracingOpticalSceneUploadHandle upload;
        for(const RayTracingOpticalSceneUploadHandle& cached : m_uploads){
            if(cached->getReferenceCount() == 1u){
                AtomicThreadFence(MemoryOrder::acquire);
                upload = cached;
                break;
            }
        }
        if(!upload){
            upload = RayTracingOpticalSceneUploadHandle(NewArenaObject<RayTracingOpticalSceneUploadControl>(m_arena, m_arena, gather), ArenaRefDeleter<RayTracingOpticalSceneUploadControl, Core::Alloc::GlobalArena>(&m_arena), s_AdoptRef);
            if(!upload)
                return false;
            m_uploads.push_back(upload);
        }
        else{
            upload->bytes.resize(byteCount);
            NWB_MEMCPY(upload->bytes.data(), upload->bytes.size(), &gather.header, sizeof(gather.header));
            if(instanceBytes != 0u)
                NWB_MEMCPY(upload->bytes.data() + sizeof(gather.header), instanceBytes, gather.instances.data(), instanceBytes);
            upload->instanceCount = static_cast<u32>(gather.instances.size());
        }
        m_resources.upload = Move(upload);
    }
    m_resources.transparentCount = gather.header.transparentCount;
    m_resources.unspecifiedBoundariesOnly = gather.unspecifiedBoundariesOnly;
    m_resources.boundsComplete = (gather.header.flags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID) != 0u;
    m_prepared = m_resources.valid();
    return m_prepared;
}

bool RayTracingOpticalSceneResources::prepareRuntimeBounds(const RayTracingOpticalSceneGather& gather, RendererShaderSystem& shaderSystem){
    if(!m_prepared)
        return false;
    m_runtimeBoundsSnapshot.reset();
    if(m_runtimeBoundsRequired != !gather.runtimeBounds.empty()){
        m_prepared = false;
        return false;
    }
    if(gather.runtimeBounds.empty())
        return true;
    auto prepared = m_runtimeBounds.prepare(gather, m_resources.descriptor, shaderSystem);
    if(!prepared){
        m_prepared = false;
        return false;
    }
    m_runtimeBoundsSnapshot = Move(*prepared);
    m_prepared = static_cast<bool>(m_runtimeBoundsSnapshot);
    return m_prepared;
}

RayTracingOpticalSceneSnapshot RayTracingOpticalSceneResources::snapshot()const noexcept{
    if(!m_prepared || (m_runtimeBoundsRequired && !m_runtimeBoundsSnapshot))
        return {};
    RayTracingOpticalSceneSnapshot result = m_resources;
    if(m_runtimeBoundsSnapshot){
        result.uploadBuffer = result.buffer;
        result.finalize = m_runtimeBoundsSnapshot;
        result.buffer = m_runtimeBoundsSnapshot->outputBuffer;
        result.descriptor = m_runtimeBoundsSnapshot->outputDescriptor;
        // CPU readiness is distinct from the validity computed from the accepted GPU position stream.
        result.boundsComplete = false;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

