// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_heap_handle_gather.h"

#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


Expected<Core::GpuDescriptorHandle> MeshHeapHandleGather::acquire(const Core::BufferHandle& bufferHandle){
    if(!bufferHandle)
        return MakeUnexpected(Failure{});

    Core::Buffer& buffer = *bufferHandle;
    const NotNull<const Core::Buffer*> bufferKey(&buffer);
    auto found = m_cache.find(bufferKey);
    if(found != m_cache.end()){
        NWB_ASSERT(found.value().handle.valid());
        found.value().seenThisFrame = true;
        return found.value().handle;
    }

    const auto handle = RayTracingDetail::RegisterHeapBuffer(m_heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false);
    if(!handle)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorRetirementGuard descriptorRetirement(m_heap, *handle);
    RtMeshHeapHandleCacheEntry entry;
    entry.keepAlive = bufferHandle;
    entry.handle = *handle;
    entry.seenThisFrame = true;
    if(!m_cache.insert({bufferKey, Move(entry)}).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    descriptorRetirement.release();
    return *handle;
}

void MeshHeapHandleGather::sweep(){
    for(auto it = m_cache.begin(); it != m_cache.end(); ){
        if(it.value().seenThisFrame){
            it.value().seenThisFrame = false;
            ++it;
        }
        else{
            m_heap.free(it.value().handle);
            it = m_cache.erase(it);
        }
    }
}

void MeshHeapHandleGather::begin()noexcept{
    for(auto it = m_cache.begin(); it != m_cache.end(); ++it)
        it.value().seenThisFrame = false;
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
