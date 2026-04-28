// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_shader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EntryPointLookupResult{
    enum Enum : u8{
        Found,
        NotFound,
        InvalidSpirv,
    };
};

inline constexpr u32 s_SpirvMagic = 0x07230203u;
inline constexpr u16 s_OpEntryPoint = 15u;
inline constexpr usize s_SpirvHeaderWords = 5;

using SpirvWordVector = Vector<u32, Alloc::ScratchAllocator<u32>>;

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

inline bool CopySpirvWords(const void* binary, const usize binarySize, SpirvWordVector& outWords){
    outWords.clear();

    if(!binary || binarySize == 0 || (binarySize & 3) != 0)
        return false;

    const usize wordCount = binarySize / sizeof(u32);
    outWords.resize(wordCount);
    NWB_MEMCPY(outWords.data(), binarySize, binary, binarySize);
    return true;
}

inline ShaderType::Mask ConvertExecutionModel(const u32 executionModel){
    switch(executionModel){
    case 0u: return ShaderType::Vertex;
    case 1u: return ShaderType::Hull;
    case 2u: return ShaderType::Domain;
    case 3u: return ShaderType::Geometry;
    case 4u: return ShaderType::Pixel;
    case 5u: return ShaderType::Compute;
    case 5267u: return ShaderType::Amplification;
    case 5268u: return ShaderType::Mesh;
    case 5313u: return ShaderType::RayGeneration;
    case 5314u: return ShaderType::Intersection;
    case 5315u: return ShaderType::AnyHit;
    case 5316u: return ShaderType::ClosestHit;
    case 5317u: return ShaderType::Miss;
    case 5318u: return ShaderType::Callable;
    case 5364u: return ShaderType::Amplification;
    case 5365u: return ShaderType::Mesh;
    default: return ShaderType::None;
    }
}

inline EntryPointLookupResult::Enum ResolveEntryPointName(
    const u32* words,
    const usize wordCount,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    AString& outEntryPointName
){
    outEntryPointName.clear();

    if(entryName.empty() || shaderType == ShaderType::None)
        return EntryPointLookupResult::NotFound;

    if(!words || wordCount < s_SpirvHeaderWords)
        return EntryPointLookupResult::InvalidSpirv;

    if(words[0] != s_SpirvMagic)
        return EntryPointLookupResult::InvalidSpirv;

    for(usize instructionIndex = s_SpirvHeaderWords; instructionIndex < wordCount; ){
        const u32 instruction = words[instructionIndex];
        const u16 opcode = static_cast<u16>(instruction & 0xFFFFu);
        const u16 instructionWordCount = static_cast<u16>(instruction >> 16u);
        if(instructionWordCount == 0)
            return EntryPointLookupResult::InvalidSpirv;

        if(static_cast<usize>(instructionWordCount) > wordCount - instructionIndex)
            return EntryPointLookupResult::InvalidSpirv;

        const usize nextInstructionIndex = instructionIndex + instructionWordCount;
        if(opcode == s_OpEntryPoint){
            if(instructionWordCount <= 3)
                return EntryPointLookupResult::InvalidSpirv;

            const ShaderType::Mask candidateShaderType = ConvertExecutionModel(words[instructionIndex + 1]);
            if(candidateShaderType != ShaderType::None && candidateShaderType == shaderType){
                const auto* entryPointBytes = reinterpret_cast<const char*>(&words[instructionIndex + 3]);
                const usize entryPointMaxBytes = (instructionWordCount - 3u) * sizeof(u32);

                usize entryPointLength = 0;
                while(entryPointLength < entryPointMaxBytes && entryPointBytes[entryPointLength] != '\0')
                    ++entryPointLength;

                if(entryPointLength == entryPointMaxBytes)
                    return EntryPointLookupResult::InvalidSpirv;

                const AStringView candidateEntryPoint(entryPointBytes, entryPointLength);
                if(candidateEntryPoint == entryName){
                    outEntryPointName.assign(candidateEntryPoint.data(), candidateEntryPoint.size());
                    return EntryPointLookupResult::Found;
                }
            }
        }

        instructionIndex = nextInstructionIndex;
    }

    return EntryPointLookupResult::NotFound;
}

inline bool ResolveShaderEntryPoint(
    const u32* words,
    const usize wordCount,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    const char* errorContext,
    AString& outEntryPointName
){
    const EntryPointLookupResult::Enum lookupResult = ResolveEntryPointName(words, wordCount, entryName, shaderType, outEntryPointName);
    switch(lookupResult){
    case EntryPointLookupResult::Found:
        return true;

    case EntryPointLookupResult::NotFound:
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Shader entry point '{}' (stage=0x{:x}) was not found in SPIR-V for {}"),
            StringConvert(entryName),
            static_cast<u32>(shaderType),
            StringConvert(errorContext)
        );
        return false;

    case EntryPointLookupResult::InvalidSpirv:
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Invalid SPIR-V while resolving shader entry point '{}' (stage=0x{:x}) for {}"),
            StringConvert(entryName),
            static_cast<u32>(shaderType),
            StringConvert(errorContext)
        );
        return false;

    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Sampler::Sampler(const VulkanContext& context)
    : RefCounter<ISampler>(context.threadPool)
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
    : RefCounter<IShader>(context.threadPool)
    , m_bytecode(Alloc::CustomAllocator<u8>(context.objectArena))
    , m_specializationEntries(Alloc::CustomAllocator<VkSpecializationMapEntry>(context.objectArena))
    , m_specializationData(Alloc::CustomAllocator<u8>(context.objectArena))
    , m_context(context)
{}
Shader::~Shader(){
    if(m_shaderModule != VK_NULL_HANDLE){
        vkDestroyShaderModule(m_context.device, m_shaderModule, m_context.allocationCallbacks);
        m_shaderModule = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderLibrary::ShaderLibrary(const VulkanContext& context)
    : RefCounter<IShaderLibrary>(context.threadPool)
    , m_bytecode(Alloc::CustomAllocator<u8>(context.objectArena))
    , m_shaders(0, ShaderLibraryKeyHasher(), EqualTo<ShaderLibraryKey>(), Alloc::CustomAllocator<Pair<const ShaderLibraryKey, RefCountPtr<Shader, ArenaRefDeleter<Shader>>>>(context.objectArena))
    , m_context(context)
{}
ShaderLibrary::~ShaderLibrary(){}

void ShaderLibrary::getBytecode(const void** ppBytecode, usize* pSize)const{
    *ppBytecode = m_bytecode.data();
    *pSize = m_bytecode.size();
}

ShaderHandle ShaderLibrary::getShader(const AStringView entryName, ShaderType::Mask shaderType){
    VkResult res = VK_SUCCESS;

    const AString requestedEntryName(entryName);
    ShaderLibraryKey key{ requestedEntryName, shaderType };

    auto it = m_shaders.find(key);
    if(it != m_shaders.end())
        return ShaderHandle(it.value().get(), ShaderHandle::deleter_type(&m_context.objectArena));

    Shader* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc.shaderType = shaderType;
    shader->m_desc.entryName = requestedEntryName;
    shader->m_bytecode = m_bytecode;

    if(shader->m_bytecode.empty() || (shader->m_bytecode.size() & 3) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader library bytecode payload for entry '{}'"), StringConvert(requestedEntryName));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    Alloc::ScratchArena<> scratchArena;
    __hidden_vulkan_shader::SpirvWordVector spirvWords{ Alloc::ScratchAllocator<u32>(scratchArena) };
    if(!__hidden_vulkan_shader::CopySpirvWords(shader->m_bytecode.data(), shader->m_bytecode.size(), spirvWords)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader library bytecode payload for entry '{}'"), StringConvert(requestedEntryName));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    if(!__hidden_vulkan_shader::ResolveShaderEntryPoint(spirvWords.data(), spirvWords.size(), AStringView(requestedEntryName), shaderType, "shader library", shader->m_entryPointName)){
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = m_bytecode.size();
    createInfo.pCode = spirvWords.data();

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for entry '{}': {}"), StringConvert(shader->m_entryPointName), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    m_shaders.emplace(
        Move(key),
        RefCountPtr<Shader, ArenaRefDeleter<Shader>>(shader, ArenaRefDeleter<Shader>(&m_context.objectArena), AdoptRef)
    );
    return ShaderHandle(shader, ShaderHandle::deleter_type(&m_context.objectArena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, usize binarySize){
    VkResult res = VK_SUCCESS;

    if(!binary || binarySize == 0 || (binarySize & 3) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader bytecode payload"));
        return nullptr;
    }

    auto* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc = d;
    shader->m_bytecode.assign(static_cast<const u8*>(binary), static_cast<const u8*>(binary) + binarySize);

    Alloc::ScratchArena<> scratchArena;
    __hidden_vulkan_shader::SpirvWordVector spirvWords{ Alloc::ScratchAllocator<u32>(scratchArena) };
    if(!__hidden_vulkan_shader::CopySpirvWords(shader->m_bytecode.data(), shader->m_bytecode.size(), spirvWords)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader bytecode payload"));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    if(!__hidden_vulkan_shader::ResolveShaderEntryPoint(spirvWords.data(), spirvWords.size(), d.entryName, d.shaderType, "standalone shader", shader->m_entryPointName)){
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = binarySize;
    createInfo.pCode = spirvWords.data();

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }
    return ShaderHandle(shader, ShaderHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

ShaderHandle Device::createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants){
    VkResult res = VK_SUCCESS;

    if(!baseShader){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader specialization: base shader is null"));
        return nullptr;
    }
    if(numConstants > 0 && !constants){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader specialization: constants are null for {} entries"), numConstants);
        return nullptr;
    }
    if(numConstants > UINT32_MAX / sizeof(uint32_t)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader specialization: constant count {} is too large"), numConstants);
        return nullptr;
    }

    auto* base = static_cast<Shader*>(baseShader);
    auto* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc = base->m_desc;
    shader->m_bytecode = base->m_bytecode;
    shader->m_entryPointName = base->m_entryPointName;

    Alloc::ScratchArena<> scratchArena;
    __hidden_vulkan_shader::SpirvWordVector spirvWords{ Alloc::ScratchAllocator<u32>(scratchArena) };
    if(!__hidden_vulkan_shader::CopySpirvWords(shader->m_bytecode.data(), shader->m_bytecode.size(), spirvWords)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader bytecode payload for specialization"));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shader->m_bytecode.size();
    createInfo.pCode = spirvWords.data();

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for specialization: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    if(constants && numConstants > 0){
        shader->m_specializationData.resize(numConstants * sizeof(uint32_t));
        shader->m_specializationEntries.resize(numConstants);

        auto fillConstant = [&](usize i){
            VkSpecializationMapEntry& entry = shader->m_specializationEntries[i];
            entry.constantID = constants[i].constantID;
            entry.offset = static_cast<uint32_t>(i * sizeof(uint32_t));
            entry.size = sizeof(uint32_t);

            NWB_MEMCPY(shader->m_specializationData.data() + entry.offset, sizeof(uint32_t), &constants[i].value, sizeof(uint32_t));
        };

        if(taskPool().isParallelEnabled() && numConstants >= s_ParallelSpecializationThreshold)
            scheduleParallelFor(static_cast<usize>(0), numConstants, fillConstant);
        else{
            for(usize i = 0; i < numConstants; ++i)
                fillConstant(i);
        }
    }

    return ShaderHandle(shader, ShaderHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

ShaderLibraryHandle Device::createShaderLibrary(const void* binary, usize binarySize){
    if(!binary || binarySize == 0 || (binarySize & 3) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader library bytecode payload"));
        return nullptr;
    }

    auto* lib = NewArenaObject<ShaderLibrary>(m_context.objectArena, m_context);
    lib->m_bytecode.assign(static_cast<const u8*>(binary), static_cast<const u8*>(binary) + binarySize);

    return ShaderLibraryHandle(lib, ShaderLibraryHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayout::InputLayout(const VulkanContext& context)
    : RefCounter<IInputLayout>(context.threadPool)
    , m_attributes(Alloc::CustomAllocator<VertexAttributeDesc>(context.objectArena))
    , m_bindings(Alloc::CustomAllocator<VkVertexInputBindingDescription>(context.objectArena))
    , m_vkAttributes(Alloc::CustomAllocator<VkVertexInputAttributeDescription>(context.objectArena))
    , m_context(context)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader*){
    if(attributeCount > 0 && !d){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute data is null for {} attributes"), attributeCount);
        return nullptr;
    }
    const auto& limits = m_context.physicalDeviceProperties.limits;
    if(attributeCount > limits.maxVertexInputAttributes){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create input layout: attribute count {} exceeds device limit {}"),
            attributeCount,
            limits.maxVertexInputAttributes
        );
        return nullptr;
    }

    struct VertexBindingBuildInfo{
        u64 requiredStride = 0;
        u32 explicitStride = 0;
        bool hasExplicitStride = false;
        bool isInstanced = false;
    };

    Alloc::ScratchArena<> scratchArena;
    HashMap<u32, VertexBindingBuildInfo, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<Pair<const u32, VertexBindingBuildInfo>>> bindingInfos(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        Alloc::ScratchAllocator<Pair<const u32, VertexBindingBuildInfo>>(scratchArena)
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
        const u64 stride =
            bindingInfo.hasExplicitStride
            ? static_cast<u64>(bindingInfo.explicitStride)
            : bindingInfo.requiredStride
        ;
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
    if(attributeCount > 0)
        layout->m_attributes.assign(d, d + attributeCount);

    layout->m_bindings.reserve(bindingInfos.size());
    for(const auto& [bufferIndex, bindingInfo] : bindingInfos){
        VkVertexInputBindingDescription binding{};
        binding.binding = bufferIndex;
        binding.stride =
            bindingInfo.hasExplicitStride
            ? bindingInfo.explicitStride
            : static_cast<u32>(bindingInfo.requiredStride)
        ;
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
    : RefCounter<IFramebuffer>(context.threadPool)
    , m_resources(Alloc::CustomAllocator<RefCountPtr<ITexture, ArenaRefDeleter<ITexture>>>(context.objectArena))
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


EventQuery::EventQuery(const VulkanContext& context)
    : RefCounter<IEventQuery>(context.threadPool)
    , m_context(context)
{
    VkResult res = VK_SUCCESS;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    res = vkCreateFence(m_context.device, &fenceInfo, m_context.allocationCallbacks, &m_fence);
    if(res != VK_SUCCESS){
        m_fence = VK_NULL_HANDLE;
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create fence for EventQuery"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create fence for EventQuery: {}"), ResultToString(res));
    }
}
EventQuery::~EventQuery(){
    if(m_fence != VK_NULL_HANDLE){
        vkDestroyFence(m_context.device, m_fence, m_context.allocationCallbacks);
        m_fence = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TimerQuery::TimerQuery(const VulkanContext& context)
    : RefCounter<ITimerQuery>(context.threadPool)
    , m_context(context)
{
    VkResult res = VK_SUCCESS;

    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = s_TimerQueryTimestampCount; // Start and end timestamps

    res = vkCreateQueryPool(m_context.device, &queryPoolInfo, m_context.allocationCallbacks, &m_queryPool);
    if(res != VK_SUCCESS){
        m_queryPool = VK_NULL_HANDLE;
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create query pool for TimerQuery: {}"), ResultToString(res));
    }
}
TimerQuery::~TimerQuery(){
    if(m_queryPool != VK_NULL_HANDLE){
        vkDestroyQueryPool(m_context.device, m_queryPool, m_context.allocationCallbacks);
        m_queryPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

