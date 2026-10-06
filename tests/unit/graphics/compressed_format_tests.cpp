// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/device_detail.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compressed_format_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Format = Core::Format;
namespace VulkanDetail = Core::GraphicsBackend::VulkanDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CompressedTextureFormats, RequiresTheEnabledFeatureForEachCompressionFamily){
    VulkanDetail::CompressedTextureFeatureState features;
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::BC1_UNORM, features));
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_4x4_UNORM, features));
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_4x4_FLOAT, features));

    features.bcEnabled = true;
    EXPECT_TRUE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::BC7_UNORM_SRGB, features));
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_4x4_UNORM, features));
    features.bcEnabled = false;

    features.astcLdrEnabled = true;
    EXPECT_TRUE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_12x12_UNORM_SRGB, features));
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_12x12_FLOAT, features));
    features.astcLdrEnabled = false;

    features.astcHdrEnabled = true;
    EXPECT_TRUE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_12x12_FLOAT, features));
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_12x12_UNORM, features));
}

TEST(CompressedTextureFormats, NegotiatesAstcHdrFromItsVersionSpecificOwner){
    VulkanDetail::AstcHdrFeatureNegotiation negotiation;
    negotiation.apiSupportsVulkan13 = true;
    negotiation.vulkan13FeatureSupported = true;
    EXPECT_TRUE(VulkanDetail::ShouldEnableAstcHdrFeature(negotiation));

    negotiation.vulkan13FeatureSupported = false;
    negotiation.extensionEnabled = true;
    negotiation.extensionFeatureSupported = true;
    EXPECT_FALSE(VulkanDetail::ShouldEnableAstcHdrFeature(negotiation));

    negotiation.apiSupportsVulkan13 = false;
    EXPECT_TRUE(VulkanDetail::ShouldEnableAstcHdrFeature(negotiation));

    negotiation.extensionEnabled = false;
    EXPECT_FALSE(VulkanDetail::ShouldEnableAstcHdrFeature(negotiation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

