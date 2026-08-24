// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TimerQueryResult, ConvertsStraightSixtyFourBitTickRange){
    const Core::TimerQueryResult result{
        .beginTicks = 100u,
        .endTicks = 145u,
        .secondsPerTick = 0.25,
        .timestampValidBits = 64u,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.timestampMask(), Limit<u64>::s_Max);
    EXPECT_EQ(result.maskedBeginTicks(), 100u);
    EXPECT_EQ(result.durationTicks(), 45u);
    EXPECT_DOUBLE_EQ(result.beginSeconds(), 25.0);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 11.25);
    EXPECT_DOUBLE_EQ(result.endSeconds(), 36.25);
}

TEST(TimerQueryResult, MasksHighBitsAndWrapsPartialWidth){
    const Core::TimerQueryResult result{
        .beginTicks = 0x1fau,
        .endTicks = 0x105u,
        .secondsPerTick = 0.5,
        .timestampValidBits = 8u,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0xffu);
    EXPECT_EQ(result.maskedBeginTicks(), 0xfau);
    EXPECT_EQ(result.durationTicks(), 11u);
    EXPECT_DOUBLE_EQ(result.beginSeconds(), 125.0);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 5.5);
    EXPECT_DOUBLE_EQ(result.endSeconds(), 130.5);
    EXPECT_GE(result.endSeconds(), result.beginSeconds());
}

TEST(TimerQueryResult, WrapsFullWidthWithoutShiftingBySixtyFour){
    const Core::TimerQueryResult result{
        .beginTicks = Limit<u64>::s_Max - 3u,
        .endTicks = 5u,
        .secondsPerTick = 1.0,
        .timestampValidBits = 64u,
    };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.durationTicks(), 9u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 9.0);
    EXPECT_GE(result.endSeconds(), result.beginSeconds());
}

TEST(TimerQueryResult, RejectsUnknownOrOutOfRangeWidths){
    Core::TimerQueryResult result{
        .beginTicks = 10u,
        .endTicks = 20u,
        .secondsPerTick = 1.0,
        .timestampValidBits = 0u,
    };

    EXPECT_FALSE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0u);
    EXPECT_EQ(result.durationTicks(), 0u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 0.0);

    result.timestampValidBits = 65u;
    EXPECT_FALSE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0u);

    result.timestampValidBits = 64u;
    result.secondsPerTick = 0.0;
    EXPECT_FALSE(result.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

