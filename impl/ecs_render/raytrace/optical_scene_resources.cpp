// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_scene_resources.h"

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalSceneResources::RayTracingOpticalSceneResources(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    const Name identity)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_identity(identity)
{}

void RayTracingOpticalSceneResources::invalidate(){
    if(m_resources.uploadState)
        m_resources.uploadState->invalidate();
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        heap.isInitialized() && m_resources.descriptor.valid() && m_resources.buffer
        && m_resources.buffer->getDeviceGeneration() == device.getDeviceGeneration()
    )
        heap.free(m_resources.descriptor);
    m_resources = {};
    m_capacity = 0u;
    m_prepared = false;
}

bool RayTracingOpticalSceneResources::prepare(const RayTracingOpticalSceneGather& gather){
    m_prepared = false;
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
        desc
            .setByteSize(byteCount)
            .setCanHaveRawViews(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(m_identity)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle buffer = m_graphics.createBuffer(desc);
        if(!buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: failed to create {}-byte metadata buffer"), byteCount);
            return false;
        }
        const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!descriptor.valid() || !heap.write(descriptor, Core::DescriptorWriteItem::RawBuffer_SRV(0u, buffer.get()))){
            if(descriptor.valid())
                heap.free(descriptor);
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: failed to register metadata buffer"));
            return false;
        }
        RayTracingOpticalUploadControlHandle uploadState = CreateRayTracingOpticalUploadControl(
            m_arena, buffer, device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)
        );
        if(!uploadState){
            heap.free(descriptor);
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: failed to create accepted upload control"));
            return false;
        }
        if(m_resources.uploadState)
            m_resources.uploadState->invalidate();
        if(m_resources.descriptor.valid())
            heap.free(m_resources.descriptor);
        m_resources.buffer = Move(buffer);
        m_resources.uploadState = Move(uploadState);
        m_resources.descriptor = descriptor;
        m_capacity = byteCount;
    }
    const bool samePayload =
        m_resources.upload
        && m_resources.upload->bytes.size() == byteCount
        && NWB_MEMCMP(m_resources.upload->bytes.data(), &gather.header, sizeof(gather.header)) == 0
        && (instanceBytes == 0u || NWB_MEMCMP(
            m_resources.upload->bytes.data() + sizeof(gather.header), gather.instances.data(), instanceBytes
        ) == 0)
    ;
    if(!samePayload){
        m_resources.upload = RayTracingOpticalSceneUploadHandle(
            NewArenaObject<RayTracingOpticalSceneUploadControl>(m_arena, m_arena, gather),
            ArenaRefDeleter<RayTracingOpticalSceneUploadControl, Core::Alloc::GlobalArena>(&m_arena),
            AdoptRef
        );
    }
    m_resources.transparentCount = gather.header.transparentCount;
    m_resources.boundsComplete = (gather.header.flags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID) != 0u;
    m_prepared = m_resources.valid();
    return m_prepared;
}

bool RayTracingOpticalSceneResources::matchesInstanceOrder(const RayTracingOpticalSceneGather& gather)const noexcept{
    if(!m_prepared || !m_resources.upload)
        return false;
    return OpticalInstanceOrderMatches(
        m_resources.upload->bytes.data() + NWB_RT_OPTICAL_SCENE_HEADER_BYTES, m_resources.upload->instanceCount,
        gather.instances.data(), gather.instances.size()
    );
}

RayTracingOpticalSceneSnapshot RayTracingOpticalSceneResources::snapshot()const{
    return m_prepared ? m_resources : RayTracingOpticalSceneSnapshot{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

