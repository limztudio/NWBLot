// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/edit/single_line_text.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_single_line_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiSingleLineText, ChecksNormalizedBytesBeforeAllocationAndPreservesOutputOnFailure){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    auto normalized1 = NormalizeSingleLineText(arena.arena, "a\r\nb", 3u);
    ASSERT_TRUE(normalized1);
    output = Move(*normalized1);
    EXPECT_EQ(output, "a b");
    EXPECT_EQ(NormalizeSingleLineText(arena.arena, "\xED\x95\x9C", 2u), MakeUnexpected(EditTextStatus::TooLarge));
    EXPECT_EQ(output, "a b");
    EXPECT_EQ(NormalizeSingleLineText(arena.arena, "\r\n\r\n", 1u), MakeUnexpected(EditTextStatus::TooLarge));
    EXPECT_EQ(output, "a b");
    auto normalized2 = NormalizeSingleLineText(arena.arena, "", 0u);
    ASSERT_TRUE(normalized2);
    output = Move(*normalized2);
    EXPECT_TRUE(output.empty());
}

TEST(UiSingleLineText, RejectsMalformedUtf8NulAndUnrenderableAsciiControlsAtomically){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    const AStringView invalid[]{ "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82", "x\x1By", "\x7F",
        AStringView("a\0b", 3u) };
    for(const auto value : invalid){
        EXPECT_EQ(NormalizeSingleLineText(arena.arena, value, 64u), MakeUnexpected(EditTextStatus::InvalidText));
        EXPECT_EQ(output, "before");
    }
}

TEST(UiSingleLineText, CopiesAliasedInputBeforePublishingOutput){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("prefix a\r\nb", arena.arena);
    const AStringView source(output.data() + 7u, output.size() - 7u);
    auto normalized3 = NormalizeSingleLineText(arena.arena, source, 3u);
    ASSERT_TRUE(normalized3);
    output = Move(*normalized3);
    EXPECT_EQ(output, "a b");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

