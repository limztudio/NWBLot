// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"
#include "backend_core_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


class VulkanNativeResourceRegistry final : NoCopy{
public:
    explicit VulkanNativeResourceRegistry(Alloc::GlobalArena& arena);
    [[nodiscard]] b8 tryRegisterBuffer(VkBuffer nativeBuffer, Buffer& buffer);
    void unregisterBuffer(VkBuffer nativeBuffer, Buffer& buffer)noexcept;
    [[nodiscard]] b8 isBufferRegistered(VkBuffer nativeBuffer, const Buffer& buffer)const noexcept;
    [[nodiscard]] b8 tryRegisterTexture(VkImage nativeImage, Texture& texture);
    void unregisterTexture(VkImage nativeImage, Texture& texture)noexcept;
    [[nodiscard]] b8 isTextureRegistered(VkImage nativeImage, const Texture& texture)const noexcept;

private:
    HashMap<u64, NotNull<Buffer*>, Alloc::GlobalArena, Hasher<u64>, EqualTo<u64>> m_bufferNativeIdentities;
    HashMap<VkImage, NotNull<Texture*>, Alloc::GlobalArena, Hasher<VkImage>, EqualTo<VkImage>> m_textureNativeIdentities;
    mutable Futex m_bufferNativeIdentityMutex;
    mutable Futex m_textureNativeIdentityMutex;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

