// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/clipboard_text.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

TEST(ClipboardText, Utf8RejectsMalformedUnicodeAndEmbeddedNull){
    EXPECT_EQ(ValidateClipboardUtf8Text({}), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text("\xF4\x8F\xBF\xBF"), ClipboardStatus::Success);
    const AStringView invalid[]{
        "\x80", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xF0\x9F",
        AStringView("before\0after", 12u),
    };
    for(const AStringView text : invalid)
        EXPECT_EQ(ValidateClipboardUtf8Text(text), ClipboardStatus::InvalidText);
}

TEST(ClipboardText, IncrementalChunksCanSplitUnicodeUntilFinalValidation){
    NWB::Tests::TestArena arena;
    ClipboardTextAccumulator accumulator(arena.arena);
    EXPECT_EQ(accumulator.appendBytes("\xED"), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text(accumulator.text()), ClipboardStatus::InvalidText);
    EXPECT_EQ(accumulator.appendBytes("\x95\x9C\xF0\x9F"), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text(accumulator.text()), ClipboardStatus::InvalidText);
    EXPECT_EQ(accumulator.appendBytes("\x98\x80"), ClipboardStatus::Success);
    EXPECT_EQ(ValidateClipboardUtf8Text(accumulator.text()), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.text(), "\xED\x95\x9C\xF0\x9F\x98\x80");
}

TEST(ClipboardText, IncrementalFailuresDiscardBytesAndRemainStickyUntilClear){
    NWB::Tests::TestArena arena;
    ClipboardTextAccumulator accumulator(arena.arena);
    ASSERT_EQ(accumulator.appendBytes("prefix"), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.appendBytes(AStringView("a\0b", 3u)), ClipboardStatus::InvalidText);
    EXPECT_TRUE(accumulator.text().empty());
    EXPECT_EQ(accumulator.appendBytes("ignored"), ClipboardStatus::InvalidText);
    EXPECT_TRUE(accumulator.text().empty());
    accumulator.clear();
    EXPECT_EQ(accumulator.appendBytes("fresh"), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.text(), "fresh");
}

TEST(ClipboardText, ByteLimitIsInclusiveAndBoundsIncrementalAccumulation){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> maximum(s_ClipboardMaxTextBytes, 'x', arena.arena);
    EXPECT_EQ(ValidateClipboardUtf8Text(maximum), ClipboardStatus::Success);
    ClipboardTextAccumulator accumulator(arena.arena);
    ASSERT_EQ(accumulator.appendBytes(maximum), ClipboardStatus::Success);
    EXPECT_EQ(accumulator.text().size(), s_ClipboardMaxTextBytes);
    EXPECT_EQ(accumulator.appendBytes("x"), ClipboardStatus::TooLarge);
    EXPECT_TRUE(accumulator.text().empty());
    EXPECT_EQ(accumulator.appendBytes("ignored"), ClipboardStatus::TooLarge);
    maximum.push_back('x');
    EXPECT_EQ(ValidateClipboardUtf8Text(maximum), ClipboardStatus::TooLarge);
    accumulator.clear();
    EXPECT_EQ(accumulator.appendBytes("fresh"), ClipboardStatus::Success);
}

TEST(ClipboardText, Latin1UpperByteBoundsAndUnsupportedUnicodeClearOutput){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> utf8(arena.arena);
    AString<Alloc::GlobalArena> latin1(arena.arena);
    const AStringView source("\x80\xFF", 2u);
    ASSERT_EQ(DecodeClipboardLatin1(source, utf8), ClipboardStatus::Success);
    EXPECT_EQ(utf8, "\xC2\x80\xC3\xBF");
    ASSERT_EQ(EncodeClipboardLatin1(utf8, latin1), ClipboardStatus::Success);
    EXPECT_EQ(latin1, source);
    EXPECT_EQ(EncodeClipboardLatin1("\xE2\x98\x83", latin1), ClipboardStatus::Unsupported);
    EXPECT_TRUE(latin1.empty());
    EXPECT_EQ(DecodeClipboardLatin1(AStringView("a\0b", 3u), utf8), ClipboardStatus::InvalidText);
    EXPECT_TRUE(utf8.empty());
}

TEST(ClipboardText, Latin1ExpansionCannotExceedUtf8ByteLimit){
    NWB::Tests::TestArena arena;
    AString<Alloc::GlobalArena> input(s_ClipboardMaxTextBytes / 2u + 1u, static_cast<char>(0xFFu), arena.arena);
    AString<Alloc::GlobalArena> output("stale bytes", arena.arena);
    EXPECT_EQ(DecodeClipboardLatin1(input, output), ClipboardStatus::TooLarge);
    EXPECT_TRUE(output.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

