// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <core/graphics/spirv_entry_point.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_shader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool IsValidSpirvBytecodeShape(const void* binary, const usize binarySize)noexcept{
    return binary && binarySize != 0u && (binarySize & s_SpirvWordAlignmentMask) == 0u;
}

template<typename WordVector>
[[nodiscard]] bool AssignValidatedSpirvWords(const void* binary, const usize binarySize, WordVector& outWords){
    outWords.clear();

    if(!IsValidSpirvBytecodeShape(binary, binarySize))
        return false;

    const usize wordCount = binarySize / sizeof(u32);
    outWords.resize(wordCount);
    NWB_MEMCPY(outWords.data(), binarySize, binary, binarySize);

    if(!IsValidSpirvModuleWords(outWords.data(), outWords.size())){
        outWords.clear();
        return false;
    }

    return true;
}

template<typename WordVector>
[[nodiscard]] inline usize SpirvByteSize(const WordVector& words)noexcept{
    return words.size() * sizeof(u32);
}

inline bool ComputeVertexAttributeBytes(const VertexAttributeDesc& attr, const u32 attributeIndex, u64& outBytes){
    outBytes = 0;

    const FormatInfo& formatInfo = GetFormatInfo(attr.format);
    if(formatInfo.bytesPerBlock == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has a zero-size vertex format"), attributeIndex);
        return false;
    }
    if(attr.arraySize > Limit<u64>::s_Max / static_cast<u64>(formatInfo.bytesPerBlock)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} byte size overflows"), attributeIndex);
        return false;
    }

    outBytes = static_cast<u64>(formatInfo.bytesPerBlock) * static_cast<u64>(attr.arraySize);
    if(outBytes == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has zero byte size"), attributeIndex);
        return false;
    }

    return true;
}

inline bool ResolveShaderEntryPoint(
    const u32* words,
    const usize wordCount,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    const char* errorContext,
    GraphicsString& outEntryPointName
){
    const SpirvEntryPointLookupResult::Enum lookupResult = ResolveSpirvEntryPointName(words, wordCount, entryName, shaderType, outEntryPointName);
    switch(lookupResult){
    case SpirvEntryPointLookupResult::Found:
        return true;

    case SpirvEntryPointLookupResult::NotFound:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Shader entry point '{}' (stage=0x{:x}) was not found in SPIR-V for {}")
            , StringConvert(entryName)
            , static_cast<u32>(shaderType)
            , StringConvert(errorContext)
        );
        return false;

    case SpirvEntryPointLookupResult::InvalidSpirv:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid SPIR-V while resolving shader entry point '{}' (stage=0x{:x}) for {}")
            , StringConvert(entryName)
            , static_cast<u32>(shaderType)
            , StringConvert(errorContext)
        );
        return false;

    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Sampler::Sampler(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_context(context)
{}
Sampler::~Sampler(){
    if(m_sampler != VK_NULL_HANDLE){
        vkDestroySampler(m_context.device, m_sampler, m_context.allocationCallbacks);
        m_sampler = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Shader::Shader(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_spirvWords(context.objectArena)
    , m_entryPointName(context.objectArena)
    , m_context(context)
{}
Shader::~Shader(){
    if(m_shaderModule != VK_NULL_HANDLE){
        vkDestroyShaderModule(m_context.device, m_shaderModule, m_context.allocationCallbacks);
        m_shaderModule = VK_NULL_HANDLE;
    }
}

ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, usize binarySize){
    auto* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc = d;
    if(!__hidden_vulkan_shader::AssignValidatedSpirvWords(binary, binarySize, shader->m_spirvWords)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader bytecode payload"));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    if(!__hidden_vulkan_shader::ResolveShaderEntryPoint(shader->m_spirvWords.data(), shader->m_spirvWords.size(), d.entryName, d.shaderType, "standalone shader", shader->m_entryPointName)){
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = binarySize;
    createInfo.pCode = shader->m_spirvWords.data();

    const VkResult res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }
    return ShaderHandle(shader, ShaderHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

InputLayout::InputLayout(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_attributes(context.objectArena)
    , m_bindings(context.objectArena)
    , m_vkAttributes(context.objectArena)
    , m_context(context)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, Shader*){
    if(attributeCount > 0 && !d){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute data is null for {} attributes"), attributeCount);
        return nullptr;
    }
    const auto& limits = m_context.physicalDeviceProperties.limits;
    if(attributeCount > limits.maxVertexInputAttributes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute count {} exceeds device limit {}")
            , attributeCount
            , limits.maxVertexInputAttributes
        );
        return nullptr;
    }

    struct VertexBindingBuildInfo{
        u64 requiredStride = 0;
        u32 explicitStride = 0;
        bool hasExplicitStride = false;
        bool isInstanced = false;
    };

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_InputLayoutArena);
    HashMap<u32, VertexBindingBuildInfo, Hasher<u32>, EqualTo<u32>, Alloc::ScratchArena> bindingInfos(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        scratchArena
    );
    bindingInfos.reserve(attributeCount);

    for(u32 i = 0; i < attributeCount; ++i){
        const VertexAttributeDesc& attr = d[i];
        if(ConvertFormat(attr.format) == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has unsupported vertex format"), i);
            return nullptr;
        }
        if(attr.arraySize == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} has zero array size"), i);
            return nullptr;
        }
        if(attr.bufferIndex >= limits.maxVertexInputBindings){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} buffer index {} exceeds device binding limit {}")
                , i
                , attr.bufferIndex
                , limits.maxVertexInputBindings
            );
            return nullptr;
        }
        if(attr.offset > limits.maxVertexInputAttributeOffset){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} offset {} exceeds device limit {}")
                , i
                , attr.offset
                , limits.maxVertexInputAttributeOffset
            );
            return nullptr;
        }

        u64 attributeBytes = 0;
        if(!__hidden_vulkan_shader::ComputeVertexAttributeBytes(attr, i, attributeBytes))
            return nullptr;
        if(static_cast<u64>(attr.offset) > Limit<u64>::s_Max - attributeBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} offset plus size overflows"), i);
            return nullptr;
        }

        const u64 attributeEnd = static_cast<u64>(attr.offset) + attributeBytes;
        if(attr.elementStride != 0 && attributeEnd > static_cast<u64>(attr.elementStride)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} extent {} exceeds explicit stride {}")
                , i
                , attributeEnd
                , attr.elementStride
            );
            return nullptr;
        }

        auto bindingInfoInsert = bindingInfos.try_emplace(attr.bufferIndex);
        VertexBindingBuildInfo& bindingInfo = bindingInfoInsert.first.value();
        if(bindingInfoInsert.second)
            bindingInfo.isInstanced = attr.isInstanced;
        if(bindingInfo.isInstanced != attr.isInstanced){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} mixes vertex and instance input rates"), attr.bufferIndex);
            return nullptr;
        }

        bindingInfo.requiredStride = Max(bindingInfo.requiredStride, attributeEnd);
        if(attr.elementStride != 0){
            if(bindingInfo.hasExplicitStride && bindingInfo.explicitStride != attr.elementStride){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} uses conflicting explicit strides {} and {}")
                    , attr.bufferIndex
                    , bindingInfo.explicitStride
                    , attr.elementStride
                );
                return nullptr;
            }

            bindingInfo.explicitStride = attr.elementStride;
            bindingInfo.hasExplicitStride = true;
        }
    }

    for(const auto& [bufferIndex, bindingInfo] : bindingInfos){
        const u64 stride = bindingInfo.hasExplicitStride ? static_cast<u64>(bindingInfo.explicitStride) : bindingInfo.requiredStride;
        if(stride == 0 || stride > limits.maxVertexInputBindingStride){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} stride {} is outside device limit {}")
                , bufferIndex
                , stride
                , limits.maxVertexInputBindingStride
            );
            return nullptr;
        }
        if(bindingInfo.requiredStride > stride){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: buffer binding {} requires {} bytes but explicit stride is {}")
                , bufferIndex
                , bindingInfo.requiredStride
                , stride
            );
            return nullptr;
        }
    }

    auto* layout = NewArenaObject<InputLayout>(m_context.objectArena, m_context);
    if(attributeCount > 0){
        static_assert(IsTriviallyCopyable_V<VertexAttributeDesc>, "vertex attribute descriptors must be trivially copyable");
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
    auto fillVkAttribute = [&](usize i){
        const auto& attr = layout->m_attributes[i];

        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = static_cast<u32>(i);
        vkAttr.binding = attr.bufferIndex;
        vkAttr.format = ConvertFormat(attr.format);
        vkAttr.offset = attr.offset;

        layout->m_vkAttributes[i] = vkAttr;
    };

    if(taskPool().isParallelEnabled() && attributeCount >= s_ParallelInputLayoutThreshold)
        scheduleParallelFor(static_cast<usize>(0), attributeCount, s_InputLayoutGrainSize, fillVkAttribute);
    else{
        for(usize i = 0; i < attributeCount; ++i)
            fillVkAttribute(i);
    }

    return InputLayoutHandle(layout, InputLayoutHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Framebuffer::Framebuffer(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_resources(context.objectArena)
    , m_context(context)
{}
Framebuffer::~Framebuffer(){
    if(m_framebuffer != VK_NULL_HANDLE){
        vkDestroyFramebuffer(m_context.device, m_framebuffer, m_context.allocationCallbacks);
        m_framebuffer = VK_NULL_HANDLE;
    }

    if(m_renderPass != VK_NULL_HANDLE){
        vkDestroyRenderPass(m_context.device, m_renderPass, m_context.allocationCallbacks);
        m_renderPass = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

