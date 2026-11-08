// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/texture_shape_policy.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_texture_shape_policy_tests{

using namespace NWB::Core;
using NWB::Core::GraphicsBackend::VulkanTextureDetail::TextureShapePolicy;

TEST(GpuTextureShapePolicyTests, CubeArraysRequireWholeCubesAndSquareFaces){
    TextureDesc desc;
    desc.dimension = TextureDimension::TextureCubeArray;
    desc.width = desc.height = 16u;
    desc.arraySize = 12u;
    EXPECT_TRUE(TextureShapePolicy::isValid(desc));
    desc.arraySize = 13u;
    EXPECT_FALSE(TextureShapePolicy::isValid(desc));
    desc.arraySize = 12u;
    desc.height = 8u;
    EXPECT_FALSE(TextureShapePolicy::isValid(desc));
}

TEST(GpuTextureShapePolicyTests, ThreeDimensionalDepthDeterminesMipLimit){
    TextureDesc desc;
    desc.dimension = TextureDimension::Texture3D;
    desc.depth = 16u;
    desc.mipLevels = 5u;
    EXPECT_EQ(TextureShapePolicy::maxMipLevels(desc), 5u);
    EXPECT_TRUE(TextureShapePolicy::isValid(desc));
    desc.mipLevels = 6u;
    EXPECT_FALSE(TextureShapePolicy::isValid(desc));
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
