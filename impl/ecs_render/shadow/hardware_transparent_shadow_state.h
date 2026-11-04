// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shadow/push_constants.adhd.h>

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct HardwareTransparentShadowState{
    Core::BindingLayoutHandle m_bindingLayout;
    Array<Core::ShaderHandle, s_HwTransparentStageCount> m_shaders{};
    Array<Core::ComputePipelineHandle, s_HwTransparentStageCount> m_pipelines{};
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

