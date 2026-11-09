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

void DestroyPipelineAndOwnedLayout(const VulkanContext& context, VkPipeline& pipeline, VkPipelineLayout& pipelineLayout, b8& ownsPipelineLayout){
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

b8 IsDescriptorBufferBackendReady(const VulkanContext& context)noexcept{
    return context.descriptorBufferManager && context.descriptorBufferManager->isEnabled();
}

Expected<DescriptorBufferSegmentKind::Enum> TryResolveBindlessDescriptorBufferLayout(const BindlessLayoutDesc& desc)noexcept{
    auto segmentKind = DescriptorBufferSegmentKind::None;
    b8 hasDescriptors = false;

    for(const auto& item : desc.registerSpaces){
        FOUNDATION_ASSERT(IsSupportedDescriptorBindingType(item.type));

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

b8 ValidateDescriptorBufferBindingFootprint(
    DescriptorBufferManager& manager,
    const VkDescriptorType descriptorType,
    const u32 descriptorCount,
    const VkDeviceSize setSizeBytes,
    const VkDeviceSize bindingOffsetBytes,
    const u32 bindingSlot,
    TStringView operationName
){
    FOUNDATION_ASSERT(descriptorCount != 0u && setSizeBytes != 0u && setSizeBytes <= UINT32_MAX);
    const u32 descriptorSize = manager.getDescriptorSize(descriptorType);
    if(descriptorSize == 0u || bindingOffsetBytes >= setSizeBytes){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create {}: descriptor-buffer binding {} has an invalid footprint."), operationName, bindingSlot);
        return false;
    }

    const VkDeviceSize availableBytes = setSizeBytes - bindingOffsetBytes;
    if(static_cast<VkDeviceSize>(descriptorCount) > availableBytes / descriptorSize){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create {}: descriptor-buffer binding {} exceeds its {}-byte set block."), operationName, bindingSlot, setSizeBytes);
        return false;
    }

    return true;
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

b8 ValidatePushConstantByteSize(const VulkanContext& context, const u32 byteSize, TStringView operationName){
    if(byteSize == 0){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: push constant size is zero"), operationName);
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed push constant operation: size is zero"));
        return false;
    }
    if((byteSize & s_BufferAlignmentMask) != 0u){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: push constant size is not 4-byte aligned"), operationName);
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed push constant operation: size is not 4-byte aligned"));
        return false;
    }
    if(byteSize > context.physicalDeviceProperties.limits.maxPushConstantsSize){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: push constant size {} exceeds device limit {}"), operationName, byteSize, context.physicalDeviceProperties.limits.maxPushConstantsSize);
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed push constant operation: size exceeds device limit"));
        return false;
    }
    return true;
}

Expected<VkPipelineLayout> CreatePipelineLayout(const VulkanContext& context, const Span<const VkDescriptorSetLayout> setLayouts, const u32 pushConstantByteSize, TStringView operationName){
    VkResult res = VK_SUCCESS;

    VkPipelineLayout layout = VK_NULL_HANDLE;

    VkPushConstantRange pushConstantRange = {};
    if(pushConstantByteSize > 0){
        FOUNDATION_ASSERT(IsPushConstantByteSizeValid(pushConstantByteSize, context.physicalDeviceProperties.limits.maxPushConstantsSize));

        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;
    }

    auto layoutInfo = MakeVkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();
    layoutInfo.pushConstantRangeCount = pushConstantByteSize > 0 ? 1u : 0u;
    layoutInfo.pPushConstantRanges = pushConstantByteSize > 0 ? &pushConstantRange : nullptr;

    res = context.deviceDispatch.vkCreatePipelineLayout(context.device, &layoutInfo, context.allocationCallbacks, &layout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create pipeline layout for {}: {}"), operationName, ResultToString(res));
        layout = VK_NULL_HANDLE;
        return MakeUnexpected(Failure{});
    }

    return layout;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayout::BindingLayout(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_desc(context.objectArena)
    , m_descriptorSetLayouts(context.objectArena)
    , m_descriptorBufferBindingOffsets(0, Hasher<u32>(), EqualTo<u32>(), context.objectArena)
    , m_context(context)
{}

BindingLayout::~BindingLayout()noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<BindingLayoutHandle> Device::createBindingLayout(const BindingLayoutDesc& desc){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("create binding layout");
    if(desc.bindings.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create descriptor set layout: binding count exceeds Vulkan limit"));
        return MakeUnexpected(Failure{});
    }

    if(!VulkanDetail::IsDescriptorBufferBackendReady(m_context)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create binding layout: descriptor-buffer backend is unavailable."));
        return MakeUnexpected(Failure{});
    }

    BindingLayoutHandle layout(NewArenaObject<BindingLayout>(m_context.objectArena, m_context), BindingLayoutHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!layout)
        return MakeUnexpected(Failure{});
    layout->m_desc = desc;

    for(usize i = 0u; i < desc.bindings.size(); ++i){
        const auto& item = desc.bindings[i];
        if(item.type == ResourceType::None)
            continue;
        if(item.type == ResourceType::PushConstants){
            if(!VulkanDetail::ValidatePushConstantByteSize(m_context, item.size, s_OperationName)){
                return MakeUnexpected(Failure{});
            }
            continue;
        }
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create binding layout: pipeline-local resource bindings are retired; register slot {} in GpuDescriptorHeap and select it through push constants."), item.slot);
        return MakeUnexpected(Failure{});
    }
    const u32 pushConstantByteSize = VulkanDetail::GetPushConstantByteSize(desc);
    layout->m_pushConstantByteSize = pushConstantByteSize;

    const auto pipelineLayout = VulkanDetail::CreatePipelineLayout(m_context, {}, pushConstantByteSize, s_OperationName);
    if(!pipelineLayout){
        return MakeUnexpected(Failure{});
    }

    layout->m_pipelineLayout = *pipelineLayout;

    // Push constants are pipeline-layout state, not descriptor-set state.
    layout->m_descriptorBufferCompatible = true;

    return layout;
}

Expected<BindingLayoutHandle> Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    if(desc.descriptorSetIndex == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create bindless layout: an explicit descriptor-set index is required."));
        return MakeUnexpected(Failure{});
    }
    if((static_cast<u16>(desc.visibility) & ~static_cast<u16>(ShaderType::All)) != 0u){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create bindless layout: shader visibility contains unknown bits."));
        return MakeUnexpected(Failure{});
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);

    BindingLayoutHandle layout(NewArenaObject<BindingLayout>(m_context.objectArena, m_context), BindingLayoutHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!layout)
        return MakeUnexpected(Failure{});
    layout->m_isBindless = true;
    layout->m_bindlessDesc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchArena> bindings{scratchArena};
    bindings.reserve(desc.registerSpaces.size());
    HashSet<u32, Alloc::ScratchArena, Hasher<u32>, EqualTo<u32>> registerSpaceSlots(0, Hasher<u32>(), EqualTo<u32>(), scratchArena);
    registerSpaceSlots.reserve(desc.registerSpaces.size());

    const u32 maxCapacity = VulkanDetail::NormalizeBindlessDescriptorCapacity(desc.maxCapacity);
    for(usize i = 0u; i < desc.registerSpaces.size(); ++i){
        const auto& item = desc.registerSpaces[i];
        if(!VulkanDetail::IsSupportedDescriptorBindingType(item.type) || (item.type == ResourceType::RayTracingAccelStruct && desc.layoutType != BindlessLayoutType::Immutable)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create bindless layout: register space slot {} has unsupported resource type {}"), item.slot, static_cast<u32>(item.type));
            return MakeUnexpected(Failure{});
        }
        const auto slotInsert = registerSpaceSlots.insert(item.slot);
        if(!slotInsert.second){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create bindless layout: duplicate register space slot {}"), item.slot);
            return MakeUnexpected(Failure{});
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
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer backend is unavailable."));
        return MakeUnexpected(Failure{});
    }
    const auto descriptorBufferSegmentKind = VulkanDetail::TryResolveBindlessDescriptorBufferLayout(desc);
    if(!descriptorBufferSegmentKind){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer layouts cannot mix sampler and resource bindings."));
        return MakeUnexpected(Failure{});
    }

    auto layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    layout->m_descriptorSetLayouts.reserve(1u);

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    res = m_context.deviceDispatch.vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to create bindless descriptor set layout: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }
    layout->m_descriptorSetLayouts.push_back(setLayout);

    FOUNDATION_ASSERT(*descriptorBufferSegmentKind != DescriptorBufferSegmentKind::None);

    const VkDescriptorSetLayout descriptorSetLayout = layout->m_descriptorSetLayouts[0];
    VkDeviceSize setSizeBytes = 0;
    m_context.deviceDispatch.vkGetDescriptorSetLayoutSizeEXT(m_context.device, descriptorSetLayout, &setSizeBytes);
    if(setSizeBytes == 0u || setSizeBytes > UINT32_MAX){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create bindless layout: descriptor-buffer set size is invalid."));
        return MakeUnexpected(Failure{});
    }

    layout->m_descriptorBufferBindingOffsets.reserve(desc.registerSpaces.size());
    for(const auto& item : desc.registerSpaces){
        VkDeviceSize bindingOffsetBytes = 0;
        m_context.deviceDispatch.vkGetDescriptorSetLayoutBindingOffsetEXT(m_context.device, descriptorSetLayout, item.slot, &bindingOffsetBytes);
        if(!VulkanDetail::ValidateDescriptorBufferBindingFootprint(*m_context.descriptorBufferManager, VulkanDetail::ConvertDescriptorType(item.type), maxCapacity, setSizeBytes, bindingOffsetBytes, item.slot, FOUNDATION_TEXT("bindless layout"))){
            return MakeUnexpected(Failure{});
        }
        layout->m_descriptorBufferBindingOffsets.insert_or_assign(item.slot, static_cast<u32>(bindingOffsetBytes));
    }
    layout->m_descriptorBufferSetSizeBytes = static_cast<u32>(setSizeBytes);
    layout->m_descriptorBufferSegmentKind = *descriptorBufferSegmentKind;
    layout->m_descriptorBufferCompatible = true;

    return layout;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

