// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/text_input_text.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_input_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextInputText, ValidatesStrictUtf8AndEmbeddedNul){
    EXPECT_EQ(ValidateTextInputUtf8({}, 100u), TextInputAdmission::Accepted);
    EXPECT_EQ(ValidateTextInputUtf8("\xC0\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xE0\x80\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xED\xA0\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xF4\x90\x80\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\xF0\x9F", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("\x80", 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8(AStringView("a\0b", 3u), 100u), TextInputAdmission::InvalidText);
    EXPECT_EQ(ValidateTextInputUtf8("four", 3u), TextInputAdmission::TooLarge);
}

TEST(TextInputText, BoundariesUseBytesForBmpAndSupplementaryScalars){
    constexpr AStringView s_Text = "A\xED\x95\x9C\xF0\x9F\x98\x80";
    for(const usize index : { 0u, 1u, 4u, 8u })
        EXPECT_TRUE(IsTextInputUtf8Boundary(s_Text, index));
    for(const usize index : { 2u, 3u, 5u, 6u, 7u, 9u })
        EXPECT_FALSE(IsTextInputUtf8Boundary(s_Text, index));
}

TEST(TextInputText, CaretBoundsRejectEmptyAndOverflowWhileAllowingClippedOrigins){
    EXPECT_TRUE(IsTextInputCaretRectValid({ -20, -10, 2, 20 }));
    EXPECT_TRUE(IsTextInputCaretRectValid({ Limit<i32>::s_Max - 1, Limit<i32>::s_Max - 1, 1, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, 0, 0, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, 0, 1, -1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ Limit<i32>::s_Max, 0, 1, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, Limit<i32>::s_Max - 10, 1, 11 }));
}

TEST(TextInputText, RejectsInvalidScalarsWithoutPublishingAnEncodedLength){
    char bytes[4] = {};
    const auto length = EncodeTextInputCodePoint(0x1f600u, bytes);
    ASSERT_TRUE(length);
    EXPECT_EQ(AStringView(bytes, *length), "\xF0\x9F\x98\x80");
    const auto invalid0 = EncodeTextInputCodePoint(0u, bytes);
    ASSERT_FALSE(invalid0);
    EXPECT_EQ(invalid0.error(), TextInputAdmission::InvalidText);
    const auto invalidd800 = EncodeTextInputCodePoint(0xd800u, bytes);
    ASSERT_FALSE(invalidd800);
    EXPECT_EQ(invalidd800.error(), TextInputAdmission::InvalidText);
    const auto invalid110000 = EncodeTextInputCodePoint(0x110000u, bytes);
    ASSERT_FALSE(invalid110000);
    EXPECT_EQ(invalid110000.error(), TextInputAdmission::InvalidText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

