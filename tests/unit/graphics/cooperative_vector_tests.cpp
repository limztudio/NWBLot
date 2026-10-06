// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/rhi/coopvec.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooperative_vector_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CooperativeVectorMatrixStride, FailsClosedForInvalidOrDriverOptimalInputs){
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::InferencingOptimal,
        s_ExpectedDualCount,
        s_ExpectedDualCount
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::TrainingOptimal,
        s_ExpectedDualCount,
        s_ExpectedDualCount
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::RowMajor,
        0u,
        s_ExpectedDualCount
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::ColumnMajor,
        s_ExpectedDualCount,
        0u
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        static_cast<CooperativeVectorDataType::Enum>(Limit<u8>::s_Max),
        CooperativeVectorMatrixLayout::RowMajor,
        s_ExpectedDualCount,
        s_ExpectedDualCount
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        static_cast<CooperativeVectorMatrixLayout::Enum>(Limit<u8>::s_Max),
        s_ExpectedDualCount,
        s_ExpectedDualCount
    ), 0u);
}

TEST(CooperativeVectorMatrixStride, DoesNotWrapLargestPublicDimension){
    const usize stride = GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float64,
        CooperativeVectorMatrixLayout::RowMajor,
        1u,
        Limit<u32>::s_Max
    );
    if constexpr(sizeof(usize) > sizeof(u32))
        EXPECT_EQ(stride, (static_cast<usize>(Limit<u32>::s_Max) + 1u) * 8u);
    else
        EXPECT_EQ(stride, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

