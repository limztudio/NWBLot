// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_input_layout.h"

#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_input_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Expected<u64> ComputeVertexAttributeBytes(const VertexAttributeDesc& attr, const u32 attributeIndex){

    const FormatInfo& formatInfo = GetFormatInfo(attr.format);
    if(formatInfo.bytesPerBlock == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has a zero-size vertex format"), attributeIndex);
        return MakeUnexpected(Failure{});
    }
    if(attr.arraySize > Limit<u64>::s_Max / static_cast<u64>(formatInfo.bytesPerBlock)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} byte size overflows"), attributeIndex);
        return MakeUnexpected(Failure{});
    }

    const u64 attributeBytes = static_cast<u64>(formatInfo.bytesPerBlock) * static_cast<u64>(attr.arraySize);
    if(attributeBytes == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has zero byte size"), attributeIndex);
        return MakeUnexpected(Failure{});
    }

    return attributeBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayout::InputLayout(const VulkanContext& context)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_attributes(context.objectArena)
    , m_bindings(context.objectArena)
    , m_vkAttributes(context.objectArena)
    , m_context(context)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<InputLayoutHandle> Device::createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, Shader*){
    if(attributeCount > 0 && !d){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute data is null for {} attributes"), attributeCount);
        return MakeUnexpected(Failure{});
    }
    const auto& limits = m_context.physicalDeviceProperties.limits;
    if(attributeCount > limits.maxVertexInputAttributes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute count {} exceeds device limit {}"), attributeCount, limits.maxVertexInputAttributes);
        return MakeUnexpected(Failure{});
    }

    struct VertexBindingBuildInfo{
        u64 requiredStride = 0;
        u32 explicitStride = 0;
        bool hasExplicitStride = false;
        bool isInstanced = false;
    };

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_InputLayoutArena);
    HashMap<u32, VertexBindingBuildInfo, Alloc::ScratchArena, Hasher<u32>, EqualTo<u32>> bindingInfos(0, Hasher<u32>(), EqualTo<u32>(), scratchArena);
    bindingInfos.reserve(attributeCount);

    for(u32 i = 0u; i < attributeCount; ++i){
        const VertexAttributeDesc& attr = d[i];
        if(ConvertFormat(attr.format) == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has unsupported vertex format"), i);
            return MakeUnexpected(Failure{});
        }
        if(attr.arraySize == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has zero array size"), i);
            return MakeUnexpected(Failure{});
        }
        if(attr.bufferIndex >= limits.maxVertexInputBindings){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} buffer index {} exceeds device binding limit {}"), i, attr.bufferIndex, limits.maxVertexInputBindings);
            return MakeUnexpected(Failure{});
        }
        if(attr.offset > limits.maxVertexInputAttributeOffset){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} offset {} exceeds device limit {}"), i, attr.offset, limits.maxVertexInputAttributeOffset);
            return MakeUnexpected(Failure{});
        }

        const auto attributeBytes = __hidden_vulkan_input_layout::ComputeVertexAttributeBytes(attr, i);
        if(!attributeBytes)
            return MakeUnexpected(Failure{});
        if(static_cast<u64>(attr.offset) > Limit<u64>::s_Max - *attributeBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} offset plus size overflows"), i);
            return MakeUnexpected(Failure{});
        }

        const u64 attributeEnd = static_cast<u64>(attr.offset) + *attributeBytes;
        if(attr.elementStride != 0 && attributeEnd > static_cast<u64>(attr.elementStride)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} extent {} exceeds explicit stride {}"), i, attributeEnd, attr.elementStride);
            return MakeUnexpected(Failure{});
        }

        auto bindingInfoInsert = bindingInfos.try_emplace(attr.bufferIndex);
        VertexBindingBuildInfo& bindingInfo = bindingInfoInsert.first.value();
        if(bindingInfoInsert.second)
            bindingInfo.isInstanced = attr.isInstanced;
        if(bindingInfo.isInstanced != attr.isInstanced){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} mixes vertex and instance input rates"), attr.bufferIndex);
            return MakeUnexpected(Failure{});
        }

        bindingInfo.requiredStride = Max(bindingInfo.requiredStride, attributeEnd);
        if(attr.elementStride != 0){
            if(bindingInfo.hasExplicitStride && bindingInfo.explicitStride != attr.elementStride){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} uses conflicting explicit strides {} and {}"), attr.bufferIndex, bindingInfo.explicitStride, attr.elementStride);
                return MakeUnexpected(Failure{});
            }

            bindingInfo.explicitStride = attr.elementStride;
            bindingInfo.hasExplicitStride = true;
        }
    }

    for(const auto& [bufferIndex, bindingInfo] : bindingInfos){
        const u64 stride = bindingInfo.hasExplicitStride ? static_cast<u64>(bindingInfo.explicitStride) : bindingInfo.requiredStride;
        if(stride == 0 || stride > limits.maxVertexInputBindingStride){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} stride {} is outside device limit {}"), bufferIndex, stride, limits.maxVertexInputBindingStride);
            return MakeUnexpected(Failure{});
        }
        if(bindingInfo.requiredStride > stride){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} requires {} bytes but explicit stride is {}"), bufferIndex, bindingInfo.requiredStride, stride);
            return MakeUnexpected(Failure{});
        }
    }

    InputLayoutHandle layout(NewArenaObject<InputLayout>(m_context.objectArena, m_context), InputLayoutHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!layout)
        return MakeUnexpected(Failure{});
    if(attributeCount > 0){
        static_assert(IsTriviallyCopyable_V<VertexAttributeDesc>, "vertex attribute descriptors must be trivially copyable");
        layout->m_attributes.reserve(attributeCount);
        layout->m_attributes.assign(d, d + attributeCount);
    }

    layout->m_bindings.reserve(bindingInfos.size());
    for(const auto& [bufferIndex, bindingInfo] : bindingInfos){
        VkVertexInputBindingDescription binding = {};
        binding.binding = bufferIndex;
        binding.stride = bindingInfo.hasExplicitStride ? bindingInfo.explicitStride : static_cast<u32>(bindingInfo.requiredStride);
        binding.inputRate = bindingInfo.isInstanced ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        layout->m_bindings.push_back(binding);
    }

    layout->m_vkAttributes.resize(attributeCount);
    auto fillVkAttribute = [&](usize i)noexcept{
        const auto& attr = layout->m_attributes[i];

        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = static_cast<u32>(i);
        vkAttr.binding = attr.bufferIndex;
        vkAttr.format = ConvertFormat(attr.format);
        vkAttr.offset = attr.offset;

        layout->m_vkAttributes[i] = vkAttr;
    };

    if(taskScheduler().isParallelEnabled() && attributeCount >= s_ParallelInputLayoutThreshold){
        const auto parallelWork = taskScheduler().parallelFor(static_cast<usize>(0), attributeCount, s_InputLayoutGrainSize, fillVkAttribute);
        if(!parallelWork){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: input layout preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
            return MakeUnexpected(Failure{});
        }
    }
    else{
        for(usize i = 0u; i < attributeCount; ++i)
            fillVkAttribute(i);
    }

    return layout;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

