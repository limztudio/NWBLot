// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct HardwareTransparentShadowState{
    static constexpr usize s_StageCount = 3u;
    static constexpr usize s_ScratchBufferCount = 3u;

    Core::BindingLayoutHandle m_bindingLayout;
    Core::ShaderHandle m_shaders[s_StageCount];
    Core::ComputePipelineHandle m_pipelines[s_StageCount];
    Core::BufferHandle m_crossingsBuffer;
    Core::BufferHandle m_overflowListBuffer;
    Core::BufferHandle m_overflowArgsBuffer;
    Core::GpuDescriptorHandle m_crossingsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle m_overflowListHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle m_overflowArgsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    u32 m_pixelsCapacity = 0u;
    b8 m_ready = false;
    b8 m_pipelineFailed = false;
    b8 m_dispatchLogged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

