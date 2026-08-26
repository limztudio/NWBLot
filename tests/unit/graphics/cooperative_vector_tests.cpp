// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


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
        { CooperativeVectorDataType::UInt16, 2u },
        { CooperativeVectorDataType::SInt16, 2u },
        { CooperativeVectorDataType::UInt32, 4u },
        { CooperativeVectorDataType::SInt32, 4u },
        { CooperativeVectorDataType::UInt64, 8u },
        { CooperativeVectorDataType::SInt64, 8u },
        { CooperativeVectorDataType::FloatE4M3, 1u },
        { CooperativeVectorDataType::FloatE5M2, 1u },
        { CooperativeVectorDataType::Float16, 2u },
        { CooperativeVectorDataType::BFloat16, 2u },
        { CooperativeVectorDataType::Float32, 4u },
        { CooperativeVectorDataType::Float64, 8u },
    };

    for(const TypeCase& typeCase : s_TypeCases){
        const usize rowByteSize = typeCase.byteSize * 3u;
        const usize columnByteSize = typeCase.byteSize * 2u;
        const usize rowStride = GetCooperativeVectorOptimalMatrixStride(
            typeCase.type,
            CooperativeVectorMatrixLayout::RowMajor,
            2u,
            3u
        );
        const usize columnStride = GetCooperativeVectorOptimalMatrixStride(
            typeCase.type,
            CooperativeVectorMatrixLayout::ColumnMajor,
            2u,
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
        2u,
        2u
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::TrainingOptimal,
        2u,
        2u
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::RowMajor,
        0u,
        2u
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::ColumnMajor,
        2u,
        0u
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        static_cast<CooperativeVectorDataType::Enum>(Limit<u8>::s_Max),
        CooperativeVectorMatrixLayout::RowMajor,
        2u,
        2u
    ), 0u);
    EXPECT_EQ(GetCooperativeVectorOptimalMatrixStride(
        CooperativeVectorDataType::Float32,
        static_cast<CooperativeVectorMatrixLayout::Enum>(Limit<u8>::s_Max),
        2u,
        2u
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

