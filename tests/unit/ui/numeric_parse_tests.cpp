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


TEST(UiNumericParseTests, IntegerPrefixesAndMalformedSyntaxRetainErrorClassification){
    const Array<ParseCase, 23> cases{
        ParseCase{ "", NumericParseStatus::Incomplete }, ParseCase{ "  ", NumericParseStatus::Incomplete },
        ParseCase{ "+", NumericParseStatus::Incomplete }, ParseCase{ "-", NumericParseStatus::Incomplete },
        ParseCase{ " + ", NumericParseStatus::Incomplete }, ParseCase{ "--1" }, ParseCase{ "+-1" }, ParseCase{ "1 2" }, ParseCase{ "1,000" },
        ParseCase{ "1_000" }, ParseCase{ "0x10" }, ParseCase{ "1.0" }, ParseCase{ "1e2" }, ParseCase{ "12x" }, ParseCase{ "nan" }, ParseCase{ "inf" },
        ParseCase{ "\xE2\x88\x92" "1" }, ParseCase{ "\xD9\xA1" }, ParseCase{ "\xC2\xA0" "1" },
        ParseCase{ AStringView("1\0", 2u) }, ParseCase{ "999999999999999999999999x" },
        ParseCase{ "9223372036854775808", NumericParseStatus::OutOfRange },
        ParseCase{ "-9223372036854775809", NumericParseStatus::OutOfRange }
    };
    for(const ParseCase& test : cases){
        SCOPED_TRACE(test.text);
        const auto parsed = ParseIntegerDraft(test.text);
        ASSERT_FALSE(parsed);
        EXPECT_EQ(parsed.error(), test.status);
    }
}

TEST(UiNumericParseTests, IntegerConversionPreservesSignedBoundsAndLargeExactValues){
    struct CompleteCase{
        AStringView text;
        i64 value = 0;
    };
    const Array<CompleteCase, 3> cases{
        CompleteCase{ "9007199254740993", 9007199254740993ll },
        CompleteCase{ "-9223372036854775808", Limit<i64>::s_Min },
        CompleteCase{ "+9223372036854775807", Limit<i64>::s_Max }
    };
    for(const CompleteCase& test : cases){
        SCOPED_TRACE(test.text);
        const auto parsed = ParseIntegerDraft(test.text);
        ASSERT_TRUE(parsed);
        EXPECT_EQ(*parsed, test.value);
    }
}

TEST(UiNumericParseTests, FloatPrefixesAndMalformedSyntaxAreClassifiedBeforeConversion){
    const Array<ParseCase, 30> cases{
        ParseCase{ "", NumericParseStatus::Incomplete }, ParseCase{ "  ", NumericParseStatus::Incomplete },
        ParseCase{ "+", NumericParseStatus::Incomplete }, ParseCase{ "-", NumericParseStatus::Incomplete },
        ParseCase{ ".", NumericParseStatus::Incomplete }, ParseCase{ "+.", NumericParseStatus::Incomplete },
        ParseCase{ "-.", NumericParseStatus::Incomplete }, ParseCase{ "1e", NumericParseStatus::Incomplete },
        ParseCase{ "1e+", NumericParseStatus::Incomplete }, ParseCase{ "1.e-", NumericParseStatus::Incomplete },
        ParseCase{ "e" }, ParseCase{ ".e1" }, ParseCase{ "1..0" }, ParseCase{ "1e--2" }, ParseCase{ "1e+x" }, ParseCase{ "1e2x" },
        ParseCase{ "1 2" }, ParseCase{ "1,2" }, ParseCase{ "1_2" }, ParseCase{ "0x1p2" }, ParseCase{ "nan" }, ParseCase{ "NaN" },
        ParseCase{ "inf" }, ParseCase{ "-infinity" }, ParseCase{ "\xC2\xA0" "1" }, ParseCase{ AStringView("1\0", 2u) },
        ParseCase{ "1e99999x" }, ParseCase{ "1e309", NumericParseStatus::OutOfRange },
        ParseCase{ "1e-9999", NumericParseStatus::OutOfRange }, ParseCase{ "2e308", NumericParseStatus::OutOfRange }
    };
    for(const ParseCase& test : cases){
        SCOPED_TRACE(test.text);
        const auto parsed = ParseFloatDraft(test.text);
        ASSERT_FALSE(parsed);
        EXPECT_EQ(parsed.error(), test.status);
    }
}

TEST(UiNumericParseTests, SignedZeroDraftSpellingsPreserveTheSignBit){
    struct CompleteCase{
        AStringView text;
        f64 value = 0.0;
    };
    const Array<CompleteCase, 3> cases{ CompleteCase{ "-0", -0.0 }, CompleteCase{ "-0.0", -0.0 }, CompleteCase{ "-0e100", -0.0 } };
    for(const CompleteCase& test : cases){
        SCOPED_TRACE(test.text);
        const auto parsed = ParseFloatDraft(test.text);
        ASSERT_TRUE(parsed);
        EXPECT_EQ(BitCast<u64>(*parsed), BitCast<u64>(test.value));
    }
}

TEST(UiNumericParseTests, EveryAsciiEdgeWhitespaceIsAcceptedButInteriorWhitespaceIsInvalid){
    const Array<char, 6> whitespace{ ' ', '\t', '\n', '\r', '\f', '\v' };
    for(const char edge : whitespace){
        const Array<char, 4> trimmed{ edge, '+', '7', edge };
        const auto integer = ParseIntegerDraft({ trimmed.data(), 4u });
        const auto floating = ParseFloatDraft({ trimmed.data(), 4u });
        ASSERT_TRUE(integer);
        ASSERT_TRUE(floating);
        EXPECT_EQ(*integer, 7);
        EXPECT_EQ(*floating, 7.0);
        const Array<char, 3> interior{ '1', edge, '2' };
        const auto invalidInteger = ParseIntegerDraft({ interior.data(), 3u });
        const auto invalidFloat = ParseFloatDraft({ interior.data(), 3u });
        ASSERT_FALSE(invalidInteger);
        ASSERT_FALSE(invalidFloat);
        EXPECT_EQ(invalidInteger.error(), NumericParseStatus::Invalid);
        EXPECT_EQ(invalidFloat.error(), NumericParseStatus::Invalid);
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

