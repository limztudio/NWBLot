// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FrustumAffineBoundaries, NonuniformScaleAndShearRetainInteriorPoints){
    const BoundingFrustum source(Float3U(0.0f, 0.0f, 0.0f), Float4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 2.0f);
    const BoundingDoubleFrustum doubleSource(Double3U(0.0, 0.0, 0.0), Double4(0.0, 0.0, 0.0, 1.0), 1.0, -1.0, 1.0, -1.0, 1.0, 2.0);
    SIMDMatrix matrices[]{ MatrixScaling(2.0f, 1.0f, 1.0f), MatrixIdentity(), MatrixScaling(-2.0f, 1.0f, 1.0f), MatrixIdentity(), MatrixScaling(1.0f, 1.0f, 0.0f) };
    matrices[1].v[0] = VectorSet(1.0f, 1.0f, 0.0f, 0.0f);
    matrices[3].v[2] = VectorSet(2.0f, 0.0f, 1.0f, 0.0f);
    SIMDMatrixDouble doubleMatrices[]{ MatrixDoubleScaling(2.0, 1.0, 1.0), MatrixDoubleIdentity(), MatrixDoubleScaling(-2.0, 1.0, 1.0), MatrixDoubleIdentity(), MatrixDoubleScaling(1.0, 1.0, 0.0) };
    doubleMatrices[1].v[0] = SIMDVectorDouble{ 1.0, 1.0, 0.0, 0.0 };
    doubleMatrices[3].v[2] = SIMDVectorDouble{ 2.0, 0.0, 1.0, 0.0 };
    for(usize index = 0u; index < LengthOf(matrices); ++index){
        SCOPED_TRACE(index);
        BoundingFrustum transformed;
        BoundingDoubleFrustum doubleTransformed;
        source.transform(transformed, matrices[index]);
        doubleSource.transform(doubleTransformed, doubleMatrices[index]);
        for(i32 xSign = -1; xSign <= 1; xSign += 2){
            for(i32 ySign = -1; ySign <= 1; ySign += 2){
                for(i32 depth = 0; depth < 2; ++depth){
                    const f32 z = depth == 0 ? 1.01f : 1.99f;
                    const SIMDVectorSingle point = VectorPermute<0, 1, 6, 7>(VectorScale(VectorScale(VectorSet(static_cast<f32>(xSign), static_cast<f32>(ySign), 0.0f, 0.0f), z), 0.99f), VectorSet(0.0f, 0.0f, z, 1.0f));
                    ASSERT_EQ(source.contains(point), ContainmentType::Contains);
                    EXPECT_EQ(transformed.contains(Vector3Transform(point, matrices[index])), ContainmentType::Contains);
                    const f64 doubleZ = depth == 0 ? 1.01 : 1.99;
                    const SIMDVectorDouble doublePoint = VectorDoublePermute<0, 1, 6, 7>(VectorDoubleScale(VectorDoubleScale(VectorDoubleSet(static_cast<f64>(xSign), static_cast<f64>(ySign), 0.0, 0.0), doubleZ), 0.99), VectorDoubleSet(0.0, 0.0, doubleZ, 1.0));
                    ASSERT_EQ(doubleSource.contains(doublePoint), ContainmentType::Contains);
                    EXPECT_EQ(doubleTransformed.contains(VectorDouble3Transform(doublePoint, doubleMatrices[index])), ContainmentType::Contains);
                }
            }
        }
    }
}

TEST(FrustumAffineBoundaries, NegativeUniformScaleRetainsAsymmetricInteriorPoints){
    const BoundingFrustum source(Float3U(0.0f, 0.0f, 0.0f), Float4(0.0f, 0.0f, 0.0f, 1.0f), 2.0f, -1.0f, 3.0f, -0.5f, 1.0f, 2.0f);
    BoundingFrustum transformed;
    source.transform(transformed, -2.0f, s_SIMDIdentityR3, VectorZero());
    const SIMDVectorSingle point = VectorSet(2.0f, 3.0f, 1.5f, 1.0f);
    ASSERT_EQ(source.contains(point), ContainmentType::Contains);
    EXPECT_EQ(transformed.contains(VectorScale(point, -2.0f)), ContainmentType::Contains);

    const BoundingDoubleFrustum doubleSource(Double3U(0.0, 0.0, 0.0), Double4(0.0, 0.0, 0.0, 1.0), 2.0, -1.0, 3.0, -0.5, 1.0, 2.0);
    BoundingDoubleFrustum doubleTransformed;
    doubleSource.transform(doubleTransformed, -2.0, s_DoubleSIMDIdentityR3, VectorDoubleZero());
    const SIMDVectorDouble doublePoint{ 2.0, 3.0, 1.5, 1.0 };
    ASSERT_EQ(doubleSource.contains(doublePoint), ContainmentType::Contains);
    EXPECT_EQ(doubleTransformed.contains(VectorDoubleScale(doublePoint, -2.0)), ContainmentType::Contains);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

