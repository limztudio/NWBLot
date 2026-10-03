// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_surfel_coverage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compile the production Slang math as C++ so these assertions exercise the same coverage path as the GPU.
struct ShaderFloat3{
    float x;
    float y;
    float z;

    explicit ShaderFloat3(float value) : x(value), y(value), z(value){}
    ShaderFloat3(float red, float green, float blue) : x(red), y(green), z(blue){}
};

struct ShaderFloat4{
    float x;
    float y;
    float z;
    float w;

    ShaderFloat4(ShaderFloat3 rgb, float alpha) : x(rgb.x), y(rgb.y), z(rgb.z), w(alpha){}
};
using float3 = ShaderFloat3;
using float4 = ShaderFloat4;

ShaderFloat3 operator/(ShaderFloat3 value, float divisor){
    return ShaderFloat3(value.x / divisor, value.y / divisor, value.z / divisor);
}

// Provide the Slang `exp` builtin for the C++ compilation through the project math wrapper.
inline float exp(const float value){ return ::Exp(value); }

#include <impl/assets/graphics/gi/surfel/surfel_coverage.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SurfelCoverage, GatherWeightEncodesEmptyPartialAndFullSupport){
    const float full = NWB_SURFEL_GATHER_FULL_COVERAGE_WEIGHT;
    ASSERT_GT(full, 0.0f);
    EXPECT_FLOAT_EQ(nwbSurfelGatherCoverage(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(nwbSurfelGatherCoverage(full * 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(nwbSurfelGatherCoverage(full), 1.0f);
    EXPECT_FLOAT_EQ(nwbSurfelGatherCoverage(full * 2.0f), 1.0f);
}

TEST(SurfelCoverage, UpsampleRejectsIncompatibleNormalsAndDistantGeometry){
    const float gate = NWB_SURFEL_UPSAMPLE_NORMAL_GATE;
    EXPECT_TRUE(nwbSurfelUpsampleNormalCompatible(gate));
    EXPECT_FALSE(nwbSurfelUpsampleNormalCompatible(gate - 0.01f));
    EXPECT_FALSE(nwbSurfelUpsampleNormalCompatible(0.0f));

    EXPECT_FLOAT_EQ(nwbSurfelUpsampleGeometryWeight(0.25f, 0.0f, 2.0f), 0.25f);
    EXPECT_LT(nwbSurfelUpsampleGeometryWeight(0.25f, 4.0f, 2.0f), 0.001f);
}

TEST(SurfelCoverage, MissingAndPartialTapsRetainFractionalAlpha){
    const float weight = nwbSurfelUpsampleGeometryWeight(0.25f, 0.0f, 1.0f);
    const float missing = nwbSurfelUpsampleCoveredWeight(weight, 0.0f);
    const float partial = nwbSurfelUpsampleCoveredWeight(weight, 0.5f);
    const float full = nwbSurfelUpsampleCoveredWeight(weight, 1.0f);
    EXPECT_FLOAT_EQ(missing, 0.0f);
    EXPECT_FLOAT_EQ(partial, 0.125f);
    EXPECT_FLOAT_EQ(full, 0.25f);

    // A fully covered tap and a missing compatible tap have half coverage, not a renormalized 1.0.
    const float4 result = nwbSurfelUpsampleFinalize(float3(0.5f, 0.25f, 0.125f), full, weight * 2.0f);
    EXPECT_FLOAT_EQ(result.x, 2.0f);
    EXPECT_FLOAT_EQ(result.y, 1.0f);
    EXPECT_FLOAT_EQ(result.z, 0.5f);
    EXPECT_FLOAT_EQ(result.w, 0.5f);

    const float4 partialOnly = nwbSurfelUpsampleFinalize(float3(0.125f, 0.0f, 0.0f), partial, weight);
    EXPECT_FLOAT_EQ(partialOnly.x, 1.0f);
    EXPECT_FLOAT_EQ(partialOnly.w, 0.5f);
}

TEST(SurfelCoverage, EmptyAndFullUpsampleEndpointsAreStable){
    const float4 empty = nwbSurfelUpsampleFinalize(float3(0.0f), 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(empty.x, 0.0f);
    EXPECT_FLOAT_EQ(empty.y, 0.0f);
    EXPECT_FLOAT_EQ(empty.z, 0.0f);
    EXPECT_FLOAT_EQ(empty.w, 0.0f);

    const float4 full = nwbSurfelUpsampleFinalize(float3(3.0f, 2.0f, 1.0f), 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(full.x, 3.0f);
    EXPECT_FLOAT_EQ(full.y, 2.0f);
    EXPECT_FLOAT_EQ(full.z, 1.0f);
    EXPECT_FLOAT_EQ(full.w, 1.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

