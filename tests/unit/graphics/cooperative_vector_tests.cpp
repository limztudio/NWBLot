// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooperative_vector_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CooperativeVectorMatrixStride, PadsEveryStandardLayoutByOneElement){
    struct TypeCase{
        CooperativeVectorDataType::Enum type;
        usize byteSize;
    };
    constexpr TypeCase s_TypeCases[]{
        { CooperativeVectorDataType::UInt8, 1u },
        { CooperativeVectorDataType::SInt8, 1u },
        { CooperativeVectorDataType::UInt8Packed, 1u },
        { CooperativeVectorDataType::SInt8Packed, 1u },
        { CooperativeVectorDataType::UInt16, s_ExpectedDualCount },
        { CooperativeVectorDataType::SInt16, s_ExpectedDualCount },
        { CooperativeVectorDataType::UInt32, 4u },
        { CooperativeVectorDataType::SInt32, 4u },
        { CooperativeVectorDataType::UInt64, 8u },
        { CooperativeVectorDataType::SInt64, 8u },
        { CooperativeVectorDataType::FloatE4M3, 1u },
        { CooperativeVectorDataType::FloatE5M2, 1u },
        { CooperativeVectorDataType::Float16, s_ExpectedDualCount },
        { CooperativeVectorDataType::BFloat16, s_ExpectedDualCount },
        { CooperativeVectorDataType::Float32, 4u },
        { CooperativeVectorDataType::Float64, 8u },
    };

    for(const TypeCase& typeCase : s_TypeCases){
        const usize rowByteSize = typeCase.byteSize * 3u;
        const usize columnByteSize = typeCase.byteSize * s_ExpectedDualCount;
        const usize rowStride = GetCooperativeVectorOptimalMatrixStride(
            typeCase.type,
            CooperativeVectorMatrixLayout::RowMajor,
            s_ExpectedDualCount,
            3u
        );
        const usize columnStride = GetCooperativeVectorOptimalMatrixStride(
            typeCase.type,
            CooperativeVectorMatrixLayout::ColumnMajor,
            s_ExpectedDualCount,
            3u
        );

        EXPECT_EQ(rowStride, rowByteSize + typeCase.byteSize);
        EXPECT_EQ(columnStride, columnByteSize + typeCase.byteSize);
        EXPECT_GT(rowStride, rowByteSize);
        EXPECT_GT(columnStride, columnByteSize);
    }
}

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

