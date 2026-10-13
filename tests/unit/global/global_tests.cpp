// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/alloc/persistent.h>
#include <core/alloc/scratch.h>
#include <core/common/name_symbols.h>

#include <global/algorithm.h>
#include <global/auto_registration.h>
#include <global/arena_memory.h>
#include <global/basic_string.h>
#include <global/binary.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/diagnostics.h>
#include <global/environment.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/fixed_buffer.h>
#include <global/hash_utils.h>
#include <global/limit.h>
#include <global/simd/type.h>
#include <global/simd/vector.h>
#include <global/simd/collision_triangle.h>
#include <global/overflow.h>
#include <global/process_execution.h>
#include <global/process_memory_map.h>
#include <global/termination.h>
#include <global/shared_library.h>
#include <global/text_utils.h>

#if defined(NWB_PLATFORM_LINUX)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ALPHA = "alpha";
static constexpr AStringView s_BETA = "beta";
static constexpr AStringView s_UNIT = "unit";
static constexpr TStringView s_PARALLEL_LOGGER_MESSAGE = NWB_TEXT("parallel logger message");
static constexpr AStringView s_VALUE_42 = "value 42";
static constexpr AStringView s_VALUE_FORMAT_TRAILING = "value {}.trailing";
static constexpr WStringView s_VALUE_FORMAT_TRAILING_WIDE = L"value {}.trailing";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = NWB::Tests::CapturingLogger;
using AString = NWB::Tests::TestAString;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


static u32 s_DiagnosticEventCaptureCount = 0u;
static AStringView s_DiagnosticEventName = {};
static AStringView s_DiagnosticEventCategory = {};
static AStringView s_DiagnosticEventExpression = {};
static AStringView s_DiagnosticEventMessage = {};
static AStringView s_DiagnosticEventFile = {};
inline constexpr usize s_DiagnosticEventCaptureTextBytes = 2048u;
static char s_DiagnosticEventNameText[s_DiagnosticEventCaptureTextBytes] = {};
static char s_DiagnosticEventCategoryText[s_DiagnosticEventCaptureTextBytes] = {};
static char s_DiagnosticEventExpressionText[s_DiagnosticEventCaptureTextBytes] = {};
static char s_DiagnosticEventMessageText[s_DiagnosticEventCaptureTextBytes] = {};
static char s_DiagnosticEventFileText[s_DiagnosticEventCaptureTextBytes] = {};

inline constexpr usize s_VerifyAlignedReallocationAlignment = 256u;
inline constexpr usize s_VerifyAlignedReallocationInitialBytes = s_VerifyAlignedReallocationAlignment;
inline constexpr usize s_VerifyAlignedReallocationBlockerBytes = s_VerifyAlignedReallocationAlignment;
inline constexpr usize s_VerifyAlignedReallocationGrownBytes = s_VerifyAlignedReallocationAlignment * 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arena>
static void VerifyAlignedReallocation(Arena& arena){
    constexpr usize s_Alignment = s_VerifyAlignedReallocationAlignment;
    constexpr usize s_InitialBytes = s_VerifyAlignedReallocationInitialBytes;
    constexpr usize s_BlockerBytes = s_VerifyAlignedReallocationBlockerBytes;
    constexpr usize s_GrownBytes = s_VerifyAlignedReallocationGrownBytes;

    EXPECT_EQ(arena.reallocate(nullptr, s_Alignment, 0u), nullptr);
    auto* initial = static_cast<u8*>(arena.reallocate(nullptr, s_Alignment, s_InitialBytes));
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(initial) % s_Alignment, 0u);
    for(usize i = 0u; i < s_InitialBytes; ++i)
        initial[i] = static_cast<u8>(i);

    void* blocker = arena.allocate(1u, s_BlockerBytes);
    ASSERT_NE(blocker, nullptr);

    auto* resized = static_cast<u8*>(arena.reallocate(initial, s_Alignment, s_GrownBytes));
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(reinterpret_cast<usize>(resized) % s_Alignment, 0u);
    for(usize i = 0u; i < s_InitialBytes; ++i)
        EXPECT_EQ(resized[i], static_cast<u8>(i));

    arena.deallocate(blocker, 1u, s_BlockerBytes);
    EXPECT_EQ(arena.reallocate(resized, s_Alignment, 0u), nullptr);
}

static AStringView CopyDiagnosticEventText(char (&outText)[s_DiagnosticEventCaptureTextBytes], const AStringView text)noexcept{
    usize copied = 0u;
    while(copied < text.size() && copied + 1u < s_DiagnosticEventCaptureTextBytes){
        outText[copied] = text[copied];
        ++copied;
    }
    outText[copied] = 0;
    return AStringView(outText, copied);
}

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
    s_DiagnosticEventName = CopyDiagnosticEventText(s_DiagnosticEventNameText, record.event);
    s_DiagnosticEventCategory = CopyDiagnosticEventText(s_DiagnosticEventCategoryText, record.category);
    s_DiagnosticEventExpression = CopyDiagnosticEventText(s_DiagnosticEventExpressionText, record.expression);
    s_DiagnosticEventMessage = CopyDiagnosticEventText(s_DiagnosticEventMessageText, record.message);
    s_DiagnosticEventFile = CopyDiagnosticEventText(s_DiagnosticEventFileText, record.file);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct U32VectorView{
    using value_type = u32;

    const u32* values = nullptr;
    usize valueCount = 0u;

    [[nodiscard]] bool empty()const noexcept{ return valueCount == 0u; }
    [[nodiscard]] usize size()const noexcept{ return valueCount; }
    [[nodiscard]] const u32* data()const noexcept{ return values; }
    [[nodiscard]] const u32* begin()const noexcept{ return values; }
    [[nodiscard]] const u32* end()const noexcept{ return values + valueCount; }
    [[nodiscard]] u32 operator[](const usize index)const noexcept{ return values[index]; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, ExhaustedPodReadsPreserveCursor){
    Vector<u8> binary;
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
    EXPECT_FALSE(::TryMultiply<usize>(Limit<usize>::s_Max, s_ExpectedDualCount));
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
    EXPECT_FALSE(ParseI64FromChars({}));
    EXPECT_FALSE(ParseU64FromChars({}));
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
    constexpr char s_Text[] = { 'a', 'b', 'c', 'd', 'e', 'f' };
    char text[5u] = {};
    CopyFixedBuffer(text, AStringView(s_Text, 3u));
    EXPECT_EQ(AStringView(text), AStringView("abc"));
    AppendFixedBuffer(text, AStringView(s_Text + 3u, 2u));
    EXPECT_EQ(AStringView(text), AStringView("abcd"));
    CopyFixedBuffer(text, AStringView(s_Text, sizeof(s_Text)));
    EXPECT_EQ(AStringView(text), AStringView("abcd"));
    CopyFixedBuffer(text, {});
    EXPECT_TRUE(AStringView(text).empty());
}


TEST(Global, AutoRegistrationQueueDeduplicatesAndSnapshots){
    ::AutoRegistrationQueue<u32, NWB::Core::Alloc::GlobalArena> queue(NWB::Tests::s_TestArena);
    const auto equal = [](const u32 lhs, const u32 rhs)noexcept{ return lhs == rhs; };

    queue.appendUnique(7u, equal);
    queue.appendUnique(7u, equal);
    queue.appendUnique(19u, equal);

    Vector<u32> values;
    queue.copyTo(values);
    ASSERT_EQ(values.size(), s_ExpectedDualCount);
    EXPECT_EQ(values[0u], 7u);
    EXPECT_EQ(values[1u], 19u);

    queue.appendUnique(23u, equal);
    EXPECT_EQ(values.size(), s_ExpectedDualCount);

    queue.copyTo(values);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[s_ThirdElementIndex], 23u);
}

TEST(Global, Vector3TryNormalizeRejectsInvalidValues){
    EXPECT_FALSE(Vector3TryNormalize(VectorZero()));
    EXPECT_FALSE(Vector3TryNormalize(s_SIMDQNaN));
    EXPECT_FALSE(Vector3TryNormalize(s_SIMDInfinity));
}

TEST(Global, MemoryMapLookupUsesHalfOpenRangesAndRejectsMissingAddresses){
    constexpr AStringView s_Maps = "00001000-00002000 r-xp 00000020 00:00 0 /tmp/first.so\n" "00003000-00004000 r--p 00000000 00:00 0 /tmp/second.so\n";
    Vector<LinuxProcessMemoryMapEntry> entries;
    ParseLinuxProcessMemoryMaps(s_Maps, entries);
    ASSERT_EQ(entries.size(), s_ExpectedDualCount);
    const auto first = FindLinuxProcessMemoryMapForAddress(entries, 0x1000u);
    const auto last = FindLinuxProcessMemoryMapForAddress(entries, 0x3fffu);
    ASSERT_TRUE(first);
    ASSERT_TRUE(last);
    EXPECT_EQ(first->path, AStringView("/tmp/first.so"));
    EXPECT_EQ(last->path, AStringView("/tmp/second.so"));
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x2000u));
}

TEST(Global, GrowingCapacitySaturatesAtSizeLimit){
    EXPECT_EQ(::NextGrowingCapacity((Limit<usize>::s_Max / s_ExpectedDualCount) + 1u, Limit<usize>::s_Max), Limit<usize>::s_Max);
}

TEST(Global, CheckedDivideUpRejectsZeroDivisor){
    EXPECT_FALSE(::DivideUpChecked(17u, 0u));
}

TEST(Global, TriangleAreaRejectsExactThreshold){
    const Float3U a(1.0f, 1.0f, 1.0f);
    const Float3U b(4.0f, 1.0f, 1.0f);
    const Float3U c(1.0f, 5.0f, 1.0f);
    EXPECT_TRUE(::TriangleHasArea(LoadFloat(a), LoadFloat(b), LoadFloat(c), 143.0));
    EXPECT_FALSE(::TriangleHasArea(LoadFloat(a), LoadFloat(b), LoadFloat(c), 144.0));
}

// Binary32 subtraction collapses this nondegenerate triangle to a line; promotion must happen before subtracting its origin.
TEST(Global, TriangleAreaPromotesBeforeCancellingLargeEdges){
    const SIMDVectorSingle a = VectorSet(33554432.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVectorSingle b = VectorSet(1.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVectorSingle c = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(::TriangleHasArea(a, b, c, 0.5));
    EXPECT_FALSE(::TriangleHasArea(a, b, c, 1.0));
}

// Inactive W coordinates must not participate in promoted cross products or area reductions.
TEST(Global, TriangleAreaIgnoresNonfiniteInactiveCoordinates){
    const SIMDVectorSingle a = VectorSetW(VectorZero(), Limit<f32>::s_Infinity);
    const SIMDVectorSingle b = VectorSetW(VectorSet(3.0f, 0.0f, 0.0f, 0.0f), VectorGetW(s_SIMDQNaN));
    const SIMDVectorSingle c = VectorSetW(VectorSet(0.0f, 4.0f, 0.0f, 0.0f), -Limit<f32>::s_Infinity);
    const SIMDVectorDouble normal = ::BuildTriangleAreaNormal64(a, b, c);
    EXPECT_EQ(BitCast<u64>(VectorDoubleGetW(normal)), 0u);
    EXPECT_TRUE(::TriangleHasArea(a, b, c, 143.0));
    EXPECT_FALSE(::TriangleHasArea(a, b, c, 144.0));
}

// The small squared components round away separately but survive their sum; retain the existing backend's strict area threshold.
TEST(Global, TriangleAreaPreservesSquaredSumOrderAtThreshold){
    const SIMDVectorSingle a = VectorZero();
    const SIMDVectorSingle b = VectorSet(1.0f, 0.0f, -1.0f, 0.0f);
    const SIMDVectorSingle c = VectorSet(0.0f, 1.25f, -134217728.0f, 0.0f);
    constexpr f64 s_LargeSquaredComponent = 18014398509481984.0;
#if defined(NWB_HAS_AVX2)
    EXPECT_TRUE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent));
    EXPECT_FALSE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent + 4.0));
#else
    EXPECT_FALSE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent));
    EXPECT_TRUE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent - 2.0));
#endif
}

TEST(Global, GlobalArenaReallocationPreservesAlignment){
    NWB::Core::Alloc::GlobalArena arena(NWB::Tests::s_TestArena);
    VerifyAlignedReallocation(arena);
}

TEST(Global, PersistentArenaReallocationPreservesAlignment){
    NWB::Core::Alloc::PersistentArena arena(NWB::Tests::s_TestArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(16u * 1024u));
    VerifyAlignedReallocation(arena);
}

TEST(Global, Utf8DecoderRespectsBoundedNonterminatedInput){
    const char bytes[]{ static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98), static_cast<char>(0x80) };
    const AStringView text(bytes, LengthOf(bytes));
    const auto decoded = DecodeUtf8CodePoint(text);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->byteCount, 4);
    EXPECT_EQ(decoded->codePoint, 0x1F600u);
    for(usize size = 0u; size < text.size(); ++size)
        EXPECT_FALSE(DecodeUtf8CodePoint(text.substr(0u, size)));
    const char overlong[]{ static_cast<char>(0xC0), static_cast<char>(0x80) };
    EXPECT_FALSE(DecodeUtf8CodePoint(AStringView(overlong, LengthOf(overlong))));
}

TEST(Global, DiagnosticFormatSupportsBoundedFormatViewsAndNullableAdapters){
    constexpr AStringView s_Format = s_VALUE_FORMAT_TRAILING;
    const DiagnosticEventText narrow = MakeDiagnosticEventText(s_Format.substr(0u, 8u), 42u);
    EXPECT_EQ(narrow.view(), s_VALUE_42);
    constexpr WStringView s_WideFormat = s_VALUE_FORMAT_TRAILING_WIDE;
    const DiagnosticEventText wide = MakeDiagnosticEventText(s_WideFormat.substr(0u, 8u), 42u);
    EXPECT_EQ(wide.view(), s_VALUE_42);
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
    Vector<u8> truncated;
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));
    usize cursor = 0u;
    EXPECT_FALSE(ReadString(truncated, cursor));
    EXPECT_EQ(cursor, 0u);
}

TEST(Global, RejectedACompactStringAssignResetsText){
    ACompactString compact("seed");
    const char textWithNull[] = { 'a', '\0', 'b' };

    EXPECT_FALSE(compact.assign(AStringView(textWithNull, sizeof(textWithNull))));
    EXPECT_TRUE(compact.empty());
    EXPECT_TRUE(compact.view().empty());
    EXPECT_EQ(compact.c_str()[0], '\0');
}

TEST(Global, OversizedWideCompactStringAssignmentClearsPreviousValue){
    wchar oversized[WCompactString::s_MaxLength + s_ExpectedDualCount] = {};
    for(usize i = 0u; i < WCompactString::s_MaxLength + 1u; ++i)
        oversized[i] = L'a';

    WCompactString rejected(L"unchanged");
    EXPECT_FALSE(rejected.assign(WStringView(oversized, WCompactString::s_MaxLength + 1u)));
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
    EXPECT_FALSE(StartsWith(AStringView("al"), s_ALPHA));

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
    Vector<u8> stringTable;
    const auto alphaOffset = AppendStringTableText(stringTable, s_ALPHA);
    const auto betaOffset = AppendStringTableText(stringTable, s_BETA);
    ASSERT_TRUE(alphaOffset);
    ASSERT_TRUE(betaOffset);

    Vector<u8> prefixedBinary;
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    const auto parsed = ReadStringTableText(prefixedBinary, 1u, stringTable.size(), *betaOffset);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->view(), s_BETA);

    const usize previousSize = stringTable.size();
    EXPECT_FALSE(AppendStringTableText(stringTable, AStringView()));
    EXPECT_EQ(stringTable.size(), previousSize);
}

TEST(Global, InvalidStringTableReads){
    Vector<u8> unterminated;
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));
    EXPECT_FALSE(ReadStringTableText(unterminated, 0u, unterminated.size(), 0u));

    Vector<u8> emptyText;
    emptyText.push_back(0u);
    EXPECT_FALSE(ReadStringTableText(emptyText, 0u, emptyText.size(), 0u));
}

TEST(Global, EmptyBinaryVectorReadClearsReusedOutputWithoutAdvancing){
    Vector<u8> binary;
    Vector<u16> source;
    source.push_back(1u);
    source.push_back(s_ExpectedDualCount);
    source.push_back(static_cast<u16>(0xBEEFu));

    ASSERT_TRUE(AppendBinaryVectorPayload(binary, source));

    usize cursor = 0u;
    Vector<u16> parsed;
    ASSERT_TRUE(ReadBinaryVectorPayload(binary, cursor, static_cast<u64>(source.size()), parsed));

    cursor = 0u;
    parsed.push_back(7u);
    ASSERT_TRUE(ReadBinaryVectorPayload(binary, cursor, 0u, parsed));
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, FixedVectorOverflowDoesNotAdvanceBinaryCursor){
    Vector<u8> vectorBinary;
    const u16 values[] = { 4u, 5u, 6u };
    for(const u16 value : values)
        AppendPOD(vectorBinary, value);

    usize vectorCursor = 0u;
    FixedVector<u16, s_ExpectedDualCount> tooSmall;
    EXPECT_EQ(ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, tooSmall), MakeUnexpected(BinaryVectorPayloadFailure::OutputOverflow));
    EXPECT_EQ(vectorCursor, 0u);
    EXPECT_TRUE(tooSmall.empty());
}

TEST(Global, RejectedBinaryVectorPayloadReadsDoNotAdvanceCursor){
    Vector<u8> truncated;
    const u32 source = 0x12345678u;
    AppendPOD(truncated, source);

    usize cursor = 0u;
    Vector<u32> parsed;
    parsed.push_back(0xAABBCCDDu);
    EXPECT_EQ(ReadBinaryVectorPayload(truncated, cursor, s_ExpectedDualCount, parsed), MakeUnexpected(BinaryVectorPayloadFailure::SourceTruncated));
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, AppendTriviallyCopyableVectorSelfAppend){
    Vector<u32> values;
    values.push_back(1u);
    values.push_back(s_ExpectedDualCount);
    values.push_back(3u);

    AppendTriviallyCopyableVector(values, values);

    EXPECT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0u], 1u);
    EXPECT_EQ(values[1u], s_ExpectedDualCount);
    EXPECT_EQ(values[s_ThirdElementIndex], 3u);
    EXPECT_EQ(values[3u], 1u);
    EXPECT_EQ(values[4u], s_ExpectedDualCount);
    EXPECT_EQ(values[5u], 3u);
}

TEST(Global, TriviallyCopyableVectorPreservesOverlappingSourceRanges){
    Vector<u32> values;
    values.push_back(1u);
    values.push_back(s_ExpectedDualCount);
    values.push_back(3u);
    values.push_back(4u);

    const U32VectorView middle{ values.data() + 1u, s_ExpectedDualCount };
    AppendTriviallyCopyableVector(values, middle);

    EXPECT_EQ(values.size(), 6u);
    EXPECT_EQ(values[0u], 1u);
    EXPECT_EQ(values[1u], s_ExpectedDualCount);
    EXPECT_EQ(values[s_ThirdElementIndex], 3u);
    EXPECT_EQ(values[3u], 4u);
    EXPECT_EQ(values[4u], s_ExpectedDualCount);
    EXPECT_EQ(values[5u], 3u);

    const U32VectorView assignedMiddle{ values.data() + 1u, 3u };
    AssignTriviallyCopyableVector(values, assignedMiddle);

    EXPECT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0u], s_ExpectedDualCount);
    EXPECT_EQ(values[1u], 3u);
    EXPECT_EQ(values[s_ThirdElementIndex], 4u);
}


#if !defined(_MSC_VER)
TEST(Global, BoundedRuntimeWrappersTerminateTruncatedText){
    char truncatedText[4] = {};
    EXPECT_NE(NWB_STRCPY(truncatedText, sizeof(truncatedText), "abcdef"), 0);
    EXPECT_STREQ(truncatedText, "abc");

    char nullTerminatedText[4] = { 'a', 'b', 'c', 'd' };
    EXPECT_NE(NWB_STRCAT(nullTerminatedText, sizeof(nullTerminatedText), "e"), 0);
    EXPECT_EQ(nullTerminatedText[sizeof(nullTerminatedText) - 1u], '\0');
}
#endif

TEST(Global, LoggerMacrosBehaveAsSingleStatements){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard guard(logger);

    bool elseBranchRan = false;
    if(false)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("unreachable"));
    else
        elseBranchRan = true;

    EXPECT_TRUE(elseBranchRan);
    EXPECT_EQ(logger.messageCount(), 0u);
}

TEST(Global, CapturingLoggerSerializesConcurrentWritersAndReaders){
    constexpr u32 s_ThreadCount = 4u;
    constexpr u32 s_MessagesPerThread = 512u;
    constexpr u32 s_ExpectedMessageCount = s_ThreadCount * s_MessagesPerThread;
    constexpr u32 s_ExpectedErrorCount = s_ExpectedMessageCount / s_ExpectedDualCount;
    CapturingLogger logger;
    Latch startGate(s_ThreadCount + s_ExpectedDualCount);
    Atomic<u32> activeWriters{ s_ThreadCount };
    Atomic<bool> invalidObservation{ false };
    bool sawConcurrentMessage = false;
    bool sawConcurrentError = false;
    Thread reader([&](){
        startGate.arrive_and_wait();
        do{
            const u32 messageCount = logger.messageCount();
            const u32 errorCount = logger.errorCount();
            const NWB::Core::Common::LogType::Enum lastType = logger.lastType();
            if(messageCount > s_ExpectedMessageCount || errorCount > s_ExpectedErrorCount || (lastType != NWB::Core::Common::LogType::Info && lastType != NWB::Core::Common::LogType::Error))
                invalidObservation.store(true, MemoryOrder::relaxed);
            if(logger.sawMessageContaining(s_PARALLEL_LOGGER_MESSAGE))
                sawConcurrentMessage = true;
            if(logger.sawErrorContaining(s_PARALLEL_LOGGER_MESSAGE))
                sawConcurrentError = true;
            YieldThread();
        }while(activeWriters.load(MemoryOrder::acquire) != 0u);
    });
    Thread writers[s_ThreadCount];
    for(u32 threadIndex = 0u; threadIndex < s_ThreadCount; ++threadIndex){
        writers[threadIndex] = Thread([&logger, &startGate, &activeWriters](){
            startGate.arrive_and_wait();
            for(u32 messageIndex = 0u; messageIndex < s_MessagesPerThread; ++messageIndex){
                const NWB::Core::Common::LogType::Enum type = (messageIndex & 1u) == 0u ? NWB::Core::Common::LogType::Info : NWB::Core::Common::LogType::Error;
                NWB::Core::Common::LoggerDetail::EnqueueMessage(logger, type, s_PARALLEL_LOGGER_MESSAGE);
            }
            activeWriters.fetch_sub(1u, MemoryOrder::release);
        });
    }

    startGate.arrive_and_wait();
    for(Thread& writer : writers)
        writer.join();
    reader.join();

    EXPECT_FALSE(invalidObservation.load(MemoryOrder::relaxed));
    EXPECT_TRUE(sawConcurrentMessage);
    EXPECT_TRUE(sawConcurrentError);
    EXPECT_EQ(logger.messageCount(), s_ExpectedMessageCount);
    EXPECT_EQ(logger.errorCount(), s_ExpectedErrorCount);
    EXPECT_TRUE(logger.sawMessageContaining(s_PARALLEL_LOGGER_MESSAGE));
    EXPECT_TRUE(logger.sawErrorContaining(s_PARALLEL_LOGGER_MESSAGE));
}

TEST(Global, DiagnosticEventHookRejectsReentryAndHonorsBoundedText){
    ResetDiagnosticEventCapture();

    const DiagnosticEventCallback callback = [](const DiagnosticEventRecord& record)noexcept{
        RecordDiagnosticEvent(record);
        CaptureDiagnosticEvent("recursive", "ignored");
    };

    SetDiagnosticEventCallback(callback);
    CaptureDiagnosticEvent(AStringView("unit.trailing").substr(0u, 4u), AStringView("message.trailing").substr(0u, 7u), "diagnostics_test.cpp", 42u);
    ClearDiagnosticEventCallback(callback);
    CaptureDiagnosticEvent(s_UNIT, "ignored");

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

TEST(Global, OwnershipInvariantTerminationIsAlwaysActive){
    EXPECT_DEATH(TerminateInvariant(), "");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

