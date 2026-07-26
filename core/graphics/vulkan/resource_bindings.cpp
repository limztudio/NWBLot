// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


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
        flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;

    if(flags == 0)
        flags = VK_SHADER_STAGE_ALL;

    return flags;
}

// Backend C: descriptorBufferOffsetAlignment clamped to a 32-bit value (1 when zero/oversized) for byte-offset math.
u32 GetDescriptorBufferOffsetAlignmentBytes(const VulkanContext& context){
    const VkDeviceSize alignment = context.descriptorBufferProperties.descriptorBufferOffsetAlignment;
    return (alignment == 0 || alignment > UINT32_MAX) ? 1u : static_cast<u32>(alignment);
}

bool ConfigurePipelineMultisampleState(
    const u32 sampleCount,
    const bool alphaToCoverageEnable,
    VkPipelineMultisampleStateCreateInfo& outState,
    const tchar* operationName
){
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
    PipelineStencilFaceMode::Enum stencilFaceMode,
    VkPipelineDepthStencilStateCreateInfo& outState
){
    outState = MakeVkStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    outState.depthTestEnable = state.depthTestEnable ? VK_TRUE : VK_FALSE;
    outState.depthWriteEnable = state.depthWriteEnable ? VK_TRUE : VK_FALSE;
    outState.depthCompareOp = ConvertCompareOp(state.depthFunc);
    outState.depthBoundsTestEnable = VK_FALSE;
    outState.stencilTestEnable = state.stencilEnable ? VK_TRUE : VK_FALSE;
    if(stencilFaceMode == PipelineStencilFaceMode::IncludeStencilFaces){
        outState.front = ConvertStencilOpState(state, state.frontFaceStencil);
        outState.back = ConvertStencilOpState(state, state.backFaceStencil);
    }
}

bool BuildGraphicsPipelineFixedState(
    const FramebufferInfo& fbinfo,
    const RenderState& renderState,
    const PipelineStencilFaceMode::Enum stencilFaceMode,
    const VkDynamicState* dynamicStates,
    const u32 dynamicStateCount,
    const tchar* operationName,
    GraphicsPipelineFixedState& outState
){
    outState.viewportState = MakeVkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    outState.viewportState.viewportCount = 1;
    outState.viewportState.scissorCount = 1;

    if(!ConfigurePipelineMultisampleState(
        fbinfo.sampleCount,
        renderState.blendState.alphaToCoverageEnable,
        outState.multisampling,
        operationName
    ))
        return false;

    ConfigurePipelineDepthStencilState(renderState.depthStencilState, stencilFaceMode, outState.depthStencil);

    outState.colorBlending = BuildPipelineColorBlendState(fbinfo, renderState.blendState, outState.blendAttachments);

    outState.dynamicState = MakeVkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    outState.dynamicState.dynamicStateCount = dynamicStateCount;
    outState.dynamicState.pDynamicStates = dynamicStates;

    return BuildPipelineRenderingInfo(fbinfo, operationName, outState.renderingInfo, outState.colorFormats);
}

bool BuildPipelineRenderingInfo(
    const FramebufferInfo& fbinfo,
    const tchar* operationName,
    VkPipelineRenderingCreateInfo& outRenderingInfo,
    PipelineRenderingFormatVector& outColorFormats
){
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
    bool& ownsPipelineLayout
){
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

// The descriptor-buffer segment a binding's descriptors live in. Samplers go in the dedicated sampler
// segment (RADV requires a separate buffer binding for sampler descriptors); every other descriptor-buffer type
// lives in the resource segment.
constexpr DescriptorBufferSegmentKind::Enum GetDescriptorBufferSegmentKind(ResourceType::Enum type){
    return type == ResourceType::Sampler ? DescriptorBufferSegmentKind::Sampler : DescriptorBufferSegmentKind::Resource;
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

// An opted-in descriptor-buffer layout must be decided before vkCreateDescriptorSetLayout: a layout created with
// VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT cannot later fall back to descriptor-set allocation.
// Keep the decision separate from the driver queries below so a mixed sampler/resource shape stays fully classic.
bool IsDescriptorBufferBackendReady(const VulkanContext& context){
    return context.extensions.EXT_descriptor_buffer
        && context.descriptorBufferManager
        && context.descriptorBufferManager->isEnabled()
    ;
}

bool TryResolveDescriptorBufferLayout(
    const BindingLayoutDesc& desc,
    DescriptorBufferSegmentKind::Enum& outSegmentKind,
    bool& outHasDescriptors
){
    outSegmentKind = DescriptorBufferSegmentKind::None;
    outHasDescriptors = false;

    for(const auto& item : desc.bindings){
        if(item.type == ResourceType::PushConstants || item.type == ResourceType::None)
            continue;
        // vkGetDescriptorEXT supports every binding type accepted by this backend, including acceleration structures.
        if(!IsSupportedDescriptorBindingType(item.type))
            return false;

        const DescriptorBufferSegmentKind::Enum itemSegmentKind = GetDescriptorBufferSegmentKind(item.type);
        if(!outHasDescriptors){
            outSegmentKind = itemSegmentKind;
            outHasDescriptors = true;
        }
        else if(outSegmentKind != itemSegmentKind)
            return false;
    }

    return true;
}

bool TryResolveBindlessDescriptorBufferLayout(
    const BindlessLayoutDesc& desc,
    DescriptorBufferSegmentKind::Enum& outSegmentKind
){
    outSegmentKind = DescriptorBufferSegmentKind::None;
    bool hasDescriptors = false;

    for(const auto& item : desc.registerSpaces){
        if(!IsSupportedDescriptorBindingType(item.type))
            return false;

        const DescriptorBufferSegmentKind::Enum itemSegmentKind = GetDescriptorBufferSegmentKind(item.type);
        if(!hasDescriptors){
            outSegmentKind = itemSegmentKind;
            hasDescriptors = true;
        }
        else if(outSegmentKind != itemSegmentKind)
            return false;
    }

    return hasDescriptors;
}

bool ValidateDescriptorBufferBindingFootprint(
    DescriptorBufferManager& manager,
    const VkDescriptorType descriptorType,
    const u32 descriptorCount,
    const VkDeviceSize setSizeBytes,
    const VkDeviceSize bindingOffsetBytes,
    const u32 bindingSlot,
    const tchar* operationName
){
    const u32 descriptorSize = manager.getDescriptorSize(descriptorType);
    if(
        descriptorSize == 0u
        || descriptorCount == 0u
        || setSizeBytes == 0u
        || setSizeBytes > UINT32_MAX
        || bindingOffsetBytes > UINT32_MAX
        || bindingOffsetBytes >= setSizeBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer binding {} has an invalid footprint."), operationName, bindingSlot);
        return false;
    }

    const VkDeviceSize availableBytes = setSizeBytes - bindingOffsetBytes;
    if(static_cast<VkDeviceSize>(descriptorCount) > availableBytes / descriptorSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer binding {} exceeds its {}-byte set block."), operationName, bindingSlot, setSizeBytes);
        return false;
    }

    return true;
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

// Supported register-space resource type for a bindless (descriptor-indexing) table. Acceleration structures
// are excluded: the descriptor-indexing bindless heap cannot serve them, so a bindless layout must not declare one.
constexpr bool IsBindlessRegisterSpaceType(ResourceType::Enum type){
    return type != ResourceType::RayTracingAccelStruct && IsSupportedDescriptorBindingType(type);
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

template<typename PoolSizeVector>
bool AddEmptyLayoutDescriptorPoolSize(PoolSizeVector& poolSizes, const u32 descriptorCount){
    if(!poolSizes.empty())
        return true;

    return AddDescriptorPoolSize(poolSizes, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorCount);
}

template<typename PoolSizeVector>
bool AddBindingLayoutDescriptorPoolSizes(PoolSizeVector& poolSizes, const BindingLayoutDesc& desc, const u32 descriptorSetCount){
    if(descriptorSetCount == 0)
        return true;

    for(const auto& item : desc.bindings){
        if(item.type == ResourceType::PushConstants || item.type == ResourceType::None)
            continue;

        const u32 arraySize = item.getArraySize() > 0 ? item.getArraySize() : 1u;
        if(arraySize > UINT32_MAX / descriptorSetCount)
            return false;

        const VkDescriptorType type = ConvertDescriptorType(item.type);
        if(!AddDescriptorPoolSize(poolSizes, type, arraySize * descriptorSetCount))
            return false;
    }

    return AddEmptyLayoutDescriptorPoolSize(poolSizes, descriptorSetCount);
}

template<typename PoolSizeVector>
bool AddBindlessLayoutDescriptorPoolSizes(PoolSizeVector& poolSizes, const BindlessLayoutDesc& desc, const u32 descriptorCount){
    for(const auto& item : desc.registerSpaces){
        const VkDescriptorType type = ConvertDescriptorType(item.type);
        if(!AddDescriptorPoolSize(poolSizes, type, descriptorCount))
            return false;
    }

    return AddEmptyLayoutDescriptorPoolSize(poolSizes, descriptorCount);
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
    if((byteSize & s_BufferAlignmentMask) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: push constant size is not 4-byte aligned"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed push constant operation: size is not 4-byte aligned"));
        return false;
    }
    if(byteSize > context.physicalDeviceProperties.limits.maxPushConstantsSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: push constant size {} exceeds device limit {}")
            , operationName
            , byteSize
            , context.physicalDeviceProperties.limits.maxPushConstantsSize
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
    const tchar* operationName
){
    VkResult res = VK_SUCCESS;

    outLayout = VK_NULL_HANDLE;

    VkPushConstantRange pushConstantRange = {};
    if(pushConstantByteSize > 0){
        if(!ValidatePushConstantByteSize(context, pushConstantByteSize, operationName))
            return false;

        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;
    }

    auto layoutInfo = MakeVkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.setLayoutCount = setLayoutCount;
    layoutInfo.pSetLayouts = setLayoutCount > 0 ? setLayouts : nullptr;
    layoutInfo.pushConstantRangeCount = pushConstantByteSize > 0 ? 1u : 0u;
    layoutInfo.pPushConstantRanges = pushConstantByteSize > 0 ? &pushConstantRange : nullptr;

    res = vkCreatePipelineLayout(context.device, &layoutInfo, context.allocationCallbacks, &outLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for {}: {}"), operationName, ResultToString(res));
        outLayout = VK_NULL_HANDLE;
        return false;
    }

    return true;
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
    const TextureDesc& textureDesc = texture.m_desc;
    TextureDimension::Enum dimension = item.dimension != TextureDimension::Unknown ? item.dimension : textureDesc.dimension;
    Format::Enum format = item.format != Format::UNKNOWN ? item.format : textureDesc.format;
    TextureSubresourceSet subresources = item.subresources.resolve(textureDesc, TextureSubresourceMipResolve::Range);
    if(subresources.numMipLevels == 0 || subresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor image view: subresource range is invalid"));
        return false;
    }

    return BuildTextureImageViewCreateInfo(
        texture,
        subresources,
        dimension,
        format,
        NWB_TEXT("descriptor image view"),
        false,
        outViewInfo
    );
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
    Alloc::ScratchArena& scratchArena
)const{
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
        auto* layout = bindingLayouts[0].get();
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: binding layout is invalid"), operationName);
            return false;
        }

        outPipelineLayout = layout->m_pipelineLayout;
        outPushConstantByteSize = layout->m_pushConstantByteSize;
        return true;
    }

    Vector<VkDescriptorSetLayout, Alloc::ScratchArena> descriptorSetLayouts{scratchArena};
    u32 pushConstantByteSize = 0;
    usize descriptorSetLayoutCount = 0;

    for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
        auto* layout = bindingLayouts[i].get();
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: binding layout {} is invalid"), operationName, i);
            return false;
        }

        pushConstantByteSize = Max<u32>(
            pushConstantByteSize,
            VulkanDetail::GetPushConstantByteSize(layout->getBindingLayoutDesc())
        );
        if(layout->m_descriptorSetLayouts.size() > static_cast<usize>(Limit<u32>::s_Max) - descriptorSetLayoutCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set layout count exceeds u32 limits")
                , operationName
            );
            return false;
        }
        descriptorSetLayoutCount += layout->m_descriptorSetLayouts.size();
    }

    // Determine each layout's descriptor-set placement. A layout may pin itself to an explicit set index - bindless via
    // BindlessLayoutDesc::descriptorSetIndexIsExplicit, classic via BindingLayoutDesc::registerSpaceIsDescriptorSet -
    // otherwise it uses positional assignment. The global bindless heap occupies reserved high sets (8/9).
    const auto layoutSetIndex = [](const BindingLayout& layout, const u32 positional, bool& outExplicit) -> u32{
        if(const BindlessLayoutDesc* bindlessDesc = layout.getBindlessDesc()){
            outExplicit = bindlessDesc->descriptorSetIndexIsExplicit;
            return outExplicit ? bindlessDesc->descriptorSetIndex : positional;
        }
        const BindingLayoutDesc& classicDesc = layout.getBindingLayoutDesc();
        outExplicit = classicDesc.registerSpaceIsDescriptorSet;
        return outExplicit ? classicDesc.registerSpace : positional;
    };

    bool anyExplicitSet = false;
    for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()) && !anyExplicitSet; ++i){
        bool isExplicit = false;
        [[maybe_unused]] const u32 setIndex = layoutSetIndex(*bindingLayouts[i].get(), i, isExplicit);
        anyExplicitSet = isExplicit;
    }

    // A descriptor-buffer set layout cannot be consumed by the classic descriptor-set bind path.  Decide this before
    // either positional or explicit placement so a mixed collection never produces a pipeline layout whose bind mode
    // is ambiguous (or whose descriptor-buffer layout is later handed to vkCmdBindDescriptorSets).
    bool anyDescriptorBufferCompatible = false;
    bool allDescriptorBufferCompatible = true;
    for(const auto& bindingLayout : bindingLayouts){
        const auto* layout = bindingLayout.get();
        if(layout && layout->isDescriptorBufferCompatible())
            anyDescriptorBufferCompatible = true;
        else
            allDescriptorBufferCompatible = false;
    }
    if(anyDescriptorBufferCompatible && !allDescriptorBufferCompatible){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer and classic descriptor-set layouts cannot be mixed in one pipeline layout."), operationName);
        return false;
    }

    if(!anyExplicitSet){
        // Positional path: concatenate every layout's sets in list order.
        descriptorSetLayouts.reserve(descriptorSetLayoutCount);
        for(const auto& bindingLayout : bindingLayouts){
            auto* layout = bindingLayout.get();
            NWB_ASSERT(layout != nullptr);
            for(const auto& descriptorSetLayout : layout->m_descriptorSetLayouts)
                descriptorSetLayouts.push_back(descriptorSetLayout);
        }
        NWB_ASSERT(descriptorSetLayouts.size() == descriptorSetLayoutCount);
    }
    else{
        // Explicit-index path: place each layout's set(s) at its target index and fill every unused set in between with
        // the cached empty (zero-binding) set layout so the pipeline layout is dense from set 0 (Vulkan requires no
        // holes). The empty gap-set layout is mandatory here.
        if(m_context.emptyDescriptorSetLayout == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: explicit descriptor-set placement needs the empty gap-set layout, which is unavailable"), operationName);
            return false;
        }

        // Backend C: when every embedded layout is descriptor-buffer-compatible (the wholesale-conversion gate that
        // configurePipelineBindings sets m_usesDescriptorBuffer on), the pipeline layout must not mix descriptor-buffer
        // set layouts with classic ones -- so gap-fill with the descriptor-buffer-flagged empty layout instead of the
        // classic one. This is the path the heap-coupled tail pipelines hit: their leaf set (0) + heap sets (8/9) are
        // all descriptor-buffer-compatible, and sets 1-7 must be the descriptor-buffer empty sibling.
        const VkDescriptorSetLayout gapSetLayout = (allDescriptorBufferCompatible && m_context.extensions.EXT_descriptor_buffer)
            ? getOrCreateEmptyDescriptorBufferSetLayout()
            : VK_NULL_HANDLE;
        if(anyDescriptorBufferCompatible && gapSetLayout == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer explicit-set placement needs the empty descriptor-buffer gap-set layout, which is unavailable"), operationName);
            return false;
        }
        const VkDescriptorSetLayout fillSetLayout = (gapSetLayout != VK_NULL_HANDLE) ? gapSetLayout : m_context.emptyDescriptorSetLayout;

        u32 maxSetIndex = 0;
        for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
            const BindingLayout& layout = *bindingLayouts[i].get();
            const usize setCount = layout.m_descriptorSetLayouts.size();
            if(setCount == 0)
                continue;
            bool isExplicit = false;
            const u32 base = layoutSetIndex(layout, i, isExplicit);
            if(base > Limit<u32>::s_Max - static_cast<u32>(setCount - 1)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor set index overflow"), operationName);
                return false;
            }
            maxSetIndex = Max<u32>(maxSetIndex, base + static_cast<u32>(setCount) - 1u);
        }

        const u32 totalSets = maxSetIndex + 1u;
        descriptorSetLayouts.reserve(totalSets);
        for(u32 s = 0; s < totalSets; ++s)
            descriptorSetLayouts.push_back(fillSetLayout);

        for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()); ++i){
            const BindingLayout& layout = *bindingLayouts[i].get();
            bool isExplicit = false;
            const u32 base = layoutSetIndex(layout, i, isExplicit);
            for(usize s = 0; s < layout.m_descriptorSetLayouts.size(); ++s){
                const u32 slot = base + static_cast<u32>(s);
                if(descriptorSetLayouts[slot] != fillSetLayout){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: two binding layouts map to descriptor set {}"), operationName, slot);
                    return false;
                }
                descriptorSetLayouts[slot] = layout.m_descriptorSetLayouts[s];
            }
        }
    }

    if(!VulkanDetail::CreatePipelineLayout(
        m_context,
        descriptorSetLayouts.data(),
        static_cast<u32>(descriptorSetLayouts.size()),
        pushConstantByteSize,
        outPipelineLayout,
        operationName
    ))
        return false;

    outPushConstantByteSize = pushConstantByteSize;
    outOwnsPipelineLayout = true;
    return true;
}

bool Device::configurePipelineBindings(
    const BindingLayoutVector& bindingLayouts,
    const tchar* operationName,
    PipelineBindingState& outBindings,
    Alloc::ScratchArena& scratchArena
)const{
    outBindings.m_pipelineLayout = VK_NULL_HANDLE;
    outBindings.m_ownsPipelineLayout = false;
    outBindings.m_usesDescriptorBuffer = false;
    outBindings.m_pushConstantByteSize = 0;

    // Backend C: a pipeline uses the descriptor-buffer path only when EVERY layout opts in and is buffer-compatible.
    // Descriptor-buffer and classic layouts cannot be mixed in one pipeline layout, so the layout assembler rejects a
    // mixed collection rather than selecting a classic bind command for descriptor-buffer-flagged set layouts.
    if(m_context.extensions.EXT_descriptor_buffer && m_context.descriptorBufferManager && m_context.descriptorBufferManager->isEnabled()){
        bool allBufferCompatible = !bindingLayouts.empty();
        for(const auto& layoutResource : bindingLayouts){
            const auto* layout = layoutResource.get();
            if(!layout || !layout->isDescriptorBufferCompatible()){
                allBufferCompatible = false;
                break;
            }
        }
        outBindings.m_usesDescriptorBuffer = allBufferCompatible;
    }

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
    Shader* shader,
    const VkShaderStageFlagBits stage,
    PipelineSpecializationInfoVector& specializationInfos,
    PipelineShaderStageVector& shaderStages
)const{
    auto* s = shader;
    auto stageInfo = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stageInfo.stage = stage;
    stageInfo.module = s->m_shaderModule;
    stageInfo.pName = s->m_entryPointName.c_str();

    if(!s->m_specializationEntries.empty()){
        specializationInfos.push_back(s->makeSpecializationInfo());
        stageInfo.pSpecializationInfo = &specializationInfos.back();
    }

    shaderStages.push_back(stageInfo);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend C - VK_EXT_descriptor_buffer manager
//
// Two HOST-mapped VkBuffers sub-allocated by byte offset through a shared free-range list + bump pointer. Descriptor
// buffers use their own usage bits, their own offset alignment (descriptorBufferOffsetAlignment), and write
// descriptors through vkGetDescriptorEXT (VkDescriptorGetInfoEXT + VkDescriptorDataEXT). vkGetDescriptorEXT is also
// the only path that encodes an acceleration-structure handle, which is why the TLAS migration routes through this
// manager. Descriptor-buffer-compatible pipelines consume these segments through CommandList's Backend-C binding
// path; Backend A remains the portability fallback.


DescriptorBufferManager::DescriptorBufferManager(const VulkanContext& context, VulkanAllocator& allocator)
    : m_context(context)
    , m_allocator(allocator)
    , m_resourceSegment(context.objectArena)
    , m_samplerSegment(context.objectArena)
{}
DescriptorBufferManager::~DescriptorBufferManager(){
    shutdown();
}

bool DescriptorBufferManager::initialize(){
    shutdown();

    if(!m_context.extensions.EXT_descriptor_buffer)
        return false;

    const auto& props = m_context.descriptorBufferProperties;

    // One global segment per type. RADV advertises multi-GB address spaces for both; we cap at modest working sizes
    // since the segments are HOST-mapped and persist for device life. The suballocator only ever hands out what gets
    // written, so the reservation is virtual from a memory-pressure standpoint only.
    constexpr u32 s_TargetResourceSegmentBytes = 32u * 1024u * 1024u;
    constexpr u32 s_TargetSamplerSegmentBytes = 2u * 1024u * 1024u;

    // Segment offsets are intentionally 32-bit, but a driver may advertise a larger address space than this manager
    // chooses to reserve. Only the alignment participates in our offset arithmetic; capacities are clamped to the
    // modest targets below before conversion to u32.
    if(props.descriptorBufferOffsetAlignment == 0 || props.descriptorBufferOffsetAlignment > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer offset alignment is outside the supported 32-bit range."));
        return false;
    }

    const u32 offsetAlignment = static_cast<u32>(props.descriptorBufferOffsetAlignment);
    if(
        props.maxDescriptorBufferBindings < 2u
        || props.maxResourceDescriptorBufferBindings == 0u
        || props.maxSamplerDescriptorBufferBindings == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer limits cannot bind the required resource and sampler segments."));
        return false;
    }

    const VkDeviceSize resourceMaxBytes = Min<VkDeviceSize>(props.resourceDescriptorBufferAddressSpaceSize, props.maxResourceDescriptorBufferRange);
    const VkDeviceSize samplerMaxBytes = Min<VkDeviceSize>(props.samplerDescriptorBufferAddressSpaceSize, props.maxSamplerDescriptorBufferRange);

    const auto makeCapacity = [&](const VkDeviceSize maximumBytes, const u32 targetBytes, u32& outCapacityBytes) -> bool{
        const VkDeviceSize cappedBytes = Min<VkDeviceSize>(maximumBytes, targetBytes);
        const VkDeviceSize alignedBytes = cappedBytes - (cappedBytes % offsetAlignment);
        if(alignedBytes == 0u)
            return false;
        outCapacityBytes = static_cast<u32>(alignedBytes);
        return true;
    };

    u32 resourceCapacityBytes = 0;
    u32 samplerCapacityBytes = 0;
    if(
        !makeCapacity(resourceMaxBytes, s_TargetResourceSegmentBytes, resourceCapacityBytes)
        || !makeCapacity(samplerMaxBytes, s_TargetSamplerSegmentBytes, samplerCapacityBytes)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer properties do not allow aligned global segments."));
        return false;
    }

    const VkDeviceSize totalCapacityBytes = static_cast<VkDeviceSize>(resourceCapacityBytes) + samplerCapacityBytes;
    if(totalCapacityBytes > props.descriptorBufferAddressSpaceSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer global address space {} cannot hold the requested {} bytes of resource and sampler segments.")
            , props.descriptorBufferAddressSpaceSize
            , totalCapacityBytes
        );
        return false;
    }

    if(!initializeSegment(m_resourceSegment, "vk_resource_descriptor_buffer", resourceCapacityBytes)){
        shutdown();
        return false;
    }

    if(!initializeSegment(m_samplerSegment, "vk_sampler_descriptor_buffer", samplerCapacityBytes)){
        shutdown();
        return false;
    }

    m_enabled = true;
    return true;
}

void DescriptorBufferManager::shutdown(){
    shutdownSegment(m_resourceSegment);
    shutdownSegment(m_samplerSegment);
    m_enabled = false;
}

u32 DescriptorBufferManager::getDescriptorSize(const VkDescriptorType descriptorType)const{
    if(!m_enabled)
        return 0;

    const auto& props = m_context.descriptorBufferProperties;
    VkDeviceSize size = 0;
    switch(descriptorType){
    case VK_DESCRIPTOR_TYPE_SAMPLER:                               size = props.samplerDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:                size = props.combinedImageSamplerDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:                         size = props.sampledImageDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:                         size = props.storageImageDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:                  size = props.uniformTexelBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:                  size = props.storageTexelBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:                        size = props.uniformBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:                        size = props.storageBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:                      size = props.inputAttachmentDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:            size = props.accelerationStructureDescriptorSize; break;
    default:                                                       return 0;
    }

    return size > UINT32_MAX ? 0u : static_cast<u32>(size);
}

u32 DescriptorBufferManager::getOffsetAlignmentBytes()const{
    return VulkanDetail::GetDescriptorBufferOffsetAlignmentBytes(m_context);
}

DescriptorBufferSegment DescriptorBufferManager::allocate(const DescriptorBufferSegmentKind::Enum kind, const u32 sizeBytes, const u32 alignmentBytes){
    DescriptorBufferSegment result{};
    if(!m_enabled || sizeBytes == 0)
        return result;
    if(kind != DescriptorBufferSegmentKind::Resource && kind != DescriptorBufferSegmentKind::Sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: invalid segment kind {}."), static_cast<u32>(kind));
        return result;
    }

    const u32 requiredAlignmentBytes = getOffsetAlignmentBytes();
    if(alignmentBytes == 0u || (alignmentBytes % requiredAlignmentBytes) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: alignment {} is not a non-zero multiple of required alignment {}.")
            , alignmentBytes
            , requiredAlignmentBytes
        );
        return result;
    }

    SegmentStorage& segment = kind == DescriptorBufferSegmentKind::Sampler ? m_samplerSegment : m_resourceSegment;
    auto clearAllocation = [&](const DescriptorBufferSegment& allocation){
        if(allocation.valid() && segment.mappedMemory)
            NWB_MEMSET(static_cast<u8*>(segment.mappedMemory) + allocation.offsetBytes, 0, allocation.sizeBytes);
    };
    auto finishAllocation = [&](const u32 offsetBytes) -> DescriptorBufferSegment{
        result.kind = kind;
        result.offsetBytes = offsetBytes;
        result.sizeBytes = sizeBytes;
        result.allocationSerial = segment.nextAllocationSerial++;
        clearAllocation(result);
        // Keep live ranges ordered by byte offset. writeDescriptor() can then locate the only possible owner with a
        // binary search instead of walking every persistent binding-set block on every descriptor update.
        usize insertIndex = 0u;
        while(
            insertIndex < segment.liveAllocations.size()
            && segment.liveAllocations[insertIndex].offsetBytes < result.offsetBytes
        )
            ++insertIndex;
        segment.liveAllocations.insert(segment.liveAllocations.begin() + insertIndex, result);
        return result;
    };

    ScopedLock lock(segment.mutex);

    if(segment.nextAllocationSerial == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: allocation serial space is exhausted."));
        return result;
    }

    for(usize i = 0; i < segment.freeRanges.size(); ++i){
        FreeRange range = segment.freeRanges[i];
        if(range.sizeBytes > UINT32_MAX - range.offsetBytes)
            continue;

        u32 alignedOffset = 0;
        if(!AlignUpU32Checked(range.offsetBytes, alignmentBytes, alignedOffset))
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
        if(consumedPrefix > 0){
            segment.freeRanges[i] = { range.offsetBytes, consumedPrefix };
            if(allocEnd < rangeEnd)
                segment.freeRanges.insert(segment.freeRanges.begin() + i + 1u, { allocEnd, rangeEnd - allocEnd });
        }
        else if(allocEnd < rangeEnd){
            segment.freeRanges[i] = { allocEnd, rangeEnd - allocEnd };
        }
        else{
            segment.freeRanges.erase(segment.freeRanges.begin() + i);
        }

        return finishAllocation(alignedOffset);
    }

    u32 alignedOffset = 0;
    if(!AlignUpU32Checked(segment.writableOffsetBytes, alignmentBytes, alignedOffset)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer alignment overflows 32-bit offsets."));
        return result;
    }
    if(alignedOffset > segment.capacityBytes || sizeBytes > segment.capacityBytes - alignedOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer is out of space (kind={}, requested={} bytes).")
            , kind == DescriptorBufferSegmentKind::Sampler ? NWB_TEXT("sampler") : NWB_TEXT("resource")
            , sizeBytes
        );
        return result;
    }

    segment.writableOffsetBytes = alignedOffset + sizeBytes;
    return finishAllocation(alignedOffset);
}

void DescriptorBufferManager::free(const DescriptorBufferSegment& segment){
    if(!m_enabled || segment.sizeBytes == 0u)
        return;
    if(segment.kind != DescriptorBufferSegmentKind::Resource && segment.kind != DescriptorBufferSegmentKind::Sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: invalid segment kind {}."), static_cast<u32>(segment.kind));
        return;
    }
    if(segment.allocationSerial == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: allocation serial is invalid."));
        return;
    }

    SegmentStorage& storage = segment.kind == DescriptorBufferSegmentKind::Sampler ? m_samplerSegment : m_resourceSegment;

    ScopedLock lock(storage.mutex);

    if(
        segment.offsetBytes > storage.capacityBytes
        || segment.sizeBytes > storage.capacityBytes - segment.offsetBytes
        || segment.offsetBytes > storage.writableOffsetBytes
        || segment.sizeBytes > storage.writableOffsetBytes - segment.offsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} is outside the live segment."), segment.offsetBytes, segment.sizeBytes);
        return;
    }

    usize allocationIndex = 0u;
    while(
        allocationIndex < storage.liveAllocations.size()
        && storage.liveAllocations[allocationIndex].offsetBytes < segment.offsetBytes
    )
        ++allocationIndex;
    if(
        allocationIndex == storage.liveAllocations.size()
        || storage.liveAllocations[allocationIndex].kind != segment.kind
        || storage.liveAllocations[allocationIndex].offsetBytes != segment.offsetBytes
        || storage.liveAllocations[allocationIndex].sizeBytes != segment.sizeBytes
        || storage.liveAllocations[allocationIndex].allocationSerial != segment.allocationSerial
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} is not a live allocation."), segment.offsetBytes, segment.sizeBytes);
        return;
    }

    const auto rangeEnd = [](const FreeRange& range, u32& outEnd) -> bool{
        if(range.sizeBytes > UINT32_MAX - range.offsetBytes)
            return false;
        outEnd = range.offsetBytes + range.sizeBytes;
        return true;
    };

    FreeRange freedRange{ segment.offsetBytes, segment.sizeBytes };
    for(const FreeRange& range : storage.freeRanges){
        u32 rangeEndBytes = 0u;
        if(
            !rangeEnd(range, rangeEndBytes)
            || (
                freedRange.offsetBytes < rangeEndBytes
                && range.offsetBytes < freedRange.offsetBytes + freedRange.sizeBytes
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} overlaps a free range."), segment.offsetBytes, segment.sizeBytes);
            return;
        }
    }

    storage.liveAllocations.erase(storage.liveAllocations.begin() + allocationIndex);
    usize insertIndex = 0u;
    while(insertIndex < storage.freeRanges.size() && storage.freeRanges[insertIndex].offsetBytes < freedRange.offsetBytes)
        ++insertIndex;

    storage.freeRanges.insert(storage.freeRanges.begin() + insertIndex, freedRange);

    const auto mergeAdjacentAt = [&](const usize leftIndex) -> bool{
        if(leftIndex + 1u >= storage.freeRanges.size())
            return false;

        FreeRange& left = storage.freeRanges[leftIndex];
        const FreeRange right = storage.freeRanges[leftIndex + 1u];

        u32 leftEnd = 0;
        if(!rangeEnd(left, leftEnd) || leftEnd != right.offsetBytes || right.sizeBytes > UINT32_MAX - left.sizeBytes)
            return false;

        left.sizeBytes += right.sizeBytes;
        storage.freeRanges.erase(storage.freeRanges.begin() + leftIndex + 1u);
        return true;
    };

    // Merge newly inserted range with its right neighbor, then re-merge leftward to coalesce a fully surrounded hole.
    if(mergeAdjacentAt(insertIndex))
        mergeAdjacentAt(insertIndex);
    if(insertIndex > 0)
        mergeAdjacentAt(insertIndex - 1u);
}

bool DescriptorBufferManager::writeDescriptor(
    const BindingSetItem& item,
    const DescriptorBufferSegment& allocation,
    const u32 dstOffsetBytes,
    const VkDescriptorType descriptorType
){
    if(!m_enabled)
        return false;
    if(!allocation.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation is invalid."));
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

    // Sampler is the one type that lives in the separate sampler segment (RADV requires samplers in their own
    // descriptor buffer binding); every other type writes into the resource segment.
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

    const u32 descriptorSize = getDescriptorSize(descriptorType);
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
    // liveAllocations stays ordered by offset, so only the range immediately preceding dstOffsetBytes can own the
    // requested descriptor bytes. Match its full allocation identity, not just its byte range, so a stale segment
    // cannot target a newly recycled allocation at the same offset.
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
            && liveAllocation.allocationSerial == allocation.allocationSerial
        ;
    }
    if(!ownsLiveAllocation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation is not live for offset {}."), dstOffsetBytes);
        return false;
    }

    auto getInfo = VulkanDetail::MakeVkStruct<VkDescriptorGetInfoEXT>(VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT);
    getInfo.type = descriptorType;

    // vkGetDescriptorEXT writes through these stack locals; their lifetimes must cover the call.
    VkDescriptorAddressInfoEXT addressInfo{};
    VkDescriptorImageInfo imageInfo{};
    VkSampler samplerHandle = VK_NULL_HANDLE;
    VkDeviceAddress accelStructAddress = 0;

    if(VulkanDetail::UsesDescriptorBufferInfo(item.type)){
        // Uniform/storage buffer (non-texel): device-address range.
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!buffer)
            return false;
        BufferRange range;
        if(!VulkanDetail::ResolveDescriptorBufferRange(item, *buffer, range))
            return false;
        const VkDeviceAddress bufferAddress = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress());
        if(bufferAddress == 0u || range.byteOffset > UINT64_MAX - bufferAddress){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: buffer has no valid device address."));
            return false;
        }
        addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
        addressInfo.address = bufferAddress + range.byteOffset;
        addressInfo.range = range.byteSize;
        getInfo.data.pStorageBuffer = &addressInfo;
        getInfo.data.pUniformBuffer = &addressInfo;
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
            const VkDeviceAddress bufferAddress = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress());
            if(bufferAddress == 0u || range.byteOffset > UINT64_MAX - bufferAddress){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed buffer has no valid device address."));
                return false;
            }
            const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : bufferDesc.format;
            const VkFormat vkFormat = ConvertFormat(viewFormat);
            if(vkFormat == VK_FORMAT_UNDEFINED)
                return false;
            addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addressInfo.address = bufferAddress + range.byteOffset;
            addressInfo.range = range.byteSize;
            addressInfo.format = vkFormat;
            getInfo.data.pUniformTexelBuffer = &addressInfo;
            getInfo.data.pStorageTexelBuffer = &addressInfo;
            break;
        }
        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:{
            auto* texture = checked_cast<Texture*>(item.resourceHandle);
            if(!texture)
                return false;
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
            if(!sampler)
                return false;
            samplerHandle = sampler->m_sampler;
            if(samplerHandle == VK_NULL_HANDLE)
                return false;
            getInfo.data.pSampler = &samplerHandle;
            break;
        }
        case ResourceType::RayTracingAccelStruct:{
            // The TLAS path. vkGetDescriptorEXT is the only Vulkan write that accepts a bare AS device address; this
            // is why the TLAS migration is gated on Backend C (no classic-descriptor-set equivalent).
            auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
            if(!as)
                return false;
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

    vkGetDescriptorEXT(m_context.device, &getInfo, descriptorSize, dstBytes + dstOffsetBytes);
    return true;
}

bool DescriptorBufferManager::initializeSegment(SegmentStorage& segment, const ACompactString& debugName, const u32 capacityBytes){
    VkResult res = VK_SUCCESS;

    shutdownSegment(segment);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = capacityBytes;
    bufferInfo.usage =
        (&segment == &m_resourceSegment ? VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT : VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT)
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res = m_allocator.createHostMappedBuffer(
        segment.buffer,
        segment.allocation,
        segment.mappedMemory,
        bufferInfo
    );
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor buffer '{}': {}")
            , StringConvert(debugName.view())
            , ResultToString(res)
        );
        return false;
    }
    if(!segment.mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map descriptor buffer memory '{}'"), StringConvert(debugName.view()));
        shutdownSegment(segment);
        return false;
    }

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = segment.buffer;
    segment.deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    if(segment.deviceAddress == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query descriptor buffer device address '{}'.")
            , StringConvert(debugName.view())
        );
        shutdownSegment(segment);
        return false;
    }

    segment.capacityBytes = capacityBytes;
    segment.bindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    segment.bindingInfo.address = segment.deviceAddress;
    segment.bindingInfo.usage =
        (&segment == &m_resourceSegment ? VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT : VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT);
    segment.writableOffsetBytes = 0u;

    NWB_MEMSET(segment.mappedMemory, 0, capacityBytes);
    return true;
}

void DescriptorBufferManager::shutdownSegment(SegmentStorage& segment){
    m_allocator.destroyHostMappedBuffer(segment.buffer, segment.allocation, segment.mappedMemory);
    segment.deviceAddress = 0;
    segment.capacityBytes = 0;
    segment.writableOffsetBytes = 0;
    segment.bindingInfo = {};
    segment.freeRanges.clear();
    segment.liveAllocations.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayout::BindingLayout(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_descriptorSetLayouts(context.objectArena)
    , m_descriptorBufferBindingOffsets(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        context.objectArena
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
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_descriptorSets(context.objectArena)
    , m_writtenItems(context.objectArena)
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
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_descriptorSets(context.objectArena)
    , m_descriptorBufferAllocations(context.objectArena)
    , m_context(context)
{}
BindingSet::~BindingSet(){
    if(m_context.descriptorBufferManager){
        for(const DescriptorBufferSegment& segment : m_descriptorBufferAllocations)
            m_context.descriptorBufferManager->free(segment);
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

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_desc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchArena> bindings{scratchArena};
    bindings.reserve(desc.bindings.size());
    HashSet<u32, Hasher<u32>, EqualTo<u32>, Alloc::ScratchArena> bindingSlots(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        scratchArena
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
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: binding slot {} has unsupported resource type {}")
                , item.slot
                , static_cast<u32>(item.type)
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

    DescriptorBufferSegmentKind::Enum descriptorBufferSegmentKind = DescriptorBufferSegmentKind::None;
    bool descriptorBufferHasDescriptors = false;
    const bool useDescriptorBuffer = desc.useDescriptorBuffer
        && VulkanDetail::IsDescriptorBufferBackendReady(m_context)
        && VulkanDetail::TryResolveDescriptorBufferLayout(
            desc,
            descriptorBufferSegmentKind,
            descriptorBufferHasDescriptors
        )
    ;

    auto layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    // Backend C: descriptor-buffer layouts must set this create flag. Classic (Backend A) layouts leave it clear; the
    // two are mutually exclusive on the descriptor-set-layout object, so a descriptor-buffer layout can only ever be
    // consumed through the descriptor-buffer command bind path.
    if(useDescriptorBuffer){
        layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    layout->m_descriptorSetLayouts.push_back(setLayout);

    if(
        !VulkanDetail::CreatePipelineLayout(
            m_context,
            layout->m_descriptorSetLayouts.data(),
            static_cast<u32>(layout->m_descriptorSetLayouts.size()),
            pushConstantByteSize,
            layout->m_pipelineLayout,
            NWB_TEXT("create binding layout")
        )
    ){
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    // Backend C (VK_EXT_descriptor_buffer): the preflight above selected the descriptor-buffer create flag only for
    // an eligible, segment-coherent layout. Once that flag is set, a failed driver query is fatal for this layout --
    // it cannot be reinterpreted as a classic descriptor-set layout after creation. Push-only layouts are valid
    // descriptor-buffer gap sets and intentionally retain a zero block size / None segment kind.
    if(useDescriptorBuffer){
        layout->m_descriptorBufferCompatible = true;
        if(descriptorBufferHasDescriptors){
            NWB_ASSERT(descriptorBufferSegmentKind != DescriptorBufferSegmentKind::None);

            const VkDescriptorSetLayout descriptorSetLayout = layout->m_descriptorSetLayouts[0];
            VkDeviceSize setSizeBytes = 0;
            vkGetDescriptorSetLayoutSizeEXT(m_context.device, descriptorSetLayout, &setSizeBytes);
            if(setSizeBytes == 0u || setSizeBytes > UINT32_MAX){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create binding layout: descriptor-buffer set size is invalid."));
                DestroyArenaObject(m_context.objectArena, layout);
                return nullptr;
            }

            layout->m_descriptorBufferBindingOffsets.reserve(desc.bindings.size());
            for(const auto& item : desc.bindings){
                if(item.type == ResourceType::PushConstants || item.type == ResourceType::None)
                    continue;

                VkDeviceSize bindingOffsetBytes = 0;
                vkGetDescriptorSetLayoutBindingOffsetEXT(m_context.device, descriptorSetLayout, item.slot, &bindingOffsetBytes);
                if(!VulkanDetail::ValidateDescriptorBufferBindingFootprint(
                    *m_context.descriptorBufferManager,
                    VulkanDetail::ConvertDescriptorType(item.type),
                    item.getArraySize(),
                    setSizeBytes,
                    bindingOffsetBytes,
                    item.slot,
                    NWB_TEXT("binding layout")
                )){
                    DestroyArenaObject(m_context.objectArena, layout);
                    return nullptr;
                }
                layout->m_descriptorBufferBindingOffsets.insert_or_assign(item.slot, static_cast<u32>(bindingOffsetBytes));
            }
            layout->m_descriptorBufferSetSizeBytes = static_cast<u32>(setSizeBytes);
            layout->m_descriptorBufferSegmentKind = descriptorBufferSegmentKind;
        }
    }

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_isBindless = true;
    layout->m_bindlessDesc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchArena> bindings{scratchArena};
    bindings.reserve(desc.registerSpaces.size());
    Vector<VkDescriptorBindingFlags, Alloc::ScratchArena> bindingFlags{scratchArena};
    bindingFlags.reserve(desc.registerSpaces.size());
    HashSet<u32, Hasher<u32>, EqualTo<u32>, Alloc::ScratchArena> registerSpaceSlots(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        scratchArena
    );
    registerSpaceSlots.reserve(desc.registerSpaces.size());

    const u32 maxCapacity = VulkanDetail::NormalizeDescriptorTableCapacity(desc.maxCapacity);
    for(usize i = 0; i < desc.registerSpaces.size(); ++i){
        const auto& item = desc.registerSpaces[i];
        if(!VulkanDetail::IsBindlessRegisterSpaceType(item.type)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: register space slot {} has unsupported resource type {}")
                , item.slot
                , static_cast<u32>(item.type)
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

        // Descriptor-buffer layouts model update-after-bind and partially-bound access intrinsically. In particular,
        // Vulkan forbids pairing VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT with the update-after-bind
        // pool flag, so Backend C must not request the descriptor-indexing binding flags either. Backend A still uses
        // the classic descriptor-table semantics below.
        bindingFlags.push_back(0);
    }

    DescriptorBufferSegmentKind::Enum descriptorBufferSegmentKind = DescriptorBufferSegmentKind::None;
    const bool useDescriptorBuffer = desc.useDescriptorBuffer
        && VulkanDetail::IsDescriptorBufferBackendReady(m_context)
        && VulkanDetail::TryResolveBindlessDescriptorBufferLayout(desc, descriptorBufferSegmentKind)
    ;

    auto bindingFlagsInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutBindingFlagsCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
    if(!useDescriptorBuffer){
        for(VkDescriptorBindingFlags& flags : bindingFlags)
            flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        if(!bindingFlags.empty())
            bindingFlags.back() |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        bindingFlagsInfo.bindingCount = static_cast<u32>(bindingFlags.size());
        bindingFlagsInfo.pBindingFlags = bindingFlags.data();
    }

    auto layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    if(useDescriptorBuffer){
        // Descriptor buffers have a fixed, driver-queried block sized for the layout's full descriptorCount. They do
        // not allocate descriptor sets, so variable-count and update-after-bind descriptor-set flags are inapplicable.
        layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }
    else{
        layoutInfo.pNext = &bindingFlagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }
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

    // Backend C bindless mirror: the preflight selects a descriptor-buffer layout only when every register space can
    // live in one segment. A failed size/offset query cannot fall back after the flagged layout exists, so reject it
    // rather than returning a layout that a classic descriptor table cannot legally allocate from.
    if(useDescriptorBuffer){
        NWB_ASSERT(descriptorBufferSegmentKind != DescriptorBufferSegmentKind::None);

        const VkDescriptorSetLayout descriptorSetLayout = layout->m_descriptorSetLayouts[0];
        VkDeviceSize setSizeBytes = 0;
        vkGetDescriptorSetLayoutSizeEXT(m_context.device, descriptorSetLayout, &setSizeBytes);
        if(setSizeBytes == 0u || setSizeBytes > UINT32_MAX){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer set size is invalid."));
            DestroyArenaObject(m_context.objectArena, layout);
            return nullptr;
        }

        layout->m_descriptorBufferBindingOffsets.reserve(desc.registerSpaces.size());
        for(const auto& item : desc.registerSpaces){
            VkDeviceSize bindingOffsetBytes = 0;
            vkGetDescriptorSetLayoutBindingOffsetEXT(m_context.device, descriptorSetLayout, item.slot, &bindingOffsetBytes);
            if(!VulkanDetail::ValidateDescriptorBufferBindingFootprint(
                *m_context.descriptorBufferManager,
                VulkanDetail::ConvertDescriptorType(item.type),
                maxCapacity,
                setSizeBytes,
                bindingOffsetBytes,
                item.slot,
                NWB_TEXT("bindless layout")
            )){
                DestroyArenaObject(m_context.objectArena, layout);
                return nullptr;
            }
            layout->m_descriptorBufferBindingOffsets.insert_or_assign(item.slot, static_cast<u32>(bindingOffsetBytes));
        }
        layout->m_descriptorBufferSetSizeBytes = static_cast<u32>(setSizeBytes);
        layout->m_descriptorBufferSegmentKind = descriptorBufferSegmentKind;
        layout->m_descriptorBufferCompatible = true;
    }

    if(
        !VulkanDetail::CreatePipelineLayout(
            m_context,
            layout->m_descriptorSetLayouts.data(),
            static_cast<u32>(layout->m_descriptorSetLayouts.size()),
            0,
            layout->m_pipelineLayout,
            NWB_TEXT("create bindless layout")
        )
    ){
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTableHandle Device::createDescriptorTable(const BindingLayoutHandle& layoutResource){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);

    auto* layout = layoutResource.get();
    if(!layout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table: binding layout is invalid"));
        return nullptr;
    }
    if(layout->m_descriptorSetLayouts.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table: binding layout has no descriptor set layouts"));
        return nullptr;
    }

    auto* table = NewArenaObject<DescriptorTable>(m_context.objectArena, m_context);
    table->m_layout = Handle<BindingLayout>(
        layout,
        Handle<BindingLayout>::deleter_type(&m_context.objectArena)
    );
    const u32 descriptorTableCapacity = layout->m_isBindless
        ? VulkanDetail::NormalizeDescriptorTableCapacity(layout->m_bindlessDesc.maxCapacity)
        : 1u
    ;
    const bool useVariableDescriptorCount = layout->m_isBindless && !layout->m_bindlessDesc.registerSpaces.empty();

    const usize poolSizeCapacity = layout->m_isBindless
        ? layout->m_bindlessDesc.registerSpaces.size()
        : layout->m_desc.bindings.size()
    ;
    Vector<VkDescriptorPoolSize, Alloc::ScratchArena> poolSizes{scratchArena};
    poolSizes.reserve(poolSizeCapacity);

    const bool poolSizesAdded = layout->m_isBindless
        ? VulkanDetail::AddBindlessLayoutDescriptorPoolSizes(poolSizes, layout->m_bindlessDesc, descriptorTableCapacity)
        : VulkanDetail::AddBindingLayoutDescriptorPoolSizes(poolSizes, layout->m_desc, 1u)
    ;
    if(!poolSizesAdded){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table: descriptor pool size overflows"));
        DestroyArenaObject(m_context.objectArena, table);
        return nullptr;
    }

    auto poolInfo = VulkanDetail::MakeVkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
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
        auto allocInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
        allocInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();

        auto variableDescriptorInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetVariableDescriptorCountAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
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

void Device::resizeDescriptorTable(DescriptorTable* m_descriptorTable, u32 newSize, bool keepContents){
    VkResult res = VK_SUCCESS;

    auto* table = m_descriptorTable;
    if(!table)
        return;

    if(!table->m_layout || newSize == 0)
        return;
    if(table->m_layout->m_isBindless){
        const u32 maxCapacity = VulkanDetail::NormalizeDescriptorTableCapacity(table->m_layout->m_bindlessDesc.maxCapacity);
        if(newSize > maxCapacity){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resize bindless descriptor table to {} descriptors: layout max capacity is {}")
                , newSize
                , maxCapacity
            );
            return;
        }
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);
    using DescriptorSetVector = Vector<VkDescriptorSet, Alloc::GlobalArena>;
    using WrittenItemVector = Vector<BindingSetItem, Alloc::GlobalArena>;

    auto commitResize = [&](VkDescriptorPool newPool, DescriptorSetVector& newDescriptorSets, u32 newCapacity) -> bool{
        VkDescriptorPool oldPool = table->m_descriptorPool;
        const u32 oldCapacity = table->m_capacity;

        DescriptorSetVector oldDescriptorSets{ m_context.objectArena };
        oldDescriptorSets = Move(table->m_descriptorSets);

        WrittenItemVector oldWrittenItems{ m_context.objectArena };
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

        Vector<VkDescriptorPoolSize, Alloc::ScratchArena> poolSizes{scratchArena};
        poolSizes.reserve(table->m_layout->m_bindlessDesc.registerSpaces.size());

        if(!VulkanDetail::AddBindlessLayoutDescriptorPoolSizes(poolSizes, table->m_layout->m_bindlessDesc, newSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resize descriptor table: descriptor pool size overflows"));
            return;
        }

        VkDescriptorPool newPool = VK_NULL_HANDLE;
        DescriptorSetVector newDescriptorSets{ m_context.objectArena };

        auto poolInfo = VulkanDetail::MakeVkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &newPool);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless descriptor pool for resize: {}"), ResultToString(res));
            return;
        }

        auto allocInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocInfo.descriptorPool = newPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = table->m_layout->m_descriptorSetLayouts.data();

        auto variableDescriptorInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetVariableDescriptorCountAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
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
            return;
        }

        if(!commitResize(newPool, newDescriptorSets, newSize))
            return;
        return;
    }

    Vector<VkDescriptorPoolSize, Alloc::ScratchArena> poolSizes{scratchArena};
    poolSizes.reserve(table->m_layout->m_desc.bindings.size());
    if(!VulkanDetail::AddBindingLayoutDescriptorPoolSizes(poolSizes, table->m_layout->m_desc, newSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to resize descriptor table: descriptor pool size overflows"));
        return;
    }

    VkDescriptorPool newPool = VK_NULL_HANDLE;
    DescriptorSetVector newDescriptorSets{ m_context.objectArena };

    auto poolInfo = VulkanDetail::MakeVkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
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
        Vector<VkDescriptorSetLayout, Alloc::ScratchArena> layouts(newSize, table->m_layout->m_descriptorSetLayouts[0], scratchArena);

        auto allocInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
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

    if(!commitResize(newPool, newDescriptorSets, newSize))
        return;
}

bool Device::writeDescriptorTable(DescriptorTable* m_descriptorTable, const BindingSetItem& item){
    auto* table = m_descriptorTable;
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
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: array element {} exceeds bindless capacity {}")
            , item.slot
            , item.arrayElement
            , table->m_capacity
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
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: layout does not contain resource type {} at that slot")
            , item.slot
            , static_cast<u32>(item.type)
        );
        return false;
    }
    if(!table->m_layout->m_isBindless && item.arrayElement >= layoutBinding->getArraySize()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: array element {} exceeds layout array size {}")
            , item.slot
            , item.arrayElement
            , layoutBinding->getArraySize()
        );
        return false;
    }

    auto write = VulkanDetail::MakeVkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = table->m_descriptorSets[0];
    write.dstBinding = item.slot;
    write.dstArrayElement = item.arrayElement;
    write.descriptorCount = 1;
    write.descriptorType = VulkanDetail::ConvertDescriptorType(item.type);

    VkDescriptorBufferInfo bufferInfo = {};
    VkDescriptorImageInfo imageInfo = {};
    VkBufferView texelBufferView = VK_NULL_HANDLE;
    auto asInfo = VulkanDetail::MakeVkStruct<VkWriteDescriptorSetAccelerationStructureKHR>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);

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
            imageInfo.imageView = texture->getView(item.subresources, item.dimension, item.format);
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
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write descriptor table slot {}: unsupported resource type {}")
                , item.slot
                , static_cast<u32>(item.type)
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


BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, const BindingLayoutHandle& layoutResource){
    auto* layout = layoutResource.get();
    if(!layout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create binding set: binding layout is invalid"));
        return nullptr;
    }

    auto* bindingSet = NewArenaObject<BindingSet>(m_context.objectArena, m_context);
    bindingSet->m_desc = desc;

    // Backend C: a descriptor-buffer layout has no classic descriptor sets (its descriptors live in the descriptor
    // buffer), so skip the pool-backed descriptor table + vkUpdateDescriptorSets path entirely. The block carve +
    // per-binding writes happen below in the descriptor-buffer branch, mirroring how the heap path writes its own
    // descriptors instead of the classic write loop. m_descriptorTable/m_descriptorSets stay empty; the command
    // bind path routes on layout->isDescriptorBufferCompatible(), not on m_descriptorSets.empty().
    if(!layout->m_descriptorBufferCompatible){
        DescriptorTableHandle tableHandle = createDescriptorTable(layoutResource);
        if(!tableHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table for binding set"));
            DestroyArenaObject(m_context.objectArena, bindingSet);
            return nullptr;
        }

        bindingSet->m_descriptorTable = Handle<DescriptorTable>(
            tableHandle.get(),
            Handle<DescriptorTable>::deleter_type(&m_context.objectArena)
        );
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
    }
    bindingSet->m_layout = Handle<BindingLayout>(
        layout,
        Handle<BindingLayout>::deleter_type(&m_context.objectArena)
    );

    // Backend C: carve ONE contiguous block for this set's descriptor buffer (sized by the layout's driver-queried
    // set size), then write each non-push-constant binding at blockOffset + bindingOffset +
    // arrayElement*descriptorSize. A single binding set's block always lives in one segment (resource OR sampler);
    // the layout is only buffer-compatible when segment-coherent, so a single carve suffices. The carve is freed in
    // the destructor through the manager; the writes are plain host writes through vkGetDescriptorEXT.
    if(layout->m_descriptorBufferCompatible){
        if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor-buffer binding set: descriptor buffer manager is unavailable."));
            DestroyArenaObject(m_context.objectArena, bindingSet);
            return nullptr;
        }

        const u32 setSizeBytes = layout->m_descriptorBufferSetSizeBytes;
        const DescriptorBufferSegmentKind::Enum segmentKind = layout->m_descriptorBufferSegmentKind;
        const auto& bindingOffsets = layout->getDescriptorBufferBindingOffsets();
        if(setSizeBytes == 0u){
            // Push-only / zero-binding layouts are valid descriptor-buffer gap sets. They intentionally carry no
            // allocation and bind through the harmless zero offset in bindDescriptorBufferState().
            if(segmentKind != DescriptorBufferSegmentKind::None || !bindingOffsets.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor-buffer binding set: zero-sized layout has descriptor metadata."));
                DestroyArenaObject(m_context.objectArena, bindingSet);
                return nullptr;
            }
        }
        else if(segmentKind == DescriptorBufferSegmentKind::None){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor buffer block: layout has no segment kind."));
            DestroyArenaObject(m_context.objectArena, bindingSet);
            return nullptr;
        }

        if(setSizeBytes > 0u){
            const u32 alignmentBytes = m_context.descriptorBufferManager->getOffsetAlignmentBytes();
            const DescriptorBufferSegment block = m_context.descriptorBufferManager->allocate(segmentKind, setSizeBytes, alignmentBytes);
            if(!block.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor buffer block ({} bytes)"), setSizeBytes);
                DestroyArenaObject(m_context.objectArena, bindingSet);
                return nullptr;
            }
            bindingSet->m_descriptorBufferAllocations.push_back(block);

            for(const auto& item : desc.bindings){
                if(item.type == ResourceType::PushConstants || item.type == ResourceType::None || !item.resourceHandle)
                    continue;

                // Match the classic write path: descriptor-buffer writes must agree with the layout's type and array
                // bounds before their byte address is calculated.
                const BindingLayoutItem* layoutBinding = VulkanDetail::FindLayoutBinding(layout->m_desc, item.slot, item.type);
                if(!layoutBinding){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring descriptor-buffer binding set item for slot {}: layout does not contain resource type {} at that slot")
                        , item.slot
                        , static_cast<u32>(item.type)
                    );
                    continue;
                }
                if(item.arrayElement >= layoutBinding->getArraySize()){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring descriptor-buffer binding set item for slot {} with out-of-range array element {}"), item.slot, item.arrayElement);
                    continue;
                }

                const auto offsetIt = bindingOffsets.find(item.slot);
                if(offsetIt == bindingOffsets.end()){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Descriptor buffer binding slot {} has no layout offset; skipped"), item.slot);
                    continue;
                }

                const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(item.type);
                const u32 descriptorSize = m_context.descriptorBufferManager->getDescriptorSize(descriptorType);
                const u64 relativeOffsetBytes = static_cast<u64>(offsetIt->second) + static_cast<u64>(item.arrayElement) * descriptorSize;
                if(
                    descriptorSize == 0u
                    || relativeOffsetBytes > block.sizeBytes
                    || descriptorSize > block.sizeBytes - relativeOffsetBytes
                    || static_cast<u64>(block.offsetBytes) + relativeOffsetBytes > UINT32_MAX
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Descriptor buffer binding slot {} does not fit in its carved block; skipped"), item.slot);
                    continue;
                }

                const u32 bindingBaseOffset = static_cast<u32>(static_cast<u64>(block.offsetBytes) + relativeOffsetBytes);
                if(!m_context.descriptorBufferManager->writeDescriptor(item, block, bindingBaseOffset, descriptorType))
                    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to write descriptor buffer entry for slot {}"), item.slot);
            }
        }
    }

    // The classic descriptor-pool write path is only for sets backed by descriptor sets (Backend A);
    // descriptor-buffer sets (Backend C) have no descriptor-set objects to write into, so their writes were already
    // performed above into the carved buffer block. Guard the whole vkUpdateDescriptorSets loop on compatibility so
    // the (empty) m_descriptorSets[0] dereference is never reached for a descriptor-buffer set.
    if(!layout->m_descriptorBufferCompatible){
        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena, s_DescriptorBindingScratchArenaBytes);

        Vector<VkWriteDescriptorSet, Alloc::ScratchArena> writes{scratchArena};
    Vector<VkDescriptorBufferInfo, Alloc::ScratchArena> bufferInfos{scratchArena};
    Vector<VkDescriptorImageInfo, Alloc::ScratchArena> imageInfos{scratchArena};
    Vector<VkBufferView, Alloc::ScratchArena> texelBufferViews{scratchArena};
    Vector<VkWriteDescriptorSetAccelerationStructureKHR, Alloc::ScratchArena> asInfos{scratchArena};

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
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: layout does not contain resource type {} at that slot")
                , item.slot
                , static_cast<u32>(item.type)
            );
            continue;
        }
        if(item.arrayElement >= layoutBinding->getArraySize()){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {} with out-of-range array element {}"), item.slot, item.arrayElement);
            continue;
        }

        auto write = VulkanDetail::MakeVkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
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
                imgInfo.imageView = texture->getView(item.subresources, item.dimension, item.format);
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
                auto asWrite = VulkanDetail::MakeVkStruct<VkWriteDescriptorSetAccelerationStructureKHR>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
                asWrite.accelerationStructureCount = 1;
                asWrite.pAccelerationStructures = &as->m_accelStruct;
                asInfos.push_back(asWrite);
                write.pNext = &asInfos.back();
                writes.push_back(write);
                break;
            }
            default:
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring binding set item for slot {}: unsupported resource type {}")
                    , item.slot
                    , static_cast<u32>(item.type)
                );
                break;
            }
        }
    }

    if(!writes.empty())
            vkUpdateDescriptorSets(m_context.device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    }

    return BindingSetHandle(bindingSet, BindingSetHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


void CommandList::retainBindingSets(const BindingSetVector& bindings){
    for(const auto& binding : bindings){
        if(binding)
            retainResource(binding);
    }
}

void CommandList::bindPipelineBindingSets(
    const VkPipelineBindPoint bindPoint,
    const VkPipelineLayout pipelineLayout,
    const bool usesDescriptorBuffer,
    const BindingSetVector& bindings){
    retainBindingSets(bindings);

    if(usesDescriptorBuffer){
        bindDescriptorBufferState(bindPoint, pipelineLayout, bindings);
        return;
    }

    if(bindings.empty() || pipelineLayout == VK_NULL_HANDLE)
        return;

    for(usize i = 0; i < bindings.size(); ++i){
        if(!bindings[i])
            continue;

        auto* bindingSet = bindings[i];
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


void CommandList::bindDescriptorHeap(GpuDescriptorHeap& heap, const VkPipelineBindPoint bindPoint, const VkPipelineLayout pipelineLayout){
    // Binds the global descriptor heap's persistent tables at their set indices against the given pipeline layout.
    // The pipeline must have been built with the heap layouts at those positions.
    if(!m_currentCmdBuf || pipelineLayout == VK_NULL_HANDLE)
        return;
    if(!heap.isInitialized())
        return;

    const DescriptorTable* resourceTable = heap.m_resourceTable.get();
    if(resourceTable && !resourceTable->m_descriptorSets.empty()){
        const VkDescriptorSet set = resourceTable->m_descriptorSets[0];
        vkCmdBindDescriptorSets(m_currentCmdBuf->m_cmdBuf, bindPoint, pipelineLayout, heap.getResourceSetIndex(), 1, &set, 0, nullptr);
    }

    const DescriptorTable* samplerTable = heap.m_samplerTable.get();
    if(samplerTable && !samplerTable->m_descriptorSets.empty()){
        const VkDescriptorSet set = samplerTable->m_descriptorSets[0];
        vkCmdBindDescriptorSets(m_currentCmdBuf->m_cmdBuf, bindPoint, pipelineLayout, heap.getSamplerSetIndex(), 1, &set, 0, nullptr);
    }
}

void CommandList::bindDescriptorBufferHeap(
    GpuDescriptorHeap& heap,
    const VkPipelineBindPoint bindPoint,
    const VkPipelineLayout pipelineLayout,
    const GpuDescriptorHandle accelStructHandle
){
    // Backend C heap bind. Pipelines always embed resource/sampler at reserved sets 8/9. Hardware TLAS consumers
    // additionally embed the fixed AS layout at set 10 and pass a handle whose descriptor-buffer block is immutable
    // for that TLAS generation. The descriptor data was written at heap write() time; this only selects offsets.
    if(!m_currentCmdBuf || pipelineLayout == VK_NULL_HANDLE)
        return;
    if(!heap.isInitialized() || !heap.usesDescriptorBuffer())
        return;
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return;

    const DescriptorBufferSegment& resourceBlock = heap.getResourceBufferBlock();
    const DescriptorBufferSegment& samplerBlock = heap.getSamplerBufferBlock();
    const DescriptorBufferSegment accelStructBlock = heap.getAccelStructBufferBlock(accelStructHandle);
    const bool bindAccelStruct = accelStructHandle.valid();
    if(
        !resourceBlock.valid()
        || resourceBlock.kind != DescriptorBufferSegmentKind::Resource
        || !samplerBlock.valid()
        || samplerBlock.kind != DescriptorBufferSegmentKind::Sampler
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer heap: persistent resource or sampler block is invalid."));
        return;
    }
    if(bindAccelStruct && (!accelStructBlock.valid() || accelStructBlock.kind != DescriptorBufferSegmentKind::Resource)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer TLAS heap handle {}: descriptor block is invalid."), accelStructHandle.value);
        return;
    }

    const u32 offsetAlignmentBytes = m_context.descriptorBufferManager->getOffsetAlignmentBytes();
    if(
        (resourceBlock.offsetBytes % offsetAlignmentBytes) != 0u
        || (samplerBlock.offsetBytes % offsetAlignmentBytes) != 0u
        || (bindAccelStruct && (accelStructBlock.offsetBytes % offsetAlignmentBytes) != 0u)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer heap: a set block offset is misaligned."));
        return;
    }

    // Bind both segments once for this command buffer (idempotent with bindDescriptorBufferState's bind; the driver
    // accepts a re-bind at the same addresses). The order (resource=0, sampler=1) matches the manager's indices.
    VkDescriptorBufferBindingInfoEXT bindingInfos[2] = {
        m_context.descriptorBufferManager->getResourceBindingInfo(),
        m_context.descriptorBufferManager->getSamplerBindingInfo()
    };
    vkCmdBindDescriptorBuffersEXT(m_currentCmdBuf->m_cmdBuf, 2u, bindingInfos);

    // The heap sets occupy the manager's two segments: resource -> resource segment, sampler -> sampler segment,
    // optional TLAS -> resource segment. They are contiguous at 8/9/10, so one API call selects all required blocks.
    const u32 resourceIndex = m_context.descriptorBufferManager->getResourceBufferIndex();
    const u32 samplerIndex = m_context.descriptorBufferManager->getSamplerBufferIndex();
    const u32 bufferIndices[3] = { resourceIndex, samplerIndex, resourceIndex };
    const VkDeviceSize offsets[3] = {
        resourceBlock.offsetBytes,
        samplerBlock.offsetBytes,
        accelStructBlock.offsetBytes
    };

    vkCmdSetDescriptorBufferOffsetsEXT(
        m_currentCmdBuf->m_cmdBuf,
        bindPoint,
        pipelineLayout,
        heap.getResourceSetIndex(),
        bindAccelStruct ? 3u : 2u,
        bufferIndices,
        offsets
    );
}

void CommandList::bindDescriptorBufferState(const VkPipelineBindPoint bindPoint, const VkPipelineLayout pipelineLayout, const BindingSetVector& bindings){
    // Backend C command bind. Binds the global resource + sampler descriptor-buffer segments ONCE per command buffer
    // (vkCmdBindDescriptorBuffersEXT, resource segment at index 0, sampler segment at index 1), then records one
    // vkCmdSetDescriptorBufferOffsetsEXT per binding set: the set's single carved block lives in one segment, so the
    // buffer index is the segment's index and the offset is the block's carved offset. The descriptor data itself was
    // written at createBindingSet time (host memcpy via vkGetDescriptorEXT), so no descriptor writes happen here.
    if(!m_currentCmdBuf || !m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return;
    if(bindings.empty() || pipelineLayout == VK_NULL_HANDLE)
        return;

    // Bind both segments once. The binding infos are stable for the manager's lifetime; the buffer index order
    // (resource=0, sampler=1) matches the manager's getResourceBufferIndex/getSamplerBufferIndex.
    VkDescriptorBufferBindingInfoEXT bindingInfos[2] = {
        m_context.descriptorBufferManager->getResourceBindingInfo(),
        m_context.descriptorBufferManager->getSamplerBindingInfo()
    };
    vkCmdBindDescriptorBuffersEXT(m_currentCmdBuf->m_cmdBuf, 2u, bindingInfos);

    // Batch consecutive binding sets into one vkCmdSetDescriptorBufferOffsetsEXT call: the API accepts setCount
    // (pBufferIndices,pOffsets) pairs for sets [firstSet .. firstSet+setCount-1], which is exactly the positional
    // layout of a compute/graphics pipeline's binding-set vector. Each set's block lives in one segment, so its
    // buffer index is the segment's index and its offset is the carved block's offset.
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena, s_DescriptorBindingScratchArenaBytes);
    // These vectors describe exactly the supplied binding-set sequence. Constructing them with bindings.size() here
    // would populate leading zero offsets, shifting every real descriptor block to later set numbers.
    Vector<u32, Alloc::ScratchArena> bufferIndices{scratchArena};
    Vector<VkDeviceSize, Alloc::ScratchArena> offsets{scratchArena};
    bufferIndices.reserve(bindings.size());
    offsets.reserve(bindings.size());

    const u32 offsetAlignmentBytes = m_context.descriptorBufferManager->getOffsetAlignmentBytes();

    for(usize i = 0; i < bindings.size(); ++i){
        auto* bindingSet = bindings[i];
        if(!bindingSet){
            // A null gap is represented by the descriptor-buffer-compatible empty layout and a harmless zero offset.
            bufferIndices.push_back(0u);
            offsets.push_back(0);
            continue;
        }

        const auto* layout = bindingSet->getLayout();
        if(!layout || !layout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer state: binding set {} is not descriptor-buffer-compatible."), i);
            return;
        }

        if(bindingSet->m_descriptorBufferAllocations.empty()){
            if(layout->getDescriptorBufferSetSizeBytes() != 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer state: binding set {} has no carved descriptor block."), i);
                return;
            }
            bufferIndices.push_back(m_context.descriptorBufferManager->getResourceBufferIndex());
            offsets.push_back(0);
            continue;
        }

        const DescriptorBufferSegment& block = bindingSet->m_descriptorBufferAllocations[0];
        const DescriptorBufferSegmentKind::Enum segmentKind = layout->getDescriptorBufferSegmentKind();
        if(
            !block.valid()
            || block.kind != segmentKind
            || (block.offsetBytes % offsetAlignmentBytes) != 0u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot bind descriptor-buffer state: binding set {} has an invalid or misaligned descriptor block."), i);
            return;
        }
        const u32 bufferIndex = segmentKind == DescriptorBufferSegmentKind::Sampler
            ? m_context.descriptorBufferManager->getSamplerBufferIndex()
            : m_context.descriptorBufferManager->getResourceBufferIndex();
        bufferIndices.push_back(bufferIndex);
        offsets.push_back(block.offsetBytes);
    }

    if(!bufferIndices.empty()){
        vkCmdSetDescriptorBufferOffsetsEXT(
            m_currentCmdBuf->m_cmdBuf,
            bindPoint,
            pipelineLayout,
            0u,
            static_cast<u32>(bufferIndices.size()),
            bufferIndices.data(),
            offsets.data()
        );
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

