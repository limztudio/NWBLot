// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "feedback_resources.h"

#include <impl/assets/graphics/reflection/feedback_constants.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionFeedbackExtent ComputeReflectionFeedbackExtent(const u32 width, const u32 height)noexcept{
    if(width == 0u || height == 0u)
        return {};
    const u32 tilesX = (width - 1u) / NWB_REFLECTION_FEEDBACK_GROUP_SIZE + 1u;
    const u32 tilesY = (height - 1u) / NWB_REFLECTION_FEEDBACK_GROUP_SIZE + 1u;
    const u64 entryCount = static_cast<u64>(tilesX) * tilesY * NWB_REFLECTION_FEEDBACK_SURFACE_COUNT;
    const u64 byteCount = NWB_REFLECTION_FEEDBACK_HEADER_BYTES + entryCount * NWB_REFLECTION_FEEDBACK_ENTRY_BYTES;
    if(entryCount > Limit<u32>::s_Max || byteCount > Limit<u32>::s_Max)
        return {};
    return ReflectionFeedbackExtent{width, height, tilesX, tilesY, static_cast<u32>(entryCount), static_cast<u32>(byteCount)};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererReflectionFeedback::RendererReflectionFeedback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)
    : m_arena(arena)
    , m_graphics(graphics)
{}

void RendererReflectionFeedback::invalidateResources(){
    if(m_control)
        m_control->reset(0u);
    releaseBuffers();
    m_extent = {};
    m_deviceGeneration = 0u;
}

bool RendererReflectionFeedback::prepareResources(
    const u32 width,
    const u32 height,
    const ReflectionSettings& settings,
    const bool enabled){
    const ReflectionFeedbackExtent extent = ComputeReflectionFeedbackExtent(width, height);
    if(!extent.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: invalid extent or feedback address range ({}x{})"), width, height);
        return false;
    }
    auto& device = m_graphics.getDevice();
    const u16 deviceGeneration = device.getDeviceGeneration();
    if(!m_control)
        m_control = CreateReflectionFeedbackControl(m_arena, deviceGeneration);
    if(!m_control){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to allocate acceptance control"));
        return false;
    }
    const bool changed = width != m_extent.width || height != m_extent.height || deviceGeneration != m_deviceGeneration;
    const bool active = enabled && settings.traceMode == ReflectionTraceMode::Hybrid && settings.maxHardwareRaysPerFrame > 0u;
    if(active && (changed || !m_banks[0].valid() || !m_banks[1].valid())){
        ReflectionFeedbackBinding banks[2];
        if(
            !prepareBank(banks[0], extent.byteCount, Name("engine/reflection/screen_feedback_0"))
            || !prepareBank(banks[1], extent.byteCount, Name("engine/reflection/screen_feedback_1"))
        ){
            Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
            for(const ReflectionFeedbackBinding& bank : banks){
                if(bank.descriptor.valid())
                    heap.free(bank.descriptor);
            }
            return false;
        }
        releaseBuffers();
        m_banks[0] = Move(banks[0]);
        m_banks[1] = Move(banks[1]);
    }
    else if(changed)
        releaseBuffers();
    if(changed)
        m_control->reset(deviceGeneration);
    m_extent = extent;
    m_deviceGeneration = deviceGeneration;
    return true;
}

ReflectionFeedbackSnapshot RendererReflectionFeedback::snapshot(
    const ReflectionSceneContentStamp& stamp,
    const ReflectionSettings& settings,
    const bool enabled,
    const u64 graphicsFrameIndex)const{
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

bool RendererReflectionFeedback::prepareBank(ReflectionFeedbackBinding& bank, const u32 byteCount, const Name name){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: descriptor heap is unavailable during preflight"));
        return false;
    }
    Core::BufferDesc desc;
    desc
        .setByteSize(byteCount)
        .setCanHaveRawViews(true)
        .setCanHaveUAVs(true)
        .setDebugName(name)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle buffer = m_graphics.createBuffer(desc);
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to create {}-byte buffer"), byteCount);
        return false;
    }
    const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!descriptor.valid() || !heap.write(descriptor, Core::DescriptorWriteItem::RawBuffer_UAV(0u, buffer.get()))){
        if(descriptor.valid())
            heap.free(descriptor);
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to register storage buffer"));
        return false;
    }
    bank = ReflectionFeedbackBinding{Move(buffer), descriptor};
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

