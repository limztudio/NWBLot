// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font_atlas/model.h>

#include <gtest/gtest.h>

#include <global/simplemath.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_glyph_sdf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShaderFloat4{
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};
using float4 = ShaderFloat4;
using uint = u32;

[[nodiscard]] ShaderFloat4 operator*(const ShaderFloat4 value, const f32 scale){
    const SIMDVector scaled = VectorScale(VectorSet(value.x, value.y, value.z, value.w), scale);
    return { VectorGetX(scaled), VectorGetY(scaled), VectorGetZ(scaled), VectorGetW(scaled) };
}

[[nodiscard]] f32 saturate(const f32 value){
    return Clamp(value, 0.f, 1.f);
}

[[nodiscard]] f32 max(const f32 first, const f32 second){
    return Max(first, second);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compile the production shader helpers unchanged; the pixel shader supplies fwidth(distance) as pixelWidth.
#include <impl/assets/graphics/ui/glyph_sdf.slangi>
#include <impl/assets/graphics/ui/glyph_coverage.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiGlyphSdf, FreeTypeZeroIs128Over255AndExteriorInteriorClampToEndpoints){
    EXPECT_NEAR(nwbUiSdfDistance(128.f / 255.f, 8u), 0.f, 0.000001f);
    EXPECT_NEAR(nwbUiSdfCoverage(nwbUiSdfDistance(128.f / 255.f, 8u), 1.f), 0.5f, 0.000001f);
    EXPECT_FLOAT_EQ(nwbUiSdfDistance(0.f, 8u), -8.f);
    EXPECT_FLOAT_EQ(nwbUiSdfDistance(1.f, 8u), 7.9375f);
    EXPECT_FLOAT_EQ(nwbUiSdfCoverage(nwbUiSdfDistance(0.f, 8u), 1.f), 0.f);
    EXPECT_FLOAT_EQ(nwbUiSdfCoverage(nwbUiSdfDistance(1.f, 8u), 1.f), 1.f);
    // A generic 0.5 threshold is incorrect for FreeType's asymmetric u8 encoding.
    EXPECT_FLOAT_EQ(nwbUiSdfCoverage(nwbUiSdfDistance(0.5f, 8u), 1.f), 0.46875f);
}


TEST(UiGlyphSdf, FlatFieldsRemainFiniteAcrossEveryAdmittedSpread){
    EXPECT_FLOAT_EQ(nwbUiSdfCoverage(0.f, 0.f), 0.5f);
    // Fused FP32 decoding can leave a small residual; the flat-field epsilon bounds it at every admitted spread.
    for(u32 spread = NWB::Impl::s_FontAtlasMinSpreadPixels; spread <= NWB::Impl::s_FontAtlasMaxSpreadPixels; ++spread)
        EXPECT_NEAR(nwbUiSdfCoverage(nwbUiSdfDistance(128.f / 255.f, spread), 0.f), 0.5f, 0.01f);
    EXPECT_FLOAT_EQ(nwbUiSdfCoverage(-1.f, 0.f), 0.f);
    EXPECT_FLOAT_EQ(nwbUiSdfCoverage(1.f, 0.f), 1.f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

