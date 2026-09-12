// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "ui_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>
#include <global/text_utils.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <core/common/log.h>

#include <cstdint>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSystem::ensureSamplerHeapHandle(){
    if(m_samplerHeapHandle.valid())
        return true;
    if(!m_sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register a missing ImGui sampler in the descriptor heap"));
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register the ImGui sampler without an initialized descriptor heap"));
        return false;
    }

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::Sampler(0u, m_sampler.get()))){
        heap.free(handle);
        return false;
    }

    m_samplerHeapHandle = handle;
    return true;
}

bool UiSystem::registerTextureHeapHandle(UiTextureResource& resource){
    if(resource.sampledImageHeapHandle.valid())
        return true;
    if(!resource.texture){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register a missing ImGui texture in the descriptor heap"));
        return false;
    }

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: cannot register an ImGui texture without an initialized descriptor heap"));
        return false;
    }

    const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!handle.valid())
        return false;
    if(!heap.write(handle, Core::DescriptorWriteItem::Texture_SRV(
        0u,
        resource.texture.get(),
        Core::Format::RGBA8_UNORM,
        Core::s_AllSubresources,
        Core::TextureDimension::Texture2D
    ))){
        heap.free(handle);
        return false;
    }

    resource.sampledImageHeapHandle = handle;
    return true;
}

void UiSystem::releaseTextureHeapHandle(UiTextureResource& resource){
    if(!resource.sampledImageHeapHandle.valid())
        return;

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized())
        heap.free(resource.sampledImageHeapHandle);
    resource.sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid();
}

void UiSystem::releaseDescriptorHeapResources(){
    for(const UiTextureResourcePtr& resource : m_textures){
        if(resource)
            releaseTextureHeapHandle(*resource);
    }

    if(m_samplerHeapHandle.valid()){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        if(heap.isInitialized())
            heap.free(m_samplerHeapHandle);
        m_samplerHeapHandle = Core::GpuDescriptorHandle::invalid();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

