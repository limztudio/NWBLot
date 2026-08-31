// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "command_validation.h"
#include "arena_names.h"
#include "resource_bindings_detail.h"

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

// Clamp descriptor-buffer alignment for 32-bit byte offsets.
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
        if(!VulkanDetail::IsPipelineColorAttachmentFormatClassValid(fbinfo.colorFormats[i])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create {}: color attachment format {} has depth or stencil aspects"),
                operationName,
                i
            );
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
    const VulkanContext& context,
    VkPipeline& pipeline,
    VkPipelineLayout& pipelineLayout,
    bool& ownsPipelineLayout
){
    if(pipeline){
        context.deviceDispatch.vkDestroyPipeline(context.device, pipeline, context.allocationCallbacks);
        pipeline = VK_NULL_HANDLE;
    }

    if(ownsPipelineLayout && pipelineLayout != VK_NULL_HANDLE){
        context.deviceDispatch.vkDestroyPipelineLayout(context.device, pipelineLayout, context.allocationCallbacks);
        pipelineLayout = VK_NULL_HANDLE;
        ownsPipelineLayout = false;
    }
}

// Samplers occupy their dedicated descriptor-buffer segment.
constexpr DescriptorBufferSegmentKind::Enum GetDescriptorBufferSegmentKind(ResourceType::Enum type){
    return type == ResourceType::Sampler ? DescriptorBufferSegmentKind::Sampler : DescriptorBufferSegmentKind::Resource;
}

bool IsDescriptorBufferBackendReady(const VulkanContext& context){
    return context.descriptorBufferManager
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

// Global heap resource type; TLAS uses its own immutable one-descriptor layout.
constexpr bool IsBindlessRegisterSpaceType(ResourceType::Enum type){
    return IsSupportedDescriptorBindingType(type);
}

constexpr u32 NormalizeBindlessDescriptorCapacity(const u32 capacity){
    return capacity > 0 ? capacity : 1u;
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

    res = context.deviceDispatch.vkCreatePipelineLayout(context.device, &layoutInfo, context.allocationCallbacks, &outLayout);
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
    const TextureDesc& textureDesc = texture.m_creationDesc;
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
        m_context.deviceDispatch.vkDestroyPipelineLayout(m_context.device, m_pipelineLayout, m_context.allocationCallbacks);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    for(VkDescriptorSetLayout layout : m_descriptorSetLayouts){
        if(layout)
            m_context.deviceDispatch.vkDestroyDescriptorSetLayout(m_context.device, layout, m_context.allocationCallbacks);
    }
    m_descriptorSetLayouts.clear();
}


BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc){
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

    if(
        !VulkanDetail::CreatePipelineLayout(
            m_context,
            nullptr,
            0u,
            pushConstantByteSize,
            layout->m_pipelineLayout,
            NWB_TEXT("create binding layout")
        )
    ){
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    // Push constants are pipeline-layout state, not descriptor-set state.
    layout->m_descriptorBufferCompatible = true;

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    if(desc.descriptorSetIndex == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: an explicit descriptor-set index is required."));
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
    layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    res = m_context.deviceDispatch.vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create bindless descriptor set layout: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    layout->m_descriptorSetLayouts.push_back(setLayout);

    NWB_ASSERT(descriptorBufferSegmentKind != DescriptorBufferSegmentKind::None);

    const VkDescriptorSetLayout descriptorSetLayout = layout->m_descriptorSetLayouts[0];
    VkDeviceSize setSizeBytes = 0;
    m_context.deviceDispatch.vkGetDescriptorSetLayoutSizeEXT(m_context.device, descriptorSetLayout, &setSizeBytes);
    if(setSizeBytes == 0u || setSizeBytes > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer set size is invalid."));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    layout->m_descriptorBufferBindingOffsets.reserve(desc.registerSpaces.size());
    for(const auto& item : desc.registerSpaces){
        VkDeviceSize bindingOffsetBytes = 0;
        m_context.deviceDispatch.vkGetDescriptorSetLayoutBindingOffsetEXT(m_context.device, descriptorSetLayout, item.slot, &bindingOffsetBytes);
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

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

