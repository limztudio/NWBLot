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

bool IsResourceTypeCompatible(
    const GpuDescriptorClass::Enum descriptorClass,
    const ResourceType::Enum resourceType
){
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:
    case GpuDescriptorClass::SampledImage2DArray:
    case GpuDescriptorClass::SampledImage3D:
    case GpuDescriptorClass::SampledImage2DArrayUint:
    case GpuDescriptorClass::SampledImageCube:
        return resourceType == ResourceType::Texture_SRV;
    case GpuDescriptorClass::StorageImage:
        return resourceType == ResourceType::Texture_UAV;
    case GpuDescriptorClass::SampledBuffer:
        return resourceType == ResourceType::TypedBuffer_SRV;
    case GpuDescriptorClass::StorageBuffer:
        return resourceType == ResourceType::StructuredBuffer_SRV
            || resourceType == ResourceType::StructuredBuffer_UAV
            || resourceType == ResourceType::RawBuffer_SRV
            || resourceType == ResourceType::RawBuffer_UAV
        ;
    case GpuDescriptorClass::UniformBuffer:
        return resourceType == ResourceType::ConstantBuffer;
    case GpuDescriptorClass::AccelStruct:
        return resourceType == ResourceType::RayTracingAccelStruct;
    case GpuDescriptorClass::Sampler:
        return resourceType == ResourceType::Sampler;
    default:
        return false;
    }
}

bool IsSampledImageClass(const GpuDescriptorClass::Enum descriptorClass){
    return descriptorClass == GpuDescriptorClass::SampledImage
        || descriptorClass == GpuDescriptorClass::SampledImage2DArray
        || descriptorClass == GpuDescriptorClass::SampledImage3D
        || descriptorClass == GpuDescriptorClass::SampledImage2DArrayUint
        || descriptorClass == GpuDescriptorClass::SampledImageCube
    ;
}

TextureDimension::Enum GetSampledImageDimension(const GpuDescriptorClass::Enum descriptorClass){
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage: return TextureDimension::Texture2D;
    case GpuDescriptorClass::SampledImage2DArray:
    case GpuDescriptorClass::SampledImage2DArrayUint: return TextureDimension::Texture2DArray;
    case GpuDescriptorClass::SampledImage3D: return TextureDimension::Texture3D;
    case GpuDescriptorClass::SampledImageCube: return TextureDimension::TextureCube;
    default: return TextureDimension::Unknown;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::write(const GpuDescriptorHandle handle, const DescriptorWriteItem& item){
    using namespace __hidden_vulkan_descriptor_heap;
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
    if(descriptorClass >= GpuDescriptorClass::kCount){
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
        if(
            !manager->m_enabled
            || manager->m_lifecycleTransitioning
            || m_descriptorBufferGeneration == 0u
            || manager->m_bindingGeneration != m_descriptorBufferGeneration
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a stale descriptor generation."));
            return false;
        }
    }

    SlotAllocator& allocator = allocatorForClass(descriptorClass);
    if(
        handle.slot() >= allocator.liveSlots.size()
        || handle.slot() >= allocator.allocatedClasses.size()
        || allocator.liveSlots[handle.slot()] == 0u
        || allocator.allocatedClasses[handle.slot()] != static_cast<u8>(descriptorClass)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected stale, retagged, or retired handle {}.")
            , handle.value
        );
        return false;
    }
    if(!IsResourceTypeCompatible(descriptorClass, item.type)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected resource type {} for descriptor class {}.")
            , static_cast<u32>(item.type)
            , static_cast<u32>(descriptorClass)
        );
        return false;
    }

    // Handle class owns binding and array index; the compatible authored type preserves storage-buffer aliases.
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
        if(!accelStruct->m_isTopLevelAtCreation){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a bottom-level AccelStruct."));
            return false;
        }
        RayTracingAccelStructHandle& retained = m_accelStructResources[handle.slot()];
        if(retained && retained.get() != accelStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: cannot replace a live AccelStruct descriptor slot; allocate a fresh handle."));
            return false;
        }
        RayTracingAccelStructHandle candidate(
            nullptr,
            RayTracingAccelStructHandle::deleter_type(&m_context.objectArena)
        );
        if(!retained){
            candidate = RayTracingAccelStructHandle(
                accelStruct,
                RayTracingAccelStructHandle::deleter_type(&m_context.objectArena)
            );
        }
        if(!writeDescriptorBuffer(writeItem, descriptorClass))
            return false;
        if(!retained)
            retained = Move(candidate);
        return true;
    }

    if(descriptorClass == GpuDescriptorClass::Sampler){
        if(handle.slot() >= m_samplerDescriptorResources.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: sampler slot {} is outside the retained table.")
                , handle.slot()
            );
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
        if(!writeDescriptorBuffer(writeItem, descriptorClass))
            return false;
        if(!retained)
            retained = Move(candidate);
        return true;
    }

    if(
        handle.slot() >= m_resourceDescriptorBuffers.size()
        || handle.slot() >= m_resourceDescriptorTextures.size()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: resource slot {} is outside the retained tables.")
            , handle.slot()
        );
        return false;
    }

    if(item.type == ResourceType::Texture_SRV || item.type == ResourceType::Texture_UAV){
        auto* const texture = static_cast<Texture*>(writeItem.resourceHandle);
        if(!m_device.isTextureReadyForGpuUse(texture)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a foreign or unready Texture."));
            return false;
        }

        const TextureDesc& textureDesc = texture->getDescription();
        if(textureDesc.sampleCount != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a multisampled bindless image."));
            return false;
        }

        if(IsSampledImageClass(descriptorClass)){
            const TextureDimension::Enum dimension = writeItem.dimension != TextureDimension::Unknown
                ? writeItem.dimension
                : textureDesc.dimension
            ;
            if(dimension != GetSampledImageDimension(descriptorClass)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected sampled-image dimension {} for class {}.")
                    , static_cast<u32>(dimension)
                    , static_cast<u32>(descriptorClass)
                );
                return false;
            }

            const Format::Enum format = writeItem.format != Format::UNKNOWN ? writeItem.format : textureDesc.format;
            if(format == Format::UNKNOWN || format >= Format::kCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected an invalid sampled-image format."));
                return false;
            }
            const FormatInfo& formatInfo = GetFormatInfo(format);
            const bool uintClass = descriptorClass == GpuDescriptorClass::SampledImage2DArrayUint;
            if(
                (uintClass && (formatInfo.kind != FormatKind::Integer || formatInfo.isSigned))
                || (!uintClass && formatInfo.kind == FormatKind::Integer)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected format {} for sampled-image class {}.")
                    , static_cast<u32>(format)
                    , static_cast<u32>(descriptorClass)
                );
                return false;
            }
        }
        else{
            const TextureDimension::Enum dimension = writeItem.dimension != TextureDimension::Unknown
                ? writeItem.dimension
                : textureDesc.dimension
            ;
            if(
                dimension != TextureDimension::Texture2D
                && dimension != TextureDimension::Texture2DArray
                && dimension != TextureDimension::Texture3D
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected texture dimension {} for StorageImage.")
                    , static_cast<u32>(dimension)
                );
                return false;
            }

            const Format::Enum format = writeItem.format != Format::UNKNOWN ? writeItem.format : textureDesc.format;
            if(format == Format::UNKNOWN || format >= Format::kCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected an invalid StorageImage format."));
                return false;
            }
            const FormatInfo& formatInfo = GetFormatInfo(format);
            const bool unsignedInteger = formatInfo.kind == FormatKind::Integer && !formatInfo.isSigned;
            const bool floatOrNormalized = formatInfo.kind == FormatKind::Float
                || formatInfo.kind == FormatKind::Normalized
            ;
            if(
                Format::IsBlockCompressedFormat(format)
                || formatInfo.hasDepth
                || formatInfo.hasStencil
                || (!unsignedInteger && !floatOrNormalized)
                || (dimension == TextureDimension::Texture3D && !floatOrNormalized)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected format {} for StorageImage dimension {}.")
                    , static_cast<u32>(format)
                    , static_cast<u32>(dimension)
                );
                return false;
            }
        }

        TextureHandle& retained = m_resourceDescriptorTextures[handle.slot()];
        if(m_resourceDescriptorBuffers[handle.slot()] || (retained && retained.get() != texture)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: live Texture replacement requires a fresh handle."));
            return false;
        }
        TextureHandle candidate(nullptr, TextureHandle::deleter_type(&m_context.objectArena));
        if(!retained)
            candidate = TextureHandle(texture, TextureHandle::deleter_type(&m_context.objectArena));
        if(!writeDescriptorBuffer(writeItem, descriptorClass))
            return false;
        if(!retained)
            retained = Move(candidate);
        return true;
    }

    auto* const buffer = static_cast<Buffer*>(writeItem.resourceHandle);
    if(!m_device.isBufferReadyForGpuUse(buffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected a foreign or unready Buffer."));
        return false;
    }
    if(descriptorClass == GpuDescriptorClass::SampledBuffer){
        const BufferDesc& bufferDesc = buffer->getDescription();
        const Format::Enum format = writeItem.format != Format::UNKNOWN ? writeItem.format : bufferDesc.format;
        if(format == Format::UNKNOWN || format >= Format::kCount){
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

    if(!writeDescriptorBuffer(writeItem, descriptorClass))
        return false;
    if(!retained)
        retained = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuDescriptorHeap::bindCompute(
    CommandList& commandList,
    const ComputePipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    // Bind persistent resource/sampler blocks at 8/9 and optional TLAS at 10.
    commandList.bindDescriptorBufferHeap(*this, pipeline, accelStructHandle);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline){
    commandList.bindDescriptorBufferHeap(*this, pipeline);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline){
    commandList.bindDescriptorBufferHeap(*this, pipeline);
}

void GpuDescriptorHeap::bindRayTracing(
    CommandList& commandList,
    const RayTracingPipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    commandList.bindDescriptorBufferHeap(*this, pipeline, accelStructHandle);
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
        const DescriptorBufferSegment block = m_context.descriptorBufferManager->allocateForBindingGeneration(
            bindingLayout->getDescriptorBufferSegmentKind(),
            setSizeBytes,
            offsetAlignmentBytes,
            m_descriptorBufferGeneration
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

        DescriptorBufferSegment& retainedBlock = m_accelStructBufferBlocks[writeItem.arrayElement];
        DescriptorBufferSegment candidateBlock{};
        const bool allocateBlock = !retainedBlock.valid();
        DescriptorBufferSegment* block = &retainedBlock;
        if(allocateBlock){
            candidateBlock = m_context.descriptorBufferManager->allocateForBindingGeneration(
                m_accelStructLayout->getDescriptorBufferSegmentKind(),
                setSizeBytes,
                m_context.descriptorBufferManager->getOffsetAlignmentBytes(),
                m_descriptorBufferGeneration
            );
            if(!candidateBlock.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: failed to carve {}-byte TLAS block."), setSizeBytes);
                return false;
            }
            block = &candidateBlock;
        }

        if(
            block->kind != DescriptorBufferSegmentKind::Resource
            || m_accelStructBufferBindingOffset > block->sizeBytes
            || descriptorSize > block->sizeBytes - m_accelStructBufferBindingOffset
            || static_cast<u64>(block->offsetBytes) + m_accelStructBufferBindingOffset > UINT32_MAX
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: TLAS descriptor block is invalid."));
            if(allocateBlock)
                m_context.descriptorBufferManager->freeForBindingGeneration(
                    candidateBlock,
                    m_descriptorBufferGeneration
                );
            return false;
        }

        if(!m_context.descriptorBufferManager->writeDescriptor(
            writeItem,
            *block,
            static_cast<u32>(static_cast<u64>(block->offsetBytes) + m_accelStructBufferBindingOffset),
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
        )){
            if(allocateBlock)
                m_context.descriptorBufferManager->freeForBindingGeneration(
                    candidateBlock,
                    m_descriptorBufferGeneration
                );
            return false;
        }
        if(allocateBlock)
            retainedBlock = candidateBlock;
        return true;
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

