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

// Descriptor-buffer offset alignment clamped to a 32-bit value (1 when zero/oversized) for byte-offset math.
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

// Descriptor-buffer layouts must be decided before vkCreateDescriptorSetLayout. A layout created with
// VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT cannot be reinterpreted as a descriptor set, and a
// mixed sampler/resource shape is therefore rejected.
bool IsDescriptorBufferBackendReady(const VulkanContext& context){
    return context.extensions.EXT_descriptor_buffer
        && context.descriptorBufferManager
        && context.descriptorBufferManager->isEnabled()
    ;
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

// Supported register-space resource type for a global descriptor-heap layout. Immutable layouts may declare a
// one-descriptor acceleration-structure surface; mutable ResourceDescriptorHeap/SamplerDescriptorHeap tables do not.
constexpr bool IsBindlessRegisterSpaceType(ResourceType::Enum type){
    return IsSupportedDescriptorBindingType(type);
}

constexpr u32 NormalizeBindlessDescriptorCapacity(const u32 capacity){
    return capacity > 0 ? capacity : 1u;
}

bool ResolveDescriptorBufferRange(const DescriptorWriteItem& item, const Buffer& buffer, BufferRange& outRange){
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

bool BuildImageViewCreateInfo(Texture& texture, const DescriptorWriteItem& item, VkImageViewCreateInfo& outViewInfo){
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

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer backend is unavailable."), operationName);
        return false;
    }

    if(bindingLayouts.empty()){
        const VkDescriptorSetLayout emptyLayout = getOrCreateEmptyDescriptorBufferSetLayout();
        if(emptyLayout == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: the required empty descriptor-buffer layout is unavailable."), operationName);
            return false;
        }
        if(!VulkanDetail::CreatePipelineLayout(m_context, &emptyLayout, 1u, 0u, outPipelineLayout, operationName))
            return false;

        outOwnsPipelineLayout = true;
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

    // Determine each layout's descriptor-set placement. Push-constant-only pipeline-local layouts use positional
    // assignment; resource-bearing global-heap layouts pin themselves to explicit reserved high sets (8/9/10).
    const auto layoutSetIndex = [](const BindingLayout& layout, const u32 positional, bool& outExplicit) -> u32{
        if(const BindlessLayoutDesc* bindlessDesc = layout.getBindlessDesc()){
            outExplicit = true;
            return bindlessDesc->descriptorSetIndex;
        }
        outExplicit = false;
        return positional;
    };

    bool anyExplicitSet = false;
    for(u32 i = 0; i < static_cast<u32>(bindingLayouts.size()) && !anyExplicitSet; ++i){
        bool isExplicit = false;
        [[maybe_unused]] const u32 setIndex = layoutSetIndex(*bindingLayouts[i].get(), i, isExplicit);
        anyExplicitSet = isExplicit;
    }


    // All layouts are descriptor-buffer layouts. No ordinary descriptor-set pipeline path exists.
    for(const auto& bindingLayout : bindingLayouts){
        const auto* layout = bindingLayout.get();
        if(!layout || !layout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: every binding layout must be descriptor-buffer-compatible."), operationName);
            return false;
        }
        if(const BindlessLayoutDesc* bindlessDesc = layout->getBindlessDesc()){
            if(bindlessDesc->descriptorSetIndex < s_MaxBindingLayouts){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: resource-bearing bindless layouts must use an explicit global-heap set at or above {}."), operationName, s_MaxBindingLayouts);
                return false;
            }
        }
    }

    if(bindingLayouts.size() == 1 && !anyExplicitSet){
        auto* layout = bindingLayouts[0].get();
        NWB_ASSERT(layout != nullptr);
        outPipelineLayout = layout->m_pipelineLayout;
        outPushConstantByteSize = layout->m_pushConstantByteSize;
        return true;
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
        // the cached descriptor-buffer empty layout so the pipeline layout is dense from set 0 (Vulkan requires no holes).
        const VkDescriptorSetLayout fillSetLayout = getOrCreateEmptyDescriptorBufferSetLayout();
        if(fillSetLayout == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: descriptor-buffer explicit-set placement needs the empty descriptor-buffer gap-set layout, which is unavailable"), operationName);
            return false;
        }

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
    outBindings.m_pushConstantByteSize = 0;

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: required descriptor-buffer backend is unavailable."), operationName);
        return false;
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
// VK_EXT_descriptor_buffer manager
//
// Two HOST-mapped VkBuffers sub-allocated by byte offset through a shared free-range list + bump pointer. Descriptor
// buffers use their own usage bits, their own offset alignment (descriptorBufferOffsetAlignment), and write
// descriptors through vkGetDescriptorEXT (VkDescriptorGetInfoEXT + VkDescriptorDataEXT), which natively encodes
// acceleration-structure handles. Descriptor-buffer-compatible pipelines consume these segments through
// CommandList's descriptor-buffer binding path; the renderer has no ordinary descriptor-set transport.


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
        // binary search instead of walking every persistent descriptor-buffer allocation on every descriptor update.
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
    const DescriptorWriteItem& item,
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
            // The TLAS heap path. vkGetDescriptorEXT directly encodes the acceleration-structure device address in
            // the descriptor-buffer block selected for this generation.
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
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(ResourceQueueSharing::GraphicsAndAsyncCompute, m_context);
    bufferInfo.sharingMode = sharingInfo.mode;
    bufferInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    bufferInfo.pQueueFamilyIndices = sharingInfo.data();

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


BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    if(desc.bindings.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: binding count exceeds Vulkan limit"));
        return nullptr;
    }

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create binding layout: descriptor-buffer backend is unavailable."));
        return nullptr;
    }

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_desc = desc;

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
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create binding layout: pipeline-local resource bindings are retired; register slot {} in GpuDescriptorHeap and select it through push constants."), item.slot);
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    const u32 pushConstantByteSize = VulkanDetail::GetPushConstantByteSize(desc);
    layout->m_pushConstantByteSize = pushConstantByteSize;

    auto layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.bindingCount = 0u;
    layoutInfo.pBindings = nullptr;

    layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

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

    // Pipeline-local layouts carry push constants only, so their descriptor-buffer block is intentionally empty.
    // Resource-bearing descriptor blocks are owned exclusively by GpuDescriptorHeap.
    layout->m_descriptorBufferCompatible = true;

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    if(
        desc.descriptorSetIndex < s_MaxBindingLayouts
        || desc.descriptorSetIndex == Limit<u32>::s_Max
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: a global-heap descriptor set at or above {} is required."), s_MaxBindingLayouts);
        return nullptr;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_isBindless = true;
    layout->m_bindlessDesc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchArena> bindings{scratchArena};
    bindings.reserve(desc.registerSpaces.size());
    HashSet<u32, Hasher<u32>, EqualTo<u32>, Alloc::ScratchArena> registerSpaceSlots(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        scratchArena
    );
    registerSpaceSlots.reserve(desc.registerSpaces.size());

    const u32 maxCapacity = VulkanDetail::NormalizeBindlessDescriptorCapacity(desc.maxCapacity);
    for(usize i = 0; i < desc.registerSpaces.size(); ++i){
        const auto& item = desc.registerSpaces[i];
        if(
            !VulkanDetail::IsBindlessRegisterSpaceType(item.type)
            || (item.type == ResourceType::RayTracingAccelStruct && desc.layoutType != BindlessLayoutType::Immutable)
        ){
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

    }

    DescriptorBufferSegmentKind::Enum descriptorBufferSegmentKind = DescriptorBufferSegmentKind::None;
    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer backend is unavailable."));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    if(!VulkanDetail::TryResolveBindlessDescriptorBufferLayout(desc, descriptorBufferSegmentKind)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer layouts cannot mix sampler and resource bindings."));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    auto layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    // Descriptor buffers have a fixed, driver-queried block sized for the layout's full descriptorCount.
    layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
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


void CommandList::bindDescriptorBufferHeap(
    GpuDescriptorHeap& heap,
    const VkPipelineBindPoint bindPoint,
    const VkPipelineLayout pipelineLayout,
    const GpuDescriptorHandle accelStructHandle
){
    // Descriptor-buffer heap bind. Pipelines always embed resource/sampler at reserved sets 8/9. Hardware TLAS consumers
    // additionally embed the fixed AS layout at set 10 and pass a handle whose descriptor-buffer block is immutable
    // for that TLAS generation. The descriptor data was written at heap write() time; this only selects offsets.
    if(!m_currentCmdBuf || pipelineLayout == VK_NULL_HANDLE)
        return;
    if(!heap.isInitialized())
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

    // Bind both segments once for this command buffer (idempotent with the empty-set bind; the driver accepts a
    // re-bind at the same addresses). The order (resource=0, sampler=1) matches the manager's indices.
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

void CommandList::bindDescriptorBufferEmptySet(const VkPipelineBindPoint bindPoint, const VkPipelineLayout pipelineLayout){
    if(!m_currentCmdBuf || pipelineLayout == VK_NULL_HANDLE)
        return;
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return;

    // Set 0 is always a descriptor-buffer empty/push-constant layout. It has no descriptor bytes, but selecting
    // the resource segment's aligned zero offset establishes descriptor-buffer state for the push-only gap. All
    // resource-bearing sets are selected separately by the global heap.
    VkDescriptorBufferBindingInfoEXT bindingInfos[2] = {
        m_context.descriptorBufferManager->getResourceBindingInfo(),
        m_context.descriptorBufferManager->getSamplerBindingInfo()
    };
    vkCmdBindDescriptorBuffersEXT(m_currentCmdBuf->m_cmdBuf, 2u, bindingInfos);

    const u32 resourceBufferIndex = m_context.descriptorBufferManager->getResourceBufferIndex();
    constexpr VkDeviceSize s_EmptySetOffsetBytes = 0u;
    vkCmdSetDescriptorBufferOffsetsEXT(
        m_currentCmdBuf->m_cmdBuf,
        bindPoint,
        pipelineLayout,
        0u,
        1u,
        &resourceBufferIndex,
        &s_EmptySetOffsetBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

