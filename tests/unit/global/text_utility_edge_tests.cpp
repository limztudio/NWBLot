// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/basic_string.h>
#include <global/binary.h>
#include <global/call_text.h>
#include <global/containers.h>
#include <global/csv_text.h>
#include <global/latin1_text.h>
#include <global/limit.h>
#include <global/source_text.h>
#include <global/string_hash.h>
#include <global/text_replacements.h>
#include <global/text_utils.h>
#include <global/unicode/grapheme.h>
#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_utility_edge_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingReplacementComparison{
    b8 value;
    explicit ThrowingReplacementComparison(const b8 inValue)noexcept : value(inValue){}
    ThrowingReplacementComparison(const ThrowingReplacementComparison&){ throw RuntimeException("comparison copy failure"); }
    explicit operator b8()const noexcept{ return value; }
};
struct ReplacementOffset{
    usize value;
    operator usize()const noexcept{ return value; }
};
[[nodiscard]] inline const ThrowingReplacementComparison& operator>(const ReplacementOffset lhs, const ReplacementOffset rhs)noexcept{
    static const ThrowingReplacementComparison s_True(true);
    static const ThrowingReplacementComparison s_False(false);
    return lhs.value > rhs.value ? s_True : s_False;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, CallAndOptionTextRejectInvalidBoundaries){
    static_assert(LiteralView("a\0b").size() == 3u);
    const auto nestedCall = SplitNonemptyCallText("  item ( a(b) ) ");
    ASSERT_TRUE(nestedCall);
    EXPECT_EQ(nestedCall->name, "item");
    EXPECT_EQ(nestedCall->arguments, "a(b)");
    const auto permissiveCall = SplitNonemptyCallText("f(a)b)");
    ASSERT_TRUE(permissiveCall);
    EXPECT_EQ(permissiveCall->arguments, "a)b");
    EXPECT_FALSE(SplitNonemptyCallText("item( \t )"));
    EXPECT_FALSE(SplitNonemptyCallText("item()"));
    EXPECT_FALSE(SplitNonemptyCallText("(value)"));
    EXPECT_FALSE(SplitNonemptyCallText("item(value)tail"));
    const char callWithNull[]{ 'f', '(', '\0', 'x', ')' };
    const auto nullCall = SplitNonemptyCallText(AStringView(callWithNull, sizeof(callWithNull)));
    ASSERT_TRUE(nullCall);
    EXPECT_EQ(nullCall->arguments.size(), 2u);
    EXPECT_EQ(nullCall->arguments[0u], '\0');
    EXPECT_EQ(SkipNumericSign(""), 0u);
    EXPECT_EQ(SkipNumericSign("-"), 1u);
    EXPECT_EQ(SkipNumericSign("+x"), 1u);
    EXPECT_EQ(ExtractLeadingIdentifier("9a-"), "9a");
    EXPECT_TRUE(IsEscapedQuoteAt("\\", 1u));
    EXPECT_FALSE(IsEscapedQuoteAt("\\\\", 2u));
    const Array<u32, 3> choices{ 0u, 1u, 2u };
    const auto optionText = [](const u32 value)noexcept -> AStringView{ return value == 2u ? "other" : "mixed"; };
    const auto parsed = ParseOptionText(AStringView("  MiXeD\t"), Span<const u32>(choices), optionText);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(*parsed, 0u);
    EXPECT_FALSE(ParseOptionText(AStringView("missing"), Span<const u32>(choices), optionText));
    EXPECT_FALSE(ParseOptionText(AStringView("mixed"), Span<const u32>{}, optionText));
}

TEST(Global, TextReplacementsAndIdentifiersPreserveInputBoundaries){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    struct TestTextReplacement{ usize begin; usize end; AStringView text; };
    Array<TestTextReplacement, 4u> textEdits{ TestTextReplacement{ 1u, 3u, "" }, TestTextReplacement{ 6u, 6u, ">" }, TestTextReplacement{ 4u, 6u, "XYZ" }, TestTextReplacement{ 0u, 0u, "<" } };
    AString<NWB::Core::Alloc::ScratchArena> editableText("abcdef", scratchArena);
    ApplyTextReplacements(Span<TestTextReplacement>(textEdits), editableText);
    EXPECT_EQ(editableText, "<adXYZ>");
    struct ProxyTextReplacement{ ReplacementOffset begin; ReplacementOffset end; AStringView text; };
    Array<ProxyTextReplacement, 2u> proxyEdits{ ProxyTextReplacement{ {4u}, {6u}, "Z" }, ProxyTextReplacement{ {1u}, {3u}, "X" } };
    AString<NWB::Core::Alloc::ScratchArena> proxyText("abcdef", scratchArena);
    EXPECT_NO_THROW(ApplyTextReplacements(Span<ProxyTextReplacement>(proxyEdits), proxyText));
    EXPECT_EQ(proxyText, "aXdZ");
    const char identifierBytes[]{ 'a', '-', '\0', static_cast<char>(0x80), 'b' };
    AString<NWB::Core::Alloc::ScratchArena> upperIdentifier("prefix:", scratchArena);
    AppendAsciiUpperIdentifier(AStringView(identifierBytes, sizeof(identifierBytes)), "VALUE", upperIdentifier);
    EXPECT_EQ(upperIdentifier, "prefix:A___B");
    AppendAsciiUpperIdentifier("___", "VALUE", upperIdentifier);
    AppendAsciiUpperIdentifier("", "VALUE", upperIdentifier);
    EXPECT_EQ(upperIdentifier, "prefix:A___B___VALUE");
    AString<NWB::Core::Alloc::ScratchArena> pascalIdentifier("prefix:", scratchArena);
    AppendUnderscoreSeparatedPascalIdentifier("_9-a_b", "Value", pascalIdentifier);
    AppendUnderscoreSeparatedPascalIdentifier("____", "Value", pascalIdentifier);
    EXPECT_EQ(pascalIdentifier, "prefix:9-aBValue");
    struct PointerFallbackProbe{
        usize length = 0u;
        u32 selected = 0u;
        [[nodiscard]] usize size()const noexcept{ return length; }
        PointerFallbackProbe& operator+=(char)noexcept{ ++length; return *this; }
        PointerFallbackProbe& operator+=(const char*&&)noexcept{ selected = 1u; ++length; return *this; }
        PointerFallbackProbe& operator+=(const char* const&){ selected = 2u; ++length; return *this; }
    };
    PointerFallbackProbe upperProbe;
    static_assert(noexcept(AppendAsciiUpperIdentifier({}, "VALUE", upperProbe)));
    AppendAsciiUpperIdentifier({}, "VALUE", upperProbe);
    EXPECT_EQ(upperProbe.selected, 1u);
    PointerFallbackProbe pascalProbe;
    static_assert(noexcept(AppendUnderscoreSeparatedPascalIdentifier({}, "Value", pascalProbe)));
    AppendUnderscoreSeparatedPascalIdentifier({}, "Value", pascalProbe);
    EXPECT_EQ(pascalProbe.selected, 1u);
}

TEST(Global, Latin1RejectsInvalidTextAndCapacity){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    AString<NWB::Core::Alloc::ScratchArena> latinText(scratchArena);
    EXPECT_TRUE(DecodeLatin1Text("\x80", 2u, latinText));
    EXPECT_EQ(AStringView(latinText), AStringView("\xC2\x80"));
    const auto tooSmall = DecodeLatin1Text("\x80", 1u, latinText);
    ASSERT_FALSE(tooSmall);
    EXPECT_EQ(tooSmall.error(), Latin1TextError::TooLarge);
    EXPECT_TRUE(latinText.empty());
    const auto embeddedNul = DecodeLatin1Text(AStringView("\x80\0", 2u), 2u, latinText);
    ASSERT_FALSE(embeddedNul);
    EXPECT_EQ(embeddedNul.error(), Latin1TextError::InvalidText);
    const auto invalidUtf8 = EncodeLatin1Text(AStringView("\xC4\x80\xFF", 3u), 3u, latinText);
    ASSERT_FALSE(invalidUtf8);
    EXPECT_EQ(invalidUtf8.error(), Latin1TextError::InvalidText);
}

TEST(Global, SourceAndNewlineTextPreserveBoundaries){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    EXPECT_EQ(ReadFirstIdentifierAfterCComments(" \t/*one*/ //two\r\n123_name!"), "123_name");
    EXPECT_TRUE(ReadFirstIdentifierAfterCComments("/*unterminated*").empty());
    EXPECT_TRUE(ReadFirstIdentifierAfterCComments("//comment without newline").empty());
    EXPECT_TRUE(ReadFirstIdentifierAfterCComments(" @name").empty());
    EXPECT_TRUE(HasMatchingOuterParens("((x))"));
    EXPECT_FALSE(HasMatchingOuterParens("(x)(y)"));
    EXPECT_FALSE(HasMatchingOuterParens("(()"));
    EXPECT_FALSE(HasMatchingOuterParens("(x))"));
    AString<NWB::Core::Alloc::ScratchArena> quoted(scratchArena);
    EXPECT_EQ(SkipQuotedLiteral("\"a\\\"b\"x", 0u), 6u);
    EXPECT_EQ(AppendQuotedLiteral("\"a\\\"b\"x", 0u, quoted), 6u);
    EXPECT_EQ(AStringView(quoted), AStringView("\"a\\\"b\""));
    EXPECT_EQ(SkipQuotedLiteral("\"a\\", 0u), 3u);
    AString<NWB::Core::Alloc::ScratchArena> source("a\\\r\nb\\\nc\\\rd", scratchArena);
    SpliceSourceLines(source);
    EXPECT_EQ(AStringView(source), AStringView("abc\\\rd"));
    source = "\"//\"/*a\nb*/x//c\n";
    MaskCStyleSourceComments(source);
    EXPECT_EQ(AStringView(source), AStringView("\"//\"   \n   x   \n"));
    const auto joinedSlashes = JoinWithSingleSlash(scratchArena, "a/", "/b");
    const auto joinedPlain = JoinWithSingleSlash(scratchArena, "a", "b");
    const auto emptyBase = JoinWithSingleSlash(scratchArena, "", "b");
    const auto emptySuffix = JoinWithSingleSlash(scratchArena, "a/", "");
    EXPECT_EQ(AStringView(joinedSlashes), "a/b");
    EXPECT_EQ(AStringView(joinedPlain), "a/b");
    EXPECT_EQ(AStringView(emptyBase), "");
    EXPECT_EQ(AStringView(emptySuffix), "a/");
    AString<NWB::Core::Alloc::ScratchArena> lineText("\r\nx\n\r", scratchArena);
    CollapseCrLfInPlace(lineText);
    EXPECT_EQ(AStringView(lineText), AStringView("\nx\n\r"));
    constexpr WStringView s_MixedLines = L"a\r\nb\n\rc";
    EXPECT_EQ(CountBareLineFeeds(s_MixedLines), 1u);
    Array<wchar, 16u> expandedLines{};
    const usize expandedLength = CopyWithCrBeforeBareLf(s_MixedLines, expandedLines.data());
    EXPECT_EQ(WStringView(expandedLines.data(), expandedLength), WStringView(L"a\r\nb\r\n\rc"));
    EXPECT_EQ(CountUtf8ScalarsAssumingValid(AStringView("a\xC3\xA9", 3u)), 2u);
}

TEST(Global, UnsignedTextAndBytesAgreeAtMaximum){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    AString<NWB::Core::Alloc::ScratchArena> u64Text(scratchArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> u64Bytes(scratchArena);
    AppendUnsignedU64Text(u64Text, Limit<u64>::s_Max);
    AppendUnsignedU64Bytes(u64Bytes, Limit<u64>::s_Max);
    EXPECT_EQ(AStringView(u64Text), AStringView("18446744073709551615"));
    ASSERT_EQ(u64Bytes.size(), u64Text.size());
    EXPECT_TRUE(BytesMatch(Span<const u8>(u64Bytes.data(), u64Bytes.size()), Span<const u8>(reinterpret_cast<const u8*>(u64Text.data()), u64Text.size())));
}

TEST(Global, GraphemeAndTransparentHashRespectInvalidAndEmbeddedNull){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    const auto emptyBoundaries = BuildGraphemeBoundaries(scratchArena, AStringView{});
    ASSERT_TRUE(emptyBoundaries);
    ASSERT_EQ(emptyBoundaries->size(), 1u);
    EXPECT_EQ((*emptyBoundaries)[0u], 0u);
    EXPECT_FALSE(BuildGraphemeBoundaries(scratchArena, AStringView("\xED\xA0\x80", 3u)));

    using ScratchText = AString<NWB::Core::Alloc::ScratchArena>;
    const ScratchText key("a\0b", 3u, scratchArena);
    const AStringView borrowed("a\0b", 3u);
    EXPECT_EQ(TransparentStringHash<ScratchText>{}(key), TransparentStringHash<ScratchText>{}(borrowed));
    EXPECT_TRUE(TransparentStringEqual<ScratchText>{}(key, borrowed));
    EXPECT_FALSE(TransparentStringEqual<ScratchText>{}(key, AStringView("a", 1u)));
}

TEST(Global, CsvAndCaseFormattingPreserveEscapes){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    AString<NWB::Core::Alloc::ScratchArena> csv(scratchArena);
    AppendTextDiscardCrAndExpandLf(csv, "a\rb\n");
    EXPECT_EQ(AStringView(csv), "ab\r\n");
    csv.clear();
    AppendCamelToSnakeCase(csv, "HTTPServerRGB8Value");
    EXPECT_EQ(AStringView(csv), "http_server_rgb8_value");
    csv.clear();
    AppendCsvCell("a,\"b\"\r\nc", csv);
    EXPECT_EQ(AStringView(csv), "\"a,\"\"b\"\"\r\nc\"");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

