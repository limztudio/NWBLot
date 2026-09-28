// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_glyph_coverage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShaderFloat4{
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};
using float4 = ShaderFloat4;

[[nodiscard]] ShaderFloat4 operator*(const ShaderFloat4 value, const f32 scale){
    return { value.x * scale, value.y * scale, value.z * scale, value.w * scale };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Exercise the production shader helper unchanged; coverage is linear and vertex color is already premultiplied.
#include <impl/assets/graphics/ui/glyph_coverage.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiGlyphCoverage, TransparentAndOpaqueCoveragePreservePremultipliedEndpoints){
    const ShaderFloat4 tint{ 0.5f, 0.25f, 0.125f, 0.5f };
    const ShaderFloat4 transparent = nwbUiGlyphCoverageColor(tint, 0.f);
    EXPECT_FLOAT_EQ(transparent.x, 0.f);
    EXPECT_FLOAT_EQ(transparent.y, 0.f);
    EXPECT_FLOAT_EQ(transparent.z, 0.f);
    EXPECT_FLOAT_EQ(transparent.w, 0.f);
    const ShaderFloat4 opaque = nwbUiGlyphCoverageColor(tint, 1.f);
    EXPECT_FLOAT_EQ(opaque.x, tint.x);
    EXPECT_FLOAT_EQ(opaque.y, tint.y);
    EXPECT_FLOAT_EQ(opaque.z, tint.z);
    EXPECT_FLOAT_EQ(opaque.w, tint.w);
}

TEST(UiGlyphCoverage, HalfCoverageAppliesOnceToColorAndOpacityWithoutSrgbDecode){
    const ShaderFloat4 result = nwbUiGlyphCoverageColor({ 0.5f, 0.25f, 0.125f, 0.5f }, 0.5f);
    EXPECT_FLOAT_EQ(result.x, 0.25f);
    EXPECT_FLOAT_EQ(result.y, 0.125f);
    EXPECT_FLOAT_EQ(result.z, 0.0625f);
    EXPECT_FLOAT_EQ(result.w, 0.25f);
    // Premultiplied source-over leaves three quarters of the destination at this coverage and tint opacity.
    EXPECT_FLOAT_EQ(result.x + 0.2f * (1.f - result.w), 0.4f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

