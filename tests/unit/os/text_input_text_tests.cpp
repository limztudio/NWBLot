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
    constexpr AStringView text = "A\xED\x95\x9C\xF0\x9F\x98\x80";
    for(const usize index : { 0u, 1u, 4u, 8u })
        EXPECT_TRUE(IsTextInputUtf8Boundary(text, index));
    for(const usize index : { 2u, 3u, 5u, 6u, 7u, 9u })
        EXPECT_FALSE(IsTextInputUtf8Boundary(text, index));
}

TEST(TextInputText, CaretBoundsRejectEmptyAndOverflowWhileAllowingClippedOrigins){
    EXPECT_TRUE(IsTextInputCaretRectValid({ -20, -10, 2, 20 }));
    EXPECT_TRUE(IsTextInputCaretRectValid({ Limit<i32>::s_Max - 1, Limit<i32>::s_Max - 1, 1, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, 0, 0, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, 0, 1, -1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ Limit<i32>::s_Max, 0, 1, 1 }));
    EXPECT_FALSE(IsTextInputCaretRectValid({ 0, Limit<i32>::s_Max - 10, 1, 11 }));
}

TEST(TextInputText, InvalidScalarsClearThePreviouslyEncodedLength){
    char bytes[4] = {};
    usize length = 0u;
    ASSERT_EQ(EncodeTextInputCodePoint(0x1f600u, bytes, length), TextInputAdmission::Accepted);
    EXPECT_EQ(AStringView(bytes, length), "\xF0\x9F\x98\x80");
    EXPECT_EQ(EncodeTextInputCodePoint(0u, bytes, length), TextInputAdmission::InvalidText);
    EXPECT_EQ(EncodeTextInputCodePoint(0xd800u, bytes, length), TextInputAdmission::InvalidText);
    EXPECT_EQ(EncodeTextInputCodePoint(0x110000u, bytes, length), TextInputAdmission::InvalidText);
    EXPECT_EQ(length, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

