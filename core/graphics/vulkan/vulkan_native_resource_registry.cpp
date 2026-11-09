// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "vulkan_native_resource_registry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


VulkanNativeResourceRegistry::VulkanNativeResourceRegistry(Alloc::GlobalArena& arena)
    : m_bufferNativeIdentities(0u, Hasher<u64>(), EqualTo<u64>(), arena)
    , m_textureNativeIdentities(0u, Hasher<VkImage>(), EqualTo<VkImage>(), arena)
{}


b8 VulkanNativeResourceRegistry::tryRegisterBuffer(VkBuffer nativeBuffer, Buffer& buffer){
    if(nativeBuffer == VK_NULL_HANDLE)
        return false;

    const u64 nativeIdentity = Object(nativeBuffer).integer;
    NothrowScopedLock lock(m_bufferNativeIdentityMutex);
    return m_bufferNativeIdentities.emplace(nativeIdentity, MakeNotNull(&buffer)).second;
}

void VulkanNativeResourceRegistry::unregisterBuffer(const VkBuffer nativeBuffer, Buffer& buffer)noexcept{
    if(nativeBuffer == VK_NULL_HANDLE)
        return;

    const u64 nativeIdentity = Object(nativeBuffer).integer;
    NothrowScopedLock lock(m_bufferNativeIdentityMutex);
    const auto found = m_bufferNativeIdentities.find(nativeIdentity);
    if(found != m_bufferNativeIdentities.end() && found.value().get() == &buffer)
        m_bufferNativeIdentities.erase(found);
}

b8 VulkanNativeResourceRegistry::isBufferRegistered(VkBuffer nativeBuffer, const Buffer& buffer)const noexcept{
    if(nativeBuffer == VK_NULL_HANDLE)
        return false;

    const u64 nativeIdentity = Object(nativeBuffer).integer;
    NothrowScopedLock lock(m_bufferNativeIdentityMutex);
    const auto found = m_bufferNativeIdentities.find(nativeIdentity);
    return found != m_bufferNativeIdentities.end() && found.value().get() == &buffer;
}

b8 VulkanNativeResourceRegistry::tryRegisterTexture(VkImage nativeImage, Texture& texture){
    if(nativeImage == VK_NULL_HANDLE)
        return false;

    NothrowScopedLock lock(m_textureNativeIdentityMutex);
    return m_textureNativeIdentities.emplace(nativeImage, MakeNotNull(&texture)).second;
}

void VulkanNativeResourceRegistry::unregisterTexture(const VkImage nativeImage, Texture& texture)noexcept{
    if(nativeImage == VK_NULL_HANDLE)
        return;

    NothrowScopedLock lock(m_textureNativeIdentityMutex);
    const auto found = m_textureNativeIdentities.find(nativeImage);
    if(found != m_textureNativeIdentities.end() && found.value().get() == &texture)
        m_textureNativeIdentities.erase(found);
}

b8 VulkanNativeResourceRegistry::isTextureRegistered(const VkImage nativeImage, const Texture& texture)const noexcept{
    if(nativeImage == VK_NULL_HANDLE)
        return false;

    NothrowScopedLock lock(m_textureNativeIdentityMutex);
    const auto found = m_textureNativeIdentities.find(nativeImage);
    return found != m_textureNativeIdentities.end() && found.value().get() == &texture;
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

