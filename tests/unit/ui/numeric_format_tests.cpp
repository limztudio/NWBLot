// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/edit/numeric_parse.h>

#include <global/bit.h>
#include <global/text_numeric_format.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_format_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiNumericFormatTests, IntegerCanonicalTextRoundtripsExtremaWithoutFloatingPointConversion){
    const i64 values[]{ 9007199254740993ll, -9007199254740993ll, Limit<i64>::s_Min, Limit<i64>::s_Max };
    for(const i64 value : values){
        SCOPED_TRACE(value);
        char buffer[s_NumericEditMaxBytes];
        const AStringView text = FormatI64(value, buffer);
        ASSERT_FALSE(text.empty());
        const auto parsed = ParseIntegerDraft(text);
        ASSERT_TRUE(parsed);
        EXPECT_EQ(*parsed, value);
        if(value == 9007199254740993ll)
            EXPECT_EQ(text, "9007199254740993");
        if(value == Limit<i64>::s_Min)
            EXPECT_EQ(text, "-9223372036854775808");
    }
}

TEST(UiNumericFormatTests, ExtremeSubnormalSignedZeroAndAdjacentUlpValuesRoundtripWithExactBits){
    const u64 patterns[]{
        0x8000000000000000ull, 0x0000000000000001ull, 0x8000000000000001ull,
        0x000fffffffffffffull, 0x0010000000000000ull, 0x3fefffffffffffffull, 0x3ff0000000000001ull, 0x4340000000000001ull,
        0x7fefffffffffffffull, 0xffefffffffffffffull
    };
    for(const u64 bits : patterns){
        SCOPED_TRACE(bits);
        char buffer[s_NumericEditMaxBytes];
        const AStringView text = FormatF64(BitCast<f64>(bits), buffer);
        ASSERT_FALSE(text.empty());
        const auto parsed = ParseFloatDraft(text);
        ASSERT_TRUE(parsed);
        EXPECT_EQ(BitCast<u64>(*parsed), bits);
    }
}

TEST(UiNumericFormatTests, InsufficientCallerStorageReturnsEmptyWithoutWritingOutsideTheBuffer){
    struct Storage{
        char first = 'a';
        char buffer[1u]{ 'x' };
        char last = 'z';
    };
    Storage integer;
    EXPECT_TRUE(FormatI64(Limit<i64>::s_Min, integer.buffer).empty());
    EXPECT_EQ(integer.first, 'a');
    EXPECT_EQ(integer.last, 'z');
    Storage floating;
    EXPECT_TRUE(FormatF64(123.25, floating.buffer).empty());
    EXPECT_EQ(floating.first, 'a');
    EXPECT_EQ(floating.last, 'z');
}

TEST(UiNumericFormatTests, NonfiniteFloatFormattingIsRejected){
    char buffer[s_NumericEditMaxBytes];
    EXPECT_TRUE(FormatF64(Limit<f64>::s_Infinity, buffer).empty());
    EXPECT_TRUE(FormatF64(-Limit<f64>::s_Infinity, buffer).empty());
    EXPECT_TRUE(FormatF64(Limit<f64>::s_QuietNaN, buffer).empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

