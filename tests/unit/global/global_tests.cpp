// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/algorithm.h>
#include <global/allocation_size.h>
#include <global/auto_registration.h>
#include <global/arena_base.h>
#include <global/arena_c_allocator.h>
#include <global/arena_object.h>
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
#include <global/filesystem/volume_naming.h>
#include <global/fixed_buffer.h>
#include <global/hash_utils.h>
#include <global/limit.h>
#include <global/math/type.h>
#include <global/math/vector.h>
#include <global/mesh/triangle_area.h>
#include <global/overflow.h>
#include <global/process_execution.h>
#include <global/process_memory_map.h>
#include <global/termination.h>
#include <global/shared_library.h>
#include <global/text_utils.h>

#include <core/alloc/persistent.h>
#include <core/alloc/scratch.h>
#include <core/common/name_symbols.h>

#if defined(NWB_PLATFORM_LINUX)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ALPHA = "alpha";
static constexpr AStringView s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_LIVE = "tests/namesymbols/before_registry_live";
static constexpr AStringView s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_RETIRE = "tests/namesymbols/before_registry_retired";
static constexpr AStringView s_CORE_ALLOC_HEAP_BACKING = "core/alloc/heap_backing";
static constexpr AStringView s_BETA = "beta";
static constexpr AStringView s_UNCHANGED = "unchanged";
static constexpr AStringView s_UNIT = "unit";
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
static constexpr AStringView s_BIN_SH = "/bin/sh";
static constexpr AStringView s_C = "-c";
#endif
static constexpr TStringView s_PARALLEL_LOGGER_MESSAGE = NWB_TEXT("parallel logger message");
#if defined(NWB_PLATFORM_WINDOWS)
static constexpr AStringView s_TRAILING_SUFFIX = ".trailing";
#endif
#if !defined(NWB_DEBUG)
static constexpr AStringView s_RUNTIME_GENERATED = "runtime/generated";
#endif
static constexpr AStringView s_VALUE_42 = "value 42";
static constexpr AStringView s_VALUE_FORMAT_TRAILING = "value {}.trailing";
static constexpr WStringView s_VALUE_FORMAT_TRAILING_WIDE = L"value {}.trailing";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = NWB::Tests::CapturingLogger;
using AString = NWB::Tests::TestAString;
using NameSymbolTestPath = ::Path<NWB::Core::Alloc::GlobalArena>;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;

struct OversizedBinarySource{
    static constexpr u8 s_Byte = 0u;

    [[nodiscard]] u64 size()const noexcept{ return static_cast<u64>(Limit<StreamSize>::s_Max) + 1u; }
    [[nodiscard]] bool empty()const noexcept{ return false; }
    [[nodiscard]] const u8* data()const noexcept{ return &s_Byte; }
};

struct NameSymbolCallbackProbe{
    NameDetail::NameSymbolRecorderState previousRecorder = NameDetail::SymbolRecorderState();
    NameDetail::NameSymbolResolverState previousResolver = NameDetail::SymbolResolverState();
    u32 recordCount = 0u;
    u32 resolveCount = 0u;

    NameSymbolCallbackProbe(){
        SetNameSymbolRecordCallback([](const NameHash&, AStringView, void* context){
            ++static_cast<NameSymbolCallbackProbe*>(context)->recordCount;
        }, this);
        SetNameSymbolResolveCallback([](const NameHash&, char*, usize, void* context){
            ++static_cast<NameSymbolCallbackProbe*>(context)->resolveCount;
            return false;
        }, this);
    }
    ~NameSymbolCallbackProbe(){
        SetNameSymbolRecordCallback(previousRecorder.callback, previousRecorder.userData);
        SetNameSymbolResolveCallback(previousResolver.callback, previousResolver.userData);
    }
};


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


TEST(Global, ExhaustedPodReadsPreserveCursorAndOutput){
    Vector<u8> binary;
    const u32 writtenValue = 0x11223344u;
    AppendPOD(binary, writtenValue);

    usize cursor = 0u;
    u32 readValue = 0u;
    EXPECT_TRUE(ReadPOD(binary, cursor, readValue));
    EXPECT_EQ(readValue, writtenValue);
    EXPECT_EQ(cursor, sizeof(writtenValue));

    const usize failedCursor = cursor;
    u32 unchangedValue = 0xAABBCCDDu;
    EXPECT_FALSE(ReadPOD(binary, cursor, unchangedValue));
    EXPECT_EQ(cursor, failedCursor);
    EXPECT_EQ(unchangedValue, 0xAABBCCDDu);
}

TEST(Global, MultiplicationOverflowClearsResult){
    usize product = Limit<usize>::s_Max;
    EXPECT_FALSE(::TryMultiply<usize>(Limit<usize>::s_Max, s_ExpectedDualCount, product));
    EXPECT_EQ(product, 0u);
}

TEST(Global, NumericParsersRespectViewBoundsAndRejectEmptyInput){
    i64 signedValue = 0;
    EXPECT_TRUE(ParseI64(AStringView("-42trailing").substr(0u, 3u), signedValue));
    EXPECT_EQ(signedValue, -42);
    EXPECT_FALSE(ParseI64("42x", signedValue));

    u64 unsignedValue = 0u;
    EXPECT_TRUE(ParseU64(AStringView("42trailing").substr(0u, 2u), unsignedValue));
    EXPECT_EQ(unsignedValue, 42u);
    EXPECT_FALSE(ParseU64("-1", unsignedValue));

    signedValue = 99;
    unsignedValue = 99u;
    EXPECT_FALSE(ParseI64FromChars({}, signedValue));
    EXPECT_FALSE(ParseU64FromChars({}, unsignedValue));
    EXPECT_EQ(signedValue, 99);
    EXPECT_EQ(unsignedValue, 99u);

    f64 doubleValue = 99.0;
    f32 floatValue = 99.0f;
    EXPECT_FALSE(ParseF64FromChars({}, doubleValue));
    EXPECT_FALSE(ParseF32FromChars({}, floatValue));
    EXPECT_EQ(doubleValue, 99.0);
    EXPECT_EQ(floatValue, 99.0f);
    EXPECT_TRUE(ParseF64FromChars(AStringView("1.25trailing").substr(0u, 4u), doubleValue));
    EXPECT_TRUE(ParseF32FromChars(AStringView("1.25trailing").substr(0u, 4u), floatValue));
    EXPECT_EQ(doubleValue, 1.25);
    EXPECT_EQ(floatValue, 1.25f);
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
    const auto equal = [](const u32 lhs, const u32 rhs){ return lhs == rhs; };

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
    SIMDVector normalized = VectorSet(9.0f, 8.0f, 7.0f, 6.0f);
    const SIMDVector unchanged = normalized;


    normalized = unchanged;
    EXPECT_FALSE(Vector3TryNormalize(VectorZero(), normalized));
    EXPECT_TRUE(Vector3Equal(normalized, unchanged));

    EXPECT_FALSE(Vector3TryNormalize(s_SIMDQNaN, normalized));
    EXPECT_TRUE(Vector3Equal(normalized, unchanged));

    EXPECT_FALSE(Vector3TryNormalize(s_SIMDInfinity, normalized));
    EXPECT_TRUE(Vector3Equal(normalized, unchanged));
}

TEST(Global, MemoryMapLookupUsesHalfOpenRangesAndClearsMissingOutput){
    constexpr AStringView s_Maps =
        "00001000-00002000 r-xp 00000020 00:00 0 /tmp/first.so\n"
        "00003000-00004000 r--p 00000000 00:00 0 /tmp/second.so\n"
    ;

    Vector<LinuxProcessMemoryMapEntry> entries;
    ParseLinuxProcessMemoryMaps(s_Maps, entries);
    ASSERT_EQ(entries.size(), s_ExpectedDualCount);

    LinuxProcessMemoryMapEntry entry;
    EXPECT_TRUE(FindLinuxProcessMemoryMapForAddress(entries, 0x1000u, entry));
    EXPECT_EQ(entry.path, AStringView("/tmp/first.so"));
    EXPECT_TRUE(FindLinuxProcessMemoryMapForAddress(entries, 0x3fffu, entry));
    EXPECT_EQ(entry.path, AStringView("/tmp/second.so"));
    EXPECT_FALSE(FindLinuxProcessMemoryMapForAddress(entries, 0x2000u, entry));
    EXPECT_TRUE(entry.path.empty());
}

TEST(Global, GrowingCapacitySaturatesAtSizeLimit){
    EXPECT_EQ(::NextGrowingCapacity((Limit<usize>::s_Max / s_ExpectedDualCount) + 1u, Limit<usize>::s_Max), Limit<usize>::s_Max);
}

TEST(Global, CheckedDivideUpRejectsZeroDivisor){
    u32 result = 99u;
    EXPECT_FALSE(::DivideUpChecked(17u, 0u, result));
}

TEST(Global, TriangleAreaRejectsExactThreshold){
    const Float3U a(1.0f, 1.0f, 1.0f);
    const Float3U b(4.0f, 1.0f, 1.0f);
    const Float3U c(1.0f, 5.0f, 1.0f);
    EXPECT_TRUE(::TriangleHasArea(LoadFloat(a), LoadFloat(b), LoadFloat(c), 143.0));
    EXPECT_FALSE(::TriangleHasArea(LoadFloat(a), LoadFloat(b), LoadFloat(c), 144.0));
}

TEST(Global, GlobalArenaReallocationPreservesAlignment){
    NWB::Core::Alloc::GlobalArena arena(NWB::Tests::s_TestArena);
    VerifyAlignedReallocation(arena);
}

TEST(Global, PersistentArenaReallocationPreservesAlignment){
    NWB::Core::Alloc::PersistentArena arena(
        NWB::Tests::s_TestArena,
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(16u * 1024u)
    );
    VerifyAlignedReallocation(arena);
}

TEST(Global, ConstexprNameViewsPreserveBoundedIdentityAndSymbolCallbacks){
    constexpr AStringView s_Source = "Identity\\Bounded.trailing";
    constexpr AStringView s_Bounded = s_Source.substr(0u, 16u);
    constexpr Name s_ViewName{s_Bounded};
    constexpr Name s_LiteralName{"identity/bounded"};
    constexpr char s_EmbeddedNull[]{ 'a', '\0', 'b' };
    constexpr Name s_InvalidName{AStringView(s_EmbeddedNull, LengthOf(s_EmbeddedNull))};
    static_assert(s_ViewName == s_LiteralName);
    static_assert(s_InvalidName == s_NameNone);
    static_assert(Name{AStringView{}} != s_NameNone);
    static_assert(Name{static_cast<const char*>(nullptr)} == s_NameNone);

    NameSymbolCallbackProbe probe;
    EXPECT_EQ(s_ViewName.identityHash(), s_LiteralName.identityHash());
    EXPECT_EQ(probe.recordCount, 0u);
    const Name runtimeView{s_Bounded};
    EXPECT_EQ(runtimeView, s_LiteralName);
    EXPECT_EQ(probe.recordCount, 1u);
    EXPECT_EQ(probe.resolveCount, 0u);
}

TEST(Global, Utf8DecoderRespectsBoundedNonterminatedInput){
    const char bytes[]{ static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x98), static_cast<char>(0x80) };
    const AStringView text(bytes, LengthOf(bytes));
    u32 unicode = 0u;
    EXPECT_EQ(DecodeUtf8CodePoint(text, unicode), 4);
    EXPECT_EQ(unicode, 0x1F600u);
    for(usize size = 0u; size < text.size(); ++size)
        EXPECT_EQ(DecodeUtf8CodePoint(text.substr(0u, size), unicode), 0);

    const char overlong[]{ static_cast<char>(0xC0), static_cast<char>(0x80) };
    EXPECT_EQ(DecodeUtf8CodePoint(AStringView(overlong, LengthOf(overlong)), unicode), 0);
}

TEST(Global, EnvironmentVariableNamesSupportSlicesAndOutputAliasing){
    NWB::Tests::TestArena<> testArena;
    ::AString<NWB::Core::Alloc::GlobalArena> expected(testArena.arena);
    ASSERT_TRUE(ReadEnvironmentVariable(AStringView("PATH"), expected));
    ::AString<NWB::Core::Alloc::GlobalArena> actual(testArena.arena);
    ASSERT_TRUE(ReadEnvironmentVariable(AStringView("PATH.trailing").substr(0u, 4u), actual));
    EXPECT_EQ(actual, expected);

    actual.assign("PATH");
    ASSERT_TRUE(ReadEnvironmentVariable(AStringView(actual), actual));
    EXPECT_EQ(actual, expected);
    const char invalidName[]{ 'P', 'A', 'T', 'H', '\0', 'X' };
    EXPECT_FALSE(ReadEnvironmentVariable(AStringView(invalidName, LengthOf(invalidName)), actual));
    EXPECT_TRUE(actual.empty());
}

TEST(Global, FileWriteSizeRejectionPreservesExistingOutput){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/oversized_file_write");
    const auto outputPath = root / "output.bin";
    ErrorCode error;
    ASSERT_TRUE(EnsureEmptyDirectory(root, error));
    ASSERT_TRUE(WriteTextFile(outputPath, "retained"));

    EXPECT_FALSE(WriteBinaryFile(outputPath, OversizedBinarySource{}));
    AString retained(testArena.arena);
    ASSERT_TRUE(ReadTextFile(outputPath, retained));
    EXPECT_EQ(retained, "retained");
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(Global, FileWritesRejectBufferedDeviceFailure){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> fullDevice(testArena.arena, "/dev/full");
    const u8 payload[]{ 1u, 2u, 3u };
    EXPECT_FALSE(WriteTextFile(fullDevice, "buffered output"));
    EXPECT_FALSE(WriteBinaryFile(fullDevice, Span<const u8>(payload)));
}
#endif

TEST(Global, ProcessArgumentValidationPreservesExistingOutput){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/invalid_process_arguments");
    const auto outputPath = root / "output.txt";
    ErrorCode error;
    ASSERT_TRUE(EnsureEmptyDirectory(root, error));
    ASSERT_TRUE(WriteTextFile(outputPath, AStringView("retained")));
    const auto outputPathText = PathToString<char>(testArena.arena, outputPath);

    const char invalidArgument[]{ 'a', '\0', 'b' };
    const AStringView arguments[]{ "unused_program", AStringView(invalidArgument, LengthOf(invalidArgument)) };
    EXPECT_EQ(RunProcessRedirectedToFile(testArena.arena, arguments, AStringView(outputPathText)), -1);
    EXPECT_EQ(RunProcessRedirectedToFile(testArena.arena, {}, AStringView(outputPathText)), -1);
    AString output(testArena.arena);
    ASSERT_TRUE(ReadTextFile(outputPath, output));
    EXPECT_EQ(output, "retained");
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

#if defined(NWB_PLATFORM_WINDOWS)
TEST(Global, WindowsProcessInputsSupportBoundedViews){
    NWB::Tests::TestArena<> testArena;
    ::AString<NWB::Core::Alloc::GlobalArena> executableText(testArena.arena);
    ASSERT_TRUE(ReadEnvironmentVariable("ComSpec", executableText));
    const Path<NWB::Core::Alloc::GlobalArena> executablePath(testArena.arena, AStringView(executableText));
    ::AString<NWB::Core::Alloc::GlobalArena> searchPathText = PathToString<char>(testArena.arena, executablePath.parentPath());
    ::AString<NWB::Core::Alloc::GlobalArena> executableNameText = PathToString<char>(testArena.arena, executablePath.filename());
    const usize searchPathLength = searchPathText.size();
    const usize executableNameLength = executableNameText.size();
    searchPathText += s_TRAILING_SUFFIX;
    executableNameText += s_TRAILING_SUFFIX;
    ASSERT_TRUE(ExecutableAvailableInPath(
        testArena.arena,
        AStringView(searchPathText).substr(0u, searchPathLength),
        AStringView(executableNameText).substr(0u, executableNameLength)
    ));

    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/bounded_process");
    const auto outputPath = root / "combined output.txt";
    ErrorCode error;
    ASSERT_TRUE(EnsureEmptyDirectory(root, error));
    ::AString<NWB::Core::Alloc::GlobalArena> outputPathText = PathToString<char>(testArena.arena, outputPath);
    const usize outputPathLength = outputPathText.size();
    const usize executableLength = executableText.size();
    outputPathText += s_TRAILING_SUFFIX;
    executableText += s_TRAILING_SUFFIX;
    const AStringView arguments[]{
        AStringView(executableText).substr(0u, executableLength),
        "/c",
        "echo stdout&echo stderr 1>&2&exit /b 23"
    };
    bool exitCodeQueryFailed = true;
    EXPECT_EQ(RunProcessRedirectedToFile(
        testArena.arena,
        arguments,
        AStringView(outputPathText).substr(0u, outputPathLength),
        &exitCodeQueryFailed
    ), 23);
    EXPECT_FALSE(exitCodeQueryFailed);
    ::AString<NWB::Core::Alloc::GlobalArena> output(testArena.arena);
    ASSERT_TRUE(ReadTextFile(outputPath, output));
    EXPECT_NE(AStringView(output).find("stdout"), AStringView::npos);
    EXPECT_NE(AStringView(output).find("stderr"), AStringView::npos);
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

TEST(Global, SharedLibraryAcceptsBoundedNamesAndResetsFailedSymbols){
    NWB::Tests::TestArena<> testArena;
    SharedLibrary library;
    constexpr TStringView s_LibraryName = NWB_TEXT("kernel32.dll.trailing");
    ASSERT_TRUE(library.open(testArena.arena, s_LibraryName.substr(0u, 12u)));
    using CurrentProcessIdFn = DWORD(WINAPI*)();
    CurrentProcessIdFn currentProcessId = nullptr;
    ASSERT_TRUE(library.resolve(testArena.arena, AStringView("GetCurrentProcessId.trailing").substr(0u, 19u), currentProcessId));
    EXPECT_EQ(currentProcessId(), ::GetCurrentProcessId());

    const char invalidSymbol[]{ 'G', '\0', 'e' };
    EXPECT_FALSE(library.resolve(testArena.arena, AStringView(invalidSymbol, LengthOf(invalidSymbol)), currentProcessId));
    EXPECT_EQ(currentProcessId, nullptr);
}
#endif

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

TEST(Global, BinaryIdentityLanesDoNotRecordNameSymbols){
    constexpr Name s_First{"Identity\\First"};
    NWB::Core::Common::NameSymbols::InstallRuntimeRegistry();
    NWB::Core::Common::NameSymbols::ClearRuntimeSymbols();

    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
        NameHash changed = s_First.identityHash();
        changed.qwords[lane] ^= 1u;
        const Name binary{changed};
        EXPECT_EQ(binary.identityHash(), changed);
        EXPECT_NE(binary.identityHash(), s_First.identityHash());
    }
    EXPECT_EQ(NWB::Core::Common::NameSymbols::EntryCount(), 0u);

#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_EQ(s_First.hash(), ComputeNameHash(s_IDENTITY_FIRST));
    EXPECT_EQ(NWB::Core::Common::NameSymbols::EntryCount(), 1u);
#endif
}

TEST(Global, NameBinaryIdentityNeverInvokesInstalledSymbolCallbacks){
    constexpr Name s_Literal{"identity/callback_probe"};
    const Name binary{ComputeNameHash("identity/binary_probe")};
    NameSymbolCallbackProbe probe;
    EXPECT_EQ(s_Literal.identityHash(), ComputeNameHash("identity/callback_probe"));
    EXPECT_EQ(binary.identityHash(), ComputeNameHash("identity/binary_probe"));
    EXPECT_EQ(s_NameNone.identityHash(), NameHash{});
    EXPECT_EQ(probe.recordCount, 0u);
    EXPECT_EQ(probe.resolveCount, 0u);

    const Name recorded{AStringView("identity/recorded_probe")};
    EXPECT_EQ(probe.recordCount, 1u);
    char resolved[32u] = {};
    EXPECT_FALSE(NameDetail::ResolveNameSymbolText(recorded.identityHash(), resolved, LengthOf(resolved)));
    EXPECT_EQ(probe.recordCount, 1u);
    EXPECT_EQ(probe.resolveCount, 1u);
#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_EQ(s_Literal.hash(), s_Literal.identityHash());
    EXPECT_EQ(probe.recordCount, s_ExpectedDualCount);
#endif
}

TEST(Global, NameResolvedTextPreservesSymbolLookupAndHashFallback){
    NWB::Core::Common::NameSymbols::InstallRuntimeRegistry();
    NWB::Core::Common::NameSymbols::ClearRuntimeSymbols();

    const Name runtimeName{AStringView("Runtime\\Generated")};
    char hashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(runtimeName.identityHash(), hashText, sizeof(hashText));
    const AStringView hashView(hashText, NameDetail::s_DebugHashTextLength);
    const Name binaryName{runtimeName.identityHash()};
    const DiagnosticEventText formattedName = MakeDiagnosticEventText("{}", binaryName);
#if defined(NWB_DEBUG)
    EXPECT_EQ(binaryName.resolvedText(), hashView);
    EXPECT_EQ(formattedName.view(), hashView);
#else
    EXPECT_EQ(binaryName.resolvedText(), s_RUNTIME_GENERATED);
    EXPECT_EQ(formattedName.view(), s_RUNTIME_GENERATED);
    EXPECT_NE(binaryName.resolvedText(), hashView);
    EXPECT_NE(binaryName.resolvedText(), binaryName.logText());
#endif

    NWB::Core::Common::NameSymbols::ClearRuntimeSymbols();
    EXPECT_EQ(binaryName.resolvedText(), hashView);
    EXPECT_STREQ(binaryName.c_str(), hashText);
}

TEST(Global, NameResolvedTextIsBoundedByStoredAndResolverBuffers){
    NameSymbolCallbackProbe probe;
    char source[NameDetail::s_DebugNameCapacity + 1u];
    for(char& character : source)
        character = 'x';
    const Name name{AStringView(source, sizeof(source))};
#if defined(NWB_DEBUG)
    EXPECT_EQ(name.resolvedText().size(), NameDetail::s_DebugNameCapacity - 1u);
#else
    SetNameSymbolResolveCallback([](const NameHash&, char* outText, const usize outTextSize, void*){
        for(usize i = 0u; i < outTextSize; ++i)
            outText[i] = 'x';
        return true;
    });
    const AStringView resolved = name.resolvedText();
    EXPECT_EQ(resolved.size(), NameDetail::s_SymbolTextBufferLength - 1u);
    EXPECT_EQ(resolved.back(), 'x');
    EXPECT_EQ(resolved.data()[resolved.size()], '\0');
#endif
}

TEST(Global, NameHashDebugTextRejectsInvalidHex){
    const NameHash source = ComputeNameHash("global_name_hash_debug_text");
    char hashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(source, hashText, sizeof(hashText));
    hashText[0u] = 'g';
    NameHash decoded = {};
    EXPECT_FALSE(NameDetail::DecodeDebugHashText(AStringView(hashText), decoded));
}

TEST(Global, NameSymbolsCollectArenaOwnersWithoutPerformanceCapture){
    namespace NameSymbols = NWB::Core::Common::NameSymbols;
    constexpr Name s_LiveOwner{"Tests/NameSymbols/Before_Registry_Live"};
    constexpr Name s_RetiredOwner{"Tests/NameSymbols/Before_Registry_Retired"};
    constexpr NameHash s_LiveHash = ComputeNameHash("tests/namesymbols/before_registry_live");
    constexpr NameHash s_RetiredHash = ComputeNameHash("tests/namesymbols/before_registry_retired");
    constexpr NameHash s_HeapHash = ComputeNameHash("core/alloc/heap_backing");
    NameSymbols::UninstallRuntimeRegistry();
    NWB::Core::Alloc::GlobalArena liveOwner(s_LiveOwner);
    {
        NWB::Core::Alloc::GlobalArena retiredOwner(s_RetiredOwner);
    }
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();

    char resolvedText[128] = {};
    EXPECT_FALSE(NameSymbols::Resolve(s_LiveHash, resolvedText, sizeof(resolvedText)));
    EXPECT_FALSE(NameSymbols::Resolve(s_RetiredHash, resolvedText, sizeof(resolvedText)));
    EXPECT_FALSE(NameSymbols::Resolve(s_HeapHash, resolvedText, sizeof(resolvedText)));

    ::AString<NWB::Core::Alloc::GlobalArena> namesymText(liveOwner);
    NameSymbols::Serialize(namesymText);
#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_TRUE(NameSymbols::Resolve(s_LiveHash, resolvedText, sizeof(resolvedText)));
    EXPECT_STREQ(resolvedText, s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_LIVE.data());
    EXPECT_TRUE(NameSymbols::Resolve(s_RetiredHash, resolvedText, sizeof(resolvedText)));
    EXPECT_STREQ(resolvedText, s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_RETIRE.data());
    EXPECT_TRUE(NameSymbols::Resolve(s_HeapHash, resolvedText, sizeof(resolvedText)));
    EXPECT_STREQ(resolvedText, s_CORE_ALLOC_HEAP_BACKING.data());
    EXPECT_NE(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_LIVE.data()), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_RETIRE.data()), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_CORE_ALLOC_HEAP_BACKING.data()), decltype(namesymText)::npos);
#else
    EXPECT_FALSE(NameSymbols::Resolve(s_LiveHash, resolvedText, sizeof(resolvedText)));
    EXPECT_FALSE(NameSymbols::Resolve(s_RetiredHash, resolvedText, sizeof(resolvedText)));
    EXPECT_FALSE(NameSymbols::Resolve(s_HeapHash, resolvedText, sizeof(resolvedText)));
#endif

    NameSymbols::ClearRuntimeSymbols();
    NameSymbolTestPath executableDirectory(liveOwner);
    ASSERT_TRUE(GetExecutableDirectory(executableDirectory));
    NameSymbolTestPath executableName(liveOwner);
    ASSERT_TRUE(GetExecutableName(executableName));
    NameSymbolTestPath namesymPath = executableDirectory / executableName;
    namesymPath.replaceExtension(NWB_TEXT(".namesym"));
    // The application exception path exports after its scoped runtime callbacks have already detached.
    NameSymbols::UninstallRuntimeRegistry();
    ASSERT_TRUE(NameSymbols::WriteDefaultFile());
    NameSymbols::InstallRuntimeRegistry();
    namesymText.clear();
    ASSERT_TRUE(ReadTextFile(namesymPath, namesymText));
#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_NE(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_LIVE.data()), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_RETIRE.data()), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_CORE_ALLOC_HEAP_BACKING.data()), decltype(namesymText)::npos);
#else
    EXPECT_EQ(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_LIVE.data()), decltype(namesymText)::npos);
    EXPECT_EQ(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_RETIRE.data()), decltype(namesymText)::npos);
    EXPECT_EQ(namesymText.find(s_CORE_ALLOC_HEAP_BACKING.data()), decltype(namesymText)::npos);
#endif
    ErrorCode removeError;
    if(!RemoveFile(namesymPath, removeError))
        EXPECT_FALSE(removeError);
}

TEST(Global, RejectedStringReadsDoNotAdvanceCursor){
    Vector<u8> truncated;
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));

    usize cursor = 0u;
    AString parsed(s_UNCHANGED);
    EXPECT_FALSE(ReadString(truncated, cursor, parsed));
    EXPECT_EQ(cursor, 0u);
    EXPECT_EQ(parsed, s_UNCHANGED);

    Vector<u8> embeddedNull;
    const char textWithNull[] = { 'a', '\0', 'b' };
    EXPECT_TRUE(AppendString(embeddedNull, AStringView(textWithNull, sizeof(textWithNull))));

    ACompactString compact(s_UNCHANGED);
    EXPECT_FALSE(ReadString(embeddedNull, cursor, compact));
    EXPECT_EQ(cursor, 0u);
    EXPECT_EQ(compact.view(), AStringView(s_UNCHANGED));
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

TEST(Global, PathNativeComponentsMatchOwningIterationAndRemainValidAfterAdvance){
    NWB::Tests::TestArena<> testArena;
#if defined(NWB_PLATFORM_WINDOWS)
    const Path<NWB::Core::Alloc::GlobalArena> path(testArena.arena, "C:\\Root//./í•œê¸€/File.TXT/");
    constexpr TStringView s_Expected[]{ NWB_TEXT("C:\\"), NWB_TEXT("Root"), NWB_TEXT("."), NWB_TEXT("í•œê¸€"), NWB_TEXT("File.TXT") };
#else
    const Path<NWB::Core::Alloc::GlobalArena> path(testArena.arena, "/Root//./í•œê¸€/File.TXT/");
    constexpr TStringView s_Expected[]{ NWB_TEXT("/"), NWB_TEXT("Root"), NWB_TEXT("."), NWB_TEXT("í•œê¸€"), NWB_TEXT("File.TXT") };
#endif
    auto componentIt = path.begin();
    const TStringView first = componentIt.nativeComponent();
    usize componentIndex = 0u;
    while(componentIt != path.end()){
        ASSERT_LT(componentIndex, LengthOf(s_Expected));
        const TStringView borrowed = componentIt.nativeComponent();
        EXPECT_EQ(borrowed, s_Expected[componentIndex]);
        const auto owned = *componentIt;
        EXPECT_EQ(owned.native(), borrowed);
        auto next = componentIt;
        ++next;
        EXPECT_EQ(componentIt.nativeComponent(), borrowed);
        componentIt = next;
        EXPECT_EQ(borrowed, s_Expected[componentIndex]);
        EXPECT_EQ(first, s_Expected[0u]);
        ++componentIndex;
    }
    EXPECT_EQ(componentIndex, LengthOf(s_Expected));
    ++componentIt;
    EXPECT_EQ(componentIt, path.end());

    const Path<NWB::Core::Alloc::GlobalArena> empty(testArena.arena);
    EXPECT_EQ(empty.begin(), empty.end());
}

TEST(Global, PathNativeComponentViewsBorrowSourceStorageWithoutAllocating){
    NWB::Core::Alloc::GlobalArena arena(Name{"tests/path/native_component_views"});
    const Path<NWB::Core::Alloc::GlobalArena> path(
        arena,
        "First_component_long_enough_to_require_owning_storage/Second_component_with_Unicode_í•œê¸€_ðŸ˜€/Third_component"
    );
    const TStringView native = path.native();
    const ArenaMemoryStats before = arena.memoryStats();
    TStringView retained;
    {
        auto componentIt = path.begin();
        retained = componentIt.nativeComponent();
        EXPECT_EQ(retained.data(), native.data());
        for(usize repetition = 0u; repetition < 32u; ++repetition){
            usize componentCount = 0u;
            for(auto it = path.begin(); it != path.end(); ++it){
                const TStringView component = it.nativeComponent();
                EXPECT_GE(component.data(), native.data());
                EXPECT_LE(component.data() + component.size(), native.data() + native.size());
                ++componentCount;
            }
            EXPECT_EQ(componentCount, 3u);
        }
        ++componentIt;
        EXPECT_NE(componentIt.nativeComponent().data(), retained.data());
    }
    EXPECT_EQ(retained, TStringView(NWB_TEXT("First_component_long_enough_to_require_owning_storage")));
    const ArenaMemoryStats after = arena.memoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
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

    u64 value = 0u;
    EXPECT_TRUE(ParseVariableHexU64(AStringView("FFFFFFFFFFFFFFFF"), value));
    EXPECT_EQ(value, Limit<u64>::s_Max);
    EXPECT_FALSE(ParseVariableHexU64(AStringView(), value));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("0x"), value));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("10000000000000000"), value));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("xyz"), value));

    constexpr AStringView s_KeyValueText("alpha=one\r\nbeta=42\nempty=\n");
    AStringView textValue;
    EXPECT_TRUE(FindLineKeyValue(s_KeyValueText, "empty", textValue));
    EXPECT_TRUE(textValue.empty());
    EXPECT_FALSE(FindLineKeyValue(s_KeyValueText, "missing", textValue));
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(Global, CaptureProcessOutputReapsTruncatedChild){
    const AStringView argv[] = {
        s_BIN_SH,
        s_C,
        "printf '%s\\nxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' \"$$\"; exec 1>&-; while :; do :; done"
    };
    NWB::Tests::TestArena<> testArena;
    AString output(testArena.arena);
    EXPECT_FALSE(CaptureProcessOutput(testArena.arena, output, argv, 16u, 64u, 250u));

    const usize lineEnd = output.find('\n');
    ASSERT_NE(lineEnd, AString::npos);

    u64 childPidValue = 0u;
    ASSERT_TRUE(ParseU64(AStringView(output.data(), lineEnd), childPidValue));
    ASSERT_GT(childPidValue, 0u);
    ASSERT_LE(childPidValue, static_cast<u64>(Limit<pid_t>::s_Max));

    const pid_t childPid = static_cast<pid_t>(childPidValue);
    int status = 0;
    errno = 0;
    const pid_t waitResult = ::waitpid(childPid, &status, WNOHANG);
    const int waitError = errno;
    if(waitResult == 0)
        ProcessExecutionDetail::KillAndReapProcess(childPid);

    EXPECT_EQ(waitResult, -1);
    EXPECT_EQ(waitError, ECHILD);
}

TEST(Global, CaptureProcessOutputTimesOutWithContinuousOutput){
    const AStringView argv[] = {
        s_BIN_SH,
        s_C,
        "while :; do printf x; done"
    };
    NWB::Tests::TestArena<> testArena;
    AString output(testArena.arena);
    const u64 startMilliseconds = ProcessExecutionDetail::MonotonicMilliseconds();
    EXPECT_FALSE(CaptureProcessOutput(testArena.arena, output, argv, 64u, 64u, 100u));
    const u64 elapsedMilliseconds = ProcessExecutionDetail::MonotonicMilliseconds() - startMilliseconds;

    if(startMilliseconds != 0u)
        EXPECT_LT(elapsedMilliseconds, 1000u);
}

TEST(Global, RunProcessRedirectedToFileCapturesBothStreams){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/redirected_process");
    const Path<NWB::Core::Alloc::GlobalArena> outputPath = root / "combined output.txt";
    ErrorCode error;
    ASSERT_TRUE(EnsureEmptyDirectory(root, error));
    ASSERT_TRUE(WriteTextFile(outputPath, AStringView("stale")));

    const AStringView argv[] = {
        s_BIN_SH,
        s_C,
        AStringView("printf stdout; printf stderr >&2; exit 23; printf ignored").substr(0u, 41u)
    };
    const auto outputPathText = PathToString<char>(testArena.arena, outputPath);
    EXPECT_EQ(RunProcessRedirectedToFile(testArena.arena, argv, AStringView(outputPathText)), 23);
    EXPECT_EQ(RunProcessRedirectedToFile(testArena.arena, {}, AStringView(outputPathText)), -1);

    AString output;
    ASSERT_TRUE(ReadTextFile(outputPath, output));
    EXPECT_EQ(AStringView(output.data(), output.size()), AStringView("stdoutstderr"));
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}
#endif

TEST(Global, FilesystemMovePathToDirectory){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/move_path_to_directory");
    const Path<NWB::Core::Alloc::GlobalArena> source = root / "source.txt";
    const Path<NWB::Core::Alloc::GlobalArena> destinationDirectory = root / "moved";
    const Path<NWB::Core::Alloc::GlobalArena> destination = destinationDirectory / "source.txt";

    ErrorCode error;
    EXPECT_TRUE(EnsureEmptyDirectory(root, error));
    EXPECT_TRUE(WriteTextFile(source, AStringView("fresh")));
    EXPECT_TRUE(EnsureDirectories(destinationDirectory, error));
    EXPECT_FALSE(error);
    EXPECT_TRUE(WriteTextFile(destination, AStringView("old")));

    Path<NWB::Core::Alloc::GlobalArena> movedPath(testArena.arena);
    EXPECT_TRUE(MovePathToDirectory(source, destinationDirectory, movedPath));
    EXPECT_EQ(movedPath, destination);

    BasicString<char, NWB::Core::Alloc::GlobalArena> movedText{testArena.arena};
    EXPECT_TRUE(ReadTextFile(destination, movedText));
    EXPECT_EQ(AStringView(movedText.data(), movedText.size()), AStringView("fresh"));
    EXPECT_FALSE(FileExists(source, error));
    EXPECT_FALSE(error);

    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

#if defined(NWB_PLATFORM_LINUX)
TEST(Global, RecursiveDirectoryIteratorDoesNotFollowDirectorySymlinks){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/recursive_directory_iterator_links");
    const Path<NWB::Core::Alloc::GlobalArena> regularFile = root / "regular.txt";
    const Path<NWB::Core::Alloc::GlobalArena> directoryLink = root / "self";

    ErrorCode error;
    ASSERT_TRUE(EnsureEmptyDirectory(root, error));
    ASSERT_TRUE(WriteTextFile(regularFile, AStringView("regular")));
    ASSERT_EQ(::symlink(".", directoryLink.c_str()), 0);

    EXPECT_TRUE(IsDirectory(directoryLink, error));
    EXPECT_FALSE(error);
    EXPECT_FALSE(IsDirectoryNoFollow(directoryLink, error));
    EXPECT_FALSE(error);

    RecursiveDirectoryIterator directory(root, error);
    ASSERT_FALSE(error);

    usize entryCount = 0u;
    for(const auto& entry : directory){
        EXPECT_FALSE(entry.path().empty());
        ++entryCount;
    }
    EXPECT_EQ(entryCount, s_ExpectedDualCount);

    EXPECT_TRUE(RemoveAllIfExists(root, error));
}
#endif

TEST(Global, VolumeNamesRejectEmptySeparatorsAndWhitespace){
    EXPECT_FALSE(ValidVolumeName(""));
    EXPECT_FALSE(ValidVolumeName("graphics/cache"));
    EXPECT_FALSE(ValidVolumeName("graphics cache"));
}

TEST(Global, StringTableUsesPrefixedBoundsAndRejectsEmptyAppend){
    Vector<u8> stringTable;
    u32 alphaOffset = Limit<u32>::s_Max;
    u32 betaOffset = Limit<u32>::s_Max;

    EXPECT_TRUE(AppendStringTableText(stringTable, s_ALPHA, alphaOffset));
    EXPECT_TRUE(AppendStringTableText(stringTable, s_BETA, betaOffset));

    ACompactString parsed;

    Vector<u8> prefixedBinary;
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    EXPECT_TRUE(ReadStringTableText(prefixedBinary, 1u, stringTable.size(), betaOffset, parsed));
    EXPECT_EQ(parsed.view(), s_BETA);

    u32 emptyOffset = 0u;
    EXPECT_FALSE(AppendStringTableText(stringTable, AStringView(), emptyOffset));
    EXPECT_EQ(emptyOffset, Limit<u32>::s_Max);
}

TEST(Global, InvalidStringTableReads){
    Vector<u8> unterminated;
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));

    ACompactString parsed(s_UNCHANGED);
    EXPECT_FALSE(ReadStringTableText(unterminated, 0u, unterminated.size(), 0u, parsed));
    EXPECT_TRUE(parsed.empty());

    Vector<u8> emptyText;
    emptyText.push_back(0u);
    EXPECT_FALSE(ReadStringTableText(emptyText, 0u, emptyText.size(), 0u, parsed));
}

TEST(Global, EmptyBinaryVectorReadClearsReusedOutputWithoutAdvancing){
    Vector<u8> binary;
    Vector<u16> source;
    source.push_back(1u);
    source.push_back(s_ExpectedDualCount);
    source.push_back(static_cast<u16>(0xBEEFu));

    EXPECT_EQ(AppendBinaryVectorPayload(binary, source), BinaryVectorPayloadFailure::None);

    usize cursor = 0u;
    Vector<u16> parsed;
    EXPECT_EQ(ReadBinaryVectorPayload(binary, cursor, static_cast<u64>(source.size()), parsed), BinaryVectorPayloadFailure::None);

    cursor = 0u;
    parsed.push_back(7u);
    EXPECT_EQ(ReadBinaryVectorPayload(binary, cursor, 0u, parsed), BinaryVectorPayloadFailure::None);
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
    EXPECT_EQ(ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, tooSmall), BinaryVectorPayloadFailure::OutputOverflow);
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
    EXPECT_EQ(ReadBinaryVectorPayload(truncated, cursor, s_ExpectedDualCount, parsed), BinaryVectorPayloadFailure::SourceTruncated);
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
            if(
                messageCount > s_ExpectedMessageCount
                || errorCount > s_ExpectedErrorCount
                || (
                    lastType != NWB::Core::Common::LogType::Info
                    && lastType != NWB::Core::Common::LogType::Error
                )
            )
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
                const NWB::Core::Common::LogType::Enum type = (messageIndex & 1u) == 0u
                    ? NWB::Core::Common::LogType::Info
                    : NWB::Core::Common::LogType::Error
                ;
                NWB::Core::Common::LoggerDetail::EnqueueMessage(
                    logger,
                    type,
                    s_PARALLEL_LOGGER_MESSAGE
                );
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

