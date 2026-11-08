// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "descriptor_buffer_writer.h"
#include "descriptor_class_policy.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuDescriptorHeap::DescriptorBufferWriter::initialize(const u32 offsetAlignmentBytes){
    if(!m_heap.m_context.descriptorBufferManager || !m_heap.m_context.descriptorBufferManager->isEnabled())
        return false;

    auto carve = [&](const BindingLayoutHandle& layout, const Span<const GpuDescriptorClass::Enum> classes) -> Expected<DescriptorBufferSegment>{
        const auto* bindingLayout = layout.get();
        if(!bindingLayout || !bindingLayout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: bindless layout is not descriptor-buffer-compatible; cannot carve heap block."));
            return MakeUnexpected(Failure{});
        }
        const u32 setSizeBytes = bindingLayout->getDescriptorBufferSetSizeBytes();
        if(setSizeBytes == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer layout reports a zero set size."));
            return MakeUnexpected(Failure{});
        }
        const BindlessLayoutDesc* bindlessDesc = bindingLayout->getBindlessDesc();
        const u32 descriptorCount = bindlessDesc ? bindlessDesc->maxCapacity : 0u;
        if(descriptorCount == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: bindless layout has no descriptor capacity."));
            return MakeUnexpected(Failure{});
        }
        const auto& bindingOffsets = bindingLayout->getDescriptorBufferBindingOffsets();
        for(const GpuDescriptorClass::Enum cls : classes){
            const auto it = bindingOffsets.find(m_heap.getRegisterSlot(cls));
            if(it == bindingOffsets.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer layout has no offset for class {}."), static_cast<u32>(cls));
                return MakeUnexpected(Failure{});
            }
            const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(DescriptorClassPolicy::ClassToResourceType(cls));
            const u32 descriptorSize = m_heap.m_context.descriptorBufferManager->getDescriptorSize(descriptorType);
            const u64 requiredBytes = static_cast<u64>(descriptorCount) * descriptorSize;
            if(descriptorSize == 0u || it->second > setSizeBytes || requiredBytes > static_cast<u64>(setSizeBytes - it->second)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer binding range is invalid for class {}."), static_cast<u32>(cls));
                return MakeUnexpected(Failure{});
            }
            m_heap.m_classBufferOffset[static_cast<u32>(cls)] = it->second;
        }
        const Expected<DescriptorBufferSegment> block = m_heap.m_context.descriptorBufferManager->allocateForBindingGeneration(bindingLayout->getDescriptorBufferSegmentKind(), setSizeBytes, offsetAlignmentBytes, m_heap.m_descriptorBufferGeneration);
        if(!block){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: failed to carve {}-byte descriptor-buffer heap block."), setSizeBytes);
            return MakeUnexpected(block.error());
        }
        return *block;
    };

    static constexpr Array<GpuDescriptorClass::Enum, 9> s_ResourceClasses = {{
        GpuDescriptorClass::SampledImage,
        GpuDescriptorClass::StorageImage,
        GpuDescriptorClass::SampledBuffer,
        GpuDescriptorClass::StorageBuffer,
        GpuDescriptorClass::UniformBuffer,
        GpuDescriptorClass::SampledImage2DArray,
        GpuDescriptorClass::SampledImage3D,
        GpuDescriptorClass::SampledImage2DArrayUint,
        GpuDescriptorClass::SampledImageCube
    }};
    static constexpr Array<GpuDescriptorClass::Enum, 1> s_SamplerClasses = {{ GpuDescriptorClass::Sampler }};
    const auto resourceBlock = carve(m_heap.m_resourceLayout, s_ResourceClasses);
    if(!resourceBlock)
        return false;
    m_heap.m_resourceBufferBlock = *resourceBlock;
    const auto samplerBlock = carve(m_heap.m_samplerLayout, s_SamplerClasses);
    if(!samplerBlock)
        return false;
    m_heap.m_samplerBufferBlock = *samplerBlock;

    return true;
}

b8 GpuDescriptorHeap::DescriptorBufferWriter::write(const DescriptorWriteItem& writeItem, const GpuDescriptorClass::Enum descriptorClass){
    if(!m_heap.m_context.descriptorBufferManager || !m_heap.m_context.descriptorBufferManager->isEnabled())
        return false;

    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        if(!m_heap.m_accelStructLayout || writeItem.arrayElement >= m_heap.m_accelStructBufferBlocks.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: invalid TLAS descriptor slot {}."), writeItem.arrayElement);
            return false;
        }

        const u32 descriptorSize = m_heap.m_context.descriptorBufferManager->getDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        const u32 setSizeBytes = m_heap.m_accelStructLayout->getDescriptorBufferSetSizeBytes();
        if(
            descriptorSize == 0u
            || setSizeBytes == 0u
            || m_heap.m_accelStructLayout->getDescriptorBufferSegmentKind() != DescriptorBufferSegmentKind::Resource
            || m_heap.m_accelStructBufferBindingOffset > setSizeBytes
            || descriptorSize > setSizeBytes - m_heap.m_accelStructBufferBindingOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: TLAS layout footprint is invalid."));
            return false;
        }

        DescriptorBufferSegment& retainedBlock = m_heap.m_accelStructBufferBlocks[writeItem.arrayElement];
        DescriptorBufferSegment candidateBlock{};
        const b8 allocateBlock = !retainedBlock.valid();
        auto block = MakeNotNull(&retainedBlock);
        if(allocateBlock){
            const Expected<DescriptorBufferSegment> allocatedBlock = m_heap.m_context.descriptorBufferManager->allocateForBindingGeneration(
                m_heap.m_accelStructLayout->getDescriptorBufferSegmentKind(),
                setSizeBytes,
                m_heap.m_context.descriptorBufferManager->getOffsetAlignmentBytes(),
                m_heap.m_descriptorBufferGeneration
            );
            if(!allocatedBlock){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: failed to carve {}-byte TLAS block."), setSizeBytes);
                return false;
            }
            candidateBlock = *allocatedBlock;
            block = &candidateBlock;
        }

        if(
            block->kind != DescriptorBufferSegmentKind::Resource
            || m_heap.m_accelStructBufferBindingOffset > block->sizeBytes
            || descriptorSize > block->sizeBytes - m_heap.m_accelStructBufferBindingOffset
            || static_cast<u64>(block->offsetBytes) + m_heap.m_accelStructBufferBindingOffset > UINT32_MAX
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: TLAS descriptor block is invalid."));
            if(allocateBlock)
                m_heap.m_context.descriptorBufferManager->freeForBindingGeneration(candidateBlock, m_heap.m_descriptorBufferGeneration);
            return false;
        }

        if(!m_heap.m_context.descriptorBufferManager->writeDescriptor(writeItem, *block, static_cast<u32>(static_cast<u64>(block->offsetBytes) + m_heap.m_accelStructBufferBindingOffset), VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)){
            if(allocateBlock)
                m_heap.m_context.descriptorBufferManager->freeForBindingGeneration(candidateBlock, m_heap.m_descriptorBufferGeneration);
            return false;
        }
        if(allocateBlock)
            retainedBlock = candidateBlock;
        return true;
    }

    const b8 isSampler = (descriptorClass == GpuDescriptorClass::Sampler);
    const DescriptorBufferSegment& block = isSampler ? m_heap.m_samplerBufferBlock : m_heap.m_resourceBufferBlock;
    const DescriptorBufferSegmentKind::Enum expectedSegmentKind = isSampler ? DescriptorBufferSegmentKind::Sampler : DescriptorBufferSegmentKind::Resource;
    if(!block.valid() || block.kind != expectedSegmentKind){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: heap block not carved for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(writeItem.type);
    const u32 descriptorSize = m_heap.m_context.descriptorBufferManager->getDescriptorSize(descriptorType);
    if(descriptorSize == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: zero descriptor size for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const u64 relativeOffsetBytes = static_cast<u64>(m_heap.m_classBufferOffset[static_cast<u32>(descriptorClass)]) + static_cast<u64>(writeItem.arrayElement) * descriptorSize;
    if(relativeOffsetBytes > block.sizeBytes || descriptorSize > block.sizeBytes - relativeOffsetBytes || static_cast<u64>(block.offsetBytes) + relativeOffsetBytes > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: descriptor range exceeds the carved block for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const u32 baseOffset = static_cast<u32>(static_cast<u64>(block.offsetBytes) + relativeOffsetBytes);
    return m_heap.m_context.descriptorBufferManager->writeDescriptor(writeItem, block, baseOffset, descriptorType);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

