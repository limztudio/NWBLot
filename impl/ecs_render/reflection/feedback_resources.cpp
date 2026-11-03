// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "feedback_resources.h"

#include <impl/ecs_render/kernel/descriptor_register_utils.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererReflectionFeedback::invalidateResources(){
    if(m_control)
        m_control->reset(0u);
    releaseBuffers();
    m_extent = {};
    m_deviceGeneration = 0u;
}

b8 RendererReflectionFeedback::prepareResources(const u32 width, const u32 height, const ReflectionSettings& settings, const b8 enabled){
    const Expected<ReflectionFeedbackExtent> extentResult = TryComputeReflectionFeedbackExtent(width, height);
    if(!extentResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: invalid extent or feedback address range ({}x{})"), width, height);
        return false;
    }
    const ReflectionFeedbackExtent& extent = *extentResult;
    auto& device = m_graphics.getDevice();
    const u16 deviceGeneration = device.getDeviceGeneration();
    if(!m_control){
        auto controlResult = CreateStatisticsControl<ReflectionFeedbackControl>(m_arena, deviceGeneration);
        if(!controlResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to allocate acceptance control"));
            return false;
        }
        m_control = Move(*controlResult);
    }
    const b8 changed = width != m_extent.width || height != m_extent.height || deviceGeneration != m_deviceGeneration;
    const b8 active = enabled && settings.traceMode == ReflectionTraceMode::Hybrid && settings.maxHardwareRaysPerFrame > 0u;
    if(active && (changed || !m_banks[0].valid() || !m_banks[1].valid())){
        auto firstBank = prepareBank(extent.byteCount, s_ReflectionScreenFeedback0Name);
        if(!firstBank)
            return false;
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        Core::GpuDescriptorRetirementGuard retireFirst(heap, firstBank->descriptor);
        auto secondBank = prepareBank(extent.byteCount, s_ReflectionScreenFeedback1Name);
        if(!secondBank)
            return false;
        Core::GpuDescriptorRetirementGuard retireSecond(heap, secondBank->descriptor);
        releaseBuffers();
        m_banks[0] = Move(*firstBank);
        m_banks[1] = Move(*secondBank);
        retireFirst.release();
        retireSecond.release();
    }
    else if(changed)
        releaseBuffers();
    if(changed)
        m_control->reset(deviceGeneration);
    m_extent = extent;
    m_deviceGeneration = deviceGeneration;
    return true;
}

ReflectionFeedbackSnapshot RendererReflectionFeedback::snapshot(const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, const b8 enabled, const u64 graphicsFrameIndex)const noexcept{
    ReflectionFeedbackSnapshot result;
    if(!m_control)
        return result;
    result.control = m_control;
    result.plan = m_control->plan(stamp, settings, enabled, graphicsFrameIndex);
    result.extent = m_extent;
    result.current = m_banks[result.plan.currentBank];
    result.previous = m_banks[result.plan.previousBank];
    return result;
}

void RendererReflectionFeedback::releaseBuffers(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    for(ReflectionFeedbackBinding& bank : m_banks){
        if(bank.descriptor.valid() && heap.isInitialized())
            heap.free(bank.descriptor);
        bank = {};
    }
}

Expected<ReflectionFeedbackBinding> RendererReflectionFeedback::prepareBank(const u32 byteCount, const Name name){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: descriptor heap is unavailable during preflight"));
        return MakeUnexpected(Failure{});
    }
    Core::BufferDesc desc;
    desc.setByteSize(byteCount).setCanHaveRawViews(true).setCanHaveUAVs(true).setDebugName(name).enableAutomaticStateTracking(Core::ResourceStates::Common);
    auto bufferResult = m_graphics.createBuffer(desc);
    if(!bufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to create {}-byte buffer"), byteCount);
        return MakeUnexpected(Failure{});
    }
    Core::BufferHandle buffer = Move(*bufferResult);
    const Expected<Core::GpuDescriptorHandle> descriptor = RendererDescriptorRegisterDetail::RegisterDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, buffer.get()));
    if(!descriptor){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to register storage buffer"));
        return MakeUnexpected(Failure{});
    }
    return ReflectionFeedbackBinding{Move(buffer), *descriptor};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

