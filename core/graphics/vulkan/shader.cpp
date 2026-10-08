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

[[nodiscard]] Expected<Vector<u32, Alloc::GlobalArena>> BuildValidatedSpirvWords(const void* binary, const usize binarySize, Alloc::GlobalArena& arena){
    Vector<u32, Alloc::GlobalArena> words(arena);

    if(!IsValidSpirvBytecodeShape(binary, binarySize))
        return MakeUnexpected(Failure{});

    const usize wordCount = binarySize / sizeof(u32);
    words.resize(wordCount);
    NWB_MEMCPY(words.data(), binarySize, binary, binarySize);

    if(!IsValidSpirvModuleWords(words.data(), words.size())){
        return MakeUnexpected(Failure{});
    }

    return words;
}

template<typename WordVector>
[[nodiscard]] inline usize SpirvByteSize(const WordVector& words)noexcept{
    return words.size() * sizeof(u32);
}

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

inline Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveShaderEntryPoint(
    const u32* words,
    const usize wordCount,
    const AStringView entryName,
    const ShaderType::Mask shaderType,
    const AStringView errorContext
){
    const auto lookupResult = ResolveSpirvEntryPointName(words, wordCount, entryName, shaderType);
    if(lookupResult)
        return lookupResult;

    switch(lookupResult.error()){

    case SpirvEntryPointLookupResult::NotFound:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Shader entry point '{}' (stage=0x{:x}) was not found in SPIR-V for {}")
            , StringConvert(entryName)
            , static_cast<u32>(shaderType)
            , StringConvert(errorContext)
        );
        return MakeUnexpected(lookupResult.error());

    case SpirvEntryPointLookupResult::InvalidSpirv:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid SPIR-V while resolving shader entry point '{}' (stage=0x{:x}) for {}")
            , StringConvert(entryName)
            , static_cast<u32>(shaderType)
            , StringConvert(errorContext)
        );
        return MakeUnexpected(lookupResult.error());

    }

    return MakeUnexpected(lookupResult.error());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Sampler::Sampler(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_context(context)
{}
Sampler::~Sampler(){
    if(m_sampler != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroySampler(m_context.device, m_sampler, m_context.allocationCallbacks);
        m_sampler = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Shader::Shader(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_spirvWords(context.objectArena)
    , m_specializationEntries(context.objectArena)
    , m_specializationData(context.objectArena)
    , m_context(context)
{}
Shader::~Shader(){
    if(m_shaderModule != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyShaderModule(m_context.device, m_shaderModule, m_context.allocationCallbacks);
        m_shaderModule = VK_NULL_HANDLE;
    }
}

VkSpecializationInfo Shader::makeSpecializationInfo()const noexcept{
    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = static_cast<u32>(m_specializationEntries.size());
    specInfo.pMapEntries = m_specializationEntries.data();
    specInfo.dataSize = m_specializationData.size() * sizeof(u32);
    specInfo.pData = m_specializationData.data();
    return specInfo;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderLibrary::ShaderLibrary(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_spirvWords(context.objectArena)
    , m_shaders(0, ShaderLibraryKeyHasher(), EqualTo<ShaderLibraryKey>(), context.objectArena)
    , m_context(context)
{}
ShaderLibrary::~ShaderLibrary(){}

void ShaderLibrary::getBytecode(const void** ppBytecode, usize* pSize)const noexcept{
    *ppBytecode = m_spirvWords.data();
    *pSize = __hidden_vulkan_shader::SpirvByteSize(m_spirvWords);
}

ShaderHandle ShaderLibrary::getShader(const AStringView entryName, ShaderType::Mask shaderType){
    ShaderLibraryKey key{ .entryName = entryName, .shaderType = shaderType };

    auto it = m_shaders.find(key);
    if(it != m_shaders.end())
        return ShaderHandle(it.value().get(), ShaderHandle::deleter_type(&m_context.objectArena));

    Shader* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc.shaderType = shaderType;
    shader->m_desc.entryName = entryName;
    NWB_ASSERT(!m_spirvWords.empty());
    shader->m_spirvWords = m_spirvWords;

    const auto entryPointName = __hidden_vulkan_shader::ResolveShaderEntryPoint(
        shader->m_spirvWords.data(), shader->m_spirvWords.size(), entryName, shaderType, "shader library"
    );
    if(!entryPointName){
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }
    shader->m_entryPointName = *entryPointName;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = __hidden_vulkan_shader::SpirvByteSize(shader->m_spirvWords);
    createInfo.pCode = shader->m_spirvWords.data();

    const VkResult res = m_context.deviceDispatch.vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for entry '{}': {}"), StringConvert(shader->m_entryPointName), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    shader->m_desc.entryName = shader->m_entryPointName;
    key.entryName = shader->m_entryPointName;
    m_shaders.emplace(
        Move(key),
        Handle<Shader>(shader, Handle<Shader>::deleter_type(&m_context.objectArena), s_AdoptRef)
    );
    return ShaderHandle(shader, ShaderHandle::deleter_type(&m_context.objectArena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, usize binarySize){
    auto* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc = d;
    auto words = __hidden_vulkan_shader::BuildValidatedSpirvWords(binary, binarySize, m_context.objectArena);
    if(!words){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader bytecode payload"));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }
    shader->m_spirvWords = Move(*words);

    const auto entryPointName = __hidden_vulkan_shader::ResolveShaderEntryPoint(
        shader->m_spirvWords.data(), shader->m_spirvWords.size(), d.entryName, d.shaderType, "standalone shader"
    );
    if(!entryPointName){
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }
    shader->m_entryPointName = *entryPointName;

    shader->m_desc.entryName = shader->m_entryPointName;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = binarySize;
    createInfo.pCode = shader->m_spirvWords.data();

    const VkResult res = m_context.deviceDispatch.vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }
    return ShaderHandle(shader, ShaderHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

ShaderHandle Device::createShaderSpecialization(Shader& baseShader, const ShaderSpecialization* constants, u32 numConstants){
    if(numConstants > 0 && !constants){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader specialization: constants are null for {} entries"), numConstants);
        return nullptr;
    }
    if(numConstants > Limit<u32>::s_Max / sizeof(u32)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader specialization: constant count {} is too large"), numConstants);
        return nullptr;
    }

    Shader& base = baseShader;
    auto* shader = NewArenaObject<Shader>(m_context.objectArena, m_context);
    shader->m_desc = base.m_desc;
    NWB_ASSERT(!base.m_spirvWords.empty());
    shader->m_spirvWords = base.m_spirvWords;
    // The validated name starts at a SPIR-V word and keeps its offset in the byte-for-byte module copy.
    const u32* const entryPointWords = reinterpret_cast<const u32*>(base.m_entryPointName.data());
    NWB_ASSERT(entryPointWords >= base.m_spirvWords.data() && entryPointWords < base.m_spirvWords.data() + base.m_spirvWords.size());
    const usize entryPointWordOffset = static_cast<usize>(entryPointWords - base.m_spirvWords.data());
    NWB_ASSERT(entryPointWordOffset < shader->m_spirvWords.size());
    NWB_ASSERT(base.m_entryPointName.size() < (shader->m_spirvWords.size() - entryPointWordOffset) * sizeof(u32));
    shader->m_entryPointName = AStringView(reinterpret_cast<const char*>(shader->m_spirvWords.data() + entryPointWordOffset), base.m_entryPointName.size());
    NWB_ASSERT(shader->m_entryPointName.data()[shader->m_entryPointName.size()] == '\0');
    shader->m_desc.entryName = shader->m_entryPointName;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = __hidden_vulkan_shader::SpirvByteSize(shader->m_spirvWords);
    createInfo.pCode = shader->m_spirvWords.data();

    const VkResult res = m_context.deviceDispatch.vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for specialization: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, shader);
        return nullptr;
    }

    if(constants && numConstants > 0){
        shader->m_specializationData.resize(numConstants);
        shader->m_specializationEntries.resize(numConstants);

        auto fillConstant = [&](usize i){
            VkSpecializationMapEntry& entry = shader->m_specializationEntries[i];
            entry.constantID = constants[i].constantID;
            entry.offset = static_cast<u32>(i * sizeof(u32));
            entry.size = sizeof(u32);

            NWB_MEMCPY(shader->m_specializationData.data() + i, sizeof(u32), &constants[i].value, sizeof(u32));
        };

        if(taskScheduler().isParallelEnabled() && numConstants >= s_ParallelSpecializationThreshold)
            taskScheduler().parallelFor(static_cast<usize>(0), numConstants, fillConstant);
        else{
            for(usize i = 0u; i < numConstants; ++i)
                fillConstant(i);
        }
    }

    return ShaderHandle(shader, ShaderHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

ShaderLibraryHandle Device::createShaderLibrary(const void* binary, usize binarySize){
    auto* lib = NewArenaObject<ShaderLibrary>(m_context.objectArena, m_context);
    auto words = __hidden_vulkan_shader::BuildValidatedSpirvWords(binary, binarySize, m_context.objectArena);
    if(!words){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader library bytecode payload"));
        DestroyArenaObject(m_context.objectArena, lib);
        return nullptr;
    }
    lib->m_spirvWords = Move(*words);

    return ShaderLibraryHandle(lib, ShaderLibraryHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputLayout::InputLayout(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
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
    HashMap<u32, VertexBindingBuildInfo, Alloc::ScratchArena, Hasher<u32>, EqualTo<u32>> bindingInfos(
        0,
        Hasher<u32>(),
        EqualTo<u32>(),
        scratchArena
    );
    bindingInfos.reserve(attributeCount);

    for(u32 i = 0u; i < attributeCount; ++i){
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

        const auto attributeBytes = __hidden_vulkan_shader::ComputeVertexAttributeBytes(attr, i);
        if(!attributeBytes)
            return nullptr;
        if(static_cast<u64>(attr.offset) > Limit<u64>::s_Max - *attributeBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create input layout: attribute {} offset plus size overflows"), i);
            return nullptr;
        }

        const u64 attributeEnd = static_cast<u64>(attr.offset) + *attributeBytes;
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
    auto fillVkAttribute = [&](usize i){
        const auto& attr = layout->m_attributes[i];

        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = static_cast<u32>(i);
        vkAttr.binding = attr.bufferIndex;
        vkAttr.format = ConvertFormat(attr.format);
        vkAttr.offset = attr.offset;

        layout->m_vkAttributes[i] = vkAttr;
    };

    if(taskScheduler().isParallelEnabled() && attributeCount >= s_ParallelInputLayoutThreshold)
        taskScheduler().parallelFor(static_cast<usize>(0), attributeCount, s_InputLayoutGrainSize, fillVkAttribute);
    else{
        for(usize i = 0u; i < attributeCount; ++i)
            fillVkAttribute(i);
    }

    return InputLayoutHandle(layout, InputLayoutHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Framebuffer::Framebuffer(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_resources(context.objectArena)
    , m_context(context)
{}
Framebuffer::~Framebuffer(){
    if(m_framebuffer != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyFramebuffer(m_context.device, m_framebuffer, m_context.allocationCallbacks);
        m_framebuffer = VK_NULL_HANDLE;
    }

    if(m_renderPass != VK_NULL_HANDLE){
        m_context.deviceDispatch.vkDestroyRenderPass(m_context.device, m_renderPass, m_context.allocationCallbacks);
        m_renderPass = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

