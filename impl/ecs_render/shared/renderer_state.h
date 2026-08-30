// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/pipeline.h>

#include <impl/assets/graphics/mesh/runtime_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererMeshSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDrawState final : NoCopy{
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;

public:
    RendererDrawState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialTypedBuffer;
    Core::BufferHandle m_meshViewBuffer;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    usize m_instanceBufferCapacity = 0;
    usize m_materialTypedBufferCapacity = 0;
    // Capacity changes get fresh heap slots after deferred retirement.
    Core::GpuDescriptorHandle m_instanceBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_materialTypedBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_meshViewBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    u8 m_meshViewGpuData[sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT] = {};
    bool m_meshViewGpuDataValid = false;
};
static_assert(sizeof(RendererDrawState) == 368u, "RendererDrawState should keep its compact CPU-only layout");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

