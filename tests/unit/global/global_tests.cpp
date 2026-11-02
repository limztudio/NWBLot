// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/auto_registration.h>
#include <global/basic_string.h>
#include <global/binary.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/fixed_buffer.h>
#include <global/limit.h>
#include <global/not_null.h>
#include <global/overflow.h>
#include <global/process_memory_map.h>
#include <global/termination.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_Alpha = "alpha";
static constexpr AStringView s_Beta = "beta";
static constexpr AStringView s_Unit = "unit";
static constexpr AStringView s_Value42 = "value 42";
static constexpr AStringView s_ValueFormatTrailing = "value {}.trailing";
static constexpr WStringView s_ValueFormatTrailingWide = L"value {}.trailing";


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

TEST(Global, OwnershipInvariantTerminationIsAlwaysActive){
    EXPECT_DEATH(TerminateInvariant(), "");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

