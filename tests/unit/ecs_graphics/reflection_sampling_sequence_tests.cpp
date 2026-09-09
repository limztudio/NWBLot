// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/reflection/sampling_sequence.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_sampling_sequence_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


TEST(ReflectionSamplingSequence, FirstPointsAndHighIndexBitsRemainExact){
    const ReflectionSampleBase expected[] = {
        {0u, 0u}, {0x80000000u, 0x80000000u}, {0x40000000u, 0xc0000000u}, {0xc0000000u, 0x40000000u},
        {0x20000000u, 0xa0000000u}, {0xa0000000u, 0x20000000u}, {0x60000000u, 0x60000000u}, {0xe0000000u, 0xe0000000u},
    };
    for(u32 index = 0u; index < 8u; ++index){
        const ReflectionSampleBase sample = ComputeReflectionSampleBase(index);
        EXPECT_EQ(sample.x, expected[index].x);
        EXPECT_EQ(sample.y, expected[index].y);
    }
    EXPECT_EQ(ComputeReflectionSampleBase(0x80000000u).x, 1u);
    EXPECT_EQ(ComputeReflectionSampleBase(0xffffffffu).x, 0xffffffffu);
    EXPECT_NE(ComputeReflectionSampleBase(16u).x, ComputeReflectionSampleBase(0u).x);
    EXPECT_NE(ComputeReflectionSampleBase(256u).x, ComputeReflectionSampleBase(0u).x);
}

TEST(ReflectionSamplingSequence, EveryPowerOfTwoPrefixStratifiesAllElementaryRectangles){
    // A digital net must visit each equal-area dyadic rectangle once, including very wide and very tall cells.
    // This distribution property catches incorrect generator recurrence and accidental history-length wrapping.
    for(u32 bits = 1u; bits <= 8u; ++bits){
        const u32 count = 1u << bits;
        for(u32 xBits = 0u; xBits <= bits; ++xBits){
            const u32 yBits = bits - xBits;
            bool occupied[256] = {};
            for(u32 index = 0u; index < count; ++index){
                const ReflectionSampleBase sample = ComputeReflectionSampleBase(index);
                const u32 x = xBits == 0u ? 0u : sample.x >> (32u - xBits);
                const u32 y = yBits == 0u ? 0u : sample.y >> (32u - yBits);
                const u32 cell = (y << xBits) | x;
                ASSERT_LT(cell, count);
                EXPECT_FALSE(occupied[cell]);
                occupied[cell] = true;
            }
            for(u32 cell = 0u; cell < count; ++cell)
                EXPECT_TRUE(occupied[cell]);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

