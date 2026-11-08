// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/texture_image_policy.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_texture_image_policy_tests{

using namespace NWB::Core;
using namespace NWB::Core::GraphicsBackend::VulkanTextureDetail;

TEST(GpuTextureImagePolicyTests, ImageSharingRequiresCompleteQueueFamilyProvenance){
    TextureDesc desc;
    desc.format = Format::RGBA8_UNORM;
    const auto metadata = TextureImagePolicy::validateTextureCreateDesc(desc, {}, false);
    ASSERT_TRUE(metadata);
    VkImageCreateInfo info = TextureImagePolicy::buildTextureImageCreateInfo(desc, *metadata);
    EXPECT_TRUE(TextureImagePolicy::isTextureImageInfoConsistent(desc, info));
    info.sharingMode = VK_SHARING_MODE_CONCURRENT;
    info.queueFamilyIndexCount = 1u;
    EXPECT_FALSE(TextureImagePolicy::isTextureImageInfoConsistent(desc, info));
    const uint32_t families[] = { 0u, 1u };
    info.queueFamilyIndexCount = 2u;
    info.pQueueFamilyIndices = families;
    EXPECT_TRUE(TextureImagePolicy::isTextureImageInfoConsistent(desc, info));
    info.flags |= VK_IMAGE_CREATE_PROTECTED_BIT;
    EXPECT_FALSE(TextureImagePolicy::isTextureImageInfoConsistent(desc, info));
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
