// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/base64.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Base64, BinaryValuesAndEveryTailLengthAreLossless){
    TestArena<> context;
    Vector<u8, Core::Alloc::GlobalArena> source(context.arena);
    for(u32 value = 0u; value < 256u; ++value)
        source.push_back(static_cast<u8>(value));
    AString<Core::Alloc::GlobalArena> encoded(context.arena);
    Vector<u8, Core::Alloc::GlobalArena> decoded(context.arena);
    for(usize count = 0u; count <= source.size(); ++count){
        ASSERT_TRUE(EncodeBase64({ source.data(), count }, encoded));
        ASSERT_TRUE(DecodeBase64(encoded, decoded, count));
        ASSERT_EQ(decoded.size(), count);
        for(usize index = 0u; index < count; ++index)
            ASSERT_EQ(decoded[index], source[index]);
    }
}

TEST(Base64, RejectsMalformedAlphabetLengthPaddingAndUnusedBits){
    TestArena<> context;
    Vector<u8, Core::Alloc::GlobalArena> output(context.arena);
    output.push_back(91u);
    const AStringView invalid[] = {
        "Z", "Zg", "Zg=", "Zg===", "=g==", "Z===", "Zg=A", "Zg==AAAA", "AAAAZg==AAAA",
        "Zm9=", "Zh==", "AB==", "AAB=", "Zg-_", "Zg\r\n", "Zg \t", "Zg#=", "====",
        AStringView("Zg\0=", 4u), AStringView("Zg\xff=", 4u),
    };
    for(const AStringView text : invalid){
        usize size = 77u;
        EXPECT_FALSE(Base64DecodedSize(text, 1024u, size));
        EXPECT_EQ(size, 77u);
        EXPECT_FALSE(DecodeBase64(text, output, 1024u));
        ASSERT_EQ(output.size(), 1u);
        EXPECT_EQ(output[0u], 91u);
    }
}

TEST(Base64, ExactDecodedBoundsAreCheckedBeforePublication){
    TestArena<> context;
    Vector<u8, Core::Alloc::GlobalArena> output(context.arena);
    output.push_back(91u);
    usize size = 77u;
    EXPECT_FALSE(Base64DecodedSize("Zm9v", 2u, size));
    EXPECT_EQ(size, 77u);
    EXPECT_FALSE(DecodeBase64("Zm9v", output, 2u));
    ASSERT_EQ(output.size(), 1u);
    EXPECT_EQ(output[0u], 91u);
    ASSERT_TRUE(Base64DecodedSize("Zm9v", 3u, size));
    EXPECT_EQ(size, 3u);
    ASSERT_TRUE(DecodeBase64("Zm9v", output, 3u));
    EXPECT_EQ(output[0u], static_cast<u8>('f'));
    ASSERT_TRUE(DecodeBase64("", output, 0u));
    EXPECT_TRUE(output.empty());
}

TEST(Base64, AliasedInputSurvivesOutputReplacement){
    TestArena<> context;
    AString<Core::Alloc::GlobalArena> text("foobar", context.arena);
    ASSERT_TRUE(EncodeBase64({ reinterpret_cast<const u8*>(text.data()), text.size() }, text));
    EXPECT_EQ(text, "Zm9vYmFy");
    ASSERT_TRUE(DecodeBase64(AStringView(text.data(), text.size()), text, 6u));
    EXPECT_EQ(text, "foobar");
}

TEST(Base64, InvalidEncodeInputAndSizeOverflowPreserveOutput){
    TestArena<> context;
    AString<Core::Alloc::GlobalArena> output("previous", context.arena);
    EXPECT_FALSE(EncodeBase64({ nullptr, 1u }, output));
    EXPECT_EQ(output, "previous");
    const u8 byte = 0u;
    EXPECT_FALSE(EncodeBase64({ &byte, Limit<usize>::s_Max }, output));
    EXPECT_EQ(output, "previous");
    ASSERT_TRUE(EncodeBase64({}, output));
    EXPECT_TRUE(output.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

