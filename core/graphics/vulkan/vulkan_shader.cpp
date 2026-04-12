// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_shader{


enum class EntryPointLookupResult : u8{
    Found,
    NotFound,
    InvalidSpirv,
    Ambiguous,
};

inline constexpr u32 s_SpirvMagic = 0x07230203u;
inline constexpr u16 s_OpEntryPoint = 15u;
inline constexpr usize s_SpirvHeaderWords = 5;

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

inline EntryPointLookupResult ResolveEntryPointName(
    const Vector<u8, Alloc::CustomAllocator<u8>>& bytecode,
    const Name& entryName,
    const ShaderType::Mask shaderType,
    AString& outEntryPointName)
{
    outEntryPointName.clear();

    if(!entryName || shaderType == ShaderType::None)
        return EntryPointLookupResult::NotFound;

    if(bytecode.empty() || (bytecode.size() & 3) != 0)
        return EntryPointLookupResult::InvalidSpirv;

    const usize wordCount = bytecode.size() / sizeof(u32);
    if(wordCount < s_SpirvHeaderWords)
        return EntryPointLookupResult::InvalidSpirv;

    const auto* words = reinterpret_cast<const u32*>(bytecode.data());
    if(words[0] != s_SpirvMagic)
        return EntryPointLookupResult::InvalidSpirv;

    for(usize instructionIndex = s_SpirvHeaderWords; instructionIndex < wordCount; ){
        const u32 instruction = words[instructionIndex];
        const u16 opcode = static_cast<u16>(instruction & 0xFFFFu);
        const u16 instructionWordCount = static_cast<u16>(instruction >> 16u);
        if(instructionWordCount == 0)
            return EntryPointLookupResult::InvalidSpirv;

        const usize nextInstructionIndex = instructionIndex + instructionWordCount;
        if(nextInstructionIndex > wordCount)
            return EntryPointLookupResult::InvalidSpirv;

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
                if(ToName(candidateEntryPoint) == entryName){
                    if(outEntryPointName.empty())
                        outEntryPointName.assign(candidateEntryPoint.data(), candidateEntryPoint.size());
                    else if(outEntryPointName != candidateEntryPoint)
                        return EntryPointLookupResult::Ambiguous;
                }
            }
        }

        instructionIndex = nextInstructionIndex;
    }

    return outEntryPointName.empty()
        ? EntryPointLookupResult::NotFound
        : EntryPointLookupResult::Found
    ;
}

inline bool ResolveShaderEntryPoint(
    const Vector<u8, Alloc::CustomAllocator<u8>>& bytecode,
    const Name& entryName,
    const ShaderType::Mask shaderType,
    const char* errorContext,
    AString& outEntryPointName)
{
    const EntryPointLookupResult lookupResult = ResolveEntryPointName(bytecode, entryName, shaderType, outEntryPointName);
    switch(lookupResult){
    case EntryPointLookupResult::Found:
        return true;

    case EntryPointLookupResult::NotFound:
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Shader entry point '{}' (stage=0x{:x}) was not found in SPIR-V for {}"),
            StringConvert(entryName.c_str()),
            static_cast<u32>(shaderType),
            StringConvert(errorContext)
        );
        return false;

    case EntryPointLookupResult::InvalidSpirv:
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Invalid SPIR-V while resolving shader entry point '{}' (stage=0x{:x}) for {}"),
            StringConvert(entryName.c_str()),
            static_cast<u32>(shaderType),
            StringConvert(errorContext)
        );
        return false;

    case EntryPointLookupResult::Ambiguous:
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Ambiguous case-sensitive SPIR-V entry point match for '{}' (stage=0x{:x}) in {}"),
            StringConvert(entryName.c_str()),
            static_cast<u32>(shaderType),
            StringConvert(errorContext)
        );
        return false;
    }

    return false;
}


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

ShaderHandle ShaderLibrary::getShader(const Name& entryName, ShaderType::Mask shaderType){
    VkResult res = VK_SUCCESS;

    const ShaderLibraryKey key{ entryName, shaderType };

    auto it = m_shaders.find(key);
    if(it != m_shaders.end())
        return ShaderHandle(it.value().get(), ShaderHandle::deleter_type(&m_context.objectArena));

    Shader* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc.shaderType = shaderType;
    shader->m_desc.entryName = entryName;
    shader->m_bytecode = m_bytecode;

    if(shader->m_bytecode.empty() || (shader->m_bytecode.size() & 3) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader library bytecode payload for entry '{}'"), StringConvert(entryName.c_str()));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    if(!__hidden_vulkan_shader::ResolveShaderEntryPoint(shader->m_bytecode, entryName, shaderType, "shader library", shader->m_entryPointName)){
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = m_bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(m_bytecode.data());

    res = vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for entry '{}': {}"), StringConvert(shader->m_entryPointName), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    m_shaders[key] = RefCountPtr<Shader, ArenaRefDeleter<Shader>>(shader, ArenaRefDeleter<Shader>(&m_context.objectArena));
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

    if(!__hidden_vulkan_shader::ResolveShaderEntryPoint(shader->m_bytecode, d.entryName, d.shaderType, "standalone shader", shader->m_entryPointName)){
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = binarySize;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(binary);

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

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shader->m_bytecode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shader->m_bytecode.data());

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
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create input layout: attribute {} buffer index {} exceeds device binding limit {}"),
                i,
                attr.bufferIndex,
                limits.maxVertexInputBindings
            );
            return nullptr;
        }
        if(attr.offset > limits.maxVertexInputAttributeOffset){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create input layout: attribute {} offset {} exceeds device limit {}"),
                i,
                attr.offset,
                limits.maxVertexInputAttributeOffset
            );
            return nullptr;
        }

        u64 stride = attr.elementStride;
        if(stride == 0){
            const FormatInfo& formatInfo = GetFormatInfo(attr.format);
            stride = static_cast<u64>(formatInfo.bytesPerBlock) * attr.arraySize;
        }
        if(stride == 0 || stride > limits.maxVertexInputBindingStride){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create input layout: attribute {} stride {} is outside device limit {}"),
                i,
                stride,
                limits.maxVertexInputBindingStride
            );
            return nullptr;
        }
    }

    auto* layout = NewArenaObject<InputLayout>(m_context.objectArena, m_context);
    if(attributeCount > 0)
        layout->m_attributes.assign(d, d + attributeCount);

    Alloc::ScratchArena<> scratchArena;

    HashMap<u32, u32, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<Pair<const u32, u32>>> bufferStrides(0, Hasher<u32>(), EqualTo<u32>(), Alloc::ScratchAllocator<Pair<const u32, u32>>(scratchArena));
    HashMap<u32, bool, Hasher<u32>, EqualTo<u32>, Alloc::ScratchAllocator<Pair<const u32, bool>>> bufferInstanced(0, Hasher<u32>(), EqualTo<u32>(), Alloc::ScratchAllocator<Pair<const u32, bool>>(scratchArena));
    for(const auto& attr : layout->m_attributes){
        u32 stride = attr.elementStride;
        if(stride == 0){
            const FormatInfo& formatInfo = GetFormatInfo(attr.format);
            stride = formatInfo.bytesPerBlock * attr.arraySize;
        }

        if(bufferStrides.find(attr.bufferIndex) == bufferStrides.end()){
            bufferStrides[attr.bufferIndex] = stride;
            bufferInstanced[attr.bufferIndex] = attr.isInstanced;
        }
        else
            bufferStrides[attr.bufferIndex] = Max(bufferStrides[attr.bufferIndex], stride);
    }

    for(const auto& [bufferIndex, stride] : bufferStrides){
        VkVertexInputBindingDescription binding{};
        binding.binding = bufferIndex;
        binding.stride = stride;
        binding.inputRate = bufferInstanced[bufferIndex] ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
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
