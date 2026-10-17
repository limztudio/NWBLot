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

inline Expected<AStringView, SpirvEntryPointLookupResult::Enum> ResolveShaderEntryPoint(const u32* words, const usize wordCount, const AStringView entryName, const ShaderType::Mask shaderType, const AStringView errorContext){
    const auto lookupResult = ResolveSpirvEntryPointName(words, wordCount, entryName, shaderType);
    if(lookupResult)
        return lookupResult;

    switch(lookupResult.error()){

    case SpirvEntryPointLookupResult::NotFound:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Shader entry point '{}' (stage=0x{:x}) was not found in SPIR-V for {}"), StringConvert(entryName), static_cast<u32>(shaderType), StringConvert(errorContext));
        return MakeUnexpected(lookupResult.error());

    case SpirvEntryPointLookupResult::InvalidSpirv:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid SPIR-V while resolving shader entry point '{}' (stage=0x{:x}) for {}"), StringConvert(entryName), static_cast<u32>(shaderType), StringConvert(errorContext));
        return MakeUnexpected(lookupResult.error());

    }

    return MakeUnexpected(lookupResult.error());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Shader::Shader(const VulkanContext& context)noexcept
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

Expected<ShaderHandle> ShaderLibrary::getShader(const AStringView entryName, ShaderType::Mask shaderType){
    ShaderLibraryKey key{ .entryName = entryName, .shaderType = shaderType };

    auto it = m_shaders.find(key);
    if(it != m_shaders.end())
        return ShaderHandle(it.value().get(), ShaderHandle::deleter_type(&m_context.objectArena));

    ShaderHandle shader(NewArenaObject<Shader>(m_context.objectArena, m_context), ShaderHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!shader)
        return MakeUnexpected(Failure{});
    shader->m_desc.shaderType = shaderType;
    shader->m_desc.entryName = entryName;
    NWB_ASSERT(!m_spirvWords.empty());
    shader->m_spirvWords = m_spirvWords;

    const auto entryPointName = __hidden_vulkan_shader::ResolveShaderEntryPoint(shader->m_spirvWords.data(), shader->m_spirvWords.size(), entryName, shaderType, "shader library");
    if(!entryPointName){
        return MakeUnexpected(Failure{});
    }
    shader->m_entryPointName = *entryPointName;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = __hidden_vulkan_shader::SpirvByteSize(shader->m_spirvWords);
    createInfo.pCode = shader->m_spirvWords.data();

    const VkResult res = m_context.deviceDispatch.vkCreateShaderModule(m_context.device, &createInfo, m_context.allocationCallbacks, &shader->m_shaderModule);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader module for entry '{}': {}"), StringConvert(shader->m_entryPointName), ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    shader->m_desc.entryName = shader->m_entryPointName;
    key.entryName = shader->m_entryPointName;
    if(!m_shaders.emplace(Move(key), shader).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    return shader;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ShaderHandle> Device::createShader(const ShaderDesc& d, const void* binary, usize binarySize){
    ShaderHandle shader(NewArenaObject<Shader>(m_context.objectArena, m_context), ShaderHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!shader)
        return MakeUnexpected(Failure{});
    shader->m_desc = d;
    auto words = __hidden_vulkan_shader::BuildValidatedSpirvWords(binary, binarySize, m_context.objectArena);
    if(!words){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader bytecode payload"));
        return MakeUnexpected(Failure{});
    }
    shader->m_spirvWords = Move(*words);

    const auto entryPointName = __hidden_vulkan_shader::ResolveShaderEntryPoint(shader->m_spirvWords.data(), shader->m_spirvWords.size(), d.entryName, d.shaderType, "standalone shader");
    if(!entryPointName){
        return MakeUnexpected(Failure{});
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
        return MakeUnexpected(Failure{});
    }
    return shader;
}

Expected<ShaderHandle> Device::createShaderSpecialization(Shader& baseShader, const ShaderSpecialization* constants, u32 numConstants){
    if(numConstants > 0 && !constants){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader specialization: constants are null for {} entries"), numConstants);
        return MakeUnexpected(Failure{});
    }
    if(numConstants > Limit<u32>::s_Max / sizeof(u32)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader specialization: constant count {} is too large"), numConstants);
        return MakeUnexpected(Failure{});
    }

    Shader& base = baseShader;
    ShaderHandle shader(NewArenaObject<Shader>(m_context.objectArena, m_context), ShaderHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!shader)
        return MakeUnexpected(Failure{});
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
        return MakeUnexpected(Failure{});
    }

    if(constants && numConstants > 0){
        shader->m_specializationData.resize(numConstants);
        shader->m_specializationEntries.resize(numConstants);

        auto fillConstant = [&](usize i)noexcept{
            VkSpecializationMapEntry& entry = shader->m_specializationEntries[i];
            entry.constantID = constants[i].constantID;
            entry.offset = static_cast<u32>(i * sizeof(u32));
            entry.size = sizeof(u32);

            NWB_MEMCPY(shader->m_specializationData.data() + i, sizeof(u32), &constants[i].value, sizeof(u32));
        };

        if(taskScheduler().isParallelEnabled() && numConstants >= s_ParallelSpecializationThreshold){
            const auto parallelWork = taskScheduler().parallelFor(static_cast<usize>(0), numConstants, fillConstant);
            if(!parallelWork){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: shader specialization rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
                return MakeUnexpected(Failure{});
            }
        }
        else{
            for(usize i = 0u; i < numConstants; ++i)
                fillConstant(i);
        }
    }

    return shader;
}

Expected<ShaderLibraryHandle> Device::createShaderLibrary(const void* binary, usize binarySize){
    ShaderLibraryHandle lib(NewArenaObject<ShaderLibrary>(m_context.objectArena, m_context), ShaderLibraryHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!lib)
        return MakeUnexpected(Failure{});
    auto words = __hidden_vulkan_shader::BuildValidatedSpirvWords(binary, binarySize, m_context.objectArena);
    if(!words){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Invalid shader library bytecode payload"));
        return MakeUnexpected(Failure{});
    }
    lib->m_spirvWords = Move(*words);

    return lib;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

