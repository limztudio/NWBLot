// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/global/name_case_types.adhd.h>

#include <gtest/gtest.h>

#include <global/basic_string.h>
#include <global/compact_string.h>
#include <global/containers.h>
#include <global/fixed_buffer.h>
#include <global/limit.h>
#include <global/text_utils.h>
#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_text_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_Alpha = NWB::Tests::GlobalCases::s_GlobalTestTableAlpha;

template<typename T>
concept CanTrimView = requires{ TrimView(DeclVal<T>()); TrimLeftView(DeclVal<T>()); };

static_assert(CanTrimView<ACompactString&>);
static_assert(CanTrimView<const ACompactString&>);
static_assert(!CanTrimView<ACompactString>);
static_assert(!CanTrimView<const ACompactString>);
static_assert(CanTrimView<AStringView>);
static_assert(CanTrimView<AStringView&>);
static_assert(CanTrimView<AString<>&>);
static_assert(!CanTrimView<AString<>>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, FixedBufferTextViewsTruncateAtCapacityWithoutReadingPastSlice){
    constexpr Array<char, 6u> s_Text = { 'a', 'b', 'c', 'd', 'e', 'f' };
    Array<char, 5u> text = {};
    CopyFixedBuffer(text, AStringView(s_Text.data(), 3u));
    EXPECT_EQ(AStringView(text.data()), AStringView("abc"));
    AppendFixedBuffer(text, AStringView(s_Text.data() + 3u, 2u));
    EXPECT_EQ(AStringView(text.data()), AStringView("abcd"));
    CopyFixedBuffer(text, AStringView(s_Text.data(), sizeof(s_Text)));
    EXPECT_EQ(AStringView(text.data()), AStringView("abcd"));
    CopyFixedBuffer(text, {});
    EXPECT_TRUE(AStringView(text.data()).empty());
}

TEST(Global, Utf8DecoderRespectsBoundedNonterminatedInput){
    const Array<char, 4u> bytes = { static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98), static_cast<char>(0x80) };
    const AStringView text(bytes.data(), bytes.size());
    const auto decoded = DecodeUtf8CodePoint(text);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->byteCount, 4);
    EXPECT_EQ(decoded->codePoint, 0x1F600u);
    for(usize size = 0u; size < text.size(); ++size)
        EXPECT_FALSE(DecodeUtf8CodePoint(text.substr(0u, size)));
    const Array<char, 2u> overlong = { static_cast<char>(0xC0), static_cast<char>(0x80) };
    EXPECT_FALSE(DecodeUtf8CodePoint(AStringView(overlong.data(), overlong.size())));
}

TEST(Global, CompactStringSearchUsesStoredCanonicalTextAndViewBoundaries){
    const ACompactString compact("Folder\\Example");
    EXPECT_EQ(compact.front(), 'f');
    EXPECT_EQ(compact.back(), 'e');
    EXPECT_EQ(compact.find(AStringView("/example")), 6u);
    EXPECT_EQ(compact.find('e', 8u), 13u);
    EXPECT_EQ(compact.find(AStringView{}, compact.size()), compact.size());
    EXPECT_EQ(compact.find(AStringView("EXAMPLE")), ACompactString::s_NPos);
    EXPECT_EQ(compact.find('z'), ACompactString::s_NPos);
}

TEST(Global, RejectedACompactStringAssignResetsText){
    ACompactString compact("seed");
    const Array<char, 3u> textWithNull = { 'a', '\0', 'b' };

    EXPECT_FALSE(compact.assign(AStringView(textWithNull.data(), textWithNull.size())));
    EXPECT_TRUE(compact.empty());
    EXPECT_TRUE(compact.view().empty());
    EXPECT_EQ(compact.c_str()[0], '\0');
}

TEST(Global, OversizedWideCompactStringAssignmentClearsPreviousValue){
    Array<wchar, WCompactString::s_MaxLength + 2u> oversized = {};
    for(usize i = 0u; i < WCompactString::s_MaxLength + 1u; ++i)
        oversized[i] = L'a';

    WCompactString rejected(L"unchanged");
    EXPECT_FALSE(rejected.assign(WStringView(oversized.data(), WCompactString::s_MaxLength + 1u)));
    EXPECT_TRUE(rejected.empty());
}

TEST(Global, TextUtilitiesRejectNullShortAndOverflowingInput){
    EXPECT_TRUE(SafeStringView(static_cast<const char*>(nullptr)).empty());
    EXPECT_TRUE(FitsU32(static_cast<usize>(Limit<u32>::s_Max)));
    EXPECT_FALSE(FitsU32(static_cast<u64>(Limit<u32>::s_Max) + 1u));
    EXPECT_FALSE(FitsU32(-1));
    EXPECT_TRUE(CanRepresentU64<u32>(Limit<u32>::s_Max));
    EXPECT_FALSE(CanRepresentU64<u32>(static_cast<u64>(Limit<u32>::s_Max) + 1u));
    EXPECT_TRUE(CanRepresentU64<i64>(static_cast<u64>(Limit<i64>::s_Max)));
    EXPECT_FALSE(CanRepresentU64<i64>(static_cast<u64>(Limit<i64>::s_Max) + 1u));
    EXPECT_FALSE(StartsWith(AStringView("al"), s_Alpha));

    const auto value = ParseVariableHexU64(AStringView("FFFFFFFFFFFFFFFF"));
    ASSERT_TRUE(value);
    EXPECT_EQ(*value, Limit<u64>::s_Max);
    EXPECT_FALSE(ParseVariableHexU64(AStringView()));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("0x")));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("10000000000000000")));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("xyz")));

    constexpr AStringView s_KeyValueText("alpha=one\r\nbeta=42\nempty=\n");
    const auto textValue = FindLineKeyValue(s_KeyValueText, "empty");
    ASSERT_TRUE(textValue);
    EXPECT_TRUE(textValue->empty());
    EXPECT_FALSE(FindLineKeyValue(s_KeyValueText, "missing"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

