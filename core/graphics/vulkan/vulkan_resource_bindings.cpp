// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkDescriptorType ConvertDescriptorType(ResourceType::Enum type){
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
    case ResourceType::VolatileConstantBuffer:
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

VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages){
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

bool ConfigurePipelineMultisampleState(
    const u32 sampleCount,
    const bool alphaToCoverageEnable,
    VkPipelineMultisampleStateCreateInfo& outState,
    const tchar* operationName)
{
    outState = MakeVkStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    if(!IsSupportedSampleCount(sampleCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: sample count {} is unsupported"), operationName, sampleCount);
        return false;
    }
    outState.rasterizationSamples = GetSampleCountFlagBits(sampleCount);
    outState.sampleShadingEnable = VK_FALSE;
    outState.alphaToCoverageEnable = alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
    return true;
}

void ConfigurePipelineDepthStencilState(
    const DepthStencilState& state,
    const bool includeStencilFaces,
    VkPipelineDepthStencilStateCreateInfo& outState)
{
    outState = MakeVkStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    outState.depthTestEnable = state.depthTestEnable ? VK_TRUE : VK_FALSE;
    outState.depthWriteEnable = state.depthWriteEnable ? VK_TRUE : VK_FALSE;
    outState.depthCompareOp = ConvertCompareOp(state.depthFunc);
    outState.depthBoundsTestEnable = VK_FALSE;
    outState.stencilTestEnable = state.stencilEnable ? VK_TRUE : VK_FALSE;
    if(includeStencilFaces){
        outState.front = ConvertStencilOpState(state, state.frontFaceStencil);
        outState.back = ConvertStencilOpState(state, state.backFaceStencil);
    }
}

bool BuildPipelineRenderingInfo(
    const FramebufferInfo& fbinfo,
    const tchar* operationName,
    VkPipelineRenderingCreateInfo& outRenderingInfo,
    PipelineRenderingFormatVector& outColorFormats)
{
    outColorFormats.clear();
    outColorFormats.reserve(fbinfo.colorFormats.size());
    for(u32 i = 0; i < static_cast<u32>(fbinfo.colorFormats.size()); ++i){
        const VkFormat vkFormat = ConvertFormat(fbinfo.colorFormats[i]);
        if(vkFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: color attachment format {} is unsupported"), operationName, i);
            return false;
        }
        outColorFormats.push_back(vkFormat);
    }

    outRenderingInfo = MakeVkStruct<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    outRenderingInfo.colorAttachmentCount = static_cast<u32>(outColorFormats.size());
    outRenderingInfo.pColorAttachmentFormats = outColorFormats.data();
    if(fbinfo.depthFormat != Format::UNKNOWN){
        const VkFormat vkDepthFormat = ConvertFormat(fbinfo.depthFormat);
        if(vkDepthFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: depth/stencil attachment format is unsupported"), operationName);
            return false;
        }
        const FormatInfo& depthFormatInfo = GetFormatInfo(fbinfo.depthFormat);
        if(!depthFormatInfo.hasDepth && !depthFormatInfo.hasStencil){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: depth/stencil attachment format has no depth or stencil aspect"), operationName);
            return false;
        }
        if(depthFormatInfo.hasDepth)
            outRenderingInfo.depthAttachmentFormat = vkDepthFormat;
        if(depthFormatInfo.hasStencil)
            outRenderingInfo.stencilAttachmentFormat = vkDepthFormat;
    }

    return true;
}

void DestroyPipelineAndOwnedLayout(
    const VkDevice device,
    const VkAllocationCallbacks* allocationCallbacks,
    VkPipeline& pipeline,
    VkPipelineLayout& pipelineLayout,
    bool& ownsPipelineLayout)
{
    if(pipeline){
        vkDestroyPipeline(device, pipeline, allocationCallbacks);
        pipeline = VK_NULL_HANDLE;
    }

    if(ownsPipelineLayout && pipelineLayout != VK_NULL_HANDLE){
        vkDestroyPipelineLayout(device, pipelineLayout, allocationCallbacks);
        pipelineLayout = VK_NULL_HANDLE;
        ownsPipelineLayout = false;
    }
}

constexpr DescriptorHeapKind::Enum GetDescriptorHeapKind(ResourceType::Enum type){
    return type == ResourceType::Sampler ? DescriptorHeapKind::Sampler : DescriptorHeapKind::Resource;
}

constexpr bool IsSupportedDescriptorBindingType(ResourceType::Enum type){
    switch(type){
    case ResourceType::Texture_SRV:
    case ResourceType::Texture_UAV:
    case ResourceType::TypedBuffer_SRV:
    case ResourceType::TypedBuffer_UAV:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::ConstantBuffer:
    case ResourceType::VolatileConstantBuffer:
    case ResourceType::Sampler:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
    case ResourceType::RayTracingAccelStruct:
        return true;
    default:
        return false;
    }
}

constexpr bool UsesDescriptorBufferInfo(ResourceType::Enum type){
    switch(type){
    case ResourceType::ConstantBuffer:
    case ResourceType::VolatileConstantBuffer:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
        return true;
    default:
        return false;
    }
}

constexpr bool IsDescriptorHeapCompatibleType(ResourceType::Enum type){
    return type != ResourceType::RayTracingAccelStruct && IsSupportedDescriptorBindingType(type);
}

constexpr u32 AlignUpU32(const u32 value, const u32 alignment){
    if(alignment == 0)
        return value;
    return value + (alignment - (value % alignment)) % alignment;
}

bool AlignUpU32Checked(const u32 value, const u32 alignment, u32& outValue){
    if(alignment == 0){
        outValue = value;
        return true;
    }

    const u32 remainder = value % alignment;
    if(remainder == 0){
        outValue = value;
        return true;
    }

    const u32 addend = alignment - remainder;
    if(value > UINT32_MAX - addend)
        return false;

    outValue = value + addend;
    return true;
}

template<typename PoolSizeVector>
bool AddDescriptorPoolSize(PoolSizeVector& poolSizes, const VkDescriptorType type, const u32 count){
    if(count == 0)
        return true;

    for(auto& poolSize : poolSizes){
        if(poolSize.type == type){
            if(poolSize.descriptorCount > UINT32_MAX - count)
                return false;

            poolSize.descriptorCount += count;
            return true;
        }
    }

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = type;
    poolSize.descriptorCount = count;
    poolSizes.push_back(poolSize);
    return true;
}

constexpr u32 NormalizeDescriptorTableCapacity(const u32 capacity){
    return capacity > 0 ? capacity : 1u;
}

bool ResolveDescriptorBufferRange(const BindingSetItem& item, const Buffer& buffer, BufferRange& outRange){
    outRange = item.range.resolve(buffer.getDescription());
    return outRange.byteSize > 0;
}

u32 GetPushConstantByteSize(const BindingLayoutDesc& desc){
    u32 pushConstantByteSize = 0;
    for(const auto& item : desc.bindings){
        if(item.type == ResourceType::PushConstants)
            pushConstantByteSize = Max<u32>(pushConstantByteSize, item.size);
    }
    return pushConstantByteSize;
}

bool ValidatePushConstantByteSize(const VulkanContext& context, const u32 byteSize, const tchar* operationName){
    if(byteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: push constant size is zero"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed push constant operation: size is zero"));
        return false;
    }
    if((byteSize & 3u) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: push constant size is not 4-byte aligned"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed push constant operation: size is not 4-byte aligned"));
        return false;
    }
    if(byteSize > context.physicalDeviceProperties.limits.maxPushConstantsSize){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to {}: push constant size {} exceeds device limit {}"),
            operationName,
            byteSize,
            context.physicalDeviceProperties.limits.maxPushConstantsSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed push constant operation: size exceeds device limit"));
        return false;
    }
    return true;
}

bool CreatePipelineLayout(
    const VulkanContext& context,
    const VkDescriptorSetLayout* setLayouts,
    const u32 setLayoutCount,
    const u32 pushConstantByteSize,
    VkPipelineLayout& outLayout,
    const tchar* operationName)
{
    outLayout = VK_NULL_HANDLE;

    VkPushConstantRange pushConstantRange = {};
    if(pushConstantByteSize > 0){
        if(!ValidatePushConstantByteSize(context, pushConstantByteSize, operationName))
            return false;

        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;
    }

    VkPipelineLayoutCreateInfo layoutInfo = MakeVkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.setLayoutCount = setLayoutCount;
    layoutInfo.pSetLayouts = setLayoutCount > 0 ? setLayouts : nullptr;
    layoutInfo.pushConstantRangeCount = pushConstantByteSize > 0 ? 1u : 0u;
    layoutInfo.pPushConstantRanges = pushConstantByteSize > 0 ? &pushConstantRange : nullptr;

    const VkResult res = vkCreatePipelineLayout(context.device, &layoutInfo, context.allocationCallbacks, &outLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for {}: {}"), operationName, ResultToString(res));
        outLayout = VK_NULL_HANDLE;
        return false;
    }

    return true;
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
    const f32 maxAnisotropy = desc.maxAnisotropy >= 1.f ? desc.maxAnisotropy : 1.f;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = desc.magFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.minFilter = desc.minFilter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = desc.mipFilter ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = ConvertSamplerAddressMode(desc.addressU);
    samplerInfo.addressModeV = ConvertSamplerAddressMode(desc.addressV);
    samplerInfo.addressModeW = ConvertSamplerAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipBias;
    samplerInfo.anisotropyEnable = maxAnisotropy > 1.f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = maxAnisotropy;
    samplerInfo.compareEnable = desc.reductionType == SamplerReductionType::Comparison ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    return samplerInfo;
}

bool BuildImageViewCreateInfo(Texture& texture, const BindingSetItem& item, VkImageViewCreateInfo& outViewInfo){
    const TextureDesc& textureDesc = texture.getDescription();
    TextureDimension::Enum dimension = item.dimension != TextureDimension::Unknown ? item.dimension : textureDesc.dimension;
    Format::Enum format = item.format != Format::UNKNOWN ? item.format : textureDesc.format;
    TextureSubresourceSet subresources = item.subresources.resolve(textureDesc, false);
    if(subresources.numMipLevels == 0 || subresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor image view: subresource range is invalid"));
        return false;
    }

    const VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor image view: format is unsupported"));
        return false;
    }

    if(dimension == TextureDimension::TextureCube && subresources.numArraySlices != 6){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor image view: cube views must include exactly 6 array layers"));
        return false;
    }
    if(dimension == TextureDimension::TextureCubeArray && (subresources.numArraySlices < 6 || (subresources.numArraySlices % 6) != 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor image view: cube array views must include a positive multiple of 6 array layers"));
        return false;
    }

    const VkImageAspectFlags aspectMask = GetImageAspectMask(GetFormatInfo(format));

    outViewInfo = {};
    outViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    outViewInfo.image = static_cast<VkImage>(static_cast<VkImage_T*>(texture.getNativeHandle(ObjectTypes::VK_Image)));
    outViewInfo.viewType = TextureDimensionToViewType(dimension);
    outViewInfo.format = vkFormat;
    outViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    outViewInfo.subresourceRange.aspectMask = aspectMask;
    outViewInfo.subresourceRange.baseMipLevel = subresources.baseMipLevel;
    outViewInfo.subresourceRange.levelCount = subresources.numMipLevels;
    outViewInfo.subresourceRange.baseArrayLayer = subresources.baseArraySlice;
    outViewInfo.subresourceRange.layerCount = subresources.numArraySlices;
    return true;
}

const BindingLayoutItem* FindLayoutBinding(const BindingLayoutDesc& desc, const u32 slot, const ResourceType::Enum type){
    for(const auto& binding : desc.bindings){
        if(binding.slot == slot && binding.type == type)
            return &binding;
    }

    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::createPipelineLayoutForBindingLayouts(
    const BindingLayoutVector& bindingLayouts,
    const tchar* operationName,
    VkPipelineLayout& outPipelineLayout,
    u32& outPushConstantByteSize,
    bool& outOwnsPipelineLayout,
    Alloc::ScratchArena<>& scratchArena)const
{
    outPipelineLayout = VK_NULL_HANDLE;
    outPushConstantByteSize = 0;
    outOwnsPipelineLayout = false;

    if(bindingLayouts.empty()){
        if(!VulkanDetail::CreatePipelineLayout(m_context, nullptr, 0, 0, outPipelineLayout, operationName))
            return false;

        outOwnsPipelineLayout = true;
        return true;
    }

    if(bindingLayouts.size() == 1){
        auto* layout = checked_cast<BindingLayout*>(bindingLayouts[0].get());
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: binding layout is invalid"), operationName);
            return false;
        }

        outPipelineLayout = layout->m_pipelineLayout;
        outPushConstantByteSize = layout->m_pushConstantByteSize;
        return true;
    }

    Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> descriptorSetLayouts{
        Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena)
    };
    descriptorSetLayouts.reserve(bindingLayouts.size());
    u32 pushConstantByteSize = 0;

    for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
        auto* layout = checked_cast<BindingLayout*>(bindingLayouts[i].get());
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: binding layout {} is invalid"), operationName, i);
            return false;
        }

        pushConstantByteSize = Max<u32>(
            pushConstantByteSize,
            VulkanDetail::GetPushConstantByteSize(layout->getBindingLayoutDesc())
        );
        for(const auto& descriptorSetLayout : layout->m_descriptorSetLayouts)
            descriptorSetLayouts.push_back(descriptorSetLayout);
    }

    if(!VulkanDetail::CreatePipelineLayout(
        m_context,
        descriptorSetLayouts.data(),
        static_cast<u32>(descriptorSetLayouts.size()),
        pushConstantByteSize,
        outPipelineLayout,
        operationName
    )){
        return false;
    }

    outPushConstantByteSize = pushConstantByteSize;
    outOwnsPipelineLayout = true;
    return true;
}

bool Device::configurePipelineBindings(
    const BindingLayoutVector& bindingLayouts,
    const tchar* operationName,
    PipelineShaderStageVector& shaderStages,
    PipelineDescriptorHeapScratch& descriptorHeapScratch,
    PipelineBindingState& outBindings,
    Alloc::ScratchArena<>& scratchArena)const
{
    outBindings.m_pipelineLayout = VK_NULL_HANDLE;
    outBindings.m_ownsPipelineLayout = false;
    outBindings.m_pushConstantByteSize = 0;

    outBindings.m_usesDescriptorHeap = DescriptorHeapManager::tryEnablePipeline(
        m_context,
        bindingLayouts,
        shaderStages,
        outBindings.m_descriptorHeapPushRanges,
        outBindings.m_descriptorHeapPushDataSize,
        descriptorHeapScratch
    );
    if(outBindings.m_usesDescriptorHeap)
        return true;

    return createPipelineLayoutForBindingLayouts(
        bindingLayouts,
        operationName,
        outBindings.m_pipelineLayout,
        outBindings.m_pushConstantByteSize,
        outBindings.m_ownsPipelineLayout,
        scratchArena
    );
}


void Device::appendPipelineShaderStage(
    IShader* shader,
    const VkShaderStageFlagBits stage,
    PipelineSpecializationInfoVector& specializationInfos,
    PipelineShaderStageVector& shaderStages)const
{
    auto* s = checked_cast<Shader*>(shader);
    VkPipelineShaderStageCreateInfo stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stageInfo.stage = stage;
    stageInfo.module = s->m_shaderModule;
    stageInfo.pName = s->m_entryPointName.c_str();

    if(!s->m_specializationEntries.empty()){
        VkSpecializationInfo specInfo{};
        specInfo.mapEntryCount = static_cast<u32>(s->m_specializationEntries.size());
        specInfo.pMapEntries = s->m_specializationEntries.data();
        specInfo.dataSize = s->m_specializationData.size();
        specInfo.pData = s->m_specializationData.data();
        specializationInfos.push_back(specInfo);
        stageInfo.pSpecializationInfo = &specializationInfos.back();
    }

    shaderStages.push_back(stageInfo);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DescriptorHeapManager::tryEnablePipeline(
    const VulkanContext& context,
    const BindingLayoutVector& bindingLayouts,
    PipelineShaderStageVector& shaderStages,
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
        if(heapBindings.size() > UINT32_MAX / sizeof(u32))
            return false;
        if(heapBindings.size() > Limit<usize>::s_Max - outMappings.size())
            return false;
        outMappings.reserve(outMappings.size() + heapBindings.size());

        const u32 pushDataBytes = static_cast<u32>(heapBindings.size() * sizeof(u32));
        if(outPushDataSize > UINT32_MAX - pushDataBytes)
            return false;
        if(outPushDataSize + pushDataBytes > context.descriptorHeapProperties.maxPushDataSize)
            return false;

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

        outPushDataSize += pushDataBytes;
    }

    if(!hasAnyDescriptors)
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

bool DescriptorHeapManager::tryEnablePipeline(
    const VulkanContext& context,
    const BindingLayoutVector& bindingLayouts,
    PipelineShaderStageVector& shaderStages,
    FixedVector<DescriptorHeapPushRange, s_MaxBindingLayouts>& outPushRanges,
    u32& outPushDataSize,
    PipelineDescriptorHeapScratch& scratch
)
{
    return tryEnablePipeline(
        context,
        bindingLayouts,
        shaderStages,
        outPushRanges,
        outPushDataSize,
        scratch.flags2,
        scratch.mappings,
        scratch.stageMappings);
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

    if(props.minResourceHeapReservedRange > UINT32_MAX || props.minSamplerHeapReservedRange > UINT32_MAX
        || props.resourceHeapAlignment > UINT32_MAX || props.samplerHeapAlignment > UINT32_MAX
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor heap properties exceed supported 32-bit heap offsets."));
        return false;
    }

    const u32 resourceReservedBytes = static_cast<u32>(props.minResourceHeapReservedRange);
    const u32 samplerReservedBytes = static_cast<u32>(props.minSamplerHeapReservedRange);
    const u32 resourceMaxBytes = props.maxResourceHeapSize > UINT32_MAX ? UINT32_MAX : static_cast<u32>(props.maxResourceHeapSize);
    const u32 samplerMaxBytes = props.maxSamplerHeapSize > UINT32_MAX ? UINT32_MAX : static_cast<u32>(props.maxSamplerHeapSize);
    const u32 resourceAlignment = static_cast<u32>(props.resourceHeapAlignment);
    const u32 samplerAlignment = static_cast<u32>(props.samplerHeapAlignment);

    u32 resourceRequestedBytes = 0;
    u32 samplerRequestedBytes = 0;
    if(resourceReservedBytes > UINT32_MAX - s_TargetResourceHeapBytes
        || samplerReservedBytes > UINT32_MAX - s_TargetSamplerHeapBytes
        || !VulkanDetail::AlignUpU32Checked(resourceReservedBytes + s_TargetResourceHeapBytes, resourceAlignment, resourceRequestedBytes)
        || !VulkanDetail::AlignUpU32Checked(samplerReservedBytes + s_TargetSamplerHeapBytes, samplerAlignment, samplerRequestedBytes)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor heap requested capacity overflows 32-bit heap offsets."));
        return false;
    }

    const u32 resourceCapacityBytes = Min(resourceMaxBytes, resourceRequestedBytes);
    const u32 samplerCapacityBytes = Min(samplerMaxBytes, samplerRequestedBytes);

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

    VkDeviceSize alignmentValue = descriptorSize;
    switch(descriptorType){
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        alignmentValue = Max<VkDeviceSize>(alignmentValue, m_context.descriptorHeapProperties.samplerDescriptorAlignment);
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        alignmentValue = Max<VkDeviceSize>(alignmentValue, m_context.descriptorHeapProperties.imageDescriptorAlignment);
        break;
    default:
        alignmentValue = Max<VkDeviceSize>(alignmentValue, m_context.descriptorHeapProperties.bufferDescriptorAlignment);
        break;
    }
    if(alignmentValue > UINT32_MAX)
        return 0;

    u32 stride = 0;
    if(!VulkanDetail::AlignUpU32Checked(descriptorSize, static_cast<u32>(alignmentValue), stride))
        return 0;

    return stride;
}

DescriptorHeapAllocation DescriptorHeapManager::allocate(const DescriptorHeapKind::Enum kind, const u32 sizeBytes, const u32 alignmentBytes){
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
        if(range.sizeBytes > UINT32_MAX - range.offsetBytes)
            continue;

        u32 alignedOffset = 0;
        if(!VulkanDetail::AlignUpU32Checked(range.offsetBytes, alignmentBytes, alignedOffset))
            continue;

        const u32 rangeEnd = range.offsetBytes + range.sizeBytes;
        if(alignedOffset >= rangeEnd)
            continue;

        const u32 consumedPrefix = alignedOffset - range.offsetBytes;
        const u32 remainingBytes = range.sizeBytes - consumedPrefix;
        if(remainingBytes < sizeBytes)
            continue;
        if(sizeBytes > UINT32_MAX - alignedOffset)
            continue;

        const u32 allocEnd = alignedOffset + sizeBytes;
        if(i + 1u != heap.freeRanges.size())
            heap.freeRanges[i] = heap.freeRanges.back();
        heap.freeRanges.pop_back();

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

    u32 alignedOffset = 0;
    if(!VulkanDetail::AlignUpU32Checked(heap.writableOffsetBytes, alignmentBytes, alignedOffset)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor heap alignment overflows 32-bit offsets."));
        return result;
    }
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

        const VkSamplerCreateInfo samplerInfo = VulkanDetail::BuildSamplerCreateInfo(sampler->getDescription());
        return vkWriteSamplerDescriptorsEXT(m_context.device, 1, &samplerInfo, &dstRange) == VK_SUCCESS;
    }

    VkResourceDescriptorInfoEXT resourceInfo = VulkanDetail::MakeVkStruct<VkResourceDescriptorInfoEXT>(VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT);
    VkImageDescriptorInfoEXT imageInfo = VulkanDetail::MakeVkStruct<VkImageDescriptorInfoEXT>(VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT);
    VkImageViewCreateInfo imageViewInfo{};
    VkTexelBufferDescriptorInfoEXT texelInfo = VulkanDetail::MakeVkStruct<VkTexelBufferDescriptorInfoEXT>(VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT);
    VkDeviceAddressRangeEXT addressRange{};

    resourceInfo.type = meta.descriptorType;

    if(VulkanDetail::UsesDescriptorBufferInfo(item.type)){
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!buffer)
            return false;
        BufferRange range;
        if(!VulkanDetail::ResolveDescriptorBufferRange(item, *buffer, range))
            return false;
        addressRange.address = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress()) + range.byteOffset;
        addressRange.size = range.byteSize;
        resourceInfo.data.pAddressRange = &addressRange;
    }
    else{
        switch(item.type){
        case ResourceType::TypedBuffer_SRV:
        case ResourceType::TypedBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            if(!buffer)
                return false;
            const BufferDesc& bufferDesc = buffer->getDescription();
            BufferRange range;
            if(!VulkanDetail::ResolveDescriptorBufferRange(item, *buffer, range))
                return false;
            const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : bufferDesc.format;
            const VkFormat vkFormat = ConvertFormat(viewFormat);
            if(vkFormat == VK_FORMAT_UNDEFINED)
                return false;
            texelInfo.format = vkFormat;
            texelInfo.addressRange.address = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress()) + range.byteOffset;
            texelInfo.addressRange.size = range.byteSize;
            resourceInfo.data.pTexelBuffer = &texelInfo;
            break;
        }
        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:{
            auto* texture = checked_cast<Texture*>(item.resourceHandle);
            if(!texture)
                return false;
            const TextureSubresourceSet subresources = item.subresources.resolve(texture->getDescription(), false);
            if(subresources.numMipLevels == 0 || subresources.numArraySlices == 0)
                return false;
            if(!VulkanDetail::BuildImageViewCreateInfo(*texture, item, imageViewInfo))
                return false;
            imageInfo.pView = &imageViewInfo;
            imageInfo.layout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            resourceInfo.data.pImage = &imageInfo;
            break;
        }
        default:
            return false;
        }
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

    const u32 memoryTypeIndex = VulkanDetail::FindMemoryTypeIndex(
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
    , m_descriptorSetLayouts(Alloc::CustomAllocator<VkDescriptorSetLayout>(context.objectArena))
    , m_descriptorHeapBindings(Alloc::CustomAllocator<DescriptorHeapBindingMeta>(context.objectArena))
    , m_descriptorHeapBindingLookup(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Alloc::CustomAllocator<Pair<const u32, usize>>(context.objectArena)
    )
    , m_context(context)
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
    , m_descriptorSets(Alloc::CustomAllocator<VkDescriptorSet>(context.objectArena))
    , m_writtenItems(Alloc::CustomAllocator<BindingSetItem>(context.objectArena))
    , m_context(context)
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
    , m_descriptorSets(Alloc::CustomAllocator<VkDescriptorSet>(context.objectArena))
    , m_descriptorHeapPushIndices(Alloc::CustomAllocator<u32>(context.objectArena))
    , m_descriptorHeapAllocations(Alloc::CustomAllocator<DescriptorHeapAllocation>(context.objectArena))
    , m_context(context)
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

    if(desc.bindings.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: binding count exceeds Vulkan limit"));
        return nullptr;
    }

    Alloc::ScratchArena<> scratchArena;

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_desc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>> bindings{ Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>(scratchArena) };
    bindings.reserve(desc.bindings.size());
    HashSet<u32, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<u32>> bindingSlots(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Alloc::ScratchAllocator<u32>(scratchArena)
    );
    bindingSlots.reserve(desc.bindings.size());

    for(usize i = 0; i < desc.bindings.size(); ++i){
        const auto& item = desc.bindings[i];
        if(item.type == ResourceType::None)
            continue;
        if(item.type == ResourceType::PushConstants){
            if(!VulkanDetail::ValidatePushConstantByteSize(m_context, item.size, NWB_TEXT("create binding layout"))){
                DestroyArenaObject(m_context.objectArena, layout);
                return nullptr;
            }
            continue;
        }
        if(item.getArraySize() == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: binding slot {} has zero descriptors"), item.slot);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create descriptor set layout: binding has zero descriptors"));
            DestroyArenaObject(m_context.objectArena, layout);
            return nullptr;
        }
        if(!VulkanDetail::IsSupportedDescriptorBindingType(item.type)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create descriptor set layout: binding slot {} has unsupported resource type {}"),
                item.slot,
                static_cast<u32>(item.type)
            );
            DestroyArenaObject(m_context.objectArena, layout);
            return nullptr;
        }
        const auto slotInsert = bindingSlots.insert(item.slot);
        if(!slotInsert.second){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: duplicate binding slot {}"), item.slot);
            DestroyArenaObject(m_context.objectArena, layout);
            return nullptr;
        }
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = VulkanDetail::ConvertDescriptorType(item.type);
        binding.descriptorCount = item.getArraySize();
        binding.stageFlags = VulkanDetail::ConvertShaderStages(desc.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }
    const u32 pushConstantByteSize = VulkanDetail::GetPushConstantByteSize(desc);
    layout->m_pushConstantByteSize = pushConstantByteSize;

    VkDescriptorSetLayoutCreateInfo layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
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

    const bool hasPushConstants = pushConstantByteSize > 0;
    if(!VulkanDetail::CreatePipelineLayout(
        m_context,
        layout->m_descriptorSetLayouts.data(),
        static_cast<u32>(layout->m_descriptorSetLayouts.size()),
        pushConstantByteSize,
        layout->m_pipelineLayout,
        NWB_TEXT("create binding layout")
    )){
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    if(m_context.extensions.EXT_descriptor_heap){
        bool compatible = !hasPushConstants;

        if(compatible){
            layout->m_descriptorHeapBindings.reserve(desc.bindings.size());
            layout->m_descriptorHeapBindingLookup.reserve(desc.bindings.size());
            for(const auto& item : desc.bindings){
                if(item.type == ResourceType::PushConstants || item.type == ResourceType::None)
                    continue;

                if(!VulkanDetail::IsDescriptorHeapCompatibleType(item.type)){
                    compatible = false;
                    break;
                }

                const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(item.type);
                const u32 descriptorSize = m_descriptorHeapManager.getDescriptorSize(descriptorType);
                const u32 descriptorStride = m_descriptorHeapManager.getDescriptorStride(descriptorType);
                if(descriptorSize == 0 || descriptorStride == 0){
                    compatible = false;
                    break;
                }

                DescriptorHeapBindingMeta meta{};
                meta.resourceType = item.type;
                meta.descriptorType = descriptorType;
                meta.heapKind = VulkanDetail::GetDescriptorHeapKind(item.type);
                meta.slot = item.slot;
                meta.arraySize = item.getArraySize();
                meta.descriptorSize = descriptorSize;
                meta.descriptorStride = descriptorStride;
                const usize metaIndex = layout->m_descriptorHeapBindings.size();
                layout->m_descriptorHeapBindings.push_back(meta);
                layout->m_descriptorHeapBindingLookup.insert_or_assign(meta.slot, metaIndex);
            }
        }

        layout->m_descriptorHeapCompatible = compatible && !layout->m_descriptorHeapBindings.empty();
        if(!layout->m_descriptorHeapCompatible){
            layout->m_descriptorHeapBindings.clear();
            layout->m_descriptorHeapBindingLookup.clear();
        }
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
    HashSet<u32, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<u32>> registerSpaceSlots(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Alloc::ScratchAllocator<u32>(scratchArena)
    );
    registerSpaceSlots.reserve(desc.registerSpaces.size());

    const u32 maxCapacity = VulkanDetail::NormalizeDescriptorTableCapacity(desc.maxCapacity);
    for(usize i = 0; i < desc.registerSpaces.size(); ++i){
        const auto& item = desc.registerSpaces[i];
        if(!VulkanDetail::IsDescriptorHeapCompatibleType(item.type)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create bindless layout: register space slot {} has unsupported resource type {}"),
                item.slot,
                static_cast<u32>(item.type)
            );
            DestroyArenaObject(m_context.objectArena, layout);
            return nullptr;
        }
        const auto slotInsert = registerSpaceSlots.insert(item.slot);
        if(!slotInsert.second){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: duplicate register space slot {}"), item.slot);
            DestroyArenaObject(m_context.objectArena, layout);
            return nullptr;
        }

        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = VulkanDetail::ConvertDescriptorType(item.type);
        binding.descriptorCount = maxCapacity;
        binding.stageFlags = VulkanDetail::ConvertShaderStages(desc.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);

        VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        bindingFlags.push_back(flags);
    }

    if(!bindingFlags.empty())
        bindingFlags.back() |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutBindingFlagsCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
    bindingFlagsInfo.bindingCount = static_cast<u32>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
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

    if(!VulkanDetail::CreatePipelineLayout(
        m_context,
        layout->m_descriptorSetLayouts.data(),
        static_cast<u32>(layout->m_descriptorSetLayouts.size()),
        0,
        layout->m_pipelineLayout,
        NWB_TEXT("create bindless layout")
    )){
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTableHandle Device::createDescriptorTable(IBindingLayout* layoutResource){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* layout = checked_cast<BindingLayout*>(layoutResource);
    if(!layout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table: binding layout is invalid"));
        return nullptr;
    }
    if(layout->m_descriptorSetLayouts.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table: binding layout has no descriptor set layouts"));
        return nullptr;
    }

    auto* table = NewArenaObject<DescriptorTable>(m_context.objectArena, m_context);
    table->m_layout = layout;
    const u32 descriptorTableCapacity = layout->m_isBindless
        ? VulkanDetail::NormalizeDescriptorTableCapacity(layout->m_bindlessDesc.maxCapacity)
        : 1u
    ;
    const bool useVariableDescriptorCount = layout->m_isBindless && !layout->m_bindlessDesc.registerSpaces.empty();

    Vector<VkDescriptorPoolSize, Alloc::ScratchAllocator<VkDescriptorPoolSize>> poolSizes{ Alloc::ScratchAllocator<VkDescriptorPoolSize>(scratchArena) };
    poolSizes.reserve(8);

    if(layout->m_isBindless){
        for(const auto& item : layout->m_bindlessDesc.registerSpaces){
            const VkDescriptorType type = VulkanDetail::ConvertDescriptorType(item.type);
            if(!VulkanDetail::AddDescriptorPoolSize(poolSizes, type, descriptorTableCapacity)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table: descriptor pool size overflows"));
                DestroyArenaObject(m_context.objectArena, table);
                return nullptr;
            }
        }
    }
    else{
        for(const auto& item : layout->m_desc.bindings){
            if(item.type == ResourceType::PushConstants || item.type == ResourceType::None)
                continue;

            const VkDescriptorType type = VulkanDetail::ConvertDescriptorType(item.type);
            if(!VulkanDetail::AddDescriptorPoolSize(poolSizes, type, item.getArraySize() > 0 ? item.getArraySize() : 1u)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table: descriptor pool size overflows"));
                DestroyArenaObject(m_context.objectArena, table);
                return nullptr;
            }
        }
    }

    if(poolSizes.empty()){
        VkDescriptorPoolSize fallback = {};
        fallback.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        fallback.descriptorCount = 1;
        poolSizes.push_back(fallback);
    }

    VkDescriptorPoolCreateInfo poolInfo = VulkanDetail::MakeVkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if(layout->m_isBindless)
        poolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
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
        VkDescriptorSetAllocateInfo allocInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
        allocInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();

        VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorInfo =
            VulkanDetail::MakeVkStruct<VkDescriptorSetVariableDescriptorCountAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
        u32 variableDescriptorCount = descriptorTableCapacity;
        if(useVariableDescriptorCount){
            variableDescriptorInfo.descriptorSetCount = 1;
            variableDescriptorInfo.pDescriptorCounts = &variableDescriptorCount;
            allocInfo.pNext = &variableDescriptorInfo;
        }

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
    table->m_capacity = descriptorTableCapacity;

    return DescriptorTableHandle(table, DescriptorTableHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void Device::resizeDescriptorTable(IDescriptorTable* m_descriptorTable, u32 newSize, bool keepContents){
    VkResult res = VK_SUCCESS;

    auto* table = checked_cast<DescriptorTable*>(m_descriptorTable);
    if(!table)
        return;

    if(!table->m_layout || newSize == 0)
        return;
    if(table->m_layout->m_isBindless){
        const u32 maxCapacity = VulkanDetail::NormalizeDescriptorTableCapacity(table->m_layout->m_bindlessDesc.maxCapacity);
        if(newSize > maxCapacity){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to resize bindless descriptor table to {} descriptors: layout max capacity is {}"),
                newSize,
                maxCapacity
            );
            return;
        }
    }

    Alloc::ScratchArena<> scratchArena;
    using DescriptorSetVector = Vector<VkDescriptorSet, Alloc::CustomAllocator<VkDescriptorSet>>;
    using WrittenItemVector = Vector<BindingSetItem, Alloc::CustomAllocator<BindingSetItem>>;

    auto commitResize = [&](VkDescriptorPool newPool, DescriptorSetVector& newDescriptorSets, u32 newCapacity) -> bool{
        VkDescriptorPool oldPool = table->m_descriptorPool;
        const u32 oldCapacity = table->m_capacity;

        DescriptorSetVector oldDescriptorSets{ Alloc::CustomAllocator<VkDescriptorSet>(m_context.objectArena) };
        oldDescriptorSets = Move(table->m_descriptorSets);

        WrittenItemVector oldWrittenItems{ Alloc::CustomAllocator<BindingSetItem>(m_context.objectArena) };
        oldWrittenItems = Move(table->m_writtenItems);

        table->m_descriptorPool = newPool;
        table->m_descriptorSets = Move(newDescriptorSets);
        table->m_capacity = newCapacity;
        table->m_writtenItems.clear();

        bool replaySucceeded = true;
        if(keepContents){
            for(const BindingSetItem& item : oldWrittenItems){
                if(table->m_layout->m_isBindless && item.arrayElement >= newCapacity)
                    continue;
                if(!writeDescriptorTable(table, item)){
                    replaySucceeded = false;
                    break;
                }
            }
        }

        if(!replaySucceeded){
            if(newPool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(m_context.device, newPool, m_context.allocationCallbacks);

            table->m_descriptorPool = oldPool;
            table->m_descriptorSets = Move(oldDescriptorSets);
            table->m_capacity = oldCapacity;
            table->m_writtenItems = Move(oldWrittenItems);
            return false;
        }

        if(!keepContents)
            table->m_writtenItems.clear();

        if(oldPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_context.device, oldPool, m_context.allocationCallbacks);

        return true;
    };

    if(table->m_layout->m_isBindless){
        if(table->m_layout->m_descriptorSetLayouts.empty())
            return;

        Vector<VkDescriptorPoolSize, Alloc::ScratchAllocator<VkDescriptorPoolSize>> poolSizes{ Alloc::ScratchAllocator<VkDescriptorPoolSize>(scratchArena) };
        poolSizes.reserve(table->m_layout->m_bindlessDesc.registerSpaces.size());

        for(const auto& item : table->m_layout->m_bindlessDesc.registerSpaces){
            if(!VulkanDetail::AddDescriptorPoolSize(poolSizes, VulkanDetail::ConvertDescriptorType(item.type), newSize)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resize descriptor table: descriptor pool size overflows"));
                (void)keepContents;
                return;
            }
        }

        if(poolSizes.empty()){
            VkDescriptorPoolSize fallback = {};
            fallback.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            fallback.descriptorCount = newSize;
            poolSizes.push_back(fallback);
        }

        VkDescriptorPool newPool = VK_NULL_HANDLE;
        DescriptorSetVector newDescriptorSets{ Alloc::CustomAllocator<VkDescriptorSet>(m_context.objectArena) };

        VkDescriptorPoolCreateInfo poolInfo = VulkanDetail::MakeVkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &newPool);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless descriptor pool for resize: {}"), ResultToString(res));
            (void)keepContents;
            return;
        }

        VkDescriptorSetAllocateInfo allocInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocInfo.descriptorPool = newPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = table->m_layout->m_descriptorSetLayouts.data();

        VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescriptorInfo =
            VulkanDetail::MakeVkStruct<VkDescriptorSetVariableDescriptorCountAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
        if(!table->m_layout->m_bindlessDesc.registerSpaces.empty()){
            variableDescriptorInfo.descriptorSetCount = 1;
            variableDescriptorInfo.pDescriptorCounts = &newSize;
            allocInfo.pNext = &variableDescriptorInfo;
        }

        newDescriptorSets.resize(1);
        res = vkAllocateDescriptorSets(m_context.device, &allocInfo, newDescriptorSets.data());
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate bindless descriptor set during resize: {}"), ResultToString(res));
            vkDestroyDescriptorPool(m_context.device, newPool, m_context.allocationCallbacks);
            (void)keepContents;
            return;
        }

        (void)commitResize(newPool, newDescriptorSets, newSize);
        return;
    }

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

    VkDescriptorPool newPool = VK_NULL_HANDLE;
    DescriptorSetVector newDescriptorSets{ Alloc::CustomAllocator<VkDescriptorSet>(m_context.objectArena) };

    VkDescriptorPoolCreateInfo poolInfo = VulkanDetail::MakeVkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = newSize;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &newPool);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor pool for resize: {}"), ResultToString(res));
        return;
    }

    if(!table->m_layout->m_descriptorSetLayouts.empty()){
        Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> layouts(newSize, table->m_layout->m_descriptorSetLayouts[0], Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena));

        VkDescriptorSetAllocateInfo allocInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocInfo.descriptorPool = newPool;
        allocInfo.descriptorSetCount = newSize;
        allocInfo.pSetLayouts = layouts.data();

        newDescriptorSets.resize(newSize);
        res = vkAllocateDescriptorSets(m_context.device, &allocInfo, newDescriptorSets.data());
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor sets during resize: {}"), ResultToString(res));
            vkDestroyDescriptorPool(m_context.device, newPool, m_context.allocationCallbacks);
            return;
        }
    }

    (void)commitResize(newPool, newDescriptorSets, newSize);
}

bool Device::writeDescriptorTable(IDescriptorTable* m_descriptorTable, const BindingSetItem& item){
    auto* table = checked_cast<DescriptorTable*>(m_descriptorTable);
    if(!table){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table: descriptor table is invalid"));
        return false;
    }
    if(!item.resourceHandle){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: resource handle is null"), item.slot);
        return false;
    }

    if(table->m_descriptorSets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: descriptor table has no descriptor sets"), item.slot);
        return false;
    }
    if(table->m_layout && table->m_layout->m_isBindless && item.arrayElement >= table->m_capacity){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: array element {} exceeds bindless capacity {}"),
            item.slot,
            item.arrayElement,
            table->m_capacity
        );
        return false;
    }
    if(!table->m_layout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: descriptor table has no binding layout"), item.slot);
        return false;
    }

    const BindingLayoutItem* layoutBinding = nullptr;
    if(table->m_layout->m_isBindless){
        for(const auto& binding : table->m_layout->m_bindlessDesc.registerSpaces){
            if(binding.slot == item.slot && binding.type == item.type){
                layoutBinding = &binding;
                break;
            }
        }
    }
    else
        layoutBinding = VulkanDetail::FindLayoutBinding(table->m_layout->m_desc, item.slot, item.type);

    if(!layoutBinding){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: layout does not contain resource type {} at that slot"),
            item.slot,
            static_cast<u32>(item.type)
        );
        return false;
    }
    if(!table->m_layout->m_isBindless && item.arrayElement >= layoutBinding->getArraySize()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: array element {} exceeds layout array size {}"),
            item.slot,
            item.arrayElement,
            layoutBinding->getArraySize()
        );
        return false;
    }

    VkWriteDescriptorSet write = VulkanDetail::MakeVkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = table->m_descriptorSets[0];
    write.dstBinding = item.slot;
    write.dstArrayElement = item.arrayElement;
    write.descriptorCount = 1;
    write.descriptorType = VulkanDetail::ConvertDescriptorType(item.type);

    VkDescriptorBufferInfo bufferInfo = {};
    VkDescriptorImageInfo imageInfo = {};
    VkBufferView texelBufferView = VK_NULL_HANDLE;
    VkWriteDescriptorSetAccelerationStructureKHR asInfo = VulkanDetail::MakeVkStruct<VkWriteDescriptorSetAccelerationStructureKHR>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);

    if(VulkanDetail::UsesDescriptorBufferInfo(item.type)){
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!buffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: buffer resource is invalid"), item.slot);
            return false;
        }
        BufferRange range;
        if(!VulkanDetail::ResolveDescriptorBufferRange(item, *buffer, range)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: buffer range is empty or outside the buffer"), item.slot);
            return false;
        }
        bufferInfo.buffer = buffer->m_buffer;
        bufferInfo.offset = range.byteOffset;
        bufferInfo.range = range.byteSize;
        write.pBufferInfo = &bufferInfo;
    }
    else{
        switch(item.type){
        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:{
            auto* texture = checked_cast<Texture*>(item.resourceHandle);
            if(!texture){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: texture resource is invalid"), item.slot);
                return false;
            }
            imageInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
            if(imageInfo.imageView == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: texture image view is null"), item.slot);
                return false;
            }
            imageInfo.imageLayout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            write.pImageInfo = &imageInfo;
            break;
        }
        case ResourceType::TypedBuffer_SRV:
        case ResourceType::TypedBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            if(!buffer){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: typed buffer resource is invalid"), item.slot);
                return false;
            }
            const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : buffer->m_desc.format;
            texelBufferView = buffer->getView(viewFormat, item.range.byteOffset, item.range.byteSize);
            if(texelBufferView == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: typed buffer view is null"), item.slot);
                return false;
            }
            write.pTexelBufferView = &texelBufferView;
            break;
        }
        case ResourceType::Sampler:{
            auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
            if(!sampler){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: sampler resource is invalid"), item.slot);
                return false;
            }
            imageInfo.sampler = sampler->m_sampler;
            write.pImageInfo = &imageInfo;
            break;
        }
        case ResourceType::RayTracingAccelStruct:{
            auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
            if(!as){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: acceleration structure resource is invalid"), item.slot);
                return false;
            }
            asInfo.accelerationStructureCount = 1;
            asInfo.pAccelerationStructures = &as->m_accelStruct;
            write.pNext = &asInfo;
            break;
        }
        default:
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: unsupported resource type {}"),
                item.slot,
                static_cast<u32>(item.type)
            );
            return false;
        }
    }

    vkUpdateDescriptorSets(m_context.device, 1, &write, 0, nullptr);
    for(BindingSetItem& writtenItem : table->m_writtenItems){
        if(writtenItem.slot == item.slot && writtenItem.arrayElement == item.arrayElement){
            writtenItem = item;
            return true;
        }
    }
    table->m_writtenItems.push_back(item);
    return true;
}


BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* layoutResource){
    auto* layout = checked_cast<BindingLayout*>(layoutResource);
    if(!layout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create binding set: binding layout is invalid"));
        return nullptr;
    }

    auto* bindingSet = NewArenaObject<BindingSet>(m_context.objectArena, m_context);
    bindingSet->m_desc = desc;

    DescriptorTableHandle tableHandle = createDescriptorTable(layoutResource);
    if(!tableHandle){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table for binding set"));
        DestroyArenaObject(m_context.objectArena, bindingSet);
        return nullptr;
    }

    bindingSet->m_descriptorTable = checked_cast<DescriptorTable*>(tableHandle.get());
    if(!bindingSet->m_descriptorTable){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create binding set: descriptor table type is invalid"));
        DestroyArenaObject(m_context.objectArena, bindingSet);
        return nullptr;
    }

    bindingSet->m_descriptorSets = bindingSet->m_descriptorTable->m_descriptorSets;
    if(bindingSet->m_descriptorSets.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create binding set: descriptor table has no descriptor sets"));
        DestroyArenaObject(m_context.objectArena, bindingSet);
        return nullptr;
    }
    bindingSet->m_layout = layout;

    if(layout->m_descriptorHeapCompatible && m_context.descriptorHeapManager){
        bindingSet->m_descriptorHeapPushIndices.resize(layout->m_descriptorHeapBindings.size(), 0u);
        bindingSet->m_descriptorHeapAllocations.resize(layout->m_descriptorHeapBindings.size());

        for(usize i = 0; i < layout->m_descriptorHeapBindings.size(); ++i){
            const DescriptorHeapBindingMeta& meta = layout->m_descriptorHeapBindings[i];
            if(meta.arraySize > UINT32_MAX / meta.descriptorStride){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor heap storage for binding slot {}: descriptor array size overflows"), meta.slot);
                DestroyArenaObject(m_context.objectArena, bindingSet);
                return nullptr;
            }
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

    for(const auto& item : desc.bindings){
        if(!item.resourceHandle)
            continue;

        const BindingLayoutItem* layoutBinding = VulkanDetail::FindLayoutBinding(layout->m_desc, item.slot, item.type);
        if(!layoutBinding){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: layout does not contain resource type {} at that slot"),
                item.slot,
                static_cast<u32>(item.type)
            );
            continue;
        }
        if(item.arrayElement >= layoutBinding->getArraySize()){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {} with out-of-range array element {}"), item.slot, item.arrayElement);
            continue;
        }

        VkWriteDescriptorSet write = VulkanDetail::MakeVkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.dstSet = bindingSet->m_descriptorSets[0];
        write.dstBinding = item.slot;
        write.dstArrayElement = item.arrayElement;
        write.descriptorCount = 1;
        write.descriptorType = VulkanDetail::ConvertDescriptorType(item.type);

        if(VulkanDetail::UsesDescriptorBufferInfo(item.type)){
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            if(!buffer){
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: buffer resource is invalid"), item.slot);
                continue;
            }
            BufferRange range;
            if(!VulkanDetail::ResolveDescriptorBufferRange(item, *buffer, range)){
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: buffer range is empty or outside the buffer"), item.slot);
                continue;
            }
            VkDescriptorBufferInfo bufInfo = {};
            bufInfo.buffer = buffer->m_buffer;
            bufInfo.offset = range.byteOffset;
            bufInfo.range = range.byteSize;
            bufferInfos.push_back(bufInfo);
            write.pBufferInfo = &bufferInfos.back();
            writes.push_back(write);
        }
        else{
            switch(item.type){
            case ResourceType::Texture_SRV:
            case ResourceType::Texture_UAV:{
                auto* texture = checked_cast<Texture*>(item.resourceHandle);
                if(!texture){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: texture resource is invalid"), item.slot);
                    continue;
                }
                VkDescriptorImageInfo imgInfo = {};
                imgInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
                if(imgInfo.imageView == VK_NULL_HANDLE){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: texture image view is null"), item.slot);
                    continue;
                }
                imgInfo.imageLayout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos.push_back(imgInfo);
                write.pImageInfo = &imageInfos.back();
                writes.push_back(write);
                break;
            }
            case ResourceType::Sampler:{
                auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
                if(!sampler){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: sampler resource is invalid"), item.slot);
                    continue;
                }
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
                if(!buffer){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: typed buffer resource is invalid"), item.slot);
                    continue;
                }
                const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : buffer->m_desc.format;
                VkBufferView view = buffer->getView(viewFormat, item.range.byteOffset, item.range.byteSize);
                if(view == VK_NULL_HANDLE){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: typed buffer view is null"), item.slot);
                    continue;
                }
                texelBufferViews.push_back(view);
                write.pTexelBufferView = &texelBufferViews.back();
                writes.push_back(write);
                break;
            }
            case ResourceType::RayTracingAccelStruct:{
                auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
                if(!as){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: acceleration structure resource is invalid"), item.slot);
                    continue;
                }
                VkWriteDescriptorSetAccelerationStructureKHR asWrite = VulkanDetail::MakeVkStruct<VkWriteDescriptorSetAccelerationStructureKHR>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
                asWrite.accelerationStructureCount = 1;
                asWrite.pAccelerationStructures = &as->m_accelStruct;
                asInfos.push_back(asWrite);
                write.pNext = &asInfos.back();
                writes.push_back(write);
                break;
            }
            default:
                NWB_LOGGER_WARNING(
                    NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: unsupported resource type {}"),
                    item.slot,
                    static_cast<u32>(item.type)
                );
                break;
            }
        }

        if(layout->m_descriptorHeapCompatible && m_context.descriptorHeapManager){
            const auto metaIt = layout->m_descriptorHeapBindingLookup.find(item.slot);
            if(metaIt != layout->m_descriptorHeapBindingLookup.end()){
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

void CommandList::bindPipelineBindingSets(
    const VkPipelineBindPoint bindPoint,
    const VkPipelineLayout pipelineLayout,
    const bool usesDescriptorHeap,
    const FixedVector<DescriptorHeapPushRange, s_MaxBindingLayouts>& pushRanges,
    const u32 pushDataSize,
    const BindingSetVector& bindings)
{
    retainBindingSets(bindings);

    if(usesDescriptorHeap){
        bindDescriptorHeapState(true, pushRanges, pushDataSize, bindings);
        return;
    }

    if(bindings.empty() || pipelineLayout == VK_NULL_HANDLE)
        return;

    for(usize i = 0; i < bindings.size(); ++i){
        if(!bindings[i])
            continue;

        auto* bindingSet = checked_cast<BindingSet*>(bindings[i]);
        if(bindingSet->m_descriptorSets.empty())
            continue;

        vkCmdBindDescriptorSets(
            m_currentCmdBuf->m_cmdBuf,
            bindPoint,
            pipelineLayout,
            static_cast<u32>(i),
            static_cast<u32>(bindingSet->m_descriptorSets.size()),
            bindingSet->m_descriptorSets.data(),
            0,
            nullptr
        );
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

