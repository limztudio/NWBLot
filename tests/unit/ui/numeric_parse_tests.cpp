// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/edit/numeric_parse.h>

#include <global/bit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_parse_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

struct ParseCase{
    AStringView text;
    NumericParseStatus::Enum status = NumericParseStatus::Invalid;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiNumericParseTests, IntegerPrefixesAndMalformedSyntaxLeaveOutputUntouched){
    const ParseCase cases[]{
        { "", NumericParseStatus::Incomplete }, { "  ", NumericParseStatus::Incomplete },
        { "+", NumericParseStatus::Incomplete }, { "-", NumericParseStatus::Incomplete },
        { " + ", NumericParseStatus::Incomplete }, { "--1" }, { "+-1" }, { "1 2" }, { "1,000" },
        { "1_000" }, { "0x10" }, { "1.0" }, { "1e2" }, { "12x" }, { "nan" }, { "inf" },
        { "\xE2\x88\x92" "1" }, { "\xD9\xA1" }, { "\xC2\xA0" "1" },
        { AStringView("1\0", 2u) }, { "999999999999999999999999x" },
        { "9223372036854775808", NumericParseStatus::OutOfRange },
        { "-9223372036854775809", NumericParseStatus::OutOfRange }
    };
    for(const ParseCase& test : cases){
        SCOPED_TRACE(test.text);
        i64 output = -13579;
        EXPECT_EQ(ParseIntegerDraft(test.text, output), test.status);
        EXPECT_EQ(output, -13579);
    }
}

TEST(UiNumericParseTests, IntegerConversionPreservesFullSignedRangeAndLargeExactValues){
    struct CompleteCase{
        AStringView text;
        i64 value = 0;
    };
    const CompleteCase cases[]{
        { "0", 0 }, { "-0", 0 },
        { "9007199254740993", 9007199254740993ll },
        { "-9223372036854775808", Limit<i64>::s_Min },
        { "+9223372036854775807", Limit<i64>::s_Max }
    };
    for(const CompleteCase& test : cases){
        SCOPED_TRACE(test.text);
        i64 output = 13;
        ASSERT_EQ(ParseIntegerDraft(test.text, output), NumericParseStatus::Complete);
        EXPECT_EQ(output, test.value);
    }
}

TEST(UiNumericParseTests, FloatPrefixesAndMalformedSyntaxAreClassifiedBeforeConversion){
    const ParseCase cases[]{
        { "", NumericParseStatus::Incomplete }, { "  ", NumericParseStatus::Incomplete },
        { "+", NumericParseStatus::Incomplete }, { "-", NumericParseStatus::Incomplete },
        { ".", NumericParseStatus::Incomplete }, { "+.", NumericParseStatus::Incomplete },
        { "-.", NumericParseStatus::Incomplete }, { "1e", NumericParseStatus::Incomplete },
        { "1e+", NumericParseStatus::Incomplete }, { "1.e-", NumericParseStatus::Incomplete },
        { "e" }, { ".e1" }, { "1..0" }, { "1e--2" }, { "1e+x" }, { "1e2x" },
        { "1 2" }, { "1,2" }, { "1_2" }, { "0x1p2" }, { "nan" }, { "NaN" },
        { "inf" }, { "-infinity" }, { "\xC2\xA0" "1" }, { AStringView("1\0", 2u) },
        { "1e99999x" }, { "1e309", NumericParseStatus::OutOfRange },
        { "1e-9999", NumericParseStatus::OutOfRange }, { "2e308", NumericParseStatus::OutOfRange }
    };
    for(const ParseCase& test : cases){
        SCOPED_TRACE(test.text);
        f64 output = -13.5;
        EXPECT_EQ(ParseFloatDraft(test.text, output), test.status);
        EXPECT_EQ(BitCast<u64>(output), BitCast<u64>(-13.5));
    }
}

TEST(UiNumericParseTests, SignedZeroDraftSpellingsPreserveTheSignBit){
    struct CompleteCase{
        AStringView text;
        f64 value = 0.0;
    };
    const CompleteCase cases[]{
        { "-0", -0.0 }, { "-0.0", -0.0 }, { "-0e100", -0.0 }
    };
    for(const CompleteCase& test : cases){
        SCOPED_TRACE(test.text);
        f64 output = 17.0;
        ASSERT_EQ(ParseFloatDraft(test.text, output), NumericParseStatus::Complete);
        EXPECT_EQ(BitCast<u64>(output), BitCast<u64>(test.value));
    }
}

TEST(UiNumericParseTests, EveryAsciiEdgeWhitespaceIsAcceptedButInteriorWhitespaceIsInvalid){
    const char whitespace[]{ ' ', '\t', '\n', '\r', '\f', '\v' };
    for(const char edge : whitespace){
        const char trimmed[]{ edge, '+', '7', edge };
        i64 integer = 0;
        f64 floating = 0.0;
        EXPECT_EQ(ParseIntegerDraft({ trimmed, 4u }, integer), NumericParseStatus::Complete);
        EXPECT_EQ(ParseFloatDraft({ trimmed, 4u }, floating), NumericParseStatus::Complete);
        EXPECT_EQ(integer, 7);
        EXPECT_EQ(floating, 7.0);
        const char interior[]{ '1', edge, '2' };
        EXPECT_EQ(ParseIntegerDraft({ interior, 3u }, integer), NumericParseStatus::Invalid);
        EXPECT_EQ(ParseFloatDraft({ interior, 3u }, floating), NumericParseStatus::Invalid);
        EXPECT_EQ(integer, 7);
        EXPECT_EQ(floating, 7.0);
    }
}

TEST(UiNumericParseTests, BoundsValidationRejectsReversalNonfiniteEndpointsAndUnknownPolicy){
    EXPECT_TRUE(ValidateIntegerBounds({ 4, 4, NumericBoundsPolicy::Clamp }));
    EXPECT_TRUE(ValidateFloatBounds({ -0.0, 0.0, NumericBoundsPolicy::Reject }));
    EXPECT_FALSE(ValidateIntegerBounds({ 8, 3 }));
    EXPECT_FALSE(ValidateFloatBounds({ 8.0, 3.0 }));
    EXPECT_FALSE(ValidateFloatBounds({ Limit<f64>::s_QuietNaN, 1.0 }));
    EXPECT_FALSE(ValidateFloatBounds({ -1.0, Limit<f64>::s_Infinity }));
    const auto unknown = static_cast<NumericBoundsPolicy::Enum>(2u);
    EXPECT_FALSE(ValidateIntegerBounds({ -1, 1, unknown }));
    EXPECT_FALSE(ValidateFloatBounds({ -1.0, 1.0, unknown }));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

