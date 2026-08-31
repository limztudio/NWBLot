// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/backend.h>
#include <core/graphics/vulkan/device_detail.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compressed_format_tests{

namespace Format = Core::Format;
using Core::GraphicsBackend::ConvertFormat;
namespace VulkanDetail = Core::GraphicsBackend::VulkanDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CompressedTextureFormats, MapsEveryAstcAndBcFormatForInitializationProbe){
    for(u32 formatValue = static_cast<u32>(Format::BC1_UNORM); formatValue <= static_cast<u32>(Format::ASTC_12x12_FLOAT); ++formatValue){
        const Format::Enum format = static_cast<Format::Enum>(formatValue);
        EXPECT_TRUE(Format::IsBlockCompressedFormat(format));
        EXPECT_NE(ConvertFormat(format), VK_FORMAT_UNDEFINED);
    }
}

TEST(CompressedTextureFormats, ClassifiesBcAndAstcHdrFamilies){
    EXPECT_TRUE(Format::IsBCCompressedFormat(Format::BC1_UNORM));
    EXPECT_TRUE(Format::IsBCCompressedFormat(Format::BC7_UNORM_SRGB));
    EXPECT_FALSE(Format::IsBCCompressedFormat(Format::ASTC_4x4_UNORM));

    EXPECT_TRUE(Format::IsASTCCompressedFormat(Format::ASTC_4x4_UNORM));
    EXPECT_TRUE(Format::IsASTCCompressedFormat(Format::ASTC_12x12_FLOAT));
    EXPECT_FALSE(Format::IsASTCCompressedFormat(Format::RGBA8_UNORM));

    EXPECT_TRUE(Format::IsASTCHdrFormat(Format::ASTC_4x4_FLOAT));
    EXPECT_TRUE(Format::IsASTCHdrFormat(Format::ASTC_12x12_FLOAT));
    EXPECT_FALSE(Format::IsASTCHdrFormat(Format::ASTC_4x4_UNORM));
    EXPECT_FALSE(Format::IsASTCHdrFormat(Format::ASTC_4x4_UNORM_SRGB));
}

TEST(CompressedTextureFormats, RequiresTheEnabledFeatureForEachCompressionFamily){
    VulkanDetail::CompressedTextureFeatureState features;
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::BC1_UNORM, features));
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_4x4_UNORM, features));
    EXPECT_FALSE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::ASTC_4x4_FLOAT, features));
    EXPECT_TRUE(VulkanDetail::IsCompressedTextureFormatFeatureEnabled(Format::RGBA8_UNORM, features));

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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

