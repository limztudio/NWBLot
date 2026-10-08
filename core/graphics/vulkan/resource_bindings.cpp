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


VkDescriptorType ConvertDescriptorType(ResourceType::Enum type)noexcept{
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

VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages)noexcept{
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
    if(stages & ShaderType::RayGeneration)
        flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    if(stages & ShaderType::AnyHit)
        flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    if(stages & ShaderType::ClosestHit)
        flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    if(stages & ShaderType::Miss)
        flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
    if(stages & ShaderType::Intersection)
        flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    if(stages & ShaderType::Callable)
        flags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    if(flags == 0)
        flags = VK_SHADER_STAGE_ALL;

    return flags;
}

// Clamp descriptor-buffer alignment for 32-bit byte offsets.
u32 GetDescriptorBufferOffsetAlignmentBytes(const VulkanContext& context)noexcept{
    const VkDeviceSize alignment = context.descriptorBufferProperties.descriptorBufferOffsetAlignment;
    return (alignment == 0 || alignment > UINT32_MAX) ? 1u : static_cast<u32>(alignment);
}

Expected<VkPipelineMultisampleStateCreateInfo> ConfigurePipelineMultisampleState(
    const u32 sampleCount,
    const bool alphaToCoverageEnable,
    TStringView operationName
){
    auto state = MakeVkStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    if(!IsSupportedSampleCount(sampleCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: sample count {} is unsupported"), operationName, sampleCount);
        return MakeUnexpected(Failure{});
    }
    state.rasterizationSamples = GetSampleCountFlagBits(sampleCount);
    state.sampleShadingEnable = VK_FALSE;
    state.alphaToCoverageEnable = alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
    return state;
}

void ConfigurePipelineDepthStencilState(
    const DepthStencilState& state,
    PipelineStencilFaceMode::Enum stencilFaceMode,
    VkPipelineDepthStencilStateCreateInfo& outState
)noexcept{
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

Expected<GraphicsPipelineFixedState> BuildGraphicsPipelineFixedState(
    const FramebufferInfo& fbinfo,
    const RenderState& renderState,
    const PipelineStencilFaceMode::Enum stencilFaceMode,
    const VkDynamicState* dynamicStates,
    const u32 dynamicStateCount,
    TStringView operationName,
    Alloc::ScratchArena& scratchArena
){
    GraphicsPipelineFixedState state(scratchArena);
    state.viewportState = MakeVkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    state.viewportState.viewportCount = 1;
    state.viewportState.scissorCount = 1;

    const auto multisampling = ConfigurePipelineMultisampleState(
        fbinfo.sampleCount,
        renderState.blendState.alphaToCoverageEnable,
        operationName
    );
    if(!multisampling)
        return MakeUnexpected(Failure{});
    state.multisampling = *multisampling;

    ConfigurePipelineDepthStencilState(renderState.depthStencilState, stencilFaceMode, state.depthStencil);

    state.colorBlending = BuildPipelineColorBlendState(fbinfo, renderState.blendState, state.blendAttachments);

    state.dynamicState = MakeVkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    state.dynamicState.dynamicStateCount = dynamicStateCount;
    state.dynamicState.pDynamicStates = dynamicStates;

    auto rendering = BuildPipelineRenderingInfo(fbinfo, operationName, scratchArena);
    if(!rendering)
        return MakeUnexpected(Failure{});
    state.renderingInfo = rendering->info;
    state.colorFormats = Move(rendering->colorFormats);
    return state;
}

Expected<PipelineRenderingInfo> BuildPipelineRenderingInfo(
    const FramebufferInfo& fbinfo,
    TStringView operationName,
    Alloc::ScratchArena& scratchArena
){
    PipelineRenderingInfo rendering(scratchArena);
    rendering.colorFormats.reserve(fbinfo.colorFormats.size());
    for(u32 i = 0u; i < static_cast<u32>(fbinfo.colorFormats.size()); ++i){
        const VkFormat vkFormat = ConvertFormat(fbinfo.colorFormats[i]);
        if(vkFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: color attachment format {} is unsupported"), operationName, i);
            return MakeUnexpected(Failure{});
        }
        if(!VulkanDetail::IsPipelineColorAttachmentFormatClassValid(fbinfo.colorFormats[i])){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create {}: color attachment format {} has depth or stencil aspects"),
                operationName,
                i
            );
            return MakeUnexpected(Failure{});
        }
        rendering.colorFormats.push_back(vkFormat);
    }

    rendering.info = MakeVkStruct<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.info.colorAttachmentCount = static_cast<u32>(rendering.colorFormats.size());
    rendering.info.pColorAttachmentFormats = rendering.colorFormats.data();
    if(fbinfo.depthFormat != Format::UNKNOWN){
        const VkFormat vkDepthFormat = ConvertFormat(fbinfo.depthFormat);
        if(vkDepthFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: depth/stencil attachment format is unsupported"), operationName);
            return MakeUnexpected(Failure{});
        }
        const FormatInfo& depthFormatInfo = GetFormatInfo(fbinfo.depthFormat);
        if(!depthFormatInfo.hasDepth && !depthFormatInfo.hasStencil){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: depth/stencil attachment format has no depth or stencil aspect"), operationName);
            return MakeUnexpected(Failure{});
        }
        if(depthFormatInfo.hasDepth)
            rendering.info.depthAttachmentFormat = vkDepthFormat;
        if(depthFormatInfo.hasStencil)
            rendering.info.stencilAttachmentFormat = vkDepthFormat;
    }

    return rendering;
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
constexpr DescriptorBufferSegmentKind::Enum GetDescriptorBufferSegmentKind(ResourceType::Enum type)noexcept{
    return type == ResourceType::Sampler ? DescriptorBufferSegmentKind::Sampler : DescriptorBufferSegmentKind::Resource;
}

bool IsDescriptorBufferBackendReady(const VulkanContext& context){
    return context.descriptorBufferManager
        && context.descriptorBufferManager->isEnabled()
    ;
}

Expected<DescriptorBufferSegmentKind::Enum> TryResolveBindlessDescriptorBufferLayout(
    const BindlessLayoutDesc& desc
)noexcept{
    auto segmentKind = DescriptorBufferSegmentKind::None;
    bool hasDescriptors = false;

    for(const auto& item : desc.registerSpaces){
        if(!IsSupportedDescriptorBindingType(item.type))
            return MakeUnexpected(Failure{});

        const DescriptorBufferSegmentKind::Enum itemSegmentKind = GetDescriptorBufferSegmentKind(item.type);
        if(!hasDescriptors){
            segmentKind = itemSegmentKind;
            hasDescriptors = true;
        }
        else if(segmentKind != itemSegmentKind)
            return MakeUnexpected(Failure{});
    }

    if(!hasDescriptors)
        return MakeUnexpected(Failure{});
    return segmentKind;
}

bool ValidateDescriptorBufferBindingFootprint(
    DescriptorBufferManager& manager,
    const VkDescriptorType descriptorType,
    const u32 descriptorCount,
    const VkDeviceSize setSizeBytes,
    const VkDeviceSize bindingOffsetBytes,
    const u32 bindingSlot,
    TStringView operationName
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

// Global heap resource type; TLAS uses its own immutable one-descriptor layout.
constexpr bool IsBindlessRegisterSpaceType(ResourceType::Enum type)noexcept{
    return IsSupportedDescriptorBindingType(type);
}

constexpr u32 NormalizeBindlessDescriptorCapacity(const u32 capacity)noexcept{
    return capacity > 0 ? capacity : 1u;
}

u32 GetPushConstantByteSize(const BindingLayoutDesc& desc)noexcept{
    u32 pushConstantByteSize = 0;
    for(const auto& item : desc.bindings){
        if(item.type == ResourceType::PushConstants)
            pushConstantByteSize = Max<u32>(pushConstantByteSize, item.size);
    }
    return pushConstantByteSize;
}

bool ValidatePushConstantByteSize(const VulkanContext& context, const u32 byteSize, TStringView operationName){
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

Expected<VkPipelineLayout> CreatePipelineLayout(
    const VulkanContext& context,
    const VkDescriptorSetLayout* setLayouts,
    const u32 setLayoutCount,
    const u32 pushConstantByteSize,
    TStringView operationName
){
    VkResult res = VK_SUCCESS;

    VkPipelineLayout layout = VK_NULL_HANDLE;

    VkPushConstantRange pushConstantRange = {};
    if(pushConstantByteSize > 0){
        if(!ValidatePushConstantByteSize(context, pushConstantByteSize, operationName))
            return MakeUnexpected(Failure{});

        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;
    }

    auto layoutInfo = MakeVkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.setLayoutCount = setLayoutCount;
    layoutInfo.pSetLayouts = setLayoutCount > 0 ? setLayouts : nullptr;
    layoutInfo.pushConstantRangeCount = pushConstantByteSize > 0 ? 1u : 0u;
    layoutInfo.pPushConstantRanges = pushConstantByteSize > 0 ? &pushConstantRange : nullptr;

    res = context.deviceDispatch.vkCreatePipelineLayout(context.device, &layoutInfo, context.allocationCallbacks, &layout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for {}: {}"), operationName, ResultToString(res));
        layout = VK_NULL_HANDLE;
        return MakeUnexpected(Failure{});
    }

    return layout;
}

VkSamplerAddressMode ConvertSamplerAddressMode(const SamplerAddressMode::Enum mode)noexcept{
    switch(mode){
    case SamplerAddressMode::Clamp:      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case SamplerAddressMode::Wrap:       return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::Border:     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case SamplerAddressMode::Mirror:     return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::MirrorOnce: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    default:                             return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

VkSamplerCreateInfo BuildSamplerCreateInfo(const SamplerDesc& desc)noexcept{
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



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayout::BindingLayout(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
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

    for(usize i = 0u; i < desc.bindings.size(); ++i){
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

    const auto pipelineLayout = VulkanDetail::CreatePipelineLayout(
            m_context,
            nullptr,
            0u,
            pushConstantByteSize,
            NWB_TEXT("create binding layout")
        );
    if(!pipelineLayout){
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }

    layout->m_pipelineLayout = *pipelineLayout;

    // Push constants are pipeline-layout state, not descriptor-set state.
    layout->m_descriptorBufferCompatible = true;

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    if(desc.descriptorSetIndex == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: an explicit descriptor-set index is required."));
        return nullptr;
    }
    if((static_cast<u16>(desc.visibility) & ~static_cast<u16>(ShaderType::All)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: shader visibility contains unknown bits."));
        return nullptr;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);

    auto* layout = NewArenaObject<BindingLayout>(m_context.objectArena, m_context);
    layout->m_isBindless = true;
    layout->m_bindlessDesc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchArena> bindings{scratchArena};
    bindings.reserve(desc.registerSpaces.size());
    HashSet<u32, Alloc::ScratchArena, Hasher<u32>, EqualTo<u32>> registerSpaceSlots(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        scratchArena
    );
    registerSpaceSlots.reserve(desc.registerSpaces.size());

    const u32 maxCapacity = VulkanDetail::NormalizeBindlessDescriptorCapacity(desc.maxCapacity);
    for(usize i = 0u; i < desc.registerSpaces.size(); ++i){
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

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer backend is unavailable."));
        DestroyArenaObject(m_context.objectArena, layout);
        return nullptr;
    }
    const auto descriptorBufferSegmentKind = VulkanDetail::TryResolveBindlessDescriptorBufferLayout(desc);
    if(!descriptorBufferSegmentKind){
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

    NWB_ASSERT(*descriptorBufferSegmentKind != DescriptorBufferSegmentKind::None);

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
    layout->m_descriptorBufferSegmentKind = *descriptorBufferSegmentKind;
    layout->m_descriptorBufferCompatible = true;

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

