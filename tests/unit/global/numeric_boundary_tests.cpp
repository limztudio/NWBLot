// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/algorithm.h>
#include <global/containers.h>
#include <global/limit.h>
#include <global/overflow.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_numeric_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, DecimalFormattingUsesExactMinimumStorageWithoutEmptySentinelsOrOverrun){
    struct Storage{
        char first = 'a';
        Array<char, s_UsizeDecimalDigitCount> buffer{};
        char last = 'z';
    };
    struct Case{
        usize value;
        AStringView expected;
    };
    static_assert(sizeof(usize) == sizeof(u32) || sizeof(usize) == sizeof(u64));
    constexpr AStringView s_MaxValueText = sizeof(usize) == sizeof(u64) ? "18446744073709551615" : "4294967295";
    const Array<Case, 7u> cases = {
        Case{ 0u, "0" },
        Case{ 1u, "1" },
        Case{ 9u, "9" },
        Case{ 10u, "10" },
        Case{ 99u, "99" },
        Case{ 100u, "100" },
        Case{ Limit<usize>::s_Max, s_MaxValueText },
    };
    for(const auto& testCase : cases){
        SCOPED_TRACE(testCase.value);
        Storage storage;
        static_assert(noexcept(FormatDecimal(usize{}, storage.buffer)));
        const AStringView text = FormatDecimal(testCase.value, storage.buffer);
        EXPECT_EQ(text, testCase.expected);
        EXPECT_EQ(text.data(), storage.buffer.data());
        EXPECT_FALSE(text.empty());
        EXPECT_EQ(storage.first, 'a');
        EXPECT_EQ(storage.last, 'z');
    }
}

TEST(Global, MultiplicationDistinguishesOverflowSignedBoundariesAndSuccessfulZero){
    EXPECT_FALSE(::TryMultiply<usize>(Limit<usize>::s_Max, 2u));
    EXPECT_FALSE(::TryMultiply<i64>(Limit<i64>::s_Min, -1));
    EXPECT_FALSE(::TryMultiply<i64>(Limit<i64>::s_Max, 2));
    constexpr auto s_Zero = ::TryMultiply<u64>(0u, Limit<u64>::s_Max);
    static_assert(s_Zero && *s_Zero == 0u);
    constexpr auto s_Minimum = ::TryMultiply<i64>(Limit<i64>::s_Min, 1);
    static_assert(s_Minimum && *s_Minimum == Limit<i64>::s_Min);
    EXPECT_EQ(*s_Zero, 0u);
    EXPECT_EQ(*s_Minimum, Limit<i64>::s_Min);
}

TEST(Global, CheckedAdditionDistinguishesSignedLimitsAndSuccessfulZero){
    EXPECT_FALSE(AddNoOverflow<i64>(Limit<i64>::s_Max, 1));
    EXPECT_FALSE(AddNoOverflow<i64>(Limit<i64>::s_Min, -1));
    const auto lower = AddNoOverflow<i64>(Limit<i64>::s_Min, 1);
    ASSERT_TRUE(lower);
    EXPECT_EQ(*lower, Limit<i64>::s_Min + 1);
    const auto upper = AddNoOverflow<i64>(Limit<i64>::s_Max, -1);
    ASSERT_TRUE(upper);
    EXPECT_EQ(*upper, Limit<i64>::s_Max - 1);
    const auto zero = AddNoOverflow<i64>(Limit<i64>::s_Max, -Limit<i64>::s_Max);
    ASSERT_TRUE(zero);
    EXPECT_EQ(*zero, 0);
    EXPECT_FALSE(AddNoOverflow<u64>(Limit<u64>::s_Max, 1u));
}

TEST(Global, NumericParsersRespectViewBoundsAndRejectEmptyInput){
    const auto signedValue = ParseI64(AStringView("-42trailing").substr(0u, 3u));
    ASSERT_TRUE(signedValue);
    EXPECT_EQ(*signedValue, -42);
    EXPECT_FALSE(ParseI64("42x"));
    const auto unsignedValue = ParseU64(AStringView("42trailing").substr(0u, 2u));
    ASSERT_TRUE(unsignedValue);
    EXPECT_EQ(*unsignedValue, 42u);
    EXPECT_FALSE(ParseU64("-1"));
    EXPECT_FALSE(ParseI64({}));
    EXPECT_FALSE(ParseU64({}));
    EXPECT_FALSE(ParseF64FromChars({}));
    EXPECT_FALSE(ParseF32FromChars({}));
    const auto doubleValue = ParseF64FromChars(AStringView("1.25trailing").substr(0u, 4u));
    const auto floatValue = ParseF32FromChars(AStringView("1.25trailing").substr(0u, 4u));
    ASSERT_TRUE(doubleValue);
    ASSERT_TRUE(floatValue);
    EXPECT_EQ(*doubleValue, 1.25);
    EXPECT_EQ(*floatValue, 1.25f);
}

TEST(Global, GrowingCapacitySaturatesAtSizeLimit){
    EXPECT_EQ(::NextGrowingCapacity((Limit<usize>::s_Max / 2u) + 1u, Limit<usize>::s_Max), Limit<usize>::s_Max);
}

TEST(Global, CheckedDivideUpRejectsZeroDivisor){
    EXPECT_FALSE(::DivideUpChecked(17u, 0u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

