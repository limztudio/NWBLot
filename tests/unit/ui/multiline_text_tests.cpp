// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/edit/multiline_text.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiMultilineText, AcceptsEmptyAndCanonicalLfLinesWithoutChangingBytes){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    const AStringView cases[]{ "", "\n", "\n\n", "first\nsecond", "\nfirst\n", "first\n\nlast\n" };
    for(const AStringView text : cases){
        SCOPED_TRACE(text);
        EXPECT_TRUE(ValidateMultilineText(text));
        ASSERT_EQ(NormalizeMultilineText(text, output, text.size()), EditTextStatus::Accepted);
        EXPECT_EQ(AStringView(output.data(), output.size()), text);
        EXPECT_TRUE(ValidateMultilineText({ output.data(), output.size() }));
    }
}

TEST(UiMultilineText, ChecksEveryAsciiScalarWithOnlyLfCanonicalAndTabCrNormalizable){
    Tests::TestArena arena;
    for(u32 scalar = 0u; scalar < 0x80u; ++scalar){
        SCOPED_TRACE(scalar);
        const char bytes[]{ 'a', static_cast<char>(scalar), 'b' };
        const AStringView text(bytes, 3u);
        const bool canonical = scalar == 0xAu || (scalar >= 0x20u && scalar < 0x7Fu);
        EXPECT_EQ(ValidateMultilineText(text), canonical);
        AString<Core::Alloc::GlobalArena> output("before", arena.arena);
        const bool normalizable = canonical || scalar == 0x9u || scalar == 0xDu;
        EXPECT_EQ(
            NormalizeMultilineText(text, output, 3u),
            normalizable ? EditTextStatus::Accepted : EditTextStatus::InvalidText
        );
        if(normalizable){
            const char replacement = scalar == 0x9u ? ' ' : scalar == 0xDu ? '\n' : static_cast<char>(scalar);
            const char expected[]{ 'a', replacement, 'b' };
            EXPECT_EQ(AStringView(output.data(), output.size()), AStringView(expected, 3u));
            EXPECT_TRUE(ValidateMultilineText({ output.data(), output.size() }));
        }
        else
            EXPECT_EQ(output, "before");
    }
}

TEST(UiMultilineText, RejectsEveryC1ControlExceptNelDuringNormalization){
    Tests::TestArena arena;
    for(u32 scalar = 0x80u; scalar <= 0x9Fu; ++scalar){
        SCOPED_TRACE(scalar);
        const char bytes[]{ 'a', static_cast<char>(0xC2u), static_cast<char>(scalar), 'b' };
        const AStringView text(bytes, 4u);
        EXPECT_FALSE(ValidateMultilineText(text));
        AString<Core::Alloc::GlobalArena> output("before", arena.arena);
        EXPECT_EQ(
            NormalizeMultilineText(text, output, 4u),
            scalar == 0x85u ? EditTextStatus::Accepted : EditTextStatus::InvalidText
        );
        EXPECT_EQ(output, scalar == 0x85u ? "a\nb" : "before");
    }
}

TEST(UiMultilineText, RequiresNormalizationForTabsAndEveryNoncanonicalLineSeparator){
    struct SeparatorCase{
        AStringView source;
        AStringView expected;
    };
    const SeparatorCase cases[]{
        { "\t", " " }, { "\r", "\n" }, { "\r\n", "\n" }, { "\xC2\x85", "\n" },
        { "\xE2\x80\xA8", "\n" }, { "\xE2\x80\xA9", "\n" }
    };
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    for(const SeparatorCase& test : cases){
        SCOPED_TRACE(test.source);
        EXPECT_FALSE(ValidateMultilineText(test.source));
        ASSERT_EQ(NormalizeMultilineText(test.source, output, 1u), EditTextStatus::Accepted);
        EXPECT_EQ(AStringView(output.data(), output.size()), test.expected);
        EXPECT_TRUE(ValidateMultilineText({ output.data(), output.size() }));
    }
}

TEST(UiMultilineText, PreservesConsecutiveLeadingAndTrailingLinesWhileNormalizingBreaks){
    struct LineCase{
        AStringView source;
        AStringView expected;
    };
    const LineCase cases[]{
        { "a\r\nb\rc\nd\te\xC2\x85" "f\xE2\x80\xA8" "g\xE2\x80\xA9" "h", "a\nb\nc\nd e\nf\ng\nh" },
        { "\r\r\n", "\n\n" }, { "\r\n\r", "\n\n" }, { "\n\r\n", "\n\n" },
        { "\r\n\r\n", "\n\n" }, { "\r\nfirst\r\n\r\nlast\r", "\nfirst\n\nlast\n" },
        { "\xC2\x85\xE2\x80\xA8\xE2\x80\xA9", "\n\n\n" }, { "last\r\n", "last\n" },
        { "\t\tlast\t", "  last " }
    };
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output(arena.arena);
    for(const LineCase& test : cases){
        SCOPED_TRACE(test.source);
        ASSERT_EQ(NormalizeMultilineText(test.source, output, test.expected.size()), EditTextStatus::Accepted);
        EXPECT_EQ(AStringView(output.data(), output.size()), test.expected);
        EXPECT_TRUE(ValidateMultilineText({ output.data(), output.size() }));
    }
}

TEST(UiMultilineText, PreservesUnicodeScalarBoundariesCombiningMarksAndEmojiBytes){
    const AStringView cases[]{
        "\xC2\xA0", "\xDF\xBF", "\xE0\xA0\x80", "\xED\x9F\xBF", "\xEE\x80\x80",
        "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF",
        "\xE2\x80\xA7", "\xE2\x80\xAA", "\xE2\x80\x8B", "\xE2\x80\x8D",
        "e\xCC\x81\n\xC3\xA9", "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB\n\xED\x95\x9C",
        "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB\n\xF0\x9F\x87\xB0\xF0\x9F\x87\xB7"
    };
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output(arena.arena);
    for(const AStringView text : cases){
        SCOPED_TRACE(text);
        EXPECT_TRUE(ValidateMultilineText(text));
        ASSERT_EQ(NormalizeMultilineText(text, output, text.size()), EditTextStatus::Accepted);
        EXPECT_EQ(AStringView(output.data(), output.size()), text);
    }
    const AStringView source = "e\xCC\x81\r\n\xED\x95\x9C\t\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
    const AStringView expected = "e\xCC\x81\n\xED\x95\x9C \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
    ASSERT_EQ(NormalizeMultilineText(source, output, expected.size()), EditTextStatus::Accepted);
    EXPECT_EQ(AStringView(output.data(), output.size()), expected);
}

TEST(UiMultilineText, RejectsMalformedUtf8AndNonScalarSequencesWithoutPublishingOutput){
    const AStringView cases[]{
        "\x80", "\xBF", "\xC0\x80", "\xC0\xAF", "\xC1\xBF", "\xE0\x80\x80", "\xF0\x80\x80\x80",
        "\xED\xA0\x80", "\xED\xBF\xBF", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFE", "\xFF",
        "\xC2", "\xE2", "\xE2\x82", "\xF0", "\xF0\x9F", "\xF0\x9F\x92",
        "\xC2 ", "\xE2\x28\xA1", "\xF0\x28\x8C\xBC", "\xE2\x82" "x",
        "\xF8\x88\x80\x80\x80", "valid\r\n\xED\xA0\x80", AStringView("a\0b", 3u)
    };
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before\nunchanged", arena.arena);
    for(const AStringView text : cases){
        SCOPED_TRACE(text);
        EXPECT_FALSE(ValidateMultilineText(text));
        EXPECT_EQ(NormalizeMultilineText(text, output, 128u), EditTextStatus::InvalidText);
        EXPECT_EQ(output, "before\nunchanged");
    }
}

TEST(UiMultilineText, RejectedControlsAfterNormalizedPrefixesLeaveOutputUnchanged){
    const AStringView cases[]{
        "prefix\r\n\t\x01", "prefix\r\n\v", "prefix\r\n\f", "prefix\xC2\x85\x7F",
        "prefix\xE2\x80\xA8\xC2\x80", "prefix\xE2\x80\xA9\xC2\x9F",
        AStringView("prefix\r\n\0tail", 13u)
    };
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    for(const AStringView text : cases){
        SCOPED_TRACE(text);
        EXPECT_EQ(NormalizeMultilineText(text, output, 64u), EditTextStatus::InvalidText);
        EXPECT_EQ(output, "before");
    }
}

TEST(UiMultilineText, LimitsNormalizedBytesAndNeverTruncatesUtf8Scalars){
    struct BudgetCase{
        AStringView source;
        AStringView expected;
    };
    const BudgetCase cases[]{
        { "a\r\nb", "a\nb" }, { "\r\n\r\n", "\n\n" }, { "\xE2\x80\xA8\xE2\x80\xA9", "\n\n" },
        { "\xC2\x85\t", "\n " }, { "\xED\x95\x9C", "\xED\x95\x9C" },
        { "\xF0\x9F\x92\xBB", "\xF0\x9F\x92\xBB" },
        { "\xED\x95\x9C\r\n\xE2\x80\xA8\t", "\xED\x95\x9C\n\n " }
    };
    Tests::TestArena arena;
    for(const BudgetCase& test : cases){
        SCOPED_TRACE(test.source);
        AString<Core::Alloc::GlobalArena> output("before", arena.arena);
        ASSERT_EQ(NormalizeMultilineText(test.source, output, test.expected.size()), EditTextStatus::Accepted);
        EXPECT_EQ(AStringView(output.data(), output.size()), test.expected);
        EXPECT_EQ(NormalizeMultilineText(test.source, output, test.expected.size() - 1u), EditTextStatus::TooLarge);
        EXPECT_EQ(AStringView(output.data(), output.size()), test.expected);
        EXPECT_TRUE(ValidateMultilineText({ output.data(), output.size() }));
    }
}

TEST(UiMultilineText, ZeroBudgetAcceptsEmptyInputAndRejectsEveryNonemptyNormalizedOutput){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before", arena.arena);
    const AStringView cases[]{ "a", "\n", "\r\n", "\t", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9" };
    for(const AStringView text : cases){
        SCOPED_TRACE(text);
        EXPECT_EQ(NormalizeMultilineText(text, output, 0u), EditTextStatus::TooLarge);
        EXPECT_EQ(output, "before");
    }
    ASSERT_EQ(NormalizeMultilineText(AStringView{}, output, 0u), EditTextStatus::Accepted);
    EXPECT_TRUE(output.empty());
    EXPECT_TRUE(ValidateMultilineText(AStringView{}));
    ASSERT_EQ(NormalizeMultilineText("", output, 0u), EditTextStatus::Accepted);
    EXPECT_TRUE(output.empty());
}

TEST(UiMultilineText, SupportsWholeOutputAndSubviewAliasingBeforePublishing){
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("a\r\nb\t\xED\x95\x9C\xE2\x80\xA9", arena.arena);
    const AStringView whole(output.data(), output.size());
    ASSERT_EQ(NormalizeMultilineText(whole, output, 8u), EditTextStatus::Accepted);
    EXPECT_EQ(output, "a\nb \xED\x95\x9C\n");

    output.assign("prefix a\r\nb\tz suffix");
    const AStringView middle(output.data() + 7u, 6u);
    ASSERT_EQ(NormalizeMultilineText(middle, output, 5u), EditTextStatus::Accepted);
    EXPECT_EQ(output, "a\nb z");

    output.assign("prefix\xE2\x80\xA8");
    const AStringView trailing(output.data() + 6u, 3u);
    ASSERT_EQ(NormalizeMultilineText(trailing, output, 1u), EditTextStatus::Accepted);
    EXPECT_EQ(output, "\n");

    const AStringView empty(output.data() + output.size(), 0u);
    ASSERT_EQ(NormalizeMultilineText(empty, output, 0u), EditTextStatus::Accepted);
    EXPECT_TRUE(output.empty());
}

TEST(UiMultilineText, FailuresPreserveOutputBytesStorageAndAliasedSourceBytes){
    struct FailureCase{
        AStringView source;
        usize maxBytes = 0u;
        EditTextStatus::Enum status = EditTextStatus::InvalidText;
    };
    const FailureCase cases[]{
        { "a\r\nb", 2u, EditTextStatus::TooLarge }, { "\xED\x95\x9C", 2u, EditTextStatus::TooLarge },
        { "\r\n\x01", 64u }, { "\r\n\xC2\x9F", 64u }, { "\r\n\xE2\x82", 64u }
    };
    Tests::TestArena arena;
    AString<Core::Alloc::GlobalArena> output("before\nunchanged output storage", arena.arena);
    output.reserve(128u);
    const AString<Core::Alloc::GlobalArena> saved(output, arena.arena);
    const char* storage = output.data();
    const usize capacity = output.capacity();
    for(const FailureCase& test : cases){
        SCOPED_TRACE(test.source);
        EXPECT_EQ(NormalizeMultilineText(test.source, output, test.maxBytes), test.status);
        EXPECT_EQ(output, saved);
        EXPECT_EQ(output.data(), storage);
        EXPECT_EQ(output.capacity(), capacity);
    }

    output.assign("prefix a\r\nb\tz suffix");
    const AString<Core::Alloc::GlobalArena> savedAlias(output, arena.arena);
    storage = output.data();
    const usize aliasCapacity = output.capacity();
    const AStringView whole(output.data(), output.size());
    EXPECT_EQ(NormalizeMultilineText(whole, output, 1u), EditTextStatus::TooLarge);
    EXPECT_EQ(output, savedAlias);
    EXPECT_EQ(output.data(), storage);
    EXPECT_EQ(output.capacity(), aliasCapacity);
    const AStringView middle(output.data() + 7u, 6u);
    EXPECT_EQ(NormalizeMultilineText(middle, output, 4u), EditTextStatus::TooLarge);
    EXPECT_EQ(output, savedAlias);
    EXPECT_EQ(output.data(), storage);
    EXPECT_EQ(output.capacity(), aliasCapacity);

    const AStringView invalidAlias[]{ "prefix a\r\nb\x01", "prefix a\r\nb\xE2\x82", AStringView("prefix a\r\nb\0tail", 16u) };
    for(const AStringView text : invalidAlias){
        output.assign(text.data(), text.size());
        const AString<Core::Alloc::GlobalArena> savedInvalid(output, arena.arena);
        const char* invalidStorage = output.data();
        const usize invalidCapacity = output.capacity();
        const AStringView source(output.data() + 7u, output.size() - 7u);
        EXPECT_EQ(NormalizeMultilineText(source, output, 64u), EditTextStatus::InvalidText);
        EXPECT_EQ(output, savedInvalid);
        EXPECT_EQ(output.data(), invalidStorage);
        EXPECT_EQ(output.capacity(), invalidCapacity);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

