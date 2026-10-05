// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_output_layer_color_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compile the production shader helper unchanged against a small scalar implementation of its vector intrinsics.
// These tests exercise its actual transfer/mapping functions without duplicating the color equations in C++.
struct ShaderFloat3{
    f64 x;
    f64 y;
    f64 z;

    ShaderFloat3(const f64 scalar)
        : x(scalar)
        , y(scalar)
        , z(scalar){}
    ShaderFloat3(const f64 red, const f64 green, const f64 blue)
        : x(red)
        , y(green)
        , z(blue){}
};
using float3 = ShaderFloat3;

[[nodiscard]] ShaderFloat3 operator+(const ShaderFloat3 a, const ShaderFloat3 b){
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

[[nodiscard]] ShaderFloat3 operator-(const ShaderFloat3 a, const ShaderFloat3 b){
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

[[nodiscard]] ShaderFloat3 operator*(const ShaderFloat3 a, const ShaderFloat3 b){
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}

[[nodiscard]] ShaderFloat3 operator/(const ShaderFloat3 a, const ShaderFloat3 b){
    return { a.x / b.x, a.y / b.y, a.z / b.z };
}

[[nodiscard]] f64 dot(const ShaderFloat3 a, const ShaderFloat3 b){
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] ShaderFloat3 max(const ShaderFloat3 a, const ShaderFloat3 b){
    return { Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z) };
}

[[nodiscard]] f32 saturate(const f32 a){
    return Clamp(a, 0.f, 1.f);
}

[[nodiscard]] ShaderFloat3 saturate(const ShaderFloat3 a){
    return { Clamp(a.x, 0., 1.), Clamp(a.y, 0., 1.), Clamp(a.z, 0., 1.) };
}

[[nodiscard]] ShaderFloat3 pow(const ShaderFloat3 a, const f64 b){
    return { Pow(a.x, b), Pow(a.y, b), Pow(a.z, b) };
}

[[nodiscard]] ShaderFloat3 step(const ShaderFloat3 a, const ShaderFloat3 b){
    return { b.x >= a.x ? 1. : 0., b.y >= a.y ? 1. : 0., b.z >= a.z ? 1. : 0. };
}

[[nodiscard]] ShaderFloat3 lerp(const ShaderFloat3 a, const ShaderFloat3 b, const ShaderFloat3 t){
    return a + (b - a) * t;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets/graphics/common/hdr10.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectColorNear(const ShaderFloat3 actual, const ShaderFloat3 expected, const f64 tolerance = 0.000001){
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

TEST(OutputLayerColor, TransparentAndOpaqueEndpointsPreservePremultipliedInputs){
    ExpectColorNear(nwbOutputLayerOver(float3(0.25), float3(0.0), 0.f), float3(0.25));
    ExpectColorNear(nwbOutputLayerOver(float3(0.25), float3(0.75), 1.f), float3(0.75));
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

