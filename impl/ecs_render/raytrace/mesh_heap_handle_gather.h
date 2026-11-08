// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shadow/rt_shadow_state.h>


NWB_IMPL_BEGIN


class MeshHeapHandleGather final{
public:
    MeshHeapHandleGather(Core::GpuDescriptorHeap& heap, RtMeshHeapHandleCache& cache)noexcept : m_heap(heap), m_cache(cache){}
    void begin()noexcept;
    [[nodiscard]] Expected<Core::GpuDescriptorHandle> acquire(const Core::BufferHandle& bufferHandle);
    void sweep();

private:
    Core::GpuDescriptorHeap& m_heap;
    RtMeshHeapHandleCache& m_cache;
};


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
