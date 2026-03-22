// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr VkDescriptorType ConvertDescriptorType(ResourceType::Enum type){
    switch(type){
    case ResourceType::Texture_SRV:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ResourceType::Texture_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case ResourceType::TypedBuffer_SRV:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case ResourceType::TypedBuffer_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case ResourceType::StructuredBuffer_SRV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::StructuredBuffer_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case ResourceType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::RayTracingAccelStruct:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    default:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

constexpr VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages){
    VkShaderStageFlags flags = 0;

    if(stages & ShaderType::Vertex)
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if(stages & ShaderType::Hull)
        flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if(stages & ShaderType::Domain)
        flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if(stages & ShaderType::Geometry)
        flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if(stages & ShaderType::Pixel)
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if(stages & ShaderType::Compute)
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if(stages & ShaderType::Amplification)
        flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if(stages & ShaderType::Mesh)
        flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
    if(stages & ShaderType::AllRayTracing)
        flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | 
                 VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

    if(flags == 0)
        flags = VK_SHADER_STAGE_ALL;

    return flags;
}

constexpr DescriptorHeapKind GetDescriptorHeapKind(ResourceType::Enum type){
    return type == ResourceType::Sampler ? DescriptorHeapKind::Sampler : DescriptorHeapKind::Resource;
}

constexpr bool IsDescriptorHeapCompatibleType(ResourceType::Enum type){
    switch(type){
    case ResourceType::Texture_SRV:
    case ResourceType::Texture_UAV:
    case ResourceType::TypedBuffer_SRV:
    case ResourceType::TypedBuffer_UAV:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::ConstantBuffer:
    case ResourceType::Sampler:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
        return true;
    default:
        return false;
    }
}

constexpr u32 AlignUpU32(const u32 value, const u32 alignment){
    if(alignment == 0)
        return value;
    return value + (alignment - (value % alignment)) % alignment;
}

u32 FindMemoryTypeIndex(const VulkanContext& context, const u32 typeBits, const VkMemoryPropertyFlags properties){
    for(u32 i = 0; i < context.memoryProperties.memoryTypeCount; ++i){
        if((typeBits & (1u << i)) && (context.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    return UINT32_MAX;
}

VkSamplerAddressMode ConvertSamplerAddressMode(const SamplerAddressMode::Enum mode){
    switch(mode){
    case SamplerAddressMode::Clamp:      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerAddressMode::Wrap:       return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::Border:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case SamplerAddressMode::Mirror:     return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::MirrorOnce: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:                             return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

VkSamplerCreateInfo BuildSamplerCreateInfo(const SamplerDesc& desc){
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = desc.magFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.minFilter = desc.minFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = desc.mipFilter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = ConvertSamplerAddressMode(desc.addressU);
    samplerInfo.addressModeV = ConvertSamplerAddressMode(desc.addressV);
    samplerInfo.addressModeW = ConvertSamplerAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipBias;
    samplerInfo.anisotropyEnable = desc.maxAnisotropy > 1.f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;
    samplerInfo.compareEnable = desc.reductionType == SamplerReductionType::Comparison ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    return samplerInfo;
}

VkImageViewCreateInfo BuildImageViewCreateInfo(Texture& texture, const BindingSetItem& item){
    const TextureDesc& textureDesc = texture.getDescription();
    TextureDimension::Enum dimension = item.dimension != TextureDimension::Unknown ? item.dimension : textureDesc.dimension;
    Format::Enum format = item.format != Format::UNKNOWN ? item.format : textureDesc.format;
    TextureSubresourceSet subresources = item.subresources.resolve(textureDesc, false);

    const FormatInfo& formatInfo = GetFormatInfo(format);
    VkImageAspectFlags aspectMask = 0;
    if(formatInfo.hasDepth)
        aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if(!formatInfo.hasDepth && !formatInfo.hasStencil)
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = static_cast<VkImage>(static_cast<VkImage_T*>(texture.getNativeHandle(ObjectTypes::VK_Image)));
    viewInfo.viewType = TextureDimensionToViewType(dimension);
    viewInfo.format = ConvertFormat(format);
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = subresources.baseMipLevel;
    viewInfo.subresourceRange.levelCount = subresources.numMipLevels;
    viewInfo.subresourceRange.baseArrayLayer = subresources.baseArraySlice;
    viewInfo.subresourceRange.layerCount = subresources.numArraySlices;
    return viewInfo;
}

const BindingLayoutItem* FindLayoutBinding(const BindingLayoutDesc& desc, const u32 slot){
    for(const auto& binding : desc.bindings){
        if(binding.slot == slot)
            return &binding;
    }

    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DescriptorHeapManager::tryEnablePipeline(
    const VulkanContext& context,
    const BindingLayoutVector& bindingLayouts,
    Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>>& shaderStages,
    FixedVector<DescriptorHeapPushRange, s_MaxBindingLayouts>& outPushRanges,
    u32& outPushDataSize,
    VkPipelineCreateFlags2CreateInfo& outFlags2,
    Vector<VkDescriptorSetAndBindingMappingEXT, Alloc::ScratchAllocator<VkDescriptorSetAndBindingMappingEXT>>& outMappings,
    Vector<VkShaderDescriptorSetAndBindingMappingInfoEXT, Alloc::ScratchAllocator<VkShaderDescriptorSetAndBindingMappingInfoEXT>>& outStageMappings
)
{
    outPushRanges.resize(0);
    outPushDataSize = 0;
    outMappings.clear();
    outStageMappings.clear();

    if(!context.extensions.EXT_descriptor_heap || shaderStages.empty())
        return false;

    auto getDescriptorSetIndex = [](const BindingLayout& layout, const u32 pipelineBindingIndex) -> u32{
        const BindingLayoutDesc& layoutDesc = layout.getBindingLayoutDesc();
        return layoutDesc.registerSpaceIsDescriptorSet ? layoutDesc.registerSpace : pipelineBindingIndex;
    };

    bool hasAnyDescriptors = false;
    for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
        auto* layout = checked_cast<BindingLayout*>(bindingLayouts[i].get());
        if(!layout)
            continue;
        if(layout->isBindlessLayout() || !layout->isDescriptorHeapCompatible())
            return false;

        const u32 descriptorSetIndex = getDescriptorSetIndex(*layout, i);
        for(u32 j = 0; j < i; ++j){
            auto* prevLayout = checked_cast<BindingLayout*>(bindingLayouts[j].get());
            if(!prevLayout)
                continue;
            if(getDescriptorSetIndex(*prevLayout, j) == descriptorSetIndex)
                return false;
        }

        const auto& heapBindings = layout->getDescriptorHeapBindings();
        if(heapBindings.empty())
            continue;

        DescriptorHeapPushRange pushRange{};
        pushRange.bindingSetIndex = i;
        pushRange.pushOffsetBytes = outPushDataSize;
        pushRange.pushWordCount = static_cast<u32>(heapBindings.size());
        outPushRanges.push_back(pushRange);

        for(usize bindingIndex = 0; bindingIndex < heapBindings.size(); ++bindingIndex){
            const DescriptorHeapBindingMeta& meta = heapBindings[bindingIndex];

            VkDescriptorSetAndBindingMappingEXT mapping{};
            mapping.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
            mapping.descriptorSet = descriptorSetIndex;
            mapping.firstBinding = meta.slot;
            mapping.bindingCount = 1;
            mapping.resourceMask = VK_SPIRV_RESOURCE_TYPE_ALL_EXT;
            mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT;
            mapping.sourceData.pushIndex.heapOffset = 0;
            mapping.sourceData.pushIndex.pushOffset = pushRange.pushOffsetBytes + static_cast<u32>(bindingIndex * sizeof(u32));
            mapping.sourceData.pushIndex.heapIndexStride = meta.descriptorStride;
            mapping.sourceData.pushIndex.heapArrayStride = meta.descriptorStride;
            outMappings.push_back(mapping);
            hasAnyDescriptors = true;
        }

        outPushDataSize += pushRange.pushWordCount * sizeof(u32);
    }

    if(!hasAnyDescriptors)
        return false;
    if(outPushDataSize > context.descriptorHeapProperties.maxPushDataSize)
        return false;

    outFlags2 = {};
    outFlags2.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO;
    outFlags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

    outStageMappings.resize(shaderStages.size());
    for(usize i = 0; i < shaderStages.size(); ++i){
        VkShaderDescriptorSetAndBindingMappingInfoEXT mappingInfo{};
        mappingInfo.sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
        mappingInfo.mappingCount = static_cast<u32>(outMappings.size());
        mappingInfo.pMappings = outMappings.data();
        mappingInfo.pNext = shaderStages[i].pNext;
        outStageMappings[i] = mappingInfo;
        shaderStages[i].pNext = &outStageMappings[i];
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorHeapManager::DescriptorHeapManager(const VulkanContext& context)
    : m_context(context)
    , m_resourceHeap(context.objectArena)
    , m_samplerHeap(context.objectArena)
{}

DescriptorHeapManager::~DescriptorHeapManager(){
    shutdown();
}

bool DescriptorHeapManager::initialize(){
    shutdown();

    if(!m_context.extensions.EXT_descriptor_heap)
        return false;

    const auto& props = m_context.descriptorHeapProperties;

    constexpr u32 s_TargetResourceHeapBytes = 32u * 1024u * 1024u;
    constexpr u32 s_TargetSamplerHeapBytes = 2u * 1024u * 1024u;

    const u32 resourceReservedBytes = static_cast<u32>(props.minResourceHeapReservedRange);
    const u32 samplerReservedBytes = static_cast<u32>(props.minSamplerHeapReservedRange);

    const u32 resourceCapacityBytes = Min(static_cast<u32>(props.maxResourceHeapSize), __hidden_vulkan::AlignUpU32(resourceReservedBytes + s_TargetResourceHeapBytes, static_cast<u32>(props.resourceHeapAlignment)));
    const u32 samplerCapacityBytes = Min(static_cast<u32>(props.maxSamplerHeapSize), __hidden_vulkan::AlignUpU32(samplerReservedBytes + s_TargetSamplerHeapBytes, static_cast<u32>(props.samplerHeapAlignment)));

    if(resourceCapacityBytes <= resourceReservedBytes || samplerCapacityBytes <= samplerReservedBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor heap properties do not allow creating usable global heaps."));
        return false;
    }

    if(!initializeHeap(m_resourceHeap, "vk_resource_heap", resourceCapacityBytes, resourceReservedBytes)){
        shutdown();
        return false;
    }

    if(!initializeHeap(m_samplerHeap, "vk_sampler_heap", samplerCapacityBytes, samplerReservedBytes)){
        shutdown();
        return false;
    }

    m_enabled = true;
    return true;
}

void DescriptorHeapManager::shutdown(){
    shutdownHeap(m_resourceHeap);
    shutdownHeap(m_samplerHeap);
    m_enabled = false;
}

u32 DescriptorHeapManager::getDescriptorSize(const VkDescriptorType descriptorType)const{
    if(!m_enabled)
        return 0;

    const VkDeviceSize size = vkGetPhysicalDeviceDescriptorSizeEXT(m_context.physicalDevice, descriptorType);
    if(size > UINT32_MAX)
        return 0;

    return static_cast<u32>(size);
}

u32 DescriptorHeapManager::getDescriptorStride(const VkDescriptorType descriptorType)const{
    const u32 descriptorSize = getDescriptorSize(descriptorType);
    if(descriptorSize == 0)
        return 0;

    u32 alignment = descriptorSize;
    switch(descriptorType){
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        alignment = Max<u32>(alignment, static_cast<u32>(m_context.descriptorHeapProperties.samplerDescriptorAlignment));
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        alignment = Max<u32>(alignment, static_cast<u32>(m_context.descriptorHeapProperties.imageDescriptorAlignment));
        break;
    default:
        alignment = Max<u32>(alignment, static_cast<u32>(m_context.descriptorHeapProperties.bufferDescriptorAlignment));
        break;
    }

    return __hidden_vulkan::AlignUpU32(descriptorSize, alignment);
}

DescriptorHeapAllocation DescriptorHeapManager::allocate(const DescriptorHeapKind kind, const u32 sizeBytes, const u32 alignmentBytes){
    DescriptorHeapAllocation result{};
    if(!m_enabled || kind == DescriptorHeapKind::None || sizeBytes == 0)
        return result;

    HeapStorage& heap = kind == DescriptorHeapKind::Sampler ? m_samplerHeap : m_resourceHeap;
    auto clearAllocation = [&](const DescriptorHeapAllocation& allocation){
        if(allocation.valid() && heap.mappedMemory)
            NWB_MEMSET(static_cast<u8*>(heap.mappedMemory) + allocation.offsetBytes, 0, allocation.sizeBytes);
    };

    ScopedLock lock(heap.mutex);

    for(usize i = 0; i < heap.freeRanges.size(); ++i){
        FreeRange range = heap.freeRanges[i];
        const u32 alignedOffset = __hidden_vulkan::AlignUpU32(range.offsetBytes, alignmentBytes);
        if(alignedOffset >= range.offsetBytes + range.sizeBytes)
            continue;

        const u32 consumedPrefix = alignedOffset - range.offsetBytes;
        const u32 remainingBytes = range.sizeBytes - consumedPrefix;
        if(remainingBytes < sizeBytes)
            continue;

        const u32 rangeEnd = range.offsetBytes + range.sizeBytes;
        const u32 allocEnd = alignedOffset + sizeBytes;
        heap.freeRanges.erase(heap.freeRanges.begin() + static_cast<isize>(i));

        if(consumedPrefix > 0)
            heap.freeRanges.push_back({ range.offsetBytes, consumedPrefix });
        if(allocEnd < rangeEnd)
            heap.freeRanges.push_back({ allocEnd, rangeEnd - allocEnd });

        result.kind = kind;
        result.offsetBytes = alignedOffset;
        result.sizeBytes = sizeBytes;
        clearAllocation(result);
        return result;
    }

    const u32 alignedOffset = __hidden_vulkan::AlignUpU32(heap.writableOffsetBytes, alignmentBytes);
    if(alignedOffset > heap.capacityBytes || sizeBytes > heap.capacityBytes - alignedOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor heap is out of space (kind={}, requested={} bytes)."),
            kind == DescriptorHeapKind::Sampler ? NWB_TEXT("sampler") : NWB_TEXT("resource"),
            sizeBytes
        );
        return result;
    }

    heap.writableOffsetBytes = alignedOffset + sizeBytes;
    result.kind = kind;
    result.offsetBytes = alignedOffset;
    result.sizeBytes = sizeBytes;
    clearAllocation(result);
    return result;
}

void DescriptorHeapManager::free(const DescriptorHeapAllocation& allocation){
    if(!allocation.valid())
        return;

    HeapStorage& heap = allocation.kind == DescriptorHeapKind::Sampler ? m_samplerHeap : m_resourceHeap;
    ScopedLock lock(heap.mutex);
    heap.freeRanges.push_back({ allocation.offsetBytes, allocation.sizeBytes });
}

bool DescriptorHeapManager::writeDescriptor(const BindingSetItem& item, const DescriptorHeapBindingMeta& meta, const u32 dstOffsetBytes){
    if(!m_enabled)
        return false;

    HeapStorage& heap = meta.heapKind == DescriptorHeapKind::Sampler ? m_samplerHeap : m_resourceHeap;
    auto* dstBytes = static_cast<u8*>(heap.mappedMemory);
    if(!dstBytes)
        return false;

    VkHostAddressRangeEXT dstRange{};
    dstRange.address = dstBytes + dstOffsetBytes;
    dstRange.size = meta.descriptorSize;

    if(meta.heapKind == DescriptorHeapKind::Sampler){
        auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
        if(!sampler)
            return false;

        const VkSamplerCreateInfo samplerInfo = __hidden_vulkan::BuildSamplerCreateInfo(sampler->getDescription());
        return vkWriteSamplerDescriptorsEXT(m_context.device, 1, &samplerInfo, &dstRange) == VK_SUCCESS;
    }

    VkResourceDescriptorInfoEXT resourceInfo{ VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT };
    VkImageDescriptorInfoEXT imageInfo{ VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT };
    VkImageViewCreateInfo imageViewInfo{};
    VkTexelBufferDescriptorInfoEXT texelInfo{ VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT };
    VkDeviceAddressRangeEXT addressRange{};

    resourceInfo.type = meta.descriptorType;

    switch(item.type){
    case ResourceType::ConstantBuffer:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:{
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!buffer)
            return false;
        const BufferDesc& bufferDesc = buffer->getDescription();
        if(item.range.byteOffset > bufferDesc.byteSize)
            return false;
        addressRange.address = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress()) + item.range.byteOffset;
        const u64 remainingBytes = bufferDesc.byteSize - item.range.byteOffset;
        addressRange.size = item.range.byteSize > 0 ? Min<u64>(item.range.byteSize, remainingBytes) : remainingBytes;
        resourceInfo.data.pAddressRange = &addressRange;
        break;
    }
    case ResourceType::TypedBuffer_SRV:
    case ResourceType::TypedBuffer_UAV:{
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!buffer)
            return false;
        const BufferDesc& bufferDesc = buffer->getDescription();
        if(item.range.byteOffset > bufferDesc.byteSize)
            return false;
        const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : bufferDesc.format;
        texelInfo.format = ConvertFormat(viewFormat);
        texelInfo.addressRange.address = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress()) + item.range.byteOffset;
        const u64 remainingBytes = bufferDesc.byteSize - item.range.byteOffset;
        texelInfo.addressRange.size = item.range.byteSize > 0 ? Min<u64>(item.range.byteSize, remainingBytes) : remainingBytes;
        resourceInfo.data.pTexelBuffer = &texelInfo;
        break;
    }
    case ResourceType::Texture_SRV:
    case ResourceType::Texture_UAV:{
        auto* texture = checked_cast<Texture*>(item.resourceHandle);
        if(!texture)
            return false;
        imageViewInfo = __hidden_vulkan::BuildImageViewCreateInfo(*texture, item);
        imageInfo.pView = &imageViewInfo;
        imageInfo.layout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        resourceInfo.data.pImage = &imageInfo;
        break;
    }
    default:
        return false;
    }

    return vkWriteResourceDescriptorsEXT(m_context.device, 1, &resourceInfo, &dstRange) == VK_SUCCESS;
}

bool DescriptorHeapManager::initializeHeap(HeapStorage& heap, const CompactString& debugName, const u32 capacityBytes, const u32 reservedRangeBytes){
    VkResult res = VK_SUCCESS;

    shutdownHeap(heap);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = capacityBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res = vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &heap.buffer);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor heap buffer '{}': {}"), StringConvert(debugName.view()), ResultToString(res));
        return false;
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(m_context.device, heap.buffer, &memRequirements);

    const u32 memoryTypeIndex = __hidden_vulkan::FindMemoryTypeIndex(
        m_context,
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    if(memoryTypeIndex == UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to find host-visible memory for descriptor heap '{}'."),
            StringConvert(debugName.view())
        );
        shutdownHeap(heap);
        return false;
    }

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    allocInfo.pNext = &flagsInfo;

    res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &heap.memory);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor heap memory '{}': {}"), StringConvert(debugName.view()), ResultToString(res));
        shutdownHeap(heap);
        return false;
    }

    res = vkBindBufferMemory(m_context.device, heap.buffer, heap.memory, 0);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind descriptor heap memory '{}': {}"), StringConvert(debugName.view()), ResultToString(res));
        shutdownHeap(heap);
        return false;
    }

    res = vkMapMemory(m_context.device, heap.memory, 0, capacityBytes, 0, &heap.mappedMemory);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map descriptor heap memory '{}': {}"), StringConvert(debugName.view()), ResultToString(res));
        shutdownHeap(heap);
        return false;
    }

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = heap.buffer;
    heap.deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    if(heap.deviceAddress == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query descriptor heap device address '{}'."),
            StringConvert(debugName.view())
        );
        shutdownHeap(heap);
        return false;
    }

    heap.capacityBytes = capacityBytes;
    heap.bindInfo.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
    heap.bindInfo.heapRange.address = heap.deviceAddress;
    heap.bindInfo.heapRange.size = capacityBytes;
    heap.bindInfo.reservedRangeOffset = 0;
    heap.bindInfo.reservedRangeSize = reservedRangeBytes;
    heap.writableOffsetBytes = reservedRangeBytes;

    NWB_MEMSET(heap.mappedMemory, 0, capacityBytes);
    return true;
}

void DescriptorHeapManager::shutdownHeap(HeapStorage& heap){
    if(heap.mappedMemory){
        vkUnmapMemory(m_context.device, heap.memory);
        heap.mappedMemory = nullptr;
    }

    if(heap.buffer != VK_NULL_HANDLE){
        vkDestroyBuffer(m_context.device, heap.buffer, m_context.allocationCallbacks);
        heap.buffer = VK_NULL_HANDLE;
    }

    if(heap.memory != VK_NULL_HANDLE){
        vkFreeMemory(m_context.device, heap.memory, m_context.allocationCallbacks);
        heap.memory = VK_NULL_HANDLE;
    }

    heap.deviceAddress = 0;
    heap.capacityBytes = 0;
    heap.writableOffsetBytes = 0;
    heap.bindInfo = {};
    heap.freeRanges.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayout::BindingLayout(const VulkanContext& context)
    : RefCounter<IBindingLayout>(context.threadPool)
    , m_context(context)
    , m_descriptorSetLayouts(Alloc::CustomAllocator<VkDescriptorSetLayout>(context.objectArena))
    , m_descriptorHeapBindings(Alloc::CustomAllocator<DescriptorHeapBindingMeta>(context.objectArena))
{}
BindingLayout::~BindingLayout(){
    if(m_pipelineLayout){
        vkDestroyPipelineLayout(m_context.device, m_pipelineLayout, m_context.allocationCallbacks);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    for(VkDescriptorSetLayout layout : m_descriptorSetLayouts){
        if(layout)
            vkDestroyDescriptorSetLayout(m_context.device, layout, m_context.allocationCallbacks);
    }
    m_descriptorSetLayouts.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTable::DescriptorTable(const VulkanContext& context)
    : RefCounter<IDescriptorTable>(context.threadPool)
    , m_context(context)
    , m_descriptorSets(Alloc::CustomAllocator<VkDescriptorSet>(context.objectArena))
{}
DescriptorTable::~DescriptorTable(){
    if(m_descriptorPool != VK_NULL_HANDLE){
        vkDestroyDescriptorPool(m_context.device, m_descriptorPool, m_context.allocationCallbacks);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingSet::BindingSet(const VulkanContext& context)
    : RefCounter<IBindingSet>(context.threadPool)
    , m_context(context)
    , m_descriptorSets(Alloc::CustomAllocator<VkDescriptorSet>(context.objectArena))
    , m_descriptorHeapPushIndices(Alloc::CustomAllocator<u32>(context.objectArena))
    , m_descriptorHeapAllocations(Alloc::CustomAllocator<DescriptorHeapAllocation>(context.objectArena))
{}
BindingSet::~BindingSet(){
    if(m_context.descriptorHeapManager){
        for(const DescriptorHeapAllocation& allocation : m_descriptorHeapAllocations)
            m_context.descriptorHeapManager->free(allocation);
    }
    m_descriptorTable.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_desc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>> bindings{ Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>(scratchArena) };
    bindings.reserve(desc.bindings.size());

    u32 pushConstantByteSize = 0;
    for(const auto& item : desc.bindings){
        if(item.type == ResourceType::PushConstants){
            pushConstantByteSize = item.size;
            continue;
        }
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
        binding.descriptorCount = item.getArraySize();
        binding.stageFlags = __hidden_vulkan::ConvertShaderStages(desc.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    layout->m_descriptorSetLayouts.push_back(setLayout);

    VkPushConstantRange pushConstantRange = {};
    bool hasPushConstants = pushConstantByteSize > 0;

    if(hasPushConstants){
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = hasPushConstants ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = hasPushConstants ? &pushConstantRange : nullptr;

    res = vkCreatePipelineLayout(m_context.device, &pipelineLayoutInfo, m_context.allocationCallbacks, &layout->m_pipelineLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for binding layout: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    if(m_context.extensions.EXT_descriptor_heap){
        bool compatible = !hasPushConstants;

        if(compatible){
            layout->m_descriptorHeapBindings.reserve(desc.bindings.size());
            for(const auto& item : desc.bindings){
                if(item.type == ResourceType::PushConstants || item.type == ResourceType::None)
                    continue;

                if(!__hidden_vulkan::IsDescriptorHeapCompatibleType(item.type)){
                    compatible = false;
                    break;
                }

                const VkDescriptorType descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
                const u32 descriptorSize = m_descriptorHeapManager.getDescriptorSize(descriptorType);
                const u32 descriptorStride = m_descriptorHeapManager.getDescriptorStride(descriptorType);
                if(descriptorSize == 0 || descriptorStride == 0){
                    compatible = false;
                    break;
                }

                DescriptorHeapBindingMeta meta{};
                meta.resourceType = item.type;
                meta.descriptorType = descriptorType;
                meta.heapKind = __hidden_vulkan::GetDescriptorHeapKind(item.type);
                meta.slot = item.slot;
                meta.arraySize = item.getArraySize();
                meta.descriptorSize = descriptorSize;
                meta.descriptorStride = descriptorStride;
                layout->m_descriptorHeapBindings.push_back(meta);
            }
        }

        layout->m_descriptorHeapCompatible = compatible && !layout->m_descriptorHeapBindings.empty();
        if(!layout->m_descriptorHeapCompatible)
            layout->m_descriptorHeapBindings.clear();
    }

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_isBindless = true;
    layout->m_bindlessDesc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>> bindings{ Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>(scratchArena) };
    bindings.reserve(desc.registerSpaces.size());
    Vector<VkDescriptorBindingFlags, Alloc::ScratchAllocator<VkDescriptorBindingFlags>> bindingFlags{ Alloc::ScratchAllocator<VkDescriptorBindingFlags>(scratchArena) };
    bindingFlags.reserve(desc.registerSpaces.size());

    for(const auto& item : desc.registerSpaces){
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
        binding.descriptorCount = desc.maxCapacity;
        binding.stageFlags = __hidden_vulkan::ConvertShaderStages(desc.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);

        VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        bindingFlags.push_back(flags);
    }

    if(!bindingFlags.empty())
        bindingFlags.back() |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    bindingFlagsInfo.bindingCount = static_cast<u32>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create bindless descriptor set layout: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    layout->m_descriptorSetLayouts.push_back(setLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();

    res = vkCreatePipelineLayout(m_context.device, &pipelineLayoutInfo, m_context.allocationCallbacks, &layout->m_pipelineLayout);

    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for bindless layout: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTableHandle Device::createDescriptorTable(IBindingLayout* _layout){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* layout = checked_cast<BindingLayout*>(_layout);
    if(!layout)
        return nullptr;
    if(layout->m_descriptorSetLayouts.empty())
        return nullptr;

    auto* table = NewArenaObject<DescriptorTable>(m_context.objectArena, m_context);
    table->m_layout = layout;

    Vector<VkDescriptorPoolSize, Alloc::ScratchAllocator<VkDescriptorPoolSize>> poolSizes{ Alloc::ScratchAllocator<VkDescriptorPoolSize>(scratchArena) };
    poolSizes.reserve(8);

    auto addPoolSize = [&](VkDescriptorType type, u32 count){
        if(count == 0)
            return;

        for(auto& poolSize : poolSizes){
            if(poolSize.type == type){
                poolSize.descriptorCount += count;
                return;
            }
        }

        VkDescriptorPoolSize poolSize = {};
        poolSize.type = type;
        poolSize.descriptorCount = count;
        poolSizes.push_back(poolSize);
    };

    if(layout->m_isBindless){
        for(const auto& item : layout->m_bindlessDesc.registerSpaces){
            const VkDescriptorType type = __hidden_vulkan::ConvertDescriptorType(item.type);
            addPoolSize(type, layout->m_bindlessDesc.maxCapacity > 0 ? layout->m_bindlessDesc.maxCapacity : 1u);
        }
    }
    else{
        for(const auto& item : layout->m_desc.bindings){
            if(item.type == ResourceType::PushConstants || item.type == ResourceType::None)
                continue;

            const VkDescriptorType type = __hidden_vulkan::ConvertDescriptorType(item.type);
            addPoolSize(type, item.getArraySize() > 0 ? item.getArraySize() : 1u);
        }
    }

    if(poolSizes.empty()){
        VkDescriptorPoolSize fallback = {};
        fallback.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        fallback.descriptorCount = 1;
        poolSizes.push_back(fallback);
    }

    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = static_cast<u32>(layout->m_descriptorSetLayouts.size());
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &pool);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor pool: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, table);
        return nullptr;
    }

    if(!layout->m_descriptorSetLayouts.empty()){
        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
        allocInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();

        table->m_descriptorSets.resize(layout->m_descriptorSetLayouts.size());
        res = vkAllocateDescriptorSets(m_context.device, &allocInfo, table->m_descriptorSets.data());

        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor sets: {}"), ResultToString(res));
            vkDestroyDescriptorPool(m_context.device, pool, m_context.allocationCallbacks);
            DestroyArenaObject(m_context.objectArena, table);
            return nullptr;
        }
    }

    table->m_descriptorPool = pool;

    return DescriptorTableHandle(table, DescriptorTableHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void Device::resizeDescriptorTable(IDescriptorTable* m_descriptorTable, u32 newSize, bool keepContents){
    VkResult res = VK_SUCCESS;

    auto* table = checked_cast<DescriptorTable*>(m_descriptorTable);
    if(!table)
        return;

    if(!table->m_layout || newSize == 0)
        return;

    if(table->m_descriptorPool != VK_NULL_HANDLE){
        vkDestroyDescriptorPool(m_context.device, table->m_descriptorPool, m_context.allocationCallbacks);
        table->m_descriptorPool = VK_NULL_HANDLE;
    }
    table->m_descriptorSets.clear();

    Alloc::ScratchArena<> scratchArena;

    Vector<VkDescriptorPoolSize, Alloc::ScratchAllocator<VkDescriptorPoolSize>> poolSizes{ Alloc::ScratchAllocator<VkDescriptorPoolSize>(scratchArena) };
    poolSizes.reserve(7);
    VkDescriptorPoolSize uniformSize = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, newSize };
    VkDescriptorPoolSize storageSize = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, newSize };
    VkDescriptorPoolSize samplerSize = { VK_DESCRIPTOR_TYPE_SAMPLER, newSize };
    VkDescriptorPoolSize sampledImageSize = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, newSize };
    VkDescriptorPoolSize storageImageSize = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, newSize };
    VkDescriptorPoolSize uniformTexelSize = { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, newSize };
    VkDescriptorPoolSize storageTexelSize = { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, newSize };
    poolSizes.push_back(uniformSize);
    poolSizes.push_back(storageSize);
    poolSizes.push_back(samplerSize);
    poolSizes.push_back(sampledImageSize);
    poolSizes.push_back(storageImageSize);
    poolSizes.push_back(uniformTexelSize);
    poolSizes.push_back(storageTexelSize);

    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = newSize;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &table->m_descriptorPool);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor pool for resize: {}"), ResultToString(res));
        return;
    }

    if(!table->m_layout->m_descriptorSetLayouts.empty()){
        Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> layouts(newSize, table->m_layout->m_descriptorSetLayouts[0], Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena));

        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = table->m_descriptorPool;
        allocInfo.descriptorSetCount = newSize;
        allocInfo.pSetLayouts = layouts.data();

        table->m_descriptorSets.resize(newSize);
        res = vkAllocateDescriptorSets(m_context.device, &allocInfo, table->m_descriptorSets.data());
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor sets during resize: {}"), ResultToString(res));
            table->m_descriptorSets.clear();
            return;
        }
    }

    (void)keepContents;
}

bool Device::writeDescriptorTable(IDescriptorTable* m_descriptorTable, const BindingSetItem& item){
    auto* table = checked_cast<DescriptorTable*>(m_descriptorTable);
    if(!table)
        return false;
    if(!item.resourceHandle)
        return false;

    if(table->m_descriptorSets.empty())
        return false;

    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = table->m_descriptorSets[0];
    write.dstBinding = item.slot;
    write.dstArrayElement = item.arrayElement;
    write.descriptorCount = 1;
    write.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);

    VkDescriptorBufferInfo bufferInfo = {};
    VkDescriptorImageInfo imageInfo = {};
    VkBufferView texelBufferView = VK_NULL_HANDLE;
    VkWriteDescriptorSetAccelerationStructureKHR asInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };

    switch(item.type){
    case ResourceType::ConstantBuffer:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:{
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!buffer)
            return false;
        bufferInfo.buffer = buffer->m_buffer;
        bufferInfo.offset = item.range.byteOffset;
        bufferInfo.range = item.range.byteSize > 0 ? item.range.byteSize : VK_WHOLE_SIZE;
        write.pBufferInfo = &bufferInfo;
        break;
    }
    case ResourceType::Texture_SRV:
    case ResourceType::Texture_UAV:{
        auto* texture = checked_cast<Texture*>(item.resourceHandle);
        if(!texture)
            return false;
        imageInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
        imageInfo.imageLayout = item.type == ResourceType::Texture_UAV ? 
                                VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write.pImageInfo = &imageInfo;
        break;
    }
    case ResourceType::TypedBuffer_SRV:
    case ResourceType::TypedBuffer_UAV:{
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!buffer)
            return false;
        const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : buffer->m_desc.format;
        texelBufferView = buffer->getView(viewFormat, item.range.byteOffset, item.range.byteSize);
        if(texelBufferView == VK_NULL_HANDLE)
            return false;
        write.pTexelBufferView = &texelBufferView;
        break;
    }
    case ResourceType::Sampler:{
        auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
        if(!sampler)
            return false;
        imageInfo.sampler = sampler->m_sampler;
        write.pImageInfo = &imageInfo;
        break;
    }
    case ResourceType::RayTracingAccelStruct:{
        auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
        if(!as)
            return false;
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &as->m_accelStruct;
        write.pNext = &asInfo;
        break;
    }
    default:
        return false;
    }

    vkUpdateDescriptorSets(m_context.device, 1, &write, 0, nullptr);
    return true;
}


BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* _layout){
    auto* layout = checked_cast<BindingLayout*>(_layout);
    if(!layout)
        return nullptr;

    auto* bindingSet = NewArenaObject<BindingSet>(m_context.objectArena, m_context);
    bindingSet->m_desc = desc;

    DescriptorTableHandle tableHandle = createDescriptorTable(_layout);
    if(!tableHandle){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table for binding set"));
        DestroyArenaObject(m_context.objectArena, bindingSet);
        return nullptr;
    }

    bindingSet->m_descriptorTable = checked_cast<DescriptorTable*>(tableHandle.get());
    if(!bindingSet->m_descriptorTable){
        DestroyArenaObject(m_context.objectArena, bindingSet);
        return nullptr;
    }

    bindingSet->m_descriptorSets = bindingSet->m_descriptorTable->m_descriptorSets;
    if(bindingSet->m_descriptorSets.empty()){
        DestroyArenaObject(m_context.objectArena, bindingSet);
        return nullptr;
    }
    bindingSet->m_layout = layout;

    if(layout->m_descriptorHeapCompatible && m_context.descriptorHeapManager){
        bindingSet->m_descriptorHeapPushIndices.resize(layout->m_descriptorHeapBindings.size(), 0u);
        bindingSet->m_descriptorHeapAllocations.resize(layout->m_descriptorHeapBindings.size());

        for(usize i = 0; i < layout->m_descriptorHeapBindings.size(); ++i){
            const DescriptorHeapBindingMeta& meta = layout->m_descriptorHeapBindings[i];
            const u32 allocationSizeBytes = meta.arraySize * meta.descriptorStride;
            const DescriptorHeapAllocation allocation = m_context.descriptorHeapManager->allocate(meta.heapKind, allocationSizeBytes, meta.descriptorStride);
            if(!allocation.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor heap storage for binding slot {}"), meta.slot);
                DestroyArenaObject(m_context.objectArena, bindingSet);
                return nullptr;
            }

            bindingSet->m_descriptorHeapAllocations[i] = allocation;
            bindingSet->m_descriptorHeapPushIndices[i] = allocation.offsetBytes / meta.descriptorStride;
        }
    }

    Alloc::ScratchArena<> scratchArena(s_DescriptorBindingScratchArenaBytes);

    Vector<VkWriteDescriptorSet, Alloc::ScratchAllocator<VkWriteDescriptorSet>> writes{ Alloc::ScratchAllocator<VkWriteDescriptorSet>(scratchArena) };
    Vector<VkDescriptorBufferInfo, Alloc::ScratchAllocator<VkDescriptorBufferInfo>> bufferInfos{ Alloc::ScratchAllocator<VkDescriptorBufferInfo>(scratchArena) };
    Vector<VkDescriptorImageInfo, Alloc::ScratchAllocator<VkDescriptorImageInfo>> imageInfos{ Alloc::ScratchAllocator<VkDescriptorImageInfo>(scratchArena) };
    Vector<VkBufferView, Alloc::ScratchAllocator<VkBufferView>> texelBufferViews{ Alloc::ScratchAllocator<VkBufferView>(scratchArena) };
    Vector<VkWriteDescriptorSetAccelerationStructureKHR, Alloc::ScratchAllocator<VkWriteDescriptorSetAccelerationStructureKHR>> asInfos{ Alloc::ScratchAllocator<VkWriteDescriptorSetAccelerationStructureKHR>(scratchArena) };

    writes.reserve(desc.bindings.size());
    bufferInfos.reserve(desc.bindings.size());
    imageInfos.reserve(desc.bindings.size());
    texelBufferViews.reserve(desc.bindings.size());
    asInfos.reserve(desc.bindings.size());
    HashMap<u32, usize, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<Pair<const u32, usize>>> descriptorHeapMetaLookup(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Alloc::ScratchAllocator<Pair<const u32, usize>>(scratchArena)
    );
    if(layout->m_descriptorHeapCompatible && m_context.descriptorHeapManager){
        descriptorHeapMetaLookup.reserve(layout->m_descriptorHeapBindings.size());
        for(usize i = 0; i < layout->m_descriptorHeapBindings.size(); ++i)
            descriptorHeapMetaLookup[layout->m_descriptorHeapBindings[i].slot] = i;
    }

    for(const auto& item : desc.bindings){
        if(!item.resourceHandle)
            continue;

        const BindingLayoutItem* layoutBinding = __hidden_vulkan::FindLayoutBinding(layout->m_desc, item.slot);
        if(!layoutBinding){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for unknown slot {}"), item.slot);
            continue;
        }
        if(item.arrayElement >= layoutBinding->getArraySize()){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {} with out-of-range array element {}"), item.slot, item.arrayElement);
            continue;
        }

        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = bindingSet->m_descriptorSets[0];
        write.dstBinding = item.slot;
        write.dstArrayElement = item.arrayElement;
        write.descriptorCount = 1;
        write.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);

        switch(item.type){
        case ResourceType::ConstantBuffer:
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_SRV:
        case ResourceType::RawBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            if(!buffer)
                continue;
            VkDescriptorBufferInfo bufInfo = {};
            bufInfo.buffer = buffer->m_buffer;
            bufInfo.offset = item.range.byteOffset;
            bufInfo.range = item.range.byteSize > 0 ? item.range.byteSize : VK_WHOLE_SIZE;
            bufferInfos.push_back(bufInfo);
            write.pBufferInfo = &bufferInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:{
            auto* texture = checked_cast<Texture*>(item.resourceHandle);
            if(!texture)
                continue;
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
            imgInfo.imageLayout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos.push_back(imgInfo);
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::Sampler:{
            auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
            if(!sampler)
                continue;
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.sampler = sampler->m_sampler;
            imageInfos.push_back(imgInfo);
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::TypedBuffer_SRV:
        case ResourceType::TypedBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            if(!buffer)
                continue;
            const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : buffer->m_desc.format;
            VkBufferView view = buffer->getView(viewFormat, item.range.byteOffset, item.range.byteSize);
            if(view == VK_NULL_HANDLE)
                continue;
            texelBufferViews.push_back(view);
            write.pTexelBufferView = &texelBufferViews.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::RayTracingAccelStruct:{
            auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
            if(!as)
                continue;
            VkWriteDescriptorSetAccelerationStructureKHR asWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &as->m_accelStruct;
            asInfos.push_back(asWrite);
            write.pNext = &asInfos.back();
            writes.push_back(write);
            break;
        }
        default:
            break;
        }

        if(layout->m_descriptorHeapCompatible && m_context.descriptorHeapManager){
            const auto metaIt = descriptorHeapMetaLookup.find(item.slot);
            if(metaIt != descriptorHeapMetaLookup.end()){
                const usize metaIndex = metaIt->second;
                const DescriptorHeapBindingMeta& meta = layout->m_descriptorHeapBindings[metaIndex];
                const DescriptorHeapAllocation& allocation = bindingSet->m_descriptorHeapAllocations[metaIndex];
                const u32 descriptorOffset = allocation.offsetBytes + item.arrayElement * meta.descriptorStride;
                if(!m_context.descriptorHeapManager->writeDescriptor(item, meta, descriptorOffset)){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to write descriptor heap entry for slot {}"), item.slot);
                }
            }
        }
    }

    if(!writes.empty())
        vkUpdateDescriptorSets(m_context.device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);

    return BindingSetHandle(bindingSet, BindingSetHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


void CommandList::retainBindingSets(const BindingSetVector& bindings){
    for(const auto& binding : bindings){
        if(binding)
            m_currentCmdBuf->m_referencedResources.push_back(binding);
    }
}

void CommandList::bindDescriptorHeapState(
    const bool usesDescriptorHeap,
    const FixedVector<DescriptorHeapPushRange, s_MaxBindingLayouts>& pushRanges,
    const u32 pushDataSize,
    const BindingSetVector& bindings)
{
    if(!usesDescriptorHeap || !m_context.descriptorHeapManager)
        return;

    vkCmdBindResourceHeapEXT(m_currentCmdBuf->m_cmdBuf, &m_context.descriptorHeapManager->getResourceBindInfo());
    vkCmdBindSamplerHeapEXT(m_currentCmdBuf->m_cmdBuf, &m_context.descriptorHeapManager->getSamplerBindInfo());

    if(pushDataSize == 0)
        return;

    Alloc::ScratchArena<> scratchArena(s_DescriptorBindingScratchArenaBytes);
    Vector<u32, Alloc::ScratchAllocator<u32>> pushWords(pushDataSize / sizeof(u32), 0u, Alloc::ScratchAllocator<u32>(scratchArena));

    for(const DescriptorHeapPushRange& range : pushRanges){
        if(range.pushWordCount == 0)
            continue;
        if(range.bindingSetIndex >= bindings.size())
            continue;

        auto* bindingSet = checked_cast<BindingSet*>(bindings[range.bindingSetIndex]);
        if(!bindingSet)
            continue;
        if(bindingSet->m_descriptorHeapPushIndices.size() < range.pushWordCount)
            continue;

        const u32 dstWordOffset = range.pushOffsetBytes / sizeof(u32);
        for(u32 i = 0; i < range.pushWordCount; ++i)
            pushWords[dstWordOffset + i] = bindingSet->m_descriptorHeapPushIndices[i];
    }

    VkPushDataInfoEXT pushDataInfo{};
    pushDataInfo.sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT;
    pushDataInfo.offset = 0;
    pushDataInfo.data.address = pushWords.data();
    pushDataInfo.data.size = pushDataSize;
    vkCmdPushDataEXT(m_currentCmdBuf->m_cmdBuf, &pushDataInfo);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

