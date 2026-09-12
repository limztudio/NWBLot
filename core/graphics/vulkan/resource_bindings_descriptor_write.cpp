// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DescriptorBufferManager::writeDescriptor(
    const DescriptorWriteItem& item,
    const DescriptorBufferSegment& allocation,
    const u32 dstOffsetBytes,
    const VkDescriptorType descriptorType
){
    ScopedLock lifecycleLock(m_lifecycleMutex);

    if(!m_enabled)
        return false;
    if(!allocation.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation is invalid."));
        return false;
    }
    if(item.type == ResourceType::VolatileConstantBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Persistent descriptor-buffer writes reject volatile constant buffers."));
        return false;
    }
    if(
        !VulkanDetail::IsSupportedDescriptorBindingType(item.type)
        || VulkanDetail::ConvertDescriptorType(item.type) != descriptorType
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: resource type {} does not match descriptor type {}.")
            , static_cast<u32>(item.type)
            , static_cast<u32>(descriptorType)
        );
        return false;
    }

    const bool isSampler = (descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER);
    const DescriptorBufferSegmentKind::Enum expectedKind = isSampler
        ? DescriptorBufferSegmentKind::Sampler
        : DescriptorBufferSegmentKind::Resource
    ;
    if(allocation.kind != expectedKind){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation has the wrong segment kind."));
        return false;
    }
    SegmentStorage& storage = isSampler ? m_samplerSegment : m_resourceSegment;
    if(allocation.storageIdentity != storage.storageIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation belongs to another storage."));
        return false;
    }

    const u32 descriptorSize = VulkanDetail::GetDescriptorSize(m_context, m_enabled, descriptorType);
    if(descriptorSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: unknown size for descriptor type {}."), static_cast<u32>(descriptorType));
        return false;
    }
    if(!item.resourceHandle){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: resource handle is null."));
        return false;
    }
    if(dstOffsetBytes < allocation.offsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: offset {} precedes its allocation."), dstOffsetBytes);
        return false;
    }
    const u32 allocationRelativeOffsetBytes = dstOffsetBytes - allocation.offsetBytes;
    if(
        allocationRelativeOffsetBytes > allocation.sizeBytes
        || descriptorSize > allocation.sizeBytes - allocationRelativeOffsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: offset {} + size {} exceeds its allocation."), dstOffsetBytes, descriptorSize);
        return false;
    }

    ScopedLock lock(storage.mutex);

    auto* dstBytes = static_cast<u8*>(storage.mappedMemory);
    if(!dstBytes)
        return false;
    if(dstOffsetBytes > storage.capacityBytes || descriptorSize > storage.capacityBytes - dstOffsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: offset {} + size {} exceeds segment capacity {}.")
            , dstOffsetBytes, descriptorSize, storage.capacityBytes
        );
        return false;
    }
    // Validate full live-allocation identity to reject stale recycled ranges.
    usize first = 0u;
    usize last = storage.liveAllocations.size();
    while(first < last){
        const usize middle = first + (last - first) / 2u;
        if(storage.liveAllocations[middle].offsetBytes <= dstOffsetBytes)
            first = middle + 1u;
        else
            last = middle;
    }

    bool ownsLiveAllocation = false;
    if(first > 0u){
        const DescriptorBufferSegment& liveAllocation = storage.liveAllocations[first - 1u];
        ownsLiveAllocation = liveAllocation.kind == allocation.kind
            && liveAllocation.offsetBytes == allocation.offsetBytes
            && liveAllocation.sizeBytes == allocation.sizeBytes
            && liveAllocation.storageIdentity == allocation.storageIdentity
            && liveAllocation.allocationSerial == allocation.allocationSerial
        ;
    }
    if(!ownsLiveAllocation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation is not live for offset {}."), dstOffsetBytes);
        return false;
    }

    auto getInfo = VulkanDetail::MakeVkStruct<VkDescriptorGetInfoEXT>(VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT);
    getInfo.type = descriptorType;

    VkDescriptorAddressInfoEXT addressInfo{};
    VkDescriptorImageInfo imageInfo{};
    VkSampler samplerHandle = VK_NULL_HANDLE;
    VkDeviceAddress accelStructAddress = 0;

    if(VulkanDetail::UsesDescriptorBufferInfo(item.type)){
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        const bool isUniform = item.type == ResourceType::ConstantBuffer;
        const VkBufferUsageFlags requiredUsage = isUniform
            ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        ;
        if(!m_device.isBufferReadyForGpuUse(buffer, requiredUsage)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready Buffer."));
            return false;
        }
        const BufferDesc& bufferDesc = buffer->getCreationDescription();
        if((buffer->m_bufferInfo.usage & requiredUsage) != requiredUsage){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer lacks native descriptor usage."));
            return false;
        }
        if(isUniform && (!bufferDesc.isConstantBuffer || bufferDesc.isVolatile)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer is not a persistent constant buffer."));
            return false;
        }
        if(
            (item.type == ResourceType::RawBuffer_SRV || item.type == ResourceType::RawBuffer_UAV)
            && !bufferDesc.canHaveRawViews
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer has no raw-view capability."));
            return false;
        }
        if(
            (item.type == ResourceType::StructuredBuffer_SRV || item.type == ResourceType::StructuredBuffer_UAV)
            && bufferDesc.structStride == 0u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer is not structured."));
            return false;
        }
        if(
            (item.type == ResourceType::StructuredBuffer_UAV || item.type == ResourceType::RawBuffer_UAV)
            && !bufferDesc.canHaveUAVs
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer has no UAV capability."));
            return false;
        }
        BufferRange range;
        if(!VulkanDetail::ResolveDescriptorBufferRange(item, bufferDesc, range))
            return false;
        const VkDeviceAddress bufferAddress = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress());
        if(bufferAddress == 0u || range.byteOffset > UINT64_MAX - bufferAddress){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: buffer has no valid device address."));
            return false;
        }
        const VkDeviceAddress descriptorAddress = bufferAddress + range.byteOffset;
        const u64 requiredAlignment = isUniform
            ? getUniformBufferAddressAlignmentBytes()
            : getStorageBufferAddressAlignmentBytes()
        ;
        if(
            (descriptorAddress % requiredAlignment) != 0u
            || range.byteSize > UINT64_MAX - descriptorAddress
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: buffer address or range is invalid."));
            return false;
        }
        addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
        addressInfo.address = descriptorAddress;
        addressInfo.range = range.byteSize;
        if(isUniform)
            getInfo.data.pUniformBuffer = &addressInfo;
        else
            getInfo.data.pStorageBuffer = &addressInfo;
    }
    else{
        switch(item.type){
        case ResourceType::TypedBuffer_SRV:
        case ResourceType::TypedBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            const VkBufferUsageFlags requiredUsage = item.type == ResourceType::TypedBuffer_UAV
                ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            ;
            if(!m_device.isBufferReadyForGpuUse(buffer, requiredUsage)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready typed Buffer."));
                return false;
            }
            const BufferDesc& bufferDesc = buffer->getCreationDescription();
            if(
                !bufferDesc.canHaveTypedViews
                || (item.type == ResourceType::TypedBuffer_UAV && !bufferDesc.canHaveUAVs)
                || (buffer->m_bufferInfo.usage & requiredUsage) != requiredUsage
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer lacks typed-view capability."));
                return false;
            }
            BufferRange range;
            if(!VulkanDetail::ResolveDescriptorBufferRange(item, bufferDesc, range))
                return false;
            const VkDeviceAddress bufferAddress = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress());
            if(bufferAddress == 0u || range.byteOffset > UINT64_MAX - bufferAddress){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed buffer has no valid device address."));
                return false;
            }
            const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : bufferDesc.format;
            if(viewFormat == Format::UNKNOWN || viewFormat >= Format::kCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed-buffer format is invalid."));
                return false;
            }
            const VkFormat vkFormat = ConvertFormat(viewFormat);
            const FormatInfo& formatInfo = GetFormatInfo(viewFormat);
            if(vkFormat == VK_FORMAT_UNDEFINED || formatInfo.bytesPerBlock == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed-buffer format is unsupported."));
                return false;
            }
            VkFormatProperties formatProperties{};
            m_context.instanceDispatch.vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, vkFormat, &formatProperties);
            const VkFormatFeatureFlags requiredFormatFeature = item.type == ResourceType::TypedBuffer_UAV
                ? VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT
                : VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT
            ;
            if((formatProperties.bufferFeatures & requiredFormatFeature) == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: format lacks texel-buffer support."));
                return false;
            }
            const VkDeviceAddress descriptorAddress = bufferAddress + range.byteOffset;
            if(
                (descriptorAddress % getTexelBufferAddressAlignmentBytes()) != 0u
                || (range.byteOffset % formatInfo.bytesPerBlock) != 0u
                || (range.byteSize % formatInfo.bytesPerBlock) != 0u
                || (range.byteSize / formatInfo.bytesPerBlock) > getMaxTexelBufferElements()
                || range.byteSize > UINT64_MAX - descriptorAddress
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed-buffer range is invalid."));
                return false;
            }
            addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addressInfo.address = descriptorAddress;
            addressInfo.range = range.byteSize;
            addressInfo.format = vkFormat;
            if(item.type == ResourceType::TypedBuffer_UAV)
                getInfo.data.pStorageTexelBuffer = &addressInfo;
            else
                getInfo.data.pUniformTexelBuffer = &addressInfo;
            break;
        }
        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:{
            auto* texture = checked_cast<Texture*>(item.resourceHandle);
            if(!m_device.isTextureReadyForGpuUse(texture)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready Texture."));
                return false;
            }
            const VkImageUsageFlags requiredUsage = item.type == ResourceType::Texture_UAV
                ? VK_IMAGE_USAGE_STORAGE_BIT
                : VK_IMAGE_USAGE_SAMPLED_BIT
            ;
            if((texture->m_imageInfo.usage & requiredUsage) == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Texture lacks native descriptor usage."));
                return false;
            }
            imageInfo.imageView = texture->getView(item.subresources, item.dimension, item.format);
            if(imageInfo.imageView == VK_NULL_HANDLE)
                return false;
            imageInfo.imageLayout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            getInfo.data.pSampledImage = &imageInfo;
            getInfo.data.pStorageImage = &imageInfo;
            break;
        }
        case ResourceType::Sampler:{
            auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
            if(&sampler->m_context != &m_context){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign Sampler."));
                return false;
            }
            samplerHandle = sampler->m_sampler;
            if(samplerHandle == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected an unready Sampler."));
                return false;
            }
            getInfo.data.pSampler = &samplerHandle;
            break;
        }
        case ResourceType::RayTracingAccelStruct:{
            // TLAS descriptor directly encodes the generation's device address.
            auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
            if(!m_device.isAccelStructReadyForGpuUse(as)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready AccelStruct."));
                return false;
            }
            if(!as->m_isTopLevelAtCreation){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a bottom-level AccelStruct."));
                return false;
            }
            accelStructAddress = static_cast<VkDeviceAddress>(as->getDeviceAddress());
            if(accelStructAddress == 0u)
                return false;
            getInfo.data.accelerationStructure = accelStructAddress;
            break;
        }
        default:
            return false;
        }
    }

    m_context.deviceDispatch.vkGetDescriptorEXT(m_context.device, &getInfo, descriptorSize, dstBytes + dstOffsetBytes);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

