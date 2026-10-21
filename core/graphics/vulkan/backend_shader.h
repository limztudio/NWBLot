// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_forward.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderLibrary;


public:
    Shader(const VulkanContext& context)noexcept;
    ~Shader()noexcept;


public:
    [[nodiscard]] const ShaderDesc& getDescription()const noexcept{ return m_desc; }

    void getBytecode(const void*& outBytecode, usize& outSize)const noexcept{
        outBytecode = m_spirvWords.data();
        outSize = m_spirvWords.size() * sizeof(u32);
    }


private:
    [[nodiscard]] VkSpecializationInfo makeSpecializationInfo()const noexcept;


private:
    ShaderDesc m_desc;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;

    Vector<u32, Alloc::GlobalArena> m_spirvWords;
    // Points into immutable m_spirvWords, including a validated null byte immediately after the view.
    AStringView m_entryPointName;

    Vector<VkSpecializationMapEntry, Alloc::GlobalArena> m_specializationEntries;
    Vector<u32, Alloc::GlobalArena> m_specializationData;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShaderLibraryKey{
    // Stored keys borrow the resolved entry name from the Shader retained by the map value.
    AStringView entryName;
    ShaderType::Mask shaderType = ShaderType::None;
};

inline bool operator==(const ShaderLibraryKey& lhs, const ShaderLibraryKey& rhs)noexcept{
    return lhs.entryName == rhs.entryName && lhs.shaderType == rhs.shaderType;
}

struct ShaderLibraryKeyHasher{
    usize operator()(const ShaderLibraryKey& value)const noexcept{
        usize seed = Hasher<AStringView>{}(value.entryName);
        ::HashCombine(seed, static_cast<u32>(value.shaderType));
        return seed;
    }
};


class ShaderLibrary final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;


public:
    ShaderLibrary(const VulkanContext& context);
    ~ShaderLibrary()noexcept;


public:
    void getBytecode(const void*& outBytecode, usize& outSize)const noexcept;
    [[nodiscard]] Expected<ShaderHandle> getShader(AStringView entryName, ShaderType::Mask shaderType);


private:
    Vector<u32, Alloc::GlobalArena> m_spirvWords;
    HashMap<ShaderLibraryKey, Handle<Shader>, GraphicsArena, ShaderLibraryKeyHasher, EqualTo<ShaderLibraryKey>> m_shaders;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

