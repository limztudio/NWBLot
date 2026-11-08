// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "descriptor_class_policy.h"
#include "descriptor_buffer_writer.h"
#include "descriptor_image_policy.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuDescriptorHeap::write(const GpuDescriptorHandle handle, const DescriptorWriteItem& item){
    ScopedLock lock(m_mutex);

    if(!m_initialized){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write called before initialize."));
        return false;
    }
    if(!handle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write called with an invalid handle."));
        return false;
    }

    const GpuDescriptorClass::Enum descriptorClass = handle.descriptorClass();
    if(descriptorClass >= GpuDescriptorClass::Count){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: handle has invalid class {}."), static_cast<u32>(descriptorClass));
        return false;
    }
    if(descriptorClass == GpuDescriptorClass::AccelStruct && !m_accelStructLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: AccelStruct requires the descriptor-buffer TLAS layout."));
        return false;
    }
    DescriptorBufferManager* const manager = m_context.descriptorBufferManager;
    if(!manager || manager != &m_device.m_descriptorBufferManager){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected an unavailable manager."));
        return false;
    }
    {
        ScopedLock lifecycleLock(manager->m_lifecycleMutex);
        if(!manager->m_enabled || manager->m_lifecycleTransitioning || m_descriptorBufferGeneration == 0u || manager->m_bindingGeneration != m_descriptorBufferGeneration){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a stale descriptor generation."));
            return false;
        }
    }

    SlotAllocator& allocator = allocatorForClass(descriptorClass);
    if(handle.slot() >= allocator.slotStates.size() || handle.slot() >= allocator.allocatedClasses.size() || allocator.slotStates[handle.slot()] != SlotState::Live || allocator.allocatedClasses[handle.slot()] != static_cast<u8>(descriptorClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected stale, retagged, or retired handle {}."), handle.value);
        return false;
    }
    if(!DescriptorClassPolicy::IsResourceTypeCompatible(descriptorClass, item.type)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected resource type {} for descriptor class {}."), static_cast<u32>(item.type), static_cast<u32>(descriptorClass));
        return false;
    }

    // Handle owns binding and index; authored type preserves storage-buffer aliases.
    DescriptorWriteItem writeItem = item;
    writeItem.slot = getRegisterSlot(descriptorClass);
    writeItem.arrayElement = handle.slot();

    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        // TLAS handles retain backing AS until deferred free; generations need fresh handles.
        if(handle.slot() >= m_accelStructBufferBlocks.size() || handle.slot() >= m_accelStructResources.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: AccelStruct handle slot {} is out of range."), handle.slot());
            return false;
        }
        auto* const accelStruct = static_cast<AccelStruct*>(writeItem.resourceHandle);
        if(!m_device.isAccelStructReadyForGpuUse(accelStruct)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a foreign or unready AccelStruct."));
            return false;
        }
        Buffer* const backingBuffer = accelStruct->getBackingBuffer();
        if(!backingBuffer || !isResourceAdmittedToActiveUsesLocked(backingBuffer->getQueueAdmissionSnapshot())){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected an AccelStruct unavailable to an active exact queue."));
            return false;
        }
        if(!accelStruct->m_isTopLevelAtCreation){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a bottom-level AccelStruct."));
            return false;
        }
        RayTracingAccelStructHandle& retained = m_accelStructResources[handle.slot()];
        if(retained && retained.get() != accelStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: cannot replace a live AccelStruct descriptor slot; allocate a fresh handle."));
            return false;
        }
        RayTracingAccelStructHandle candidate(nullptr, RayTracingAccelStructHandle::deleter_type(&m_context.objectArena));
        if(!retained){
            candidate = RayTracingAccelStructHandle(accelStruct, RayTracingAccelStructHandle::deleter_type(&m_context.objectArena));
        }
        if(!DescriptorBufferWriter(*this).write(writeItem, descriptorClass))
            return false;
        if(!retained)
            retained = Move(candidate);
        return true;
    }

    if(descriptorClass == GpuDescriptorClass::Sampler){
        if(handle.slot() >= m_samplerDescriptorResources.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: sampler slot {} is outside the retained table."), handle.slot());
            return false;
        }
        auto* const sampler = static_cast<Sampler*>(writeItem.resourceHandle);
        if(!sampler || &sampler->m_context != &m_context || sampler->m_sampler == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a foreign or unready Sampler."));
            return false;
        }
        SamplerHandle& retained = m_samplerDescriptorResources[handle.slot()];
        if(retained && retained.get() != sampler){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: live Sampler replacement requires a fresh handle."));
            return false;
        }
        SamplerHandle candidate(nullptr, SamplerHandle::deleter_type(&m_context.objectArena));
        if(!retained)
            candidate = SamplerHandle(sampler, SamplerHandle::deleter_type(&m_context.objectArena));
        if(!DescriptorBufferWriter(*this).write(writeItem, descriptorClass))
            return false;
        if(!retained)
            retained = Move(candidate);
        return true;
    }

    if(handle.slot() >= m_resourceDescriptorBuffers.size() || handle.slot() >= m_resourceDescriptorTextures.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: resource slot {} is outside the retained tables."), handle.slot());
        return false;
    }

    if(item.type == ResourceType::Texture_SRV || item.type == ResourceType::Texture_UAV){
        auto* const texture = static_cast<Texture*>(writeItem.resourceHandle);
        if(!m_device.isTextureReadyForGpuUse(texture)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a foreign or unready Texture."));
            return false;
        }
        if(!isResourceAdmittedToActiveUsesLocked(texture->getQueueAdmissionSnapshot())){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a Texture unavailable to an active exact queue."));
            return false;
        }

        if(!DescriptorImagePolicy::Validate(descriptorClass, writeItem, texture->getCreationDescription()))
            return false;

        TextureHandle& retained = m_resourceDescriptorTextures[handle.slot()];
        if(m_resourceDescriptorBuffers[handle.slot()] || (retained && retained.get() != texture)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: live Texture replacement requires a fresh handle."));
            return false;
        }
        TextureHandle candidate(nullptr, TextureHandle::deleter_type(&m_context.objectArena));
        if(!retained)
            candidate = TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena));
        if(!DescriptorBufferWriter(*this).write(writeItem, descriptorClass))
            return false;
        if(!retained)
            retained = Move(candidate);
        return true;
    }

    auto* const buffer = static_cast<Buffer*>(writeItem.resourceHandle);
    VkBufferUsageFlags requiredUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if(descriptorClass == GpuDescriptorClass::SampledBuffer)
        requiredUsage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    else if(descriptorClass == GpuDescriptorClass::StorageBuffer)
        requiredUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    else if(descriptorClass == GpuDescriptorClass::UniformBuffer)
        requiredUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected an invalid Buffer descriptor class."));
        return false;
    }
    if(!m_device.isBufferReadyForGpuUse(buffer, requiredUsage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a foreign or unready Buffer."));
        return false;
    }
    if(!isResourceAdmittedToActiveUsesLocked(buffer->getQueueAdmissionSnapshot())){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a Buffer unavailable to an active exact queue."));
        return false;
    }
    if(descriptorClass == GpuDescriptorClass::SampledBuffer){
        const BufferDesc& bufferDesc = buffer->getCreationDescription();
        const Format::Enum format = writeItem.format != Format::UNKNOWN ? writeItem.format : bufferDesc.format;
        if(format == Format::UNKNOWN || format >= Format::Count){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected an invalid sampled-buffer format."));
            return false;
        }
        const FormatInfo& formatInfo = GetFormatInfo(format);
        if(formatInfo.kind != FormatKind::Integer || formatInfo.isSigned){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write requires an unsigned-integer sampled-buffer format."));
            return false;
        }
    }

    BufferHandle& retained = m_resourceDescriptorBuffers[handle.slot()];
    if(m_resourceDescriptorTextures[handle.slot()] || (retained && retained.get() != buffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: live Buffer replacement requires a fresh handle."));
        return false;
    }
    BufferHandle candidate(nullptr, BufferHandle::deleter_type(&m_context.objectArena));
    if(!retained)
        candidate = BufferHandle(buffer, BufferHandle::deleter_type(&m_context.objectArena));

    if(!DescriptorBufferWriter(*this).write(writeItem, descriptorClass))
        return false;
    if(!retained)
        retained = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuDescriptorHeap::bindCompute(CommandList& commandList, const ComputePipeline& pipeline, const GpuDescriptorHandle accelStructHandle){
    // Bind persistent resource/sampler blocks at 0/1 and optional TLAS at 2.
    commandList.bindDescriptorBufferHeap(*this, pipeline, accelStructHandle);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline){
    commandList.bindDescriptorBufferHeap(*this, pipeline);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline){
    commandList.bindDescriptorBufferHeap(*this, pipeline);
}

void GpuDescriptorHeap::bindRayTracing(CommandList& commandList, const RayTracingPipeline& pipeline, const GpuDescriptorHandle accelStructHandle){
    commandList.bindDescriptorBufferHeap(*this, pipeline, accelStructHandle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

