// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_output_layer_color_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compile the production shader helper unchanged against a binary64 implementation of its vector intrinsics.
// These tests exercise its actual transfer/mapping functions without duplicating the color equations in C++.
struct ShaderFloat3{
    f64 x;
    f64 y;
    f64 z;

    ShaderFloat3(const f64 scalar)noexcept
        : x(scalar)
        , y(scalar)
        , z(scalar){}
    ShaderFloat3(const f64 red, const f64 green, const f64 blue)noexcept
        : x(red)
        , y(green)
        , z(blue){}
};
using float3 = ShaderFloat3;

[[nodiscard]] ShaderFloat3 operator+(const ShaderFloat3 a, const ShaderFloat3 b)noexcept{
    const SIMDVectorDouble4 result = SIMDVectorDouble4{ a.x, a.y, a.z, 0. } + SIMDVectorDouble4{ b.x, b.y, b.z, 0. };
    return { result.x, result.y, result.z };
}

[[nodiscard]] ShaderFloat3 operator*(const ShaderFloat3 a, const ShaderFloat3 b)noexcept{
    const SIMDVectorDouble4 result = SIMDVectorDouble4{ a.x, a.y, a.z, 0. } * SIMDVectorDouble4{ b.x, b.y, b.z, 0. };
    return { result.x, result.y, result.z };
}

[[nodiscard]] ShaderFloat3 operator/(const ShaderFloat3 a, const ShaderFloat3 b)noexcept{
    const SIMDVectorDouble4 result = SIMDVectorDouble4{ a.x, a.y, a.z, 0. } / SIMDVectorDouble4{ b.x, b.y, b.z, 1. };
    return { result.x, result.y, result.z };
}

[[nodiscard]] f64 dot(const ShaderFloat3 a, const ShaderFloat3 b)noexcept{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] ShaderFloat3 max(const ShaderFloat3 a, const ShaderFloat3 b)noexcept{
    const SIMDVectorDouble4 first{ a.x, a.y, a.z, 0. };
    const SIMDVectorDouble4 second{ b.x, b.y, b.z, 0. };
    const SIMDVectorDouble4 result = (first > second) ? first : second;
    return { result.x, result.y, result.z };
}

[[nodiscard]] f32 saturate(const f32 a)noexcept{
    return Clamp(a, 0.f, 1.f);
}

[[nodiscard]] ShaderFloat3 saturate(const ShaderFloat3 a)noexcept{
    const SIMDVectorDouble4 value{ a.x, a.y, a.z, 0. };
    const SIMDVectorDouble4 zero{ 0., 0., 0., 0. };
    const SIMDVectorDouble4 one{ 1., 1., 1., 1. };
    const SIMDVectorDouble4 lower = (value > zero) ? value : zero;
    const SIMDVectorDouble4 result = (lower > one) ? one : lower;
    return { result.x, result.y, result.z };
}

[[nodiscard]] ShaderFloat3 pow(const ShaderFloat3 a, const f64 b)noexcept{
    return { Pow(a.x, b), Pow(a.y, b), Pow(a.z, b) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets/graphics/common/hdr10.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectColorNear(const ShaderFloat3 actual, const ShaderFloat3 expected, const f64 tolerance = 0.000001){
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}


TEST(OutputLayerColor, TransparentHdrLayerPreservesSceneAndOpaqueUiIgnoresSceneExposure){
    const float3 scene(0.1, 3., 100.);
    ExpectColorNear(nwbHdr10EncodeSceneWithUi(scene, float3(0.), 0.f), nwbHdr10EncodeScene(scene));
    const float3 ui(0.2, 0.5, 0.9);
    const float3 expected = nwbHdr10EncodeUiLinear(ui);
    ExpectColorNear(nwbHdr10EncodeSceneWithUi(scene, ui, 1.f), expected);
    ExpectColorNear(nwbHdr10EncodeSceneWithUi(scene * 64., ui, 1.f), expected);
}

TEST(OutputLayerColor, HdrBlendsInLinearNitsBeforeOnePqEncoding){
    // Half-opacity paper white over black is 101.5 nits, not half of the encoded paper-white signal.
    const float3 actual = nwbHdr10EncodeSceneWithUi(float3(0.), float3(0.5), 0.5f);
    ExpectColorNear(actual, nwbHdr10PqEncodeNits(float3(101.5)));
    const float3 encodedBlend = nwbHdr10EncodeUiLinear(float3(1.)) * 0.5;
    EXPECT_GT(actual.x - encodedBlend.x, 0.1);
}

TEST(OutputLayerColor, NegativeUiRadianceClampsAndHighlightsRemainBelowTheOutputBound){
    EXPECT_GT(nwbHdr10SceneToNits(float3(100.)).x, nwbHdr10SceneToNits(float3(1.)).x);
    EXPECT_LT(nwbHdr10SceneToNits(float3(100.)).x, 1000.);
    ExpectColorNear(nwbHdr10EncodeUiLinear(float3(-1.)), nwbHdr10EncodeUiLinear(float3(0.)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

