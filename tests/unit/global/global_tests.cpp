// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/atomic_identity.h>
#include <global/auto_registration.h>
#include <global/basic_string.h>
#include <global/binary.h>
#include <global/binary_big_endian.h>
#include <global/binary_little_endian.h>
#include <global/bit.h>
#include <global/bit_matrix.h>
#include <global/call_text.h>
#include <global/checked_sum.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/csv_text.h>
#include <global/disjoint_set.h>
#include <global/diagnostics.h>
#include <global/filesystem/exclusive_publication.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/fixed_buffer.h>
#include <global/hierarchical_index_set.h>
#include <global/integer_rect.h>
#include <global/limit.h>
#include <global/name_hash.h>
#include <global/not_null.h>
#include <global/overflow.h>
#include <global/process_memory_map.h>
#include <global/projected_index.h>
#include <global/quantization.h>
#include <global/simplemath.h>
#include <global/simd/color.h>
#include <global/sha1.h>
#include <global/append_rollback.h>
#include <global/range_partition.h>
#include <global/range_algorithms.h>
#include <global/filesystem/file_staging.h>
#include <global/latin1_text.h>
#include <global/interval.h>
#include <global/source_text.h>
#include <global/string_hash.h>
#include <global/statistics.h>
#include <global/sync.h>
#include <global/termination.h>
#include <global/thread.h>
#include <global/text_utils.h>
#include <global/text_replacements.h>
#include <global/unicode/grapheme.h>
#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_tests{


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


struct ThrowingByteSize{};
struct ThrowingByteSizeComparison{
    explicit operator b8()const{ throw RuntimeException("byte size comparison failure"); }
};
[[nodiscard]] inline ThrowingByteSizeComparison operator>(const usize, const ThrowingByteSize)noexcept{ return {}; }


static constexpr AStringView s_Alpha = "alpha";
static constexpr AStringView s_Beta = "beta";
static constexpr AStringView s_Unit = "unit";
static constexpr AStringView s_Value42 = "value 42";
static constexpr AStringView s_ValueFormatTrailing = "value {}.trailing";
static constexpr WStringView s_ValueFormatTrailingWide = L"value {}.trailing";

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


static u32 s_DiagnosticEventCaptureCount = 0u;
static AStringView s_DiagnosticEventName = {};
static AStringView s_DiagnosticEventCategory = {};
static AStringView s_DiagnosticEventExpression = {};
static AStringView s_DiagnosticEventMessage = {};
static AStringView s_DiagnosticEventFile = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventNameText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventCategoryText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventExpressionText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventMessageText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventFileText = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ResetDiagnosticEventCapture()noexcept{
    s_DiagnosticEventCaptureCount = 0u;
    s_DiagnosticEventName = {};
    s_DiagnosticEventCategory = {};
    s_DiagnosticEventExpression = {};
    s_DiagnosticEventMessage = {};
    s_DiagnosticEventFile = {};
    s_DiagnosticEventNameText[0u] = 0;
    s_DiagnosticEventCategoryText[0u] = 0;
    s_DiagnosticEventExpressionText[0u] = 0;
    s_DiagnosticEventMessageText[0u] = 0;
    s_DiagnosticEventFileText[0u] = 0;
}

static void RecordDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
    ++s_DiagnosticEventCaptureCount;
    CopyFixedBuffer(s_DiagnosticEventNameText, record.event);
    CopyFixedBuffer(s_DiagnosticEventCategoryText, record.category);
    CopyFixedBuffer(s_DiagnosticEventExpressionText, record.expression);
    CopyFixedBuffer(s_DiagnosticEventMessageText, record.message);
    CopyFixedBuffer(s_DiagnosticEventFileText, record.file);
    s_DiagnosticEventName = AStringView(s_DiagnosticEventNameText.data());
    s_DiagnosticEventCategory = AStringView(s_DiagnosticEventCategoryText.data());
    s_DiagnosticEventExpression = AStringView(s_DiagnosticEventExpressionText.data());
    s_DiagnosticEventMessage = AStringView(s_DiagnosticEventMessageText.data());
    s_DiagnosticEventFile = AStringView(s_DiagnosticEventFileText.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct U32VectorView{
    using value_type = u32;


public:
    const u32* values = nullptr;
    usize valueCount = 0u;


public:
    [[nodiscard]] u32 operator[](const usize index)const noexcept{ return values[index]; }


public:
    [[nodiscard]] b8 empty()const noexcept{ return valueCount == 0u; }

    [[nodiscard]] usize size()const noexcept{ return valueCount; }

    [[nodiscard]] const u32* data()const noexcept{ return values; }

    [[nodiscard]] const u32* begin()const noexcept{ return values; }

    [[nodiscard]] const u32* end()const noexcept{ return values + valueCount; }

};


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

TEST(Global, ExhaustedPodReadsPreserveCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> binary(scratchArena);
    binary.reserve(4u);
    const u32 writtenValue = 0x11223344u;
    AppendPOD(binary, writtenValue);

    usize cursor = 0u;
    const auto readValue = ReadPOD<u32>(binary, cursor);
    ASSERT_TRUE(readValue);
    EXPECT_EQ(*readValue, writtenValue);
    EXPECT_EQ(cursor, sizeof(writtenValue));

    const usize failedCursor = cursor;
    EXPECT_FALSE(ReadPOD<u32>(binary, cursor));
    EXPECT_EQ(cursor, failedCursor);
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


TEST(Global, AutoRegistrationQueueDeduplicatesAndSnapshots){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::AutoRegistrationQueue<u32, NWB::Core::Alloc::GlobalArena> queue(NWB::Tests::s_TestArena);
    const auto equal = [](const u32 lhs, const u32 rhs)noexcept{ return lhs == rhs; };

    queue.appendUnique(7u, equal);
    queue.appendUnique(7u, equal);
    queue.appendUnique(19u, equal);

    ::Vector<u32, NWB::Core::Alloc::ScratchArena> values(scratchArena);
    values.reserve(6u);
    queue.copyTo(values);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0u], 7u);
    EXPECT_EQ(values[1u], 19u);

    queue.appendUnique(23u, equal);
    EXPECT_EQ(values.size(), 2u);

    queue.copyTo(values);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[2u], 23u);
}

TEST(Global, MemoryMapLookupUsesHalfOpenRangesAndRejectsMissingAddresses){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    constexpr AStringView s_Maps = "00001000-00002000 r-xp 00000020 00:00 0 /tmp/first.so\n" "00003000-00004000 r--p 00000000 00:00 0 /tmp/second.so\n";
    ::Vector<LinuxProcessMemoryMapEntry, NWB::Core::Alloc::ScratchArena> entries(scratchArena);
    entries.reserve(2u);
    ParseLinuxProcessMemoryMaps(s_Maps, entries);
    ASSERT_EQ(entries.size(), 2u);
    const auto first = FindLinuxProcessMemoryMapForAddress(entries, 0x1000u);
    const auto last = FindLinuxProcessMemoryMapForAddress(entries, 0x3fffu);
    ASSERT_TRUE(first);
    ASSERT_TRUE(last);
    EXPECT_EQ(first->path, AStringView("/tmp/first.so"));
    EXPECT_EQ(last->path, AStringView("/tmp/second.so"));
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x2000u));
}

TEST(Global, MemoryMapRejectsMalformedOffsetsWithoutDiscardingValidNeighbors){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    constexpr AStringView s_Maps = "1000-2000 r-xp 0 00:00 0 /valid\n" "2000-3000 r-xp invalid 00:00 0 /bad\n" "3000-4000 r-xp 10000000000000000 00:00 0 /overflow\n" "4000-5000 r--p 20 00:00 0\n";
    ::Vector<LinuxProcessMemoryMapEntry, NWB::Core::Alloc::ScratchArena> entries(scratchArena);
    ParseLinuxProcessMemoryMaps(s_Maps, entries);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].fileOffset, 0u);
    EXPECT_EQ(entries[1].fileOffset, 0x20u);
    EXPECT_TRUE(entries[1].path.empty());
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x2000u));
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x3000u));
}

TEST(Global, GenericPathKeepsNativeFilenameCharacters){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    const ::Path<NWB::Core::Alloc::ScratchArena> path(scratchArena, "one\\two");
    const auto text = PathToGenericString<char>(scratchArena, path);
#if defined(NWB_PLATFORM_WINDOWS)
    EXPECT_EQ(AStringView(text), "one/two");
#else
    EXPECT_EQ(AStringView(text), "one\\two");
#endif
}

TEST(Global, GrowingCapacitySaturatesAtSizeLimit){
    EXPECT_EQ(::NextGrowingCapacity((Limit<usize>::s_Max / 2u) + 1u, Limit<usize>::s_Max), Limit<usize>::s_Max);
}

TEST(Global, CheckedDivideUpRejectsZeroDivisor){
    EXPECT_FALSE(::DivideUpChecked(17u, 0u));
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

TEST(Global, DiagnosticFormatSupportsBoundedFormatViewsAndNullableAdapters){
    const DiagnosticEventText narrow = MakeDiagnosticEventText(s_ValueFormatTrailing.substr(0u, 8u), 42u);
    EXPECT_EQ(narrow.view(), s_Value42);
    const DiagnosticEventText wide = MakeDiagnosticEventText(s_ValueFormatTrailingWide.substr(0u, 8u), 42u);
    EXPECT_EQ(wide.view(), s_Value42);
    EXPECT_EQ(MakeDiagnosticEventText(static_cast<const char*>(nullptr), 42u).view(), AStringView{});
}

TEST(Global, DiagnosticEventTextViewsStopAtNullOrBufferBounds){
    DiagnosticEventText text;
    for(char& character : text.value)
        character = 'x';
    EXPECT_EQ(text.view().size(), sizeof(text.value));
    const DiagnosticEventText formatted = MakeDiagnosticEventText("{}", text);
    EXPECT_EQ(formatted.view().size(), sizeof(text.value) - 1u);
    EXPECT_EQ(formatted.view(), text.view().substr(0u, sizeof(text.value) - 1u));

    text.value[3u] = '\0';
    EXPECT_EQ(text.view(), AStringView("xxx"));
}

TEST(Global, TruncatedStringReadsDoNotAdvanceCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> truncated(scratchArena);
    truncated.reserve(5u);
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));
    usize cursor = 0u;
    EXPECT_FALSE(ReadString(truncated, cursor));
    EXPECT_EQ(cursor, 0u);
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

TEST(Global, StringTableUsesPrefixedBoundsAndRejectsEmptyAppend){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> stringTable(scratchArena);
    stringTable.reserve(16u);
    const auto alphaOffset = AppendStringTableText(stringTable, s_Alpha);
    const auto betaOffset = AppendStringTableText(stringTable, s_Beta);
    ASSERT_TRUE(alphaOffset);
    ASSERT_TRUE(betaOffset);

    ::Vector<u8, NWB::Core::Alloc::ScratchArena> prefixedBinary(scratchArena);
    prefixedBinary.reserve(17u);
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    const auto parsed = ReadStringTableText(prefixedBinary, 1u, stringTable.size(), *betaOffset);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->view(), s_Beta);

    const usize previousSize = stringTable.size();
    EXPECT_FALSE(AppendStringTableText(stringTable, AStringView()));
    EXPECT_EQ(stringTable.size(), previousSize);
}

TEST(Global, InvalidStringTableReads){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> unterminated(scratchArena);
    unterminated.reserve(2u);
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));
    EXPECT_FALSE(ReadStringTableText(unterminated, 0u, unterminated.size(), 0u));

    ::Vector<u8, NWB::Core::Alloc::ScratchArena> emptyText(scratchArena);
    emptyText.reserve(1u);
    emptyText.push_back(0u);
    EXPECT_FALSE(ReadStringTableText(emptyText, 0u, emptyText.size(), 0u));
}

TEST(Global, EmptyBinaryVectorReadClearsReusedOutputWithoutAdvancing){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> binary(scratchArena);
    binary.reserve(6u);
    ::Vector<u16, NWB::Core::Alloc::ScratchArena> source(scratchArena);
    source.reserve(3u);
    source.push_back(1u);
    source.push_back(2u);
    source.push_back(static_cast<u16>(0xBEEFu));

    ASSERT_TRUE(AppendBinaryVectorPayload(binary, source));

    usize cursor = 0u;
    ::Vector<u16, NWB::Core::Alloc::ScratchArena> parsed(scratchArena);
    parsed.reserve(4u);
    ASSERT_TRUE(ReadBinaryVectorPayload(binary, static_cast<u64>(source.size()), cursor, parsed));

    cursor = 0u;
    parsed.push_back(7u);
    ASSERT_TRUE(ReadBinaryVectorPayload(binary, 0u, cursor, parsed));
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, FixedVectorOverflowDoesNotAdvanceBinaryCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> vectorBinary(scratchArena);
    vectorBinary.reserve(6u);
    const Array<u16, 3u> values = { 4u, 5u, 6u };
    for(const u16 value : values)
        AppendPOD(vectorBinary, value);

    usize vectorCursor = 0u;
    FixedVector<u16, 2u> tooSmall;
    EXPECT_EQ(ReadBinaryVectorPayload(vectorBinary, 3u, vectorCursor, tooSmall), MakeUnexpected(BinaryVectorPayloadFailure::OutputOverflow));
    EXPECT_EQ(vectorCursor, 0u);
    EXPECT_TRUE(tooSmall.empty());
}

TEST(Global, RejectedBinaryVectorPayloadReadsDoNotAdvanceCursor){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u8, NWB::Core::Alloc::ScratchArena> truncated(scratchArena);
    truncated.reserve(5u);
    const u32 source = 0x12345678u;
    AppendPOD(truncated, source);

    usize cursor = 0u;
    ::Vector<u32, NWB::Core::Alloc::ScratchArena> parsed(scratchArena);
    parsed.reserve(4u);
    parsed.push_back(0xAABBCCDDu);
    EXPECT_EQ(ReadBinaryVectorPayload(truncated, 2u, cursor, parsed), MakeUnexpected(BinaryVectorPayloadFailure::SourceTruncated));
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, AppendTriviallyCopyableVectorSelfAppend){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u32, NWB::Core::Alloc::ScratchArena> values(scratchArena);
    values.reserve(3u);
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);

    AppendTriviallyCopyableVector(values, values);

    EXPECT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0u], 1u);
    EXPECT_EQ(values[1u], 2u);
    EXPECT_EQ(values[2u], 3u);
    EXPECT_EQ(values[3u], 1u);
    EXPECT_EQ(values[4u], 2u);
    EXPECT_EQ(values[5u], 3u);
}

TEST(Global, TriviallyCopyableVectorPreservesOverlappingSourceRanges){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    ::Vector<u32, NWB::Core::Alloc::ScratchArena> values(scratchArena);
    values.reserve(4u);
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);
    values.push_back(4u);

    const U32VectorView middle{ values.data() + 1u, 2u };
    AppendTriviallyCopyableVector(values, middle);

    EXPECT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0u], 1u);
    EXPECT_EQ(values[1u], 2u);
    EXPECT_EQ(values[2u], 3u);
    EXPECT_EQ(values[3u], 4u);
    EXPECT_EQ(values[4u], 2u);
    EXPECT_EQ(values[5u], 3u);

    const U32VectorView assignedMiddle{ values.data() + 1u, 3u };
    AssignTriviallyCopyableVector(values, assignedMiddle);

    EXPECT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0u], 2u);
    EXPECT_EQ(values[1u], 3u);
    EXPECT_EQ(values[2u], 4u);
}


#if !defined(_MSC_VER)
TEST(Global, BoundedRuntimeWrappersTerminateTruncatedText){
    Array<char, 4u> truncatedText = {};
    EXPECT_NE(NWB_STRCPY(truncatedText.data(), sizeof(truncatedText), "abcdef"), 0);
    EXPECT_STREQ(truncatedText.data(), "abc");

    Array<char, 4u> nullTerminatedText = { 'a', 'b', 'c', 'd' };
    EXPECT_NE(NWB_STRCAT(nullTerminatedText.data(), sizeof(nullTerminatedText), "e"), 0);
    EXPECT_EQ(nullTerminatedText[sizeof(nullTerminatedText) - 1u], '\0');
}
#endif

TEST(Global, DiagnosticEventHookRejectsReentryAndHonorsBoundedText){
    ResetDiagnosticEventCapture();

    const auto callback = MakeNotNull<DiagnosticEventCallback>([](const DiagnosticEventRecord& record)noexcept{
        RecordDiagnosticEvent(record);
        CaptureDiagnosticEvent("recursive", "ignored");
    });

    SetDiagnosticEventCallback(callback.get());
    CaptureDiagnosticEvent(AStringView("unit.trailing").substr(0u, 4u), AStringView("message.trailing").substr(0u, 7u), "diagnostics_test.cpp", 42u);
    ClearDiagnosticEventCallback(callback.get());
    CaptureDiagnosticEvent(s_Unit, "ignored");

    EXPECT_EQ(s_DiagnosticEventCaptureCount, 1u);
    ASSERT_NE(s_DiagnosticEventName.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventCategory.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventExpression.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventMessage.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventFile.data(), nullptr);
    EXPECT_EQ(s_DiagnosticEventName, AStringView(""));
    EXPECT_EQ(s_DiagnosticEventCategory, AStringView("unit"));
    EXPECT_EQ(s_DiagnosticEventExpression, AStringView(""));
    EXPECT_EQ(s_DiagnosticEventMessage, AStringView("message"));
    EXPECT_EQ(DiagnosticEventNameFromCategory("unknown"), StringView{});
}

TEST(Global, UnicodeAndAsciiHelpersRejectSurrogatesOverlongsAndInvalidIdentifiers){
    EXPECT_FALSE(IsUtf16HighSurrogate(0xD7FFu));
    EXPECT_TRUE(IsUtf16HighSurrogate(0xD800u));
    EXPECT_TRUE(IsUtf16HighSurrogate(0xDBFFu));
    EXPECT_FALSE(IsUtf16HighSurrogate(0xDC00u));
    EXPECT_FALSE(IsUtf16LowSurrogate(0xDBFFu));
    EXPECT_TRUE(IsUtf16LowSurrogate(0xDC00u));
    EXPECT_TRUE(IsUtf16LowSurrogate(0xDFFFu));
    EXPECT_FALSE(IsUtf16LowSurrogate(0xE000u));
    EXPECT_TRUE(IsUnicodeScalarValue(0xD7FFu));
    EXPECT_FALSE(IsUnicodeScalarValue(0xD800u));
    EXPECT_FALSE(IsUnicodeScalarValue(0xDFFFu));
    EXPECT_TRUE(IsUnicodeScalarValue(0xE000u));
    EXPECT_TRUE(IsUnicodeScalarValue(s_UnicodeMaxCodePoint));
    EXPECT_FALSE(IsUnicodeScalarValue(s_UnicodeMaxCodePoint + 1u));
    EXPECT_EQ(DecodeUtf16SurrogatePair(0xD800u, 0xDC00u), 0x10000u);
    EXPECT_EQ(DecodeUtf16SurrogatePair(0xDBFFu, 0xDFFFu), s_UnicodeMaxCodePoint);

    const Array<char, 4u> bomPrefixed = { static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF), 'a' };
    EXPECT_TRUE(HasUtf8Bom(AStringView(bomPrefixed.data(), bomPrefixed.size())));
    EXPECT_FALSE(HasUtf8Bom(AStringView(bomPrefixed.data(), 2u)));

    Array<char, s_Utf8MaxSequenceBytes> encoded = {};
    EXPECT_FALSE(EncodeUtf8CodePoint(0xD800u, encoded));
    EXPECT_FALSE(EncodeUtf8CodePoint(s_UnicodeMaxCodePoint + 1u, encoded));
    const auto encodedBytes = EncodeUtf8CodePoint(s_UnicodeMaxCodePoint, encoded);
    ASSERT_TRUE(encodedBytes);
    ASSERT_EQ(*encodedBytes, 4u);
    EXPECT_TRUE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 0u));
    EXPECT_FALSE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 1u));
    EXPECT_TRUE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 4u));
    EXPECT_FALSE(IsUtf8ScalarBoundary(AStringView(encoded.data(), encoded.size()), 5u));

    const auto decoded = DecodeUtf8Scalar(AStringView(encoded.data(), encoded.size()));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->codePoint, s_UnicodeMaxCodePoint);
    EXPECT_EQ(decoded->byteCount, 4);
    EXPECT_FALSE(DecodeUtf8Scalar(AStringView(encoded.data(), 3u)));
    const Array<char, 3u> surrogateBytes = { static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80) };
    EXPECT_FALSE(DecodeUtf8Scalar(AStringView(surrogateBytes.data(), surrogateBytes.size())));

    EXPECT_TRUE(IsUnicodeWhitespace(0x2000u));
    EXPECT_TRUE(IsUnicodeWhitespace(0x200au));
    EXPECT_FALSE(IsUnicodeWhitespace(0x1fffu));
    EXPECT_FALSE(IsUnicodeWhitespace(0x200bu));
    EXPECT_TRUE(IsUnicodeWhitespace(0xa0u));
    EXPECT_FALSE(IsUnicodeWhitespace(0x180eu));
    EXPECT_TRUE(IsAsciiPunctuation(0x21u));
    EXPECT_TRUE(IsAsciiPunctuation(0x7eu));
    EXPECT_FALSE(IsAsciiPunctuation(0x20u));
    EXPECT_FALSE(IsAsciiPunctuation(0x7fu));
    EXPECT_EQ(StripMatchingTrailingByte("f", 'f', 'F'), "f");
    EXPECT_EQ(StripMatchingTrailingByte("4F", 'f', 'F'), "4");
    usize tokenCursor = 0u;
    const auto token = ReadCommaOrSpaceDelimitedToken(" , x,,", tokenCursor);
    ASSERT_TRUE(token);
    EXPECT_EQ(*token, "x");
    EXPECT_EQ(tokenCursor, 4u);
    EXPECT_FALSE(ReadCommaOrSpaceDelimitedToken(" , x,,", tokenCursor));
    EXPECT_EQ(tokenCursor, 6u);
    EXPECT_EQ(*AlignU32PowerOfTwoChecked(Limit<u32>::s_Max - 3u, 4u), Limit<u32>::s_Max - 3u);
    EXPECT_FALSE(AlignU32PowerOfTwoChecked(Limit<u32>::s_Max, 4u));
    EXPECT_FALSE(AlignU32PowerOfTwoChecked(1u, 3u));
    EXPECT_EQ(*ComputeStridedRangeByteSize(0u, Limit<u64>::s_Max, Limit<u64>::s_Max), 0u);
    EXPECT_EQ(*ComputeStridedRangeByteSize(2u, 0u, Limit<u64>::s_Max), Limit<u64>::s_Max);
    EXPECT_FALSE(ComputeStridedRangeByteSize(2u, 1u, Limit<u64>::s_Max));
    Atomic<u64> highWater{ Limit<u64>::s_Max - 1u };
    UpdateAtomicMaxRelaxed(highWater, u64(0));
    EXPECT_EQ(highWater.load(MemoryOrder::relaxed), Limit<u64>::s_Max - 1u);
    UpdateAtomicMaxRelaxed(highWater, Limit<u64>::s_Max);
    EXPECT_EQ(DecrementAtomicOrTerminate(highWater), Limit<u64>::s_Max);
    EXPECT_EQ(highWater.load(MemoryOrder::relaxed), Limit<u64>::s_Max - 1u);
    Atomic<u64> identities{ Limit<u64>::s_Max - 1u };
    EXPECT_EQ(TryNextAtomicIdentity(identities), Limit<u64>::s_Max - 1u);
    EXPECT_EQ(TryNextAtomicIdentity(identities), 0u);
    EXPECT_EQ(identities.load(MemoryOrder::relaxed), Limit<u64>::s_Max);
    Atomic<u32> boundedIdentity{ Limit<u16>::s_Max };
    const auto lastBoundedIdentity = TryNextAtomicBoundedIdentity<u16>(boundedIdentity);
    ASSERT_TRUE(lastBoundedIdentity);
    EXPECT_EQ(*lastBoundedIdentity, Limit<u16>::s_Max);
    EXPECT_EQ(boundedIdentity.load(MemoryOrder::relaxed), static_cast<u32>(Limit<u16>::s_Max) + 1u);
    EXPECT_FALSE(TryNextAtomicBoundedIdentity<u16>(boundedIdentity));
    Atomic<u32> zeroBoundedIdentity{ 0u };
    EXPECT_FALSE(TryNextAtomicBoundedIdentity<u16>(zeroBoundedIdentity));
    EXPECT_EQ(zeroBoundedIdentity.load(MemoryOrder::relaxed), 0u);
    Atomic<u64> zeroIdentity{ 0u };
    EXPECT_EQ(TryNextAtomicIdentity(zeroIdentity), 0u);
    EXPECT_EQ(zeroIdentity.load(MemoryOrder::relaxed), 0u);
    const auto allowedZeroIdentity = TryNextAtomicIdentityAllowZero(zeroIdentity);
    ASSERT_TRUE(allowedZeroIdentity);
    EXPECT_EQ(*allowedZeroIdentity, 0u);
    EXPECT_EQ(zeroIdentity.load(MemoryOrder::relaxed), 1u);
    EXPECT_FALSE(TryNextAtomicIdentityAllowZero(identities));
    EXPECT_EQ(identities.load(MemoryOrder::relaxed), Limit<u64>::s_Max);
    EXPECT_EQ(MultiplySaturatingU64(Limit<u64>::s_Max, 0u), 0u);
    EXPECT_EQ(MultiplySaturatingU64(Limit<u64>::s_Max, 2u), Limit<u64>::s_Max);
    EXPECT_EQ(SaturateI64ToI32(Limit<i64>::s_Min), Limit<i32>::s_Min);
    EXPECT_EQ(SaturateI64ToI32(Limit<i64>::s_Max), Limit<i32>::s_Max);
    EXPECT_TRUE(IsRelativeSlashPathWithoutDotSegments("c:segment/file"));
    EXPECT_FALSE(IsRelativeSlashPathWithoutDotSegments("a/../b"));
    EXPECT_FALSE(IsRelativeSlashPathWithoutDotSegments("a//b"));
    EXPECT_FALSE(IsRelativeSlashPathWithoutDotSegments("a\\b"));
    EXPECT_FALSE(FitsFixedText(AStringView{}, 0u));
    EXPECT_TRUE(FitsFixedText(AStringView{}, 1u));
    EXPECT_FALSE(FitsFixedText(AStringView("a\0b", 3u), 4u));
    EXPECT_FALSE(TryFiniteF32FromF64(Limit<f64>::s_Infinity));
    EXPECT_FALSE(TryFiniteF32FromF64(static_cast<f64>(Limit<f32>::s_Max) * 2.0));
    EXPECT_TRUE(ParseFiniteF32ViaF64("-0.0"));
    EXPECT_EQ(BitCast<u32>(*ParseFiniteF32ViaF64("-0.0")), 0x80000000u);
    EXPECT_FALSE(IsAsciiIdentifier(AStringView()));
    EXPECT_FALSE(IsAsciiIdentifier(AStringView("9abc")));
    EXPECT_FALSE(IsAsciiIdentifier(AStringView("a-b")));
    EXPECT_TRUE(IsAsciiIdentifier(AStringView("_a9")));
}

TEST(Global, FilesystemStreamAndStageHelpersRejectInvalidAndTruncatedInputs){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    const Path<NWB::Core::Alloc::ScratchArena> emptyPath(scratchArena);
    EXPECT_EQ(PathDepth(emptyPath), 0u);
    const Path<NWB::Core::Alloc::ScratchArena> stageDir(scratchArena, "global_test_artifacts/stage_edge_test");
    ASSERT_TRUE(EnsureEmptyDirectory(stageDir));
    const auto stagedFile = stageDir / "payload.bin";
    const Array<u8, 4u> payload = { 0x10u, 0x20u, 0x30u, 0x40u };
    b8 created = false;
    ASSERT_TRUE(StageNewBinaryFileDurable(stagedFile, payload.data(), payload.size(), created));
    EXPECT_TRUE(created);
    EXPECT_TRUE(VerifyBinaryFileBytes(stagedFile, payload.data(), payload.size()));
    const Array<u8, 4u> mismatched = { 0x10u, 0x20u, 0x30u, 0x41u };
    EXPECT_FALSE(VerifyBinaryFileBytes(stagedFile, mismatched.data(), mismatched.size()));
    EXPECT_FALSE(VerifyBinaryFileBytes(stagedFile, payload.data(), payload.size() - 1u));
    EXPECT_FALSE(StageNewBinaryFileDurable(stagedFile, mismatched.data(), mismatched.size(), created));
    EXPECT_FALSE(created);
    EXPECT_TRUE(VerifyBinaryFileBytes(stagedFile, payload.data(), payload.size()));
    ASSERT_TRUE(ResizeFile(stagedFile, 2u));
    EXPECT_TRUE(VerifyBinaryFileBytes(stagedFile, payload.data(), 2u));
    ASSERT_TRUE(RemoveAllIfExists(stageDir));
}

TEST(Global, UniquePublicationPreservesWinnersAcrossRaces){
    NWB::Core::Alloc::ScratchArena arena(NWB::Tests::s_TestArena);
    const Path<NWB::Core::Alloc::ScratchArena> directory(arena, "global_test_artifacts");
    ASSERT_TRUE(EnsureDirectories(directory));
    const auto suggestedPath = directory / "publication.bin";
    auto reserved = StageUniqueBinaryFileDurable(suggestedPath, nullptr, 0u);
    ASSERT_TRUE(reserved);
    const Path<NWB::Core::Alloc::ScratchArena> path = Move(*reserved);
    const auto released = RemoveFile(path);
    ASSERT_TRUE(released && *released);
    const Array<u8, 4u> bytes{ 1u, 2u, 3u, 4u };
    const Array<u8, 4u> different{ 1u, 2u, 3u, 5u };
    const auto first = PublishUniqueFileIfAbsentOrIdentical(path, bytes);
    ASSERT_TRUE(first);
    EXPECT_TRUE(*first);
    const auto repeated = PublishUniqueFileIfAbsentOrIdentical(path, bytes);
    ASSERT_TRUE(repeated);
    EXPECT_FALSE(*repeated);
    const auto mismatch = PublishUniqueFileIfAbsentOrIdentical(path, different);
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error(), MakeErrorCode(ErrorCondition::file_exists));
    EXPECT_TRUE(VerifyBinaryFileBytes(path, bytes.data(), bytes.size()));
    Vector<u8, NWB::Core::Alloc::ScratchArena> borrowedBytes(arena);
    borrowedBytes.assign(different.begin(), different.end());
    EXPECT_FALSE(PublishUniqueFileIfAbsentOrIdentical(path, Span<const u8>(borrowedBytes.data(), borrowedBytes.size())));
    EXPECT_TRUE(BytesMatch(Span<const u8>(borrowedBytes.data(), borrowedBytes.size()), different));
    const auto removed = RemoveFile(path);
    ASSERT_TRUE(removed && *removed);

    Atomic<b8> start{ false };
    Array<u8, 2u> results{};
    Array<Thread, 2u> threads;
    for(usize index = 0u; index < threads.size(); ++index){
        threads[index] = Thread([&, index](){
            NWB::Core::Alloc::ScratchArena threadArena(NWB::Tests::s_TestArena);
            const Path<NWB::Core::Alloc::ScratchArena> threadPath(threadArena, path);
            while(!start.load(MemoryOrder::acquire))
                YieldThread();
            const auto result = PublishUniqueFileIfAbsentOrIdentical(threadPath, bytes);
            results[index] = result ? (*result ? 1u : 2u) : 0u;
        });
    }
    start.store(true, MemoryOrder::release);
    for(Thread& thread : threads)
        thread.join();
    EXPECT_TRUE((results[0u] == 1u && results[1u] == 2u) || (results[0u] == 2u && results[1u] == 1u));
    EXPECT_TRUE(VerifyBinaryFileBytes(path, bytes.data(), bytes.size()));
    const auto cleanup = RemoveFile(path);
    ASSERT_TRUE(cleanup && *cleanup);

    start.store(false, MemoryOrder::relaxed);
    results = {};
    Array<i32, 2u> publicationErrors{};
    for(usize index = 0u; index < threads.size(); ++index){
        threads[index] = Thread([&, index](){
            NWB::Core::Alloc::ScratchArena threadArena(NWB::Tests::s_TestArena);
            const Path<NWB::Core::Alloc::ScratchArena> threadPath(threadArena, path);
            while(!start.load(MemoryOrder::acquire))
                YieldThread();
            const Span<const u8> candidate = index == 0u ? Span<const u8>(bytes) : Span<const u8>(different);
            const auto result = PublishUniqueFileIfAbsentOrIdentical(threadPath, candidate);
            if(result && *result)
                results[index] = 1u;
            else if(!result){
                publicationErrors[index] = result.error().value();
                if(result.error() == MakeErrorCode(ErrorCondition::file_exists))
                    results[index] = 2u;
            }
            else
                results[index] = 3u;
        });
    }
    start.store(true, MemoryOrder::release);
    for(Thread& thread : threads)
        thread.join();
    EXPECT_TRUE((results[0u] == 1u && results[1u] == 2u) || (results[0u] == 2u && results[1u] == 1u)) << "results " << static_cast<u32>(results[0u]) << ", " << static_cast<u32>(results[1u]) << "; errors " << publicationErrors[0u] << ", " << publicationErrors[1u];
    const Span<const u8> winner = results[0u] == 1u ? Span<const u8>(bytes) : Span<const u8>(different);
    EXPECT_TRUE(VerifyBinaryFileBytes(path, winner.data(), winner.size()));
    const auto finalCleanup = RemoveFile(path);
    ASSERT_TRUE(finalCleanup && *finalCleanup);
}

TEST(Global, StagedPairFailureIdentifiesEachWorkPath){
    NWB::Core::Alloc::ScratchArena arena(NWB::Tests::s_TestArena);
    const Path<NWB::Core::Alloc::ScratchArena> primary(arena, "output.bin");
    const Path<NWB::Core::Alloc::ScratchArena> secondary(arena, "output.meta");
    const StagedFilePairPaths paths(arena, primary, secondary);
    const Array<const Path<NWB::Core::Alloc::ScratchArena>*, 4u> expected{ &paths.primaryTemporary, &paths.secondaryTemporary, &paths.primaryBackup, &paths.secondaryBackup };
    for(const auto stage : { StagedFilePairFailureStage::InspectOutput, StagedFilePairFailureStage::NonRegularOutput }){
        StagedFilePairResult result;
        result.failureStage = stage;
        result.failedWorkPath = true;
        for(u32 index = 0u; index < expected.size(); ++index){
            result.failedIndex = index;
            EXPECT_EQ(FailedStagedFilePairPath(paths, result), *expected[index]);
        }
        result.failedWorkPath = false;
        result.failedIndex = 0u;
        EXPECT_EQ(FailedStagedFilePairPath(paths, result), paths.primary);
        result.failedIndex = 1u;
        EXPECT_EQ(FailedStagedFilePairPath(paths, result), paths.secondary);
    }
    StagedFilePairResult state;
    state.backedUp[1u] = true;
    state.temporaryErrors[0u] = MakeErrorCode(ErrorCondition::io_error);
    StagedFilePairResult check;
    EXPECT_TRUE(GlobalFilesystemStagingDetail::AcceptPreCommitValidation(check, state));
    check.failureStage = StagedFilePairFailureStage::WorkPathAlreadyExists;
    EXPECT_TRUE(GlobalFilesystemStagingDetail::AcceptPreCommitValidation(check, state));
    EXPECT_TRUE(state.succeeded());
    check.failureStage = StagedFilePairFailureStage::NonRegularOutput;
    check.failedIndex = 2u;
    check.failedWorkPath = true;
    check.error = MakeErrorCode(ErrorCondition::invalid_argument);
    EXPECT_FALSE(GlobalFilesystemStagingDetail::AcceptPreCommitValidation(check, state));
    EXPECT_EQ(state.failureStage, check.failureStage);
    EXPECT_EQ(state.error, check.error);
    EXPECT_EQ(FailedStagedFilePairPath(paths, state), paths.primaryBackup);
    EXPECT_TRUE(state.backedUp[1u]);
    EXPECT_EQ(state.temporaryErrors[0u], MakeErrorCode(ErrorCondition::io_error));
}


TEST(Global, OwnershipInvariantTerminationIsAlwaysActive){
    EXPECT_DEATH(TerminateInvariant(), "");
}


TEST(Global, SharedBinaryAndTextHelpersPreserveEdgeCases){
    const Array<u8, 2> bytes{ 0u, 255u };
    const Array<u8, 2> different{ 0u, 254u };
    EXPECT_TRUE(BytesMatch(Span<const u8>{}, Span<const u8>{}));
    EXPECT_FALSE(BytesMatch(bytes, Span<const u8>(bytes.data(), 1u)));
    EXPECT_FALSE(BytesMatch(bytes, different));
    const Array<u8, 3> pattern{ 1u, 2u, 3u };
    Array<u8, 7> repeated{};
    FillRepeatingBytes(repeated.size(), pattern, repeated.data());
    EXPECT_EQ(repeated, (Array<u8, 7>{ 1u, 2u, 3u, 1u, 2u, 3u, 1u }));
    FillRepeatingBytes(0u, pattern, nullptr);
    const Array<f64, 2u> signedZeroSamples{ 0.0, -0.0 };
    const OnlineF64Statistics zeroStatistics = ComputeOnlineF64Statistics(signedZeroSamples);
    EXPECT_EQ(zeroStatistics.count, 2u);
    EXPECT_EQ(BitCast<u64>(zeroStatistics.minimum), 0x8000000000000000ull);
    EXPECT_EQ(BitCast<u64>(zeroStatistics.maximum), 0x8000000000000000ull);
    EXPECT_EQ(QuantizeUNormScalar(-Limit<f32>::s_Infinity, 255.0f), 0.0f);
    EXPECT_EQ(QuantizeUNormScalar(Limit<f32>::s_Infinity, 255.0f), 255.0f);
    EXPECT_EQ(QuantizeUNormScalar(0.5f, 255.0f), 128.0f);
    EXPECT_EQ(QuantizeSNormScalar(-0.5f, 127.0f), -64.0f);
    EXPECT_EQ(BitCast<u32>(QuantizeSNormScalar(-0.0f, 127.0f)), 0u);
    EXPECT_EQ(QuantizeSNormScalar(BitCast<f32>(0x7fc01234u), 127.0f), -127.0f);
    const SIMDVectorSingle signedColor = VectorQuantizeSNorm(VectorSet(-0.0f, -0.5f, 0.5f, -2.0f), VectorReplicate(127.0f));
    EXPECT_EQ(VectorGetX(signedColor), 0.0f);
    EXPECT_EQ(VectorGetY(signedColor), -64.0f);
    EXPECT_EQ(VectorGetZ(signedColor), 64.0f);
    EXPECT_EQ(VectorGetW(signedColor), -127.0f);
    const SIMDVectorSingle quantizedColor = VectorQuantizeUNorm(VectorSet(-1.0f, 0.5f, 1.0f, 2.0f), VectorReplicate(255.0f));
    EXPECT_EQ(VectorGetX(quantizedColor), 0.0f);
    EXPECT_EQ(VectorGetY(quantizedColor), 128.0f);
    EXPECT_EQ(VectorGetZ(quantizedColor), 255.0f);
    EXPECT_EQ(VectorGetW(quantizedColor), 255.0f);
    const SIMDVectorSingle encodedColor = VectorLinearToSRGB(VectorSet(-1.0f, 0.0031308f, 1.0f, 2.0f));
    EXPECT_EQ(VectorGetX(encodedColor), 0.0f);
    EXPECT_NEAR(VectorGetY(encodedColor), 0.0031308f * 12.92f, 0.000001f);
    EXPECT_NEAR(VectorGetZ(encodedColor), 1.0f, 0.000001f);
    EXPECT_NEAR(VectorGetW(encodedColor), 1.0f, 0.000001f);
    EXPECT_FALSE(UnsignedRangesOverlap(1u, 0u, Limit<u64>::s_Max, 2u));
    EXPECT_TRUE(UnsignedRangesOverlap(Limit<u64>::s_Max, 2u, 0u, 1u));
    Array<u64, s_BitsPerU64> matrix{};
    matrix[1u] = u64(1) << 63u;
    TransposeBitMatrix64(matrix);
    EXPECT_EQ(matrix[63u], 2u);
    TransposeBitMatrix64(matrix);
    EXPECT_EQ(matrix[1u], u64(1) << 63u);

    usize emptyCursor = 0u;
    const auto emptyText = ReadBinaryText(BinaryByteView{}, 0u, emptyCursor);
    ASSERT_TRUE(emptyText);
    EXPECT_TRUE(emptyText->empty());
    EXPECT_EQ(emptyCursor, 0u);
    usize cursor = bytes.size();
    EXPECT_FALSE(ReadBinaryText(BinaryByteView{ bytes.data(), bytes.size() }, 1u, cursor));
    EXPECT_EQ(cursor, bytes.size());

    const Array<u32, 2u> narrowSum{ Limit<u32>::s_Max, 1u };
    const auto wrappedNarrowSum = TrySumUnsignedWithU64Limit<u32>(narrowSum);
    ASSERT_TRUE(wrappedNarrowSum);
    EXPECT_EQ(*wrappedNarrowSum, 0u);
    const Array<u64, 2u> wideSumOverflow{ Limit<u64>::s_Max, 1u };
    EXPECT_FALSE(TrySumUnsignedWithU64Limit<u64>(wideSumOverflow));
    EXPECT_EQ(TrySumUnsignedWithU64Limit<u64>(Span<const u64>{}).value(), 0u);
    const Array<u64, 2u> exactMaximumSum{ Limit<u64>::s_Max - 1u, 1u };
    EXPECT_EQ(TrySumUnsignedWithU64Limit<u64>(exactMaximumSum).value(), Limit<u64>::s_Max);
    const Array<u64, 2u> maximumThenZero{ Limit<u64>::s_Max, 0u };
    EXPECT_FALSE(TrySumNonzeroU64(maximumThenZero));
    const Array<u64, 1u> maximumCount{ Limit<u64>::s_Max };
    const Array<u64, 2u> overflowingCounts{ Limit<u64>::s_Max, 1u };
    const Array<u64, 1u> zeroCount{ 0u };
    EXPECT_EQ(TrySumNonzeroU64(Span<const u64>{}).value(), 0u);
    EXPECT_EQ(TrySumNonzeroU64(maximumCount).value(), Limit<u64>::s_Max);
    EXPECT_FALSE(TrySumNonzeroU64(overflowingCounts));
    EXPECT_FALSE(TrySumNonzeroU64(zeroCount));
    u64 total = 11u;
    EXPECT_FALSE(AccumulateBoundedCount(0u, 10u, total));
    EXPECT_EQ(total, 11u);
    total = Limit<u64>::s_Max - 1u;
    EXPECT_FALSE(AccumulateBoundedCount(2u, Limit<u64>::s_Max, total));
    EXPECT_EQ(total, Limit<u64>::s_Max - 1u);

    EXPECT_EQ(PackUnorderedU32Pair(0u, Limit<u32>::s_Max), 0xffffffffull);
    EXPECT_EQ(PackUnorderedU32Pair(Limit<u32>::s_Max, 0u), 0xffffffffull);
    EXPECT_EQ(PackUnorderedU32Pair(Limit<u32>::s_Max, Limit<u32>::s_Max), Limit<u64>::s_Max);
    constexpr u64 packedWords = PackU32Words(0xffffffffu, 0x12345678u);
    EXPECT_EQ(packedWords, 0x12345678ffffffffull);
    EXPECT_EQ(LowU32Word(packedWords), 0xffffffffu);
    EXPECT_EQ(HighU32Word(packedWords), 0x12345678u);
    EXPECT_EQ(PackFourCharTag('H', 'a', 'n', 'g'), 0x48616e67u);
    EXPECT_EQ(PackFourCharTag(static_cast<char>(0x80), 'a', 'b', 'c'), (static_cast<u32>(static_cast<char>(0x80)) << 24u) | 0x00616263u);
    EXPECT_EQ(*SignedDifference(0u, static_cast<u64>(Limit<i64>::s_Max) + 1u), Limit<i64>::s_Min);
    EXPECT_FALSE(SignedDifference(0u, static_cast<u64>(Limit<i64>::s_Max) + 2u));
    EXPECT_FALSE(SignedDifference(static_cast<u64>(Limit<i64>::s_Max) + 1u, 0u));
    const Array<usize, 4> ordered{ 1u, 3u, 3u, 8u };
    EXPECT_EQ(LowerBoundIndex(Span<const usize>{}, 3u), 0u);
    EXPECT_EQ(LowerBoundIndex(ordered, 3u), 1u);
    EXPECT_EQ(LowerBoundIndex(ordered, 9u), ordered.size());
    const Array<u8, 3u> partialBytes{ 0x12u, 0x34u, 0x56u };
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, 0u), 0x00563412u);
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, 2u), 0x56u);
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, 3u), 0u);
    EXPECT_EQ(ReadPartialU32LittleEndian(partialBytes, Limit<usize>::s_Max), 0x56341200u);
    EXPECT_EQ(ReadAdmittedU16BigEndian(partialBytes, 1u), 0x3456u);
    struct ThrowingByteContainer{
        [[nodiscard]] usize size()const noexcept{ return 4u; }
        u8& operator[](usize){ throw RuntimeException("byte assignment failure"); }
    };
    ThrowingByteContainer throwingBytes;
    EXPECT_THROW(WriteF32LittleEndianAt(1.0f, 0u, throwingBytes), RuntimeException);
    struct ThrowingLvalueByteContainer{
        u8 byte = 0u;
        [[nodiscard]] usize size()const noexcept{ return 4u; }
        u8& operator[](const usize&){ throw RuntimeException("lvalue byte index failure"); }
        u8& operator[](usize&&)noexcept{ return byte; }
    };
    ThrowingLvalueByteContainer lvalueBytes;
    static_assert(!noexcept(WriteF32LittleEndianAt(1.0f, 0u, lvalueBytes)));
    EXPECT_THROW(WriteF32LittleEndianAt(1.0f, 0u, lvalueBytes), RuntimeException);
    struct ThrowingSizeByteContainer{
        u8 byte = 0u;
        [[nodiscard]] ThrowingByteSize size()const noexcept{ return {}; }
        u8& operator[](usize)noexcept{ return byte; }
    };
    ThrowingSizeByteContainer sizeBytes;
    static_assert(!noexcept(WriteF32LittleEndianAt(1.0f, 0u, sizeBytes)));
    EXPECT_THROW(WriteF32LittleEndianAt(1.0f, 0u, sizeBytes), RuntimeException);
    FixedVector<u8, 8u> floatBytes;
    floatBytes.resize(7u);
    WriteF32LittleEndianAt(BitCast<f32>(0x80000000u), 0u, floatBytes);
    EXPECT_EQ(floatBytes[3u], 0x80u);
    WriteF32LittleEndianAt(BitCast<f32>(0x7fc01234u), 4u, floatBytes);
    EXPECT_EQ(floatBytes[4u], 0u);
    floatBytes.resize(8u);
    WriteF32LittleEndianAt(BitCast<f32>(0x7fc01234u), 4u, floatBytes);
    EXPECT_EQ(floatBytes[4u], 0x34u);
    EXPECT_EQ(floatBytes[5u], 0x12u);
    EXPECT_EQ(floatBytes[6u], 0xc0u);
    EXPECT_EQ(floatBytes[7u], 0x7fu);
    EXPECT_DEATH(WriteF32LittleEndianAt(0.0f, Limit<usize>::s_Max, floatBytes), "");
    EXPECT_FALSE(AlignUpCheckedSize(Limit<usize>::s_Max, 4u));
    EXPECT_FALSE(AlignUpChecked(Limit<usize>::s_Max, usize{4u}));
    ASSERT_TRUE(AlignUpChecked(Limit<usize>::s_Max - 3u, usize{4u}));
    EXPECT_EQ(*AlignUpChecked(Limit<usize>::s_Max - 3u, usize{4u}), Limit<usize>::s_Max - 3u);
    EXPECT_TRUE(DifferentF64Bits(0.0, -0.0));
    EXPECT_FALSE(DifferentF64Bits(BitCast<f64>(0x7ff8000000001234ull), BitCast<f64>(0x7ff8000000001234ull)));
    EXPECT_TRUE(DifferentF64Bits(BitCast<f64>(0x7ff8000000001234ull), BitCast<f64>(0x7ff8000000001235ull)));
    EXPECT_EQ(MixMurmur64Finalizer(0u), 0u);
    EXPECT_EQ(MixMurmur64Finalizer(1u), 0xb456bcfc34c2cb2cull);
    EXPECT_EQ(BoolToYesNoText(true), "yes");
    EXPECT_EQ(BoolToYesNoText(false), "no");
    EXPECT_EQ(UpdateFnv64U32LittleEndian(s_Fnv64OffsetBasis, 0x12345678u), 0xcccfd053e47c3365ull);
    EXPECT_EQ(UpdateFnv64U64LittleEndian(s_Fnv64OffsetBasis, 0x0123456789abcdefull), 0x37eb3f3347761c55ull);
    NameHash lanes{};
    lanes.qwords = { 0x0123456789abcdefull, 0u, 0xffffffffffffffffull, 0x0102030405060708ull, 5u, 6u, 7u, 8u };
    EXPECT_EQ(UpdateFnv64NameHash(0x123456789abcdef0ull, lanes), 0x08d4d531507706dcull);
    EXPECT_EQ(FoldNameHash64(lanes), 0xe33b773ee6cabb20ull);
    EXPECT_EQ(ToAsciiLowerByte(0xC3u), 0xC3u);
    EXPECT_NE(ToAsciiLowerByte(static_cast<u8>('\\')), ToAsciiLowerByte(static_cast<u8>('/')));
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
    EXPECT_EQ(SaturateU64ToI32(static_cast<u64>(Limit<i32>::s_Max) + 1u), Limit<i32>::s_Max);

    const Array<u32, 3> choices{ 0u, 1u, 2u };
    const auto optionText = [](const u32 value)noexcept -> AStringView{ return value == 2u ? "other" : "mixed"; };
    const auto parsed = ParseOptionText(AStringView("  MiXeD\t"), Span<const u32>(choices), optionText);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(*parsed, 0u);
    EXPECT_FALSE(ParseOptionText(AStringView("missing"), Span<const u32>(choices), optionText));
    EXPECT_FALSE(ParseOptionText(AStringView("mixed"), Span<const u32>{}, optionText));

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
    u32 countCalls = 0u;
    const auto countRows = [&countCalls]()noexcept{ ++countCalls; return u64(3u); };
    const auto matchFirst = [](const u64 index)noexcept{ return index == 0u; };
    const auto firstReverse = FindDirectionalIndex(2u, true, countRows, matchFirst);
    ASSERT_TRUE(firstReverse);
    EXPECT_EQ(*firstReverse, 0u);
    EXPECT_EQ(countCalls, 4u);
    countCalls = 0u;
    EXPECT_FALSE(FindDirectionalIndex(3u, false, countRows, matchFirst));
    EXPECT_EQ(countCalls, 1u);
    const auto matchNone = [](const u64)noexcept{ return false; };
    EXPECT_FALSE(FindDirectionalIndex(0u, true, countRows, matchNone));
    const auto throwingMatch = [](const u64) -> b8{ throw RuntimeException("directional predicate failure"); };
    static_assert(!noexcept(FindDirectionalIndex(0u, false, countRows, throwingMatch)));
    EXPECT_THROW(static_cast<void>(FindDirectionalIndex(0u, false, countRows, throwingMatch)), RuntimeException);
    u32 shrinkingCalls = 0u;
    u32 matchedCalls = 0u;
    const auto shrinkingCount = [&shrinkingCalls]()noexcept{ return ++shrinkingCalls == 1u ? u64(3u) : u64(1u); };
    const auto trackMatch = [&matchedCalls](const u64)noexcept{ ++matchedCalls; return true; };
    EXPECT_FALSE(FindDirectionalIndex(2u, false, shrinkingCount, trackMatch));
    EXPECT_EQ(shrinkingCalls, 2u);
    EXPECT_EQ(matchedCalls, 0u);
    const auto maximumRowCount = []()noexcept{ return Limit<u64>::s_Max; };
    EXPECT_FALSE(FindDirectionalIndex(Limit<u64>::s_Max - 1u, false, maximumRowCount, matchNone));
    const auto throwingCount = []() -> u64{ throw RuntimeException("directional count failure"); };
    static_assert(!noexcept(FindDirectionalIndex(0u, false, throwingCount, matchNone)));
    EXPECT_THROW(static_cast<void>(FindDirectionalIndex(0u, false, throwingCount, matchNone)), RuntimeException);
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
    const Array<u32, 3u> valuesToValidate{ 2u, 3u, 0u };
    Array<u32, 4u> validationEvents{};
    usize validationEventCount = 0u;
    const auto recordValid = [&validationEvents, &validationEventCount](const u32 value)noexcept{ validationEvents[validationEventCount++] = 100u + value; return value != 0u; };
    const auto recordDuplicate = [&validationEvents, &validationEventCount](const u32 current, const u32 previous)noexcept{ validationEvents[validationEventCount++] = current * 10u + previous; return current == previous; };
    EXPECT_TRUE(AllValidAndDistinct(Span<const u32>{}, recordValid, recordDuplicate));
    EXPECT_TRUE(AllValidAndDistinct(Span<const u32>(valuesToValidate.data(), 1u), [](const u32 value)noexcept{ return value != 0u; }, [](const u32 lhs, const u32 rhs)noexcept{ return lhs == rhs; }));
    EXPECT_FALSE(AllValidAndDistinct(Span<const u32>(valuesToValidate), recordValid, recordDuplicate));
    EXPECT_EQ(validationEventCount, 4u);
    EXPECT_EQ(validationEvents, (Array<u32, 4u>{ 102u, 103u, 32u, 100u }));
    const Array<u32, 4u> duplicateValues{ 2u, 3u, 3u, 5u };
    u32 visitedValues = 0u;
    u32 comparedValues = 0u;
    const auto countValid = [&visitedValues](const u32)noexcept{ ++visitedValues; return true; };
    const auto countDuplicate = [&comparedValues](const u32 current, const u32 previous)noexcept{ ++comparedValues; return current == previous; };
    EXPECT_FALSE(AllValidAndDistinct(Span<const u32>(duplicateValues), countValid, countDuplicate));
    EXPECT_EQ(visitedValues, 3u);
    EXPECT_EQ(comparedValues, 3u);
    const auto throwingValid = [](const u32) -> b8{ throw RuntimeException("validity predicate failure"); };
    static_assert(!noexcept(AllValidAndDistinct(Span<const u32>{}, throwingValid, recordDuplicate)));
    EXPECT_THROW(static_cast<void>(AllValidAndDistinct(Span<const u32>(valuesToValidate), throwingValid, recordDuplicate)), RuntimeException);
    struct KeyedValue{ u32 key; u32 order; };
    Vector<KeyedValue, NWB::Core::Alloc::ScratchArena> duplicates(scratchArena);
    duplicates.push_back({ 2u, 0u });
    duplicates.push_back({ 1u, 1u });
    duplicates.push_back({ 2u, 2u });
    duplicates.push_back({ 1u, 3u });
    duplicates.push_back({ 3u, 4u });
    StableDeduplicate(duplicates, [](const KeyedValue& lhs, const KeyedValue& rhs)noexcept{ return lhs.key == rhs.key; });
    ASSERT_EQ(duplicates.size(), 3u);
    EXPECT_EQ(duplicates[0u].order, 0u);
    EXPECT_EQ(duplicates[1u].order, 1u);
    EXPECT_EQ(duplicates[2u].order, 4u);
    const auto throwingSameKey = [](const KeyedValue&, const KeyedValue&) -> b8{ throw RuntimeException("deduplication predicate failure"); };
    EXPECT_THROW(StableDeduplicate(duplicates, throwingSameKey), RuntimeException);
    EXPECT_EQ(duplicates.size(), 3u);
    struct ThrowingAssignment{
        u32 key = 0u;
        ThrowingAssignment& operator=(ThrowingAssignment& other)noexcept{ key = other.key; return *this; }
        ThrowingAssignment& operator=(const ThrowingAssignment&){ throw RuntimeException("const assignment failure"); }
    };
    struct SmallSequence{
        Array<ThrowingAssignment, 3u> entries{ ThrowingAssignment{ 1u }, ThrowingAssignment{ 1u }, ThrowingAssignment{ 2u } };
        usize count = 3u;
        [[nodiscard]] usize size()const noexcept{ return count; }
        ThrowingAssignment& operator[](const usize index)noexcept{ return entries[index]; }
        void resize(const usize value)noexcept{ count = value; }
    };
    struct ThrowingComparison{
        [[nodiscard]] b8 operator()(ThrowingAssignment& lhs, ThrowingAssignment& rhs)const noexcept{ return lhs.key == rhs.key; }
        [[nodiscard]] b8 operator()(ThrowingAssignment&, const ThrowingAssignment&)const{ throw RuntimeException("const comparison failure"); }
    };
    const auto sameAssignmentKey = [](const ThrowingAssignment& lhs, const ThrowingAssignment& rhs)noexcept{ return lhs.key == rhs.key; };
    SmallSequence assignmentValues;
    static_assert(!noexcept(StableDeduplicate(assignmentValues, sameAssignmentKey)));
    EXPECT_THROW(StableDeduplicate(assignmentValues, sameAssignmentKey), RuntimeException);
    SmallSequence comparisonValues;
    const ThrowingComparison throwingComparison;
    static_assert(!noexcept(StableDeduplicate(comparisonValues, throwingComparison)));
    EXPECT_THROW(StableDeduplicate(comparisonValues, throwingComparison), RuntimeException);
    struct ThrowingIndexSequence{
        KeyedValue value{};
        [[nodiscard]] usize size()const noexcept{ return 1u; }
        KeyedValue& operator[](usize&){ throw RuntimeException("lvalue index failure"); }
        KeyedValue& operator[](usize&&)noexcept{ return value; }
        void resize(usize&)noexcept{}
        void resize(usize&&)noexcept{}
    };
    const auto sameKeyedValue = [](const KeyedValue& lhs, const KeyedValue& rhs)noexcept{ return lhs.key == rhs.key; };
    ThrowingIndexSequence indexValues;
    static_assert(!noexcept(StableDeduplicate(indexValues, sameKeyedValue)));
    EXPECT_THROW(StableDeduplicate(indexValues, sameKeyedValue), RuntimeException);
    struct ThrowingResizeSequence{
        KeyedValue value{};
        [[nodiscard]] usize size()const noexcept{ return 0u; }
        KeyedValue& operator[](usize&)noexcept{ return value; }
        KeyedValue& operator[](usize&&)noexcept{ return value; }
        void resize(usize&){ throw RuntimeException("lvalue resize failure"); }
        void resize(usize&&)noexcept{}
    };
    ThrowingResizeSequence resizeValues;
    static_assert(!noexcept(StableDeduplicate(resizeValues, sameKeyedValue)));
    EXPECT_THROW(StableDeduplicate(resizeValues, sameKeyedValue), RuntimeException);
    const Array<i32, 4u> projectedValues{ 3, 0, 1, -2 };
    Vector<usize, NWB::Core::Alloc::ScratchArena> projectedIndices(scratchArena);
    BuildProjectedRecordIndex(projectedIndices, projectedValues, [](const i32 value)noexcept{ return value != 0; }, [](const i32 lhs, const i32 rhs)noexcept{ return lhs < rhs; });
    ASSERT_EQ(projectedIndices.size(), 3u);
    EXPECT_EQ(projectedIndices[0u], 3u);
    EXPECT_EQ(projectedIndices[1u], 2u);
    EXPECT_EQ(projectedIndices[2u], 0u);
    const u32 one = 1u;
    const u32 two = 2u;
    const u32 three = 3u;
    const Array<const u32*, 4u> items{ &one, &two, nullptr, &three };
    Vector<u8, NWB::Core::Alloc::ScratchArena> state(items.size(), 0u, scratchArena);
    Vector<const u32*, NWB::Core::Alloc::ScratchArena> sorted(scratchArena);
    TopologicallySortPointers(Span<const u32* const>(items.data(), items.size()), [](const u32 current, const u32 dependency)noexcept{
        return (current == 1u && dependency == 2u) || (current == 2u && dependency == 1u);
    }, state, sorted);
    ASSERT_EQ(sorted.size(), 4u);
    EXPECT_EQ(sorted[0u], &two);
    EXPECT_EQ(sorted[1u], &one);
    EXPECT_EQ(sorted[2u], nullptr);
    EXPECT_EQ(sorted[3u], &three);
    EXPECT_EQ(AddSignedOffset(u64(1) << 63u, Limit<i64>::s_Min).value(), 0u);
    EXPECT_FALSE(AddSignedOffset(0u, Limit<i64>::s_Min));
    EXPECT_FALSE(AddSignedOffset(Limit<u64>::s_Max, 1));
    EXPECT_EQ(DivideUpWrapping(Limit<u32>::s_Max, 8u), 0u);
    EXPECT_EQ(DivideUpWrapping(Limit<u64>::s_Max, u64(2)), 0u);
    EXPECT_EQ(DivideUpWrapping(usize(9), 8u), usize(2));
    const BalancedPartitionRange firstRange = ComputeBalancedPartitionRange(10u, 0u, 2u, 1u);
    const BalancedPartitionRange lastRange = ComputeBalancedPartitionRange(10u, 2u, 2u, 1u);
    EXPECT_EQ(firstRange.first, 10u);
    EXPECT_EQ(firstRange.last, 13u);
    EXPECT_EQ(lastRange.first, 15u);
    EXPECT_EQ(lastRange.last, 17u);
    Atomic<u64> wrappingIdentity{ Limit<u64>::s_Max };
    EXPECT_EQ(NextAtomicWrappingNonzero(wrappingIdentity), 1u);
    u64 wrappingRevision = Limit<u64>::s_Max;
    AdvanceWrappingNonzero(wrappingRevision);
    EXPECT_EQ(wrappingRevision, 1u);
    EXPECT_FALSE(HasSingleSetBit(u16(0)));
    EXPECT_TRUE(HasSingleSetBit(u16(0x8000)));
    EXPECT_EQ(LowestSetBit(u8(0x90)), u8(0x10));
    EXPECT_EQ(AlignmentOffsetPowerOfTwo(Limit<usize>::s_Max, 8u), 1u);
    EXPECT_EQ(SubtractSaturating(u64(1), u64(2)), 0u);
    EXPECT_EQ(BitWidthLowU32(0xffffffffu, 0u), 0u);
    EXPECT_EQ(BitWidthLowU32(0x80000100u, 8u), 0u);
    EXPECT_EQ(BitWidthLowU32(0x80000000u, 32u), 32u);
    EXPECT_TRUE(IsNewerSerialU32(0u, Limit<u32>::s_Max));
    EXPECT_FALSE(IsNewerSerialU32(0x80000000u, 0u));
    EXPECT_FALSE(IsNewerSerialU32(7u, 7u));
    EXPECT_TRUE(IsNotOlderSerialU32(7u, 7u));
    EXPECT_TRUE(IsNotOlderSerialU32(0u, Limit<u32>::s_Max));
    EXPECT_TRUE(IsNotOlderSerialU32(0x7fffffffu, 0u));
    EXPECT_FALSE(IsNotOlderSerialU32(0x80000000u, 0u));
    EXPECT_FALSE(IsNotOlderSerialU32(0x80000001u, 0u));
    EXPECT_TRUE(IsSerialU32InWindow(0u, 0xfffffffeu, 1u));
    EXPECT_TRUE(IsSerialU32InWindow(1u, 0xfffffffeu, 1u));
    EXPECT_FALSE(IsSerialU32InWindow(2u, 0xfffffffeu, 1u));
    EXPECT_FALSE(IsSerialU32InWindow(0xfffffffdu, 0xfffffffeu, 1u));
    EXPECT_TRUE(IsSerialU32InWindow(0x7fffffffu, 0u, 0x7fffffffu));
    EXPECT_FALSE(IsSerialU32InWindow(0x80000000u, 0u, 0x7fffffffu));
    EXPECT_FALSE(IsSerialU32InWindow(0x80000001u, 0u, 0x7fffffffu));
    EXPECT_FALSE(IsSerialU32InWindow(0u, 0u, 0x80000000u));
    EXPECT_EQ(ComputeSobol2U32(0u), 0u);
    EXPECT_EQ(ComputeSobol2U32(1u), 0x8000000080000000ull);
    EXPECT_EQ(ComputeSobol2U32(2u), 0xc000000040000000ull);
    EXPECT_EQ(ComputeSobol2U32(3u), 0x40000000c0000000ull);
    EXPECT_EQ(DivideFloorI64(Limit<i64>::s_Min, 1), Limit<i64>::s_Min);
    EXPECT_EQ(DivideFloorI64(-3, 2), -2);
    EXPECT_EQ(DivideCeilI64(-3, 2), -1);
    EXPECT_EQ(DivideCeilI64(Limit<i64>::s_Max, 2), Limit<i64>::s_Max / 2 + 1);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(0x1p-150, false)), 0u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(0x1p-150, true)), 1u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(-0x1p-150, false)), 0x80000001u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(-0x1p-150, true)), 0x80000000u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(-0.0, false)), 0x80000000u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(1.0 + 0x1p-24, false)), 0x3f800000u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(1.0 + 0x1p-24, true)), 0x3f800001u);
    EXPECT_EQ(ClassifyIntegerGrammar("-"), s_NumericGrammarIncomplete);
    EXPECT_EQ(ClassifyIntegerGrammar("--1"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyIntegerGrammar(" 1"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyFloatGrammar("."), s_NumericGrammarIncomplete);
    EXPECT_EQ(ClassifyFloatGrammar("1e+"), s_NumericGrammarIncomplete);
    EXPECT_EQ(ClassifyFloatGrammar("1e+x"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyFloatGrammar(".e"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyFloatGrammar("1."), s_NumericGrammarComplete);
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
    struct TestInterval{ u64 first; u64 last; u32 index; };
    const Array<TestInterval, 3u> unsortedIntervals{ TestInterval{ 5u, 10u, 0u }, TestInterval{ 0u, 5u, 1u }, TestInterval{ 2u, 3u, 2u } };
    const auto eligible = [](const TestInterval&)noexcept{ return true; };
    const auto intervalBegin = [](const TestInterval& interval)noexcept{ return interval.first; };
    const auto intervalEnd = [](const TestInterval& interval)noexcept{ return interval.last; };
    static_assert(noexcept(CoversHalfOpenRange(0u, 0u, Span<const TestInterval>{}, eligible, intervalBegin, intervalEnd)));
    EXPECT_TRUE(CoversHalfOpenRange(0u, 0u, Span<const TestInterval>{}, eligible, intervalBegin, intervalEnd));
    const auto throwingEligible = [](const TestInterval&) -> b8{ throw RuntimeException("interval predicate failure"); };
    static_assert(!noexcept(CoversHalfOpenRange(0u, 1u, Span<const TestInterval>{}, throwingEligible, intervalBegin, intervalEnd)));
    EXPECT_THROW(static_cast<void>(CoversHalfOpenRange(0u, 10u, Span<const TestInterval>(unsortedIntervals), throwingEligible, intervalBegin, intervalEnd)), RuntimeException);
    EXPECT_TRUE(CoversHalfOpenRange(0u, 10u, Span<const TestInterval>(unsortedIntervals), eligible, intervalBegin, intervalEnd));
    EXPECT_FALSE(CoversHalfOpenRange(0u, 11u, Span<const TestInterval>(unsortedIntervals), eligible, intervalBegin, intervalEnd));
    const Array<TestInterval, 5u> sortedIntervals{ TestInterval{ 0u, 10u, 0u }, TestInterval{ 1u, 2u, 1u }, TestInterval{ 3u, 4u, 2u }, TestInterval{ 10u, 11u, 3u }, TestInterval{ 10u, 11u, 4u } };
    Array<u8, 5u> overlapFlags{};
    const auto noVisit = [](const TestInterval&)noexcept{};
    static_assert(noexcept(VisitOverlappingSortedIntervals(Span<const TestInterval>{}, intervalBegin, intervalEnd, noVisit)));
    const auto throwingVisit = [](const TestInterval&){ throw RuntimeException("interval visitor failure"); };
    static_assert(!noexcept(VisitOverlappingSortedIntervals(Span<const TestInterval>{}, intervalBegin, intervalEnd, throwingVisit)));
    EXPECT_THROW(VisitOverlappingSortedIntervals(Span<const TestInterval>(sortedIntervals), intervalBegin, intervalEnd, throwingVisit), RuntimeException);
    VisitOverlappingSortedIntervals(Span<const TestInterval>(sortedIntervals), intervalBegin, intervalEnd, [&overlapFlags](const TestInterval& interval)noexcept{ overlapFlags[interval.index] = 1u; });
    for(const u8 overlap : overlapFlags)
        EXPECT_EQ(overlap, 1u);
    Vector<u32, NWB::Core::Alloc::ScratchArena> cachedValues(scratchArena);
    cachedValues.push_back(1u); cachedValues.push_back(2u); cachedValues.push_back(4u);
    const Array<u32, 3u> requestedValues{ 2u, 3u, 3u };
    const auto sameValue = [](const u32 cached, const u32 requested)noexcept{ return cached == requested; };
    EXPECT_EQ(CountMissingCacheEntries(cachedValues, requestedValues, sameValue), 2u);
    const auto throwingKey = [](const u32, const u32) -> b8 { throw RuntimeException("predicate failure"); };
    EXPECT_THROW(static_cast<void>(CountMissingCacheEntries(cachedValues, requestedValues, throwingKey)), RuntimeException);
    EXPECT_THROW(RetainReferencedCacheEntries(requestedValues, throwingKey, cachedValues), RuntimeException);
    RetainReferencedCacheEntries(requestedValues, sameValue, cachedValues);
    ASSERT_EQ(cachedValues.size(), 1u);
    EXPECT_EQ(cachedValues[0u], 2u);
    Vector<u32, NWB::Core::Alloc::ScratchArena> rollbackValues(scratchArena);
    rollbackValues.push_back(7u);
    u32 underflows = 0u;
    {
        AppendedContainerRollbackScope rollback(rollbackValues, [&underflows]()noexcept{ ++underflows; });
        rollbackValues.push_back(8u);
    }
    EXPECT_EQ(rollbackValues.size(), 1u);
    EXPECT_EQ(rollbackValues[0u], 7u);
    {
        AppendedContainerRollbackScope rollback(rollbackValues, [&underflows]()noexcept{ ++underflows; });
        rollbackValues.pop_back();
    }
    EXPECT_EQ(underflows, 1u);
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
    AString<NWB::Core::Alloc::ScratchArena> u64Text(scratchArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> u64Bytes(scratchArena);
    AppendUnsignedU64Text(u64Text, Limit<u64>::s_Max);
    AppendUnsignedU64Bytes(u64Bytes, Limit<u64>::s_Max);
    EXPECT_EQ(AStringView(u64Text), AStringView("18446744073709551615"));
    ASSERT_EQ(u64Bytes.size(), u64Text.size());
    EXPECT_TRUE(BytesMatch(Span<const u8>(u64Bytes.data(), u64Bytes.size()), Span<const u8>(reinterpret_cast<const u8*>(u64Text.data()), u64Text.size())));
    Array<u32, 130u> indexFlags{};
    indexFlags.fill(1u);
    indexFlags[0u] = 0u;
    indexFlags[64u] = 0u;
    indexFlags[129u] = 0u;
    HierarchicalIndexSet<NWB::Core::Alloc::ScratchArena> ready(scratchArena);
    ready.initializeZeros(Span<const u32>(indexFlags));
    EXPECT_EQ(ready.takeLowest(), 0u);
    EXPECT_EQ(ready.takeLowest(), 64u);
    EXPECT_EQ(ready.takeLowest(), 129u);
    EXPECT_EQ(ready.takeLowest(), 130u);
    ready.add(64u);
    ready.add(64u);
    EXPECT_EQ(ready.takeLowest(), 64u);
    EXPECT_EQ(ready.takeLowest(), 130u);

    Vector<u64, NWB::Core::Alloc::ScratchArena> denseMatrix(130u, 0u, scratchArena);
    denseMatrix[1u] = 1u;
    denseMatrix[2u] = 1u;
    SymmetrizeDenseBitMatrix(denseMatrix, 65u, 2u);
    EXPECT_EQ(denseMatrix[128u] & 1u, 1u);
    EXPECT_EQ(denseMatrix[0u] & 2u, 2u);

    struct WordRange{ usize begin; usize end; };
    Array<WordRange, 65u> ranges{};
    Array<usize, 65u> offsets{};
    usize wordCount = 0u;
    for(usize i = 0u; i < ranges.size(); ++i){
        ranges[i] = WordRange{ 0u, i == 0u || i == 1u || i == 64u ? 2u : 1u };
        offsets[i] = wordCount;
        wordCount += ranges[i].end;
    }
    Vector<u64, NWB::Core::Alloc::ScratchArena> compactMatrix(wordCount, 0u, scratchArena);
    compactMatrix[offsets[0u] + 1u] = 1u;
    compactMatrix[offsets[1u]] = 1u;
    compactMatrix[offsets[64u]] = 2u;
    SymmetrizeCompactBitMatrix(compactMatrix, offsets, ranges, 65u, 2u);
    EXPECT_EQ(compactMatrix[offsets[64u]] & 1u, 1u);
    EXPECT_EQ(compactMatrix[offsets[0u]] & 2u, 2u);
    EXPECT_EQ(compactMatrix[offsets[1u] + 1u] & 1u, 1u);

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

    Vector<IntegerRectBounds, NWB::Core::Alloc::ScratchArena> fragments(scratchArena);
    AppendIntegerRectRemainder(IntegerRectBounds{ 0u, 4u, 0u, 4u }, IntegerRectBounds{ 1u, 3u, 1u, 3u }, fragments);
    ASSERT_EQ(fragments.size(), 4u);
    EXPECT_EQ(fragments[0u].xEnd, 1u);
    EXPECT_EQ(fragments[1u].xBegin, 3u);
    EXPECT_EQ(fragments[2u].yEnd, 1u);
    EXPECT_EQ(fragments[3u].yBegin, 3u);

    struct CharSink{
        Array<char, Sha1Digest::s_HexTextLength> data{};
        usize size = 0u;
        CharSink& operator+=(const char ch)noexcept{ data[size++] = ch; return *this; }
    };
    CharSink sink;
    AppendHexByte(0xAFu, sink);
    EXPECT_EQ(sink.data[0u], 'a');
    EXPECT_EQ(sink.data[1u], 'f');
    sink.size = 0u;
    AppendSha1Hex(Sha1Digest{}, sink);
    EXPECT_EQ(sink.size, Sha1Digest::s_HexTextLength);
    EXPECT_EQ(sink.data[0u], '0');
    EXPECT_EQ(sink.data[sink.size - 1u], '0');

    AString<NWB::Core::Alloc::ScratchArena> csv(scratchArena);
    AppendTextDiscardCrAndExpandLf(csv, "a\rb\n");
    EXPECT_EQ(AStringView(csv), "ab\r\n");
    csv.clear();
    AppendCamelToSnakeCase(csv, "HTTPServerRGB8Value");
    EXPECT_EQ(AStringView(csv), "http_server_rgb8_value");
    csv.clear();
    AppendCsvCell("a,\"b\"\r\nc", csv);
    EXPECT_EQ(AStringView(csv), "\"a,\"\"b\"\"\r\nc\"");

    Array<u32, 4> parents{ 0u, 1u, 2u, 3u };
    Array<u8, 4> ranks{ 0u, 0u, 0u, 0u };
    JoinDisjointSets(0u, 1u, parents, ranks);
    JoinDisjointSets(2u, 3u, parents, ranks);
    JoinDisjointSets(0u, 2u, parents, ranks);
    EXPECT_EQ(FindDisjointRoot(3u, parents), 0u);
    EXPECT_EQ(parents[3u], 0u);
    EXPECT_EQ(ranks[0u], 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

