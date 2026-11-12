// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"
#include "backend_core_context.h"
#include "vulkan_native_resource_registry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferAllocation{
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle allocation = nullptr;
    void* mappedMemory = nullptr;
    b8 requiresInvalidate = false;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanAllocator final : NoCopy{
    friend class Buffer;
    friend class Device;
    friend class Texture;


public:
    explicit VulkanAllocator(const VulkanContext& context);
    ~VulkanAllocator()noexcept;


public:
    [[nodiscard]] b8 initialize();

    VkResult createBuffer(Buffer& buffer, const VkBufferCreateInfo& bufferInfo);
    void destroyBuffer(Buffer& buffer);
    [[nodiscard]] Expected<void*, VkResult> mapBufferMemory(Buffer& buffer);
    void unmapBufferMemory(Buffer& buffer);
    VkResult invalidateBufferMemory(Buffer& buffer);

    VkResult createTexture(Texture& texture, const VkImageCreateInfo& imageInfo);
    void destroyTexture(Texture& texture);

    VkResult createStagingTexture(StagingTexture& texture, const VkBufferCreateInfo& bufferInfo, CpuAccessMode::Enum cpuAccess);
    void destroyStagingTexture(StagingTexture& texture);
    VkResult invalidateStagingTextureMemory(StagingTexture& texture, u64 offset, u64 size);
    VkResult allocateHeap(Heap& heap);
    void freeHeap(Heap& heap);
    VkResult invalidateHeapMemory(Heap& heap, u64 offset, u64 size);
    VkResult bindHeapBufferMemory(Buffer& buffer, Heap& heap, u64 offset);
    VkResult bindHeapTextureMemory(Texture& texture, Heap& heap, u64 offset);
    [[nodiscard]] Expected<BufferAllocation, VkResult> createHostMappedBuffer(const VkBufferCreateInfo& bufferInfo);
    void destroyHostMappedBuffer(VkBuffer& buffer, VulkanAllocationHandle& allocation, void*& mappedMemory)noexcept;


private:
    [[nodiscard]] b8 tryRegisterBufferNativeIdentity(Buffer& buffer);
    void unregisterBufferNativeIdentity(VkBuffer nativeBuffer, Buffer& buffer)noexcept;
    [[nodiscard]] b8 isBufferNativeIdentityRegistered(const Buffer& buffer)const noexcept;
    [[nodiscard]] b8 tryRegisterTextureNativeIdentity(Texture& texture);
    void unregisterTextureNativeIdentity(VkImage nativeImage, Texture& texture)noexcept;
    [[nodiscard]] b8 isTextureNativeIdentityRegistered(VkImage nativeImage, const Texture& texture)const noexcept;


private:
    const VulkanContext& m_context;
    VulkanAllocatorHandle m_allocator = nullptr;
    VulkanNativeResourceRegistry m_nativeResources;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

