// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/capturing_logger.h>
#include <tests/test_context.h>
#include <gtest/gtest.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/hash_utils.h>
#include <global/limit.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
using CapturingLogger = NWB::Tests::CapturingLogger;
using AString = NWB::Tests::TestAString;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;

static u32 s_DiagnosticEventCaptureCount = 0u;
static const char* s_DiagnosticEventName = nullptr;
static const char* s_DiagnosticEventCategory = nullptr;
static const char* s_DiagnosticEventExpression = nullptr;
static const char* s_DiagnosticEventMessage = nullptr;
static const char* s_DiagnosticEventFile = nullptr;
static u32 s_DiagnosticEventLine = 0u;

static void ResetDiagnosticEventCapture()noexcept{
    s_DiagnosticEventCaptureCount = 0u;
    s_DiagnosticEventName = nullptr;
    s_DiagnosticEventCategory = nullptr;
    s_DiagnosticEventExpression = nullptr;
    s_DiagnosticEventMessage = nullptr;
    s_DiagnosticEventFile = nullptr;
    s_DiagnosticEventLine = 0u;
}

static void RecordDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
    ++s_DiagnosticEventCaptureCount;
    s_DiagnosticEventName = record.event;
    s_DiagnosticEventCategory = record.category;
    s_DiagnosticEventExpression = record.expression;
    s_DiagnosticEventMessage = record.message;
    s_DiagnosticEventFile = record.file;
    s_DiagnosticEventLine = record.line;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct U32VectorView{
    using value_type = u32;

    const u32* values = nullptr;
    usize valueCount = 0u;

    [[nodiscard]] bool empty()const{ return valueCount == 0u; }
    [[nodiscard]] usize size()const{ return valueCount; }
    [[nodiscard]] const u32* data()const{ return values; }
    [[nodiscard]] const u32* begin()const{ return values; }
    [[nodiscard]] const u32* end()const{ return values + valueCount; }
    [[nodiscard]] u32 operator[](const usize index)const{ return values[index]; }
};

struct MoveOnlySwapValue{
    i32 value = 0;

    MoveOnlySwapValue() = default;
    explicit MoveOnlySwapValue(const i32 initialValue)
        : value(initialValue)
    {}
    MoveOnlySwapValue(const MoveOnlySwapValue&) = delete;
    MoveOnlySwapValue& operator=(const MoveOnlySwapValue&) = delete;
    MoveOnlySwapValue(MoveOnlySwapValue&& rhs)noexcept
        : value(Exchange(rhs.value, -1))
    {}
    MoveOnlySwapValue& operator=(MoveOnlySwapValue&& rhs)noexcept{
        value = Exchange(rhs.value, -1);
        return *this;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, PodRoundTrip){
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

    const BinaryByteView byteView{ binary.data(), binary.size() };
    EXPECT_FALSE(byteView.empty());
    EXPECT_EQ(byteView.size(), binary.size());
    EXPECT_EQ(byteView[0u], binary[0u]);

    cursor = 0u;
    readValue = 0u;
    EXPECT_TRUE(ReadPOD(byteView, cursor, readValue));
    EXPECT_EQ(readValue, writtenValue);
}

TEST(Global, LengthPrefixedStringRoundTrip){
    Vector<u8> binary;
    const AString source("alpha");
    EXPECT_TRUE(AppendString(binary, AStringView(source.data(), source.size())));

    usize cursor = 0u;
    AString parsed;
    EXPECT_TRUE(ReadString(binary, cursor, parsed));
    EXPECT_EQ(parsed, source);
    EXPECT_EQ(cursor, binary.size());
}

TEST(Global, RejectedStringReadsDoNotAdvanceCursor){
    Vector<u8> truncated;
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));

    usize cursor = 0u;
    AString parsed("unchanged");
    EXPECT_FALSE(ReadString(truncated, cursor, parsed));
    EXPECT_EQ(cursor, 0u);
    EXPECT_EQ(parsed, "unchanged");

    Vector<u8> embeddedNull;
    const char textWithNull[] = { 'a', '\0', 'b' };
    EXPECT_TRUE(AppendString(embeddedNull, AStringView(textWithNull, sizeof(textWithNull))));

    ACompactString compact("unchanged");
    EXPECT_FALSE(ReadString(embeddedNull, cursor, compact));
    EXPECT_EQ(cursor, 0u);
    EXPECT_EQ(compact.view(), AStringView("unchanged"));
}

TEST(Global, RejectedACompactStringAssignResetsText){
    ACompactString compact("seed");
    const char textWithNull[] = { 'a', '\0', 'b' };

    EXPECT_FALSE(compact.assign(AStringView(textWithNull, sizeof(textWithNull))));
    EXPECT_TRUE(compact.empty());
    EXPECT_TRUE(compact.view().empty());
    EXPECT_EQ(compact.c_str()[0], '\0');
}

TEST(Global, BasicCompactStringTypes){
    static_assert(sizeof(ACompactString) == ACompactString::s_StorageBytes);
    static_assert(sizeof(WCompactString) == WCompactString::s_StorageBytes);

    ACompactString narrow("Folder\\File.TXT");
    EXPECT_EQ(narrow.view(), AStringView("folder/file.txt"));

    WCompactString wide(L"Folder\\File.TXT");
    EXPECT_EQ(wide.view(), WStringView(L"folder/file.txt"));

    wide += L"\\More";
    EXPECT_EQ(wide.view(), WStringView(L"folder/file.txt/more"));

    const WCompactString fileText = wide.substr(7u, 4u);
    EXPECT_EQ(fileText.view(), WStringView(L"file"));

    wchar oversized[WCompactString::s_MaxLength + 2u] = {};
    for(usize i = 0u; i < WCompactString::s_MaxLength + 1u; ++i)
        oversized[i] = L'a';

    WCompactString rejected(L"unchanged");
    EXPECT_FALSE(rejected.assign(WStringView(oversized, WCompactString::s_MaxLength + 1u)));
    EXPECT_TRUE(rejected.empty());
}

TEST(Global, TextUtilityHelpers){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> genericPath(testArena.arena, "alpha\\beta/file.txt");
    const auto genericPathText = PathToGenericString<char>(testArena.arena, genericPath);

    EXPECT_EQ(AStringView(genericPathText.data(), genericPathText.size()), AStringView("alpha/beta/file.txt"));
    EXPECT_EQ(TrimLeftView(AStringView(" \talpha ")), AStringView("alpha "));
    EXPECT_EQ(TrimView(AStringView(" \talpha \r\n")), AStringView("alpha"));
    EXPECT_EQ(TrimCopy(AString(" \tMiXeD \r\n")), AString("MiXeD"));
    EXPECT_EQ(ToAsciiLowerCopy(AString("MiXeD")), AString("mixed"));
    EXPECT_EQ(UnquoteMatchingAsciiQuotes(AString(" 'asset path' ")), AString("asset path"));
    EXPECT_EQ(UnquoteMatchingAsciiQuotes(AString(" \"asset path\" ")), AString("asset path"));
    EXPECT_TRUE(SafeStringView(static_cast<const char*>(nullptr)).empty());
    EXPECT_EQ(SafeStringView("safe"), AStringView("safe"));
    EXPECT_TRUE(FitsU32(static_cast<usize>(Limit<u32>::s_Max)));
    EXPECT_FALSE(FitsU32(static_cast<u64>(Limit<u32>::s_Max) + 1u));
    EXPECT_FALSE(FitsU32(-1));
    EXPECT_TRUE(StartsWith(AStringView("alpha"), AStringView("alp")));
    EXPECT_TRUE(StartsWith(AStringView("alpha"), "al"));
    EXPECT_FALSE(StartsWith(AStringView("alpha"), AStringView("beta")));
    EXPECT_FALSE(StartsWith(AStringView("al"), AStringView("alpha")));

    u64 value = 0u;
    EXPECT_TRUE(ParseVariableHexU64(AStringView("0x10"), value));
    EXPECT_EQ(value, 16u);
    EXPECT_TRUE(ParseVariableHexU64(AStringView("FFFFFFFFFFFFFFFF"), value));
    EXPECT_EQ(value, Limit<u64>::s_Max);
    EXPECT_FALSE(ParseVariableHexU64(AStringView(), value));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("0x"), value));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("10000000000000000"), value));
    EXPECT_FALSE(ParseVariableHexU64(AStringView("xyz"), value));

    constexpr AStringView s_KeyValueText("alpha=one\r\nbeta=42\nempty=\n");
    AStringView textValue;
    EXPECT_TRUE(FindLineKeyValue(s_KeyValueText, "alpha", textValue));
    EXPECT_EQ(textValue, AStringView("one"));
    EXPECT_TRUE(FindLineKeyValue(s_KeyValueText, "empty", textValue));
    EXPECT_TRUE(textValue.empty());
    EXPECT_FALSE(FindLineKeyValue(s_KeyValueText, "missing", textValue));
    EXPECT_TRUE(FindLineKeyValueU64(s_KeyValueText, "beta", value));
    EXPECT_EQ(value, 42u);
}

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

TEST(Global, StringTableText){
    Vector<u8> stringTable;
    u32 alphaOffset = Limit<u32>::s_Max;
    u32 betaOffset = Limit<u32>::s_Max;

    EXPECT_TRUE(AppendStringTableText(stringTable, AStringView("alpha"), alphaOffset));
    EXPECT_TRUE(AppendStringTableText(stringTable, AStringView("beta"), betaOffset));
    EXPECT_EQ(alphaOffset, 0u);
    EXPECT_EQ(betaOffset, 6u);

    ACompactString parsed;
    EXPECT_TRUE(ReadStringTableText(stringTable, 0u, stringTable.size(), alphaOffset, parsed));
    EXPECT_EQ(parsed.view(), AStringView("alpha"));

    Vector<u8> prefixedBinary;
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    EXPECT_TRUE(ReadStringTableText(prefixedBinary, 1u, stringTable.size(), betaOffset, parsed));
    EXPECT_EQ(parsed.view(), AStringView("beta"));

    u32 emptyOffset = 0u;
    EXPECT_FALSE(AppendStringTableText(stringTable, AStringView(), emptyOffset));
    EXPECT_EQ(emptyOffset, Limit<u32>::s_Max);
}

TEST(Global, InvalidStringTableReads){
    Vector<u8> unterminated;
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));

    ACompactString parsed("unchanged");
    EXPECT_FALSE(ReadStringTableText(unterminated, 0u, unterminated.size(), 0u, parsed));
    EXPECT_TRUE(parsed.empty());

    Vector<u8> emptyText;
    emptyText.push_back(0u);
    EXPECT_FALSE(ReadStringTableText(emptyText, 0u, emptyText.size(), 0u, parsed));
}

TEST(Global, BinaryVectorPayloadRoundTrip){
    Vector<u8> binary;
    Vector<u16> source;
    source.push_back(1u);
    source.push_back(2u);
    source.push_back(static_cast<u16>(0xBEEFu));

    EXPECT_EQ(AppendBinaryVectorPayload(binary, source), BinaryVectorPayloadFailure::None);

    usize cursor = 0u;
    Vector<u16> parsed;
    EXPECT_EQ(ReadBinaryVectorPayload(binary, cursor, static_cast<u64>(source.size()), parsed), BinaryVectorPayloadFailure::None);
    EXPECT_EQ(cursor, binary.size());
    EXPECT_EQ(parsed, source);

    cursor = 0u;
    parsed.push_back(7u);
    EXPECT_EQ(ReadBinaryVectorPayload(binary, cursor, 0u, parsed), BinaryVectorPayloadFailure::None);
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, FixedVectorBinaryPayloadRoundTrip){
    FixedVector<u8, 16u> fixedBinary;
    const u32 writtenValue = 0x55667788u;
    AppendPOD(fixedBinary, writtenValue);

    usize podCursor = 0u;
    u32 readValue = 0u;
    EXPECT_TRUE(ReadPOD(fixedBinary, podCursor, readValue));
    EXPECT_EQ(readValue, writtenValue);
    EXPECT_EQ(podCursor, sizeof(writtenValue));

    Vector<u8> vectorBinary;
    const u16 values[] = { 4u, 5u, 6u };
    for(const u16 value : values)
        AppendPOD(vectorBinary, value);

    usize vectorCursor = 0u;
    FixedVector<u16, 4u> parsedValues;
    EXPECT_EQ(ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, parsedValues), BinaryVectorPayloadFailure::None);
    EXPECT_EQ(vectorCursor, vectorBinary.size());
    EXPECT_EQ(parsedValues.size(), 3u);
    EXPECT_EQ(parsedValues[0u], values[0u]);
    EXPECT_EQ(parsedValues[1u], values[1u]);
    EXPECT_EQ(parsedValues[2u], values[2u]);

    vectorCursor = 0u;
    FixedVector<u16, 2u> tooSmall;
    EXPECT_EQ(ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, tooSmall), BinaryVectorPayloadFailure::OutputOverflow);
    EXPECT_EQ(vectorCursor, 0u);
    EXPECT_TRUE(tooSmall.empty());
}

TEST(Global, FixedVectorBinaryStringWrites){
    FixedVector<u8, 16u> fixedBinary;
    EXPECT_TRUE(AppendString(fixedBinary, AStringView("fixed")));

    usize cursor = 0u;
    AString parsed;
    EXPECT_TRUE(ReadString(fixedBinary, cursor, parsed));
    EXPECT_EQ(parsed, "fixed");
    EXPECT_EQ(cursor, fixedBinary.size());

    FixedVector<u8, 8u> fixedStringTable;
    u32 textOffset = Limit<u32>::s_Max;
    EXPECT_TRUE(AppendStringTableText(fixedStringTable, AStringView("bind"), textOffset));
    EXPECT_EQ(textOffset, 0u);

    ACompactString tableText;
    EXPECT_TRUE(ReadStringTableText(fixedStringTable, 0u, fixedStringTable.size(), textOffset, tableText));
    EXPECT_EQ(tableText.view(), AStringView("bind"));

    FixedVector<u8, 8u> rawText;
    AppendTextBytesNoReserveUnchecked(rawText, AStringView("raw"));
    ASSERT_EQ(rawText.size(), 3u);
    EXPECT_EQ(rawText[0u], static_cast<u8>('r'));
    EXPECT_EQ(rawText[1u], static_cast<u8>('a'));
    EXPECT_EQ(rawText[2u], static_cast<u8>('w'));
}

TEST(Global, RejectedBinaryVectorPayloadReadsDoNotAdvanceCursor){
    Vector<u8> truncated;
    const u32 source = 0x12345678u;
    AppendPOD(truncated, source);

    usize cursor = 0u;
    Vector<u32> parsed;
    parsed.push_back(0xAABBCCDDu);
    EXPECT_EQ(ReadBinaryVectorPayload(truncated, cursor, 2u, parsed), BinaryVectorPayloadFailure::SourceTruncated);
    EXPECT_EQ(cursor, 0u);
    EXPECT_TRUE(parsed.empty());
}

TEST(Global, AppendTriviallyCopyableVectorSelfAppend){
    Vector<u32> values;
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

TEST(Global, TriviallyCopyableVectorAlias){
    Vector<u32> values;
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

TEST(Global, CompressedPairSwapUsesMove){
    CompressedPair<MoveOnlySwapValue, MoveOnlySwapValue> lhs;
    CompressedPair<MoveOnlySwapValue, MoveOnlySwapValue> rhs;
    lhs.first().value = 1;
    lhs.second().value = 2;
    rhs.first().value = 3;
    rhs.second().value = 4;

    static_assert(noexcept(lhs.swap(rhs)));
    using std::swap;
    static_assert(noexcept(swap(lhs, rhs)));

    lhs.swap(rhs);
    EXPECT_EQ(lhs.first().value, 3);
    EXPECT_EQ(lhs.second().value, 4);
    EXPECT_EQ(rhs.first().value, 1);
    EXPECT_EQ(rhs.second().value, 2);

    swap(lhs, rhs);
    EXPECT_EQ(lhs.first().value, 1);
    EXPECT_EQ(lhs.second().value, 2);
    EXPECT_EQ(rhs.first().value, 3);
    EXPECT_EQ(rhs.second().value, 4);
}

TEST(Global, BoundedRuntimeWrappers){
    u32 copiedValue = 0u;
    const u32 sourceValue = 0xAABBCCDDu;
    NWB_MEMCPY(&copiedValue, sizeof(copiedValue), &sourceValue, sizeof(sourceValue));
    EXPECT_EQ(copiedValue, sourceValue);

    char copiedText[8] = {};
    EXPECT_EQ(NWB_STRCPY(copiedText, sizeof(copiedText), "alpha"), 0);
    EXPECT_STREQ(copiedText, "alpha");

    char copiedPrefix[8] = {};
    EXPECT_EQ(NWB_STRNCPY(copiedPrefix, sizeof(copiedPrefix), "abcdef", 3u), 0);
    EXPECT_STREQ(copiedPrefix, "abc");

    char appendedText[8] = {};
    EXPECT_EQ(NWB_STRCPY(appendedText, sizeof(appendedText), "ab"), 0);
    EXPECT_EQ(NWB_STRCAT(appendedText, sizeof(appendedText), "cd"), 0);
    EXPECT_STREQ(appendedText, "abcd");

    char formattedText[8] = {};
    EXPECT_EQ(NWB_SPRINTF(formattedText, sizeof(formattedText), "%s", "ok"), 2);
    EXPECT_STREQ(formattedText, "ok");

    wchar wideText[8] = {};
    EXPECT_EQ(NWB_WSTRCPY(wideText, sizeof(wideText) / sizeof(wideText[0]), L"wide"), 0);
    EXPECT_STREQ(wideText, L"wide");

    wchar formattedWideText[8] = {};
    EXPECT_EQ(NWB_WSPRINTF(formattedWideText, sizeof(formattedWideText) / sizeof(formattedWideText[0]), L"%ls", L"ok"), 2);
    EXPECT_STREQ(formattedWideText, L"ok");

#if !defined(_MSC_VER)
    char truncatedText[4] = {};
    EXPECT_NE(NWB_STRCPY(truncatedText, sizeof(truncatedText), "abcdef"), 0);
    EXPECT_STREQ(truncatedText, "abc");

    char nullTerminatedText[4] = { 'a', 'b', 'c', 'd' };
    EXPECT_NE(NWB_STRCAT(nullTerminatedText, sizeof(nullTerminatedText), "e"), 0);
    EXPECT_EQ(nullTerminatedText[sizeof(nullTerminatedText) - 1u], '\0');
#endif
}

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

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("macro {}"), 42);

    EXPECT_EQ(logger.messageCount(), 1u);
    EXPECT_EQ(logger.lastType(), NWB::Core::Common::LogType::EssentialInfo);
    EXPECT_TRUE(logger.sawMessageContaining(NWB_TEXT("macro 42")));

    const AString rawMessage("raw converted warning");
    NWB_LOGGER_WARNING(StringConvert(rawMessage));

#if NWB_OCCUR_WARNING
    EXPECT_EQ(logger.messageCount(), 2u);
    EXPECT_EQ(logger.lastType(), NWB::Core::Common::LogType::Warning);
    EXPECT_TRUE(logger.sawMessageContaining(NWB_TEXT("raw converted warning")));
#else
    EXPECT_EQ(logger.messageCount(), 1u);
    EXPECT_EQ(logger.lastType(), NWB::Core::Common::LogType::EssentialInfo);
    EXPECT_FALSE(logger.sawMessageContaining(NWB_TEXT("raw converted warning")));
#endif
}

TEST(Global, LoggerDiagnosticCaptureUsesFormattedMessage){
    ResetDiagnosticEventCapture();

    CapturingLogger logger;
    SetDiagnosticEventCallback(RecordDiagnosticEvent);
    NWB::Core::Common::LoggerDetail::EnqueueMessageAndCapture(
        logger,
        NWB::Core::Common::LogType::Error,
        NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
        "logger_diagnostic_test.cpp",
        77u,
        NWB_TEXT("recoverable error {}"),
        13
    );
    ClearDiagnosticEventCallback(RecordDiagnosticEvent);

    EXPECT_EQ(logger.messageCount(), 1u);
    EXPECT_EQ(logger.lastType(), NWB::Core::Common::LogType::Error);
    EXPECT_TRUE(logger.sawMessageContaining(NWB_TEXT("recoverable error 13")));
    EXPECT_EQ(s_DiagnosticEventCaptureCount, 1u);
    ASSERT_NE(s_DiagnosticEventName, nullptr);
    ASSERT_NE(s_DiagnosticEventCategory, nullptr);
    ASSERT_NE(s_DiagnosticEventExpression, nullptr);
    ASSERT_NE(s_DiagnosticEventMessage, nullptr);
    ASSERT_NE(s_DiagnosticEventFile, nullptr);
    EXPECT_STREQ(s_DiagnosticEventName, DiagnosticEventName::s_Error);
    EXPECT_STREQ(s_DiagnosticEventCategory, NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError);
    EXPECT_STREQ(s_DiagnosticEventExpression, "");
    EXPECT_STREQ(s_DiagnosticEventMessage, "recoverable error 13");
    EXPECT_STREQ(s_DiagnosticEventFile, "logger_diagnostic_test.cpp");
    EXPECT_EQ(s_DiagnosticEventLine, 77u);
}

TEST(Global, LoggerAssertTypeCapturesAssertDiagnostic){
    ResetDiagnosticEventCapture();

    CapturingLogger logger;
    SetDiagnosticEventCallback(RecordDiagnosticEvent);
    NWB::Core::Common::LoggerDetail::EnqueueMessageAndCapture(
        logger,
        NWB::Core::Common::LogType::Assert,
        NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryAssert,
        "logger_assert_test.cpp",
        91u,
        NWB_TEXT("assert log {}"),
        21
    );
    ClearDiagnosticEventCallback(RecordDiagnosticEvent);

    EXPECT_EQ(logger.messageCount(), 1u);
    EXPECT_EQ(logger.lastType(), NWB::Core::Common::LogType::Assert);
    EXPECT_TRUE(logger.sawMessageContaining(NWB_TEXT("assert log 21")));
    EXPECT_EQ(s_DiagnosticEventCaptureCount, 1u);
    ASSERT_NE(s_DiagnosticEventName, nullptr);
    ASSERT_NE(s_DiagnosticEventCategory, nullptr);
    ASSERT_NE(s_DiagnosticEventMessage, nullptr);
    ASSERT_NE(s_DiagnosticEventFile, nullptr);
    EXPECT_STREQ(s_DiagnosticEventName, DiagnosticEventName::s_Assert);
    EXPECT_STREQ(s_DiagnosticEventCategory, NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryAssert);
    EXPECT_STREQ(s_DiagnosticEventMessage, "assert log 21");
    EXPECT_STREQ(s_DiagnosticEventFile, "logger_assert_test.cpp");
    EXPECT_EQ(s_DiagnosticEventLine, 91u);
}

TEST(Global, DiagnosticEventHook){
    ResetDiagnosticEventCapture();

    const DiagnosticEventCallback callback = [](const DiagnosticEventRecord& record)noexcept{
        RecordDiagnosticEvent(record);
        CaptureDiagnosticEvent("recursive", "ignored");
    };

    SetDiagnosticEventCallback(callback);
    CaptureDiagnosticEvent("unit", "message", "diagnostics_test.cpp", 42u);
    ClearDiagnosticEventCallback(callback);
    CaptureDiagnosticEvent("unit", "ignored");

    EXPECT_EQ(s_DiagnosticEventCaptureCount, 1u);
    ASSERT_NE(s_DiagnosticEventName, nullptr);
    ASSERT_NE(s_DiagnosticEventCategory, nullptr);
    ASSERT_NE(s_DiagnosticEventExpression, nullptr);
    ASSERT_NE(s_DiagnosticEventMessage, nullptr);
    ASSERT_NE(s_DiagnosticEventFile, nullptr);
    EXPECT_STREQ(s_DiagnosticEventName, "");
    EXPECT_STREQ(s_DiagnosticEventCategory, "unit");
    EXPECT_STREQ(s_DiagnosticEventExpression, "");
    EXPECT_STREQ(s_DiagnosticEventMessage, "message");
    EXPECT_STREQ(s_DiagnosticEventFile, "diagnostics_test.cpp");
    EXPECT_EQ(s_DiagnosticEventLine, 42u);
    EXPECT_EQ(DiagnosticEventNameFromCategory(DiagnosticEventCategory::s_Assert), DiagnosticEventName::s_Assert);
    EXPECT_EQ(DiagnosticEventNameFromCategory(DiagnosticEventCategory::s_FatalAssert), DiagnosticEventName::s_Assert);
    EXPECT_EQ(DiagnosticEventNameFromCategory("unknown"), nullptr);
    EXPECT_EQ(DiagnosticEventNameFromRecord(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error }), DiagnosticEventName::s_Error);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

