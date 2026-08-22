// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_descriptor_heap{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ResourceType::Enum ClassToResourceType(const GpuDescriptorClass::Enum descriptorClass){
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:  return ResourceType::Texture_SRV;
    case GpuDescriptorClass::StorageImage:  return ResourceType::Texture_UAV;
    case GpuDescriptorClass::SampledBuffer: return ResourceType::TypedBuffer_SRV;
    case GpuDescriptorClass::StorageBuffer: return ResourceType::StructuredBuffer_UAV;
    case GpuDescriptorClass::UniformBuffer: return ResourceType::ConstantBuffer;
    case GpuDescriptorClass::AccelStruct:   return ResourceType::RayTracingAccelStruct;
    case GpuDescriptorClass::Sampler:       return ResourceType::Sampler;
    case GpuDescriptorClass::SampledImage2DArray: return ResourceType::Texture_SRV;
    case GpuDescriptorClass::SampledImage3D: return ResourceType::Texture_SRV;
    case GpuDescriptorClass::SampledImage2DArrayUint: return ResourceType::Texture_SRV;
    case GpuDescriptorClass::SampledImageCube: return ResourceType::Texture_SRV;
    default:                                return ResourceType::None;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::write(const GpuDescriptorHandle handle, const DescriptorWriteItem& item){
    using namespace __hidden_vulkan_descriptor_heap;

    if(!m_initialized){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write called before initialize."));
        return false;
    }
    if(!handle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write called with an invalid handle."));
        return false;
    }

    const GpuDescriptorClass::Enum descriptorClass = handle.descriptorClass();
    if(descriptorClass >= GpuDescriptorClass::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: handle has invalid class {}."), static_cast<u32>(descriptorClass));
        return false;
    }
    if(descriptorClass == GpuDescriptorClass::AccelStruct && !m_accelStructLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: AccelStruct requires the descriptor-buffer TLAS layout."));
        return false;
    }

    ScopedLock lock(m_mutex);
    SlotAllocator& allocator = allocatorForClass(descriptorClass);
    if(handle.slot() >= allocator.liveSlots.size() || allocator.liveSlots[handle.slot()] == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected stale or retired handle {}."), handle.value);
        return false;
    }

    // Handle class owns binding, array index, and descriptor type.
    DescriptorWriteItem writeItem = item;
    writeItem.slot = getRegisterSlot(descriptorClass);
    writeItem.arrayElement = handle.slot();
    writeItem.type = ClassToResourceType(descriptorClass);

    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        // TLAS handles retain backing AS until deferred free; generations need fresh handles.
        if(handle.slot() >= m_accelStructBufferBlocks.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: AccelStruct handle slot {} is out of range."), handle.slot());
            return false;
        }
        RayTracingAccelStructHandle& retained = m_accelStructResources[handle.slot()];
        if(retained && retained.get() != writeItem.resourceHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: cannot replace a live AccelStruct descriptor slot; allocate a fresh handle."));
            return false;
        }
        if(!writeDescriptorBuffer(writeItem, descriptorClass))
            return false;
        if(!retained){
            retained = RayTracingAccelStructHandle(
                static_cast<RayTracingAccelStruct*>(writeItem.resourceHandle),
                RayTracingAccelStructHandle::deleter_type(&m_context.objectArena)
            );
        }
        return true;
    }

    // Descriptor handles retain resources through quarantine; generations need fresh handles.
    auto& retainedResources = descriptorClass == GpuDescriptorClass::Sampler
        ? m_samplerDescriptorResources
        : m_resourceDescriptorResources
    ;
    if(handle.slot() >= retainedResources.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: descriptor slot {} is outside the retained-resource table."), handle.slot());
        return false;
    }
    GraphicsResource* const resource = static_cast<GraphicsResource*>(writeItem.resourceHandle);
    if(!resource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: resource is null for descriptor slot {}."), handle.slot());
        return false;
    }
    Handle<GraphicsResource>& retained = retainedResources[handle.slot()];
    if(retained && retained.get() != resource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: cannot replace a live descriptor slot; allocate a fresh handle."));
        return false;
    }

    if(!writeDescriptorBuffer(writeItem, descriptorClass))
        return false;
    if(!retained)
        retained = resource;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuDescriptorHeap::bindCompute(
    CommandList& commandList,
    const ComputePipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    // Bind persistent resource/sampler blocks at 8/9 and optional TLAS at 10.
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.m_pipelineLayout, accelStructHandle);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline){
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipelineLayout);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline){
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipelineLayout);
}

void GpuDescriptorHeap::bindRayTracing(
    CommandList& commandList,
    const RayTracingPipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.m_pipelineLayout, accelStructHandle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::initializeDescriptorBufferBlocks(const u32 offsetAlignmentBytes){
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return false;

    auto carve = [&](const BindingLayoutHandle& layout, DescriptorBufferSegment& outBlock, const GpuDescriptorClass::Enum* classes, const u32 classCount) -> bool{
        const auto* bindingLayout = layout.get();
        if(!bindingLayout || !bindingLayout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: bindless layout is not descriptor-buffer-compatible; cannot carve heap block."));
            return false;
        }
        const u32 setSizeBytes = bindingLayout->getDescriptorBufferSetSizeBytes();
        if(setSizeBytes == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer layout reports a zero set size."));
            return false;
        }
        const BindlessLayoutDesc* bindlessDesc = bindingLayout->getBindlessDesc();
        const u32 descriptorCount = bindlessDesc ? bindlessDesc->maxCapacity : 0u;
        if(descriptorCount == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: bindless layout has no descriptor capacity."));
            return false;
        }
        const DescriptorBufferSegment block = m_context.descriptorBufferManager->allocate(
            bindingLayout->getDescriptorBufferSegmentKind(),
            setSizeBytes,
            offsetAlignmentBytes
        );
        if(!block.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: failed to carve {}-byte descriptor-buffer heap block."), setSizeBytes);
            return false;
        }
        outBlock = block;
        const auto& bindingOffsets = bindingLayout->getDescriptorBufferBindingOffsets();
        for(u32 c = 0; c < classCount; ++c){
            const GpuDescriptorClass::Enum cls = classes[c];
            const auto it = bindingOffsets.find(getRegisterSlot(cls));
            if(it == bindingOffsets.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer layout has no offset for class {}."), static_cast<u32>(cls));
                return false;
            }
            const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(__hidden_vulkan_descriptor_heap::ClassToResourceType(cls));
            const u32 descriptorSize = m_context.descriptorBufferManager->getDescriptorSize(descriptorType);
            const u64 requiredBytes = static_cast<u64>(descriptorCount) * descriptorSize;
            if(
                descriptorSize == 0u
                || it->second > setSizeBytes
                || requiredBytes > static_cast<u64>(setSizeBytes - it->second)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer binding range is invalid for class {}."), static_cast<u32>(cls));
                return false;
            }
            m_classBufferOffset[static_cast<u32>(cls)] = it->second;
        }
        return true;
    };

    static constexpr GpuDescriptorClass::Enum s_ResourceClasses[] = {
        GpuDescriptorClass::SampledImage,
        GpuDescriptorClass::StorageImage,
        GpuDescriptorClass::SampledBuffer,
        GpuDescriptorClass::StorageBuffer,
        GpuDescriptorClass::UniformBuffer,
        GpuDescriptorClass::SampledImage2DArray,
        GpuDescriptorClass::SampledImage3D,
        GpuDescriptorClass::SampledImage2DArrayUint,
        GpuDescriptorClass::SampledImageCube
    };
    static constexpr GpuDescriptorClass::Enum s_SamplerClasses[] = {
        GpuDescriptorClass::Sampler
    };
    if(!carve(m_resourceLayout, m_resourceBufferBlock, s_ResourceClasses, static_cast<u32>(sizeof(s_ResourceClasses) / sizeof(s_ResourceClasses[0]))))
        return false;
    if(!carve(m_samplerLayout, m_samplerBufferBlock, s_SamplerClasses, static_cast<u32>(sizeof(s_SamplerClasses) / sizeof(s_SamplerClasses[0]))))
        return false;

    return true;
}

bool GpuDescriptorHeap::writeDescriptorBuffer(const DescriptorWriteItem& writeItem, const GpuDescriptorClass::Enum descriptorClass){
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return false;

    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        if(!m_accelStructLayout || writeItem.arrayElement >= m_accelStructBufferBlocks.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: invalid TLAS descriptor slot {}."), writeItem.arrayElement);
            return false;
        }

        const u32 descriptorSize = m_context.descriptorBufferManager->getDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        const u32 setSizeBytes = m_accelStructLayout->getDescriptorBufferSetSizeBytes();
        if(
            descriptorSize == 0u
            || setSizeBytes == 0u
            || m_accelStructLayout->getDescriptorBufferSegmentKind() != DescriptorBufferSegmentKind::Resource
            || m_accelStructBufferBindingOffset > setSizeBytes
            || descriptorSize > setSizeBytes - m_accelStructBufferBindingOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: TLAS layout footprint is invalid."));
            return false;
        }

        DescriptorBufferSegment& block = m_accelStructBufferBlocks[writeItem.arrayElement];
        if(!block.valid()){
            block = m_context.descriptorBufferManager->allocate(
                m_accelStructLayout->getDescriptorBufferSegmentKind(),
                setSizeBytes,
                m_context.descriptorBufferManager->getOffsetAlignmentBytes()
            );
            if(!block.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: failed to carve {}-byte TLAS block."), setSizeBytes);
                return false;
            }
        }

        if(
            block.kind != DescriptorBufferSegmentKind::Resource
            || m_accelStructBufferBindingOffset > block.sizeBytes
            || descriptorSize > block.sizeBytes - m_accelStructBufferBindingOffset
            || static_cast<u64>(block.offsetBytes) + m_accelStructBufferBindingOffset > UINT32_MAX
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: TLAS descriptor block is invalid."));
            return false;
        }

        return m_context.descriptorBufferManager->writeDescriptor(
            writeItem,
            block,
            static_cast<u32>(static_cast<u64>(block.offsetBytes) + m_accelStructBufferBindingOffset),
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
        );
    }

    const bool isSampler = (descriptorClass == GpuDescriptorClass::Sampler);
    const DescriptorBufferSegment& block = isSampler ? m_samplerBufferBlock : m_resourceBufferBlock;
    const DescriptorBufferSegmentKind::Enum expectedSegmentKind = isSampler
        ? DescriptorBufferSegmentKind::Sampler
        : DescriptorBufferSegmentKind::Resource
    ;
    if(!block.valid() || block.kind != expectedSegmentKind){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: heap block not carved for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(writeItem.type);
    const u32 descriptorSize = m_context.descriptorBufferManager->getDescriptorSize(descriptorType);
    if(descriptorSize == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: zero descriptor size for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const u64 relativeOffsetBytes = static_cast<u64>(m_classBufferOffset[static_cast<u32>(descriptorClass)])
        + static_cast<u64>(writeItem.arrayElement) * descriptorSize
    ;
    if(
        relativeOffsetBytes > block.sizeBytes
        || descriptorSize > block.sizeBytes - relativeOffsetBytes
        || static_cast<u64>(block.offsetBytes) + relativeOffsetBytes > UINT32_MAX
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: descriptor range exceeds the carved block for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const u32 baseOffset = static_cast<u32>(static_cast<u64>(block.offsetBytes) + relativeOffsetBytes);
    return m_context.descriptorBufferManager->writeDescriptor(writeItem, block, baseOffset, descriptorType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

