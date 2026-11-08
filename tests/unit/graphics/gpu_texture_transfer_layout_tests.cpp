// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/texture_transfer_layout.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_texture_transfer_layout_tests{

using namespace NWB::Core::GraphicsBackend::VulkanDetail;

TEST(GpuTextureTransferLayoutTests, CompressedPaddedSlicesPreserveTouchedByteBoundary){
    const VkExtent3D extent{ 5u, 5u, 2u };
    const TextureFormatBlockLayout block{ 4u, 4u, 16u };
    const auto touched = TextureTransferLayout::BuildBufferImageCopyLayout(extent, block, 48u, 144u, BufferImageCopyRequiredSize::TouchedBytes, BufferImageCopyPitchFields::EmitExplicit);
    const auto padded = TextureTransferLayout::BuildBufferImageCopyLayout(extent, block, 48u, 144u, BufferImageCopyRequiredSize::PaddedSlices, BufferImageCopyPitchFields::EmitExplicit);
    ASSERT_TRUE(touched);
    ASSERT_TRUE(padded);
    EXPECT_EQ(touched->requiredSize, 224u);
    EXPECT_EQ(padded->requiredSize, 240u);
    EXPECT_EQ(touched->bufferRowLength, 12u);
    EXPECT_EQ(touched->bufferImageHeight, 12u);
    EXPECT_FALSE(TextureTransferLayout::BuildBufferImageCopyLayout(extent, block, 17u, 0u, BufferImageCopyRequiredSize::TouchedBytes, BufferImageCopyPitchFields::OmitImplicit));
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
