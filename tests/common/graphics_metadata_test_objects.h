// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Core::Texture* NewMetadataOnlyTexture(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsBackend::VulkanContext& context,
    Core::GraphicsBackend::VulkanAllocator& allocator,
    const Core::TextureDesc& description
){
    return NewArenaObject<Core::Texture>(
        arena,
        context,
        allocator,
        description,
        VkImageCreateInfo{},
        false
    );
}

[[nodiscard]] inline Core::Buffer* NewMetadataOnlyBuffer(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsBackend::VulkanContext& context,
    Core::GraphicsBackend::VulkanAllocator& allocator,
    const Core::BufferDesc& description,
    const bool initialStateKnown = false,
    const VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    const u32 queueFamilyIndexCount = 0u,
    const u32* const queueFamilyIndices = nullptr
){
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = Max<u64>(description.byteSize, 1u);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = sharingMode;
    bufferInfo.queueFamilyIndexCount = queueFamilyIndexCount;
    bufferInfo.pQueueFamilyIndices = queueFamilyIndices;
    return NewArenaObject<Core::Buffer>(arena, context, allocator, description, bufferInfo, initialStateKnown);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

