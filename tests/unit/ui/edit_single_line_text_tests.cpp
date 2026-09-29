// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/edit/single_line_text.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_single_line_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiSingleLineText, NormalizesNativeAndUnicodeLineBreaksWhilePreservingUnicodeBytes){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output(arena.arena);
    ASSERT_EQ(NormalizeSingleLineText("a\r\nb\nc\rd\te\v\ff\xC2\x85g\xE2\x80\xA8h\xE2\x80\xA9\xED\x95\x9C", output, 64u),
        EditTextStatus::Accepted);
    EXPECT_EQ(output, "a b c d e  f g h \xED\x95\x9C");
}

TEST(UiSingleLineText, ChecksNormalizedBytesBeforeAllocationAndPreservesOutputOnFailure){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    EXPECT_EQ(NormalizeSingleLineText("a\r\nb", output, 3u), EditTextStatus::Accepted);
    EXPECT_EQ(output, "a b");
    EXPECT_EQ(NormalizeSingleLineText("\xED\x95\x9C", output, 2u), EditTextStatus::TooLarge);
    EXPECT_EQ(output, "a b");
    EXPECT_EQ(NormalizeSingleLineText("\r\n\r\n", output, 1u), EditTextStatus::TooLarge);
    EXPECT_EQ(output, "a b");
    EXPECT_EQ(NormalizeSingleLineText("", output, 0u), EditTextStatus::Accepted);
    EXPECT_TRUE(output.empty());
}

TEST(UiSingleLineText, RejectsMalformedUtf8NulAndUnrenderableAsciiControlsAtomically){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    const AStringView invalid[]{ "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82", "x\x1By", "\x7F",
        AStringView("a\0b", 3u) };
    for(const auto value : invalid){
        EXPECT_EQ(NormalizeSingleLineText(value, output, 64u), EditTextStatus::InvalidText);
        EXPECT_EQ(output, "before");
    }
}

TEST(UiSingleLineText, CopiesAliasedInputBeforePublishingOutput){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("prefix a\r\nb", arena.arena);
    const AStringView source(output.data() + 7u, output.size() - 7u);
    ASSERT_EQ(NormalizeSingleLineText(source, output, 3u), EditTextStatus::Accepted);
    EXPECT_EQ(output, "a b");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

