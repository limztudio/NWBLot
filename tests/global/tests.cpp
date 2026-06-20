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


namespace __hidden_tests{


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


static void TestPodRoundTrip(){
    Vector<u8> binary;
    const u32 writtenValue = 0x11223344u;
    AppendPOD(binary, writtenValue);

    usize cursor = 0u;
    u32 readValue = 0u;
    EXPECT_TRUE((ReadPOD(binary, cursor, readValue)));
    EXPECT_TRUE((readValue == writtenValue));
    EXPECT_TRUE((cursor == sizeof(writtenValue)));

    const usize failedCursor = cursor;
    u32 unchangedValue = 0xAABBCCDDu;
    EXPECT_TRUE((!ReadPOD(binary, cursor, unchangedValue)));
    EXPECT_TRUE((cursor == failedCursor));
    EXPECT_TRUE((unchangedValue == 0xAABBCCDDu));
}

static void TestLengthPrefixedStringRoundTrip(){
    Vector<u8> binary;
    const AString source("alpha");
    EXPECT_TRUE((AppendString(binary, AStringView(source.data(), source.size()))));

    usize cursor = 0u;
    AString parsed;
    EXPECT_TRUE((ReadString(binary, cursor, parsed)));
    EXPECT_TRUE((parsed == source));
    EXPECT_TRUE((cursor == binary.size()));
}

static void TestRejectedStringReadsDoNotAdvanceCursor(){
    Vector<u8> truncated;
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));

    usize cursor = 0u;
    AString parsed("unchanged");
    EXPECT_TRUE((!ReadString(truncated, cursor, parsed)));
    EXPECT_TRUE((cursor == 0u));
    EXPECT_TRUE((parsed == "unchanged"));

    Vector<u8> embeddedNull;
    const char textWithNull[] = { 'a', '\0', 'b' };
    EXPECT_TRUE((AppendString(embeddedNull, AStringView(textWithNull, sizeof(textWithNull)))));

    ACompactString compact("unchanged");
    EXPECT_TRUE((!ReadString(embeddedNull, cursor, compact)));
    EXPECT_TRUE((cursor == 0u));
    EXPECT_TRUE((compact.view() == AStringView("unchanged")));
}

static void TestRejectedACompactStringAssignResetsText(){
    ACompactString compact("seed");
    const char textWithNull[] = { 'a', '\0', 'b' };

    EXPECT_TRUE((!compact.assign(AStringView(textWithNull, sizeof(textWithNull)))));
    EXPECT_TRUE((compact.empty()));
    EXPECT_TRUE((compact.view().empty()));
    EXPECT_TRUE((compact.c_str()[0] == '\0'));
}

static void TestBasicCompactStringTypes(){
    static_assert(sizeof(ACompactString) == ACompactString::s_StorageBytes);
    static_assert(sizeof(WCompactString) == WCompactString::s_StorageBytes);

    ACompactString narrow("Folder\\File.TXT");
    EXPECT_TRUE((narrow.view() == AStringView("folder/file.txt")));

    WCompactString wide(L"Folder\\File.TXT");
    EXPECT_TRUE((wide.view() == WStringView(L"folder/file.txt")));

    wide += L"\\More";
    EXPECT_TRUE((wide.view() == WStringView(L"folder/file.txt/more")));

    const WCompactString fileText = wide.substr(7u, 4u);
    EXPECT_TRUE((fileText.view() == WStringView(L"file")));

    wchar oversized[WCompactString::s_MaxLength + 2u] = {};
    for(usize i = 0u; i < WCompactString::s_MaxLength + 1u; ++i)
        oversized[i] = L'a';

    WCompactString rejected(L"unchanged");
    EXPECT_TRUE((!rejected.assign(WStringView(oversized, WCompactString::s_MaxLength + 1u))));
    EXPECT_TRUE((rejected.empty()));
}

static void TestTextUtilityHelpers(){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> genericPath(testArena.arena, "alpha\\beta/file.txt");
    const auto genericPathText = PathToGenericString<char>(testArena.arena, genericPath);

    EXPECT_TRUE((AStringView(genericPathText.data(), genericPathText.size()) == AStringView("alpha/beta/file.txt")));
    EXPECT_TRUE((TrimLeftView(AStringView(" \talpha ")) == AStringView("alpha ")));
    EXPECT_TRUE((TrimView(AStringView(" \talpha \r\n")) == AStringView("alpha")));
    EXPECT_TRUE((StartsWith(AStringView("alpha"), AStringView("alp"))));
    EXPECT_TRUE((StartsWith(AStringView("alpha"), "al")));
    EXPECT_TRUE((!StartsWith(AStringView("alpha"), AStringView("beta"))));
    EXPECT_TRUE((!StartsWith(AStringView("al"), AStringView("alpha"))));

    u64 value = 0u;
    EXPECT_TRUE((ParseVariableHexU64(AStringView("0x10"), value)));
    EXPECT_TRUE((value == 16u));
    EXPECT_TRUE((ParseVariableHexU64(AStringView("FFFFFFFFFFFFFFFF"), value)));
    EXPECT_TRUE((value == Limit<u64>::s_Max));
    EXPECT_TRUE((!ParseVariableHexU64(AStringView(), value)));
    EXPECT_TRUE((!ParseVariableHexU64(AStringView("0x"), value)));
    EXPECT_TRUE((!ParseVariableHexU64(AStringView("10000000000000000"), value)));
    EXPECT_TRUE((!ParseVariableHexU64(AStringView("xyz"), value)));

    constexpr AStringView s_KeyValueText("alpha=one\r\nbeta=42\nempty=\n");
    AStringView textValue;
    EXPECT_TRUE((FindLineKeyValue(s_KeyValueText, "alpha", textValue)));
    EXPECT_TRUE((textValue == AStringView("one")));
    EXPECT_TRUE((FindLineKeyValue(s_KeyValueText, "empty", textValue)));
    EXPECT_TRUE((textValue.empty()));
    EXPECT_TRUE((!FindLineKeyValue(s_KeyValueText, "missing", textValue)));
    EXPECT_TRUE((FindLineKeyValueU64(s_KeyValueText, "beta", value)));
    EXPECT_TRUE((value == 42u));
}

static void TestFilesystemMovePathToDirectory(){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/move_path_to_directory");
    const Path<NWB::Core::Alloc::GlobalArena> source = root / "source.txt";
    const Path<NWB::Core::Alloc::GlobalArena> destinationDirectory = root / "moved";
    const Path<NWB::Core::Alloc::GlobalArena> destination = destinationDirectory / "source.txt";

    ErrorCode error;
    EXPECT_TRUE((EnsureEmptyDirectory(root, error)));
    EXPECT_TRUE((WriteTextFile(source, AStringView("fresh"))));
    EXPECT_TRUE((EnsureDirectories(destinationDirectory, error)));
    EXPECT_TRUE((!error));
    EXPECT_TRUE((WriteTextFile(destination, AStringView("old"))));

    Path<NWB::Core::Alloc::GlobalArena> movedPath(testArena.arena);
    EXPECT_TRUE((MovePathToDirectory(source, destinationDirectory, movedPath)));
    EXPECT_TRUE((movedPath == destination));

    BasicString<char, NWB::Core::Alloc::GlobalArena> movedText{testArena.arena};
    EXPECT_TRUE((ReadTextFile(destination, movedText)));
    EXPECT_TRUE((AStringView(movedText.data(), movedText.size()) == AStringView("fresh")));
    EXPECT_TRUE((!FileExists(source, error)));
    EXPECT_TRUE((!error));

    EXPECT_TRUE((RemoveAllIfExists(root, error)));
}

static void TestStringTableText(){
    Vector<u8> stringTable;
    u32 alphaOffset = Limit<u32>::s_Max;
    u32 betaOffset = Limit<u32>::s_Max;

    EXPECT_TRUE((AppendStringTableText(stringTable, AStringView("alpha"), alphaOffset)));
    EXPECT_TRUE((AppendStringTableText(stringTable, AStringView("beta"), betaOffset)));
    EXPECT_TRUE((alphaOffset == 0u));
    EXPECT_TRUE((betaOffset == 6u));

    ACompactString parsed;
    EXPECT_TRUE((ReadStringTableText(stringTable, 0u, stringTable.size(), alphaOffset, parsed)));
    EXPECT_TRUE((parsed.view() == AStringView("alpha")));

    Vector<u8> prefixedBinary;
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    EXPECT_TRUE((ReadStringTableText(prefixedBinary, 1u, stringTable.size(), betaOffset, parsed)));
    EXPECT_TRUE((parsed.view() == AStringView("beta")));

    u32 emptyOffset = 0u;
    EXPECT_TRUE((!AppendStringTableText(stringTable, AStringView(), emptyOffset)));
    EXPECT_TRUE((emptyOffset == Limit<u32>::s_Max));
}

static void TestInvalidStringTableReads(){
    Vector<u8> unterminated;
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));

    ACompactString parsed("unchanged");
    EXPECT_TRUE((!ReadStringTableText(unterminated, 0u, unterminated.size(), 0u, parsed)));
    EXPECT_TRUE((parsed.empty()));

    Vector<u8> emptyText;
    emptyText.push_back(0u);
    EXPECT_TRUE((!ReadStringTableText(emptyText, 0u, emptyText.size(), 0u, parsed)));
}

static void TestBinaryVectorPayloadRoundTrip(){
    Vector<u8> binary;
    Vector<u16> source;
    source.push_back(1u);
    source.push_back(2u);
    source.push_back(static_cast<u16>(0xBEEFu));

    EXPECT_TRUE((AppendBinaryVectorPayload(binary, source) == BinaryVectorPayloadFailure::None));

    usize cursor = 0u;
    Vector<u16> parsed;
    EXPECT_TRUE((ReadBinaryVectorPayload(binary, cursor, static_cast<u64>(source.size()), parsed) == BinaryVectorPayloadFailure::None));
    EXPECT_TRUE((cursor == binary.size()));
    EXPECT_TRUE((parsed == source));

    cursor = 0u;
    parsed.push_back(7u);
    EXPECT_TRUE((ReadBinaryVectorPayload(binary, cursor, 0u, parsed) == BinaryVectorPayloadFailure::None));
    EXPECT_TRUE((cursor == 0u));
    EXPECT_TRUE((parsed.empty()));
}

static void TestFixedVectorBinaryPayloadRoundTrip(){
    FixedVector<u8, 16u> fixedBinary;
    const u32 writtenValue = 0x55667788u;
    AppendPOD(fixedBinary, writtenValue);

    usize podCursor = 0u;
    u32 readValue = 0u;
    EXPECT_TRUE((ReadPOD(fixedBinary, podCursor, readValue)));
    EXPECT_TRUE((readValue == writtenValue));
    EXPECT_TRUE((podCursor == sizeof(writtenValue)));

    Vector<u8> vectorBinary;
    const u16 values[] = { 4u, 5u, 6u };
    for(const u16 value : values)
        AppendPOD(vectorBinary, value);

    usize vectorCursor = 0u;
    FixedVector<u16, 4u> parsedValues;
    EXPECT_TRUE((ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, parsedValues) == BinaryVectorPayloadFailure::None));
    EXPECT_TRUE((vectorCursor == vectorBinary.size()));
    EXPECT_TRUE((parsedValues.size() == 3u));
    EXPECT_TRUE((parsedValues[0u] == values[0u]));
    EXPECT_TRUE((parsedValues[1u] == values[1u]));
    EXPECT_TRUE((parsedValues[2u] == values[2u]));

    vectorCursor = 0u;
    FixedVector<u16, 2u> tooSmall;
    EXPECT_TRUE((ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, tooSmall) == BinaryVectorPayloadFailure::OutputOverflow));
    EXPECT_TRUE((vectorCursor == 0u));
    EXPECT_TRUE((tooSmall.empty()));
}

static void TestFixedVectorBinaryStringWrites(){
    FixedVector<u8, 16u> fixedBinary;
    EXPECT_TRUE((AppendString(fixedBinary, AStringView("fixed"))));

    usize cursor = 0u;
    AString parsed;
    EXPECT_TRUE((ReadString(fixedBinary, cursor, parsed)));
    EXPECT_TRUE((parsed == "fixed"));
    EXPECT_TRUE((cursor == fixedBinary.size()));

    FixedVector<u8, 8u> fixedStringTable;
    u32 textOffset = Limit<u32>::s_Max;
    EXPECT_TRUE((AppendStringTableText(fixedStringTable, AStringView("bind"), textOffset)));
    EXPECT_TRUE((textOffset == 0u));

    ACompactString tableText;
    EXPECT_TRUE((ReadStringTableText(fixedStringTable, 0u, fixedStringTable.size(), textOffset, tableText)));
    EXPECT_TRUE((tableText.view() == AStringView("bind")));
}

static void TestRejectedBinaryVectorPayloadReadsDoNotAdvanceCursor(){
    Vector<u8> truncated;
    const u32 source = 0x12345678u;
    AppendPOD(truncated, source);

    usize cursor = 0u;
    Vector<u32> parsed;
    parsed.push_back(0xAABBCCDDu);
    EXPECT_TRUE((ReadBinaryVectorPayload(truncated, cursor, 2u, parsed) == BinaryVectorPayloadFailure::SourceTruncated));
    EXPECT_TRUE((cursor == 0u));
    EXPECT_TRUE((parsed.empty()));
}

static void TestAppendTriviallyCopyableVectorSelfAppend(){
    Vector<u32> values;
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);

    AppendTriviallyCopyableVector(values, values);

    EXPECT_TRUE((values.size() == 6u));
    EXPECT_TRUE((values[0u] == 1u));
    EXPECT_TRUE((values[1u] == 2u));
    EXPECT_TRUE((values[2u] == 3u));
    EXPECT_TRUE((values[3u] == 1u));
    EXPECT_TRUE((values[4u] == 2u));
    EXPECT_TRUE((values[5u] == 3u));
}

static void TestTriviallyCopyableVectorAlias(){
    Vector<u32> values;
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);
    values.push_back(4u);

    const U32VectorView middle{ values.data() + 1u, 2u };
    AppendTriviallyCopyableVector(values, middle);

    EXPECT_TRUE((values.size() == 6u));
    EXPECT_TRUE((values[0u] == 1u));
    EXPECT_TRUE((values[1u] == 2u));
    EXPECT_TRUE((values[2u] == 3u));
    EXPECT_TRUE((values[3u] == 4u));
    EXPECT_TRUE((values[4u] == 2u));
    EXPECT_TRUE((values[5u] == 3u));

    const U32VectorView assignedMiddle{ values.data() + 1u, 3u };
    AssignTriviallyCopyableVector(values, assignedMiddle);

    EXPECT_TRUE((values.size() == 3u));
    EXPECT_TRUE((values[0u] == 2u));
    EXPECT_TRUE((values[1u] == 3u));
    EXPECT_TRUE((values[2u] == 4u));
}

static void TestCompressedPairSwapUsesMove(){
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
    EXPECT_TRUE((lhs.first().value == 3));
    EXPECT_TRUE((lhs.second().value == 4));
    EXPECT_TRUE((rhs.first().value == 1));
    EXPECT_TRUE((rhs.second().value == 2));

    swap(lhs, rhs);
    EXPECT_TRUE((lhs.first().value == 1));
    EXPECT_TRUE((lhs.second().value == 2));
    EXPECT_TRUE((rhs.first().value == 3));
    EXPECT_TRUE((rhs.second().value == 4));
}

static void TestBoundedRuntimeWrappers(){
    u32 copiedValue = 0u;
    const u32 sourceValue = 0xAABBCCDDu;
    NWB_MEMCPY(&copiedValue, sizeof(copiedValue), &sourceValue, sizeof(sourceValue));
    EXPECT_TRUE((copiedValue == sourceValue));

    char copiedText[8] = {};
    EXPECT_TRUE((NWB_STRCPY(copiedText, sizeof(copiedText), "alpha") == 0));
    EXPECT_TRUE((NWB_STRCMP(copiedText, "alpha") == 0));

    char copiedPrefix[8] = {};
    EXPECT_TRUE((NWB_STRNCPY(copiedPrefix, sizeof(copiedPrefix), "abcdef", 3u) == 0));
    EXPECT_TRUE((NWB_STRCMP(copiedPrefix, "abc") == 0));

    char appendedText[8] = {};
    EXPECT_TRUE((NWB_STRCPY(appendedText, sizeof(appendedText), "ab") == 0));
    EXPECT_TRUE((NWB_STRCAT(appendedText, sizeof(appendedText), "cd") == 0));
    EXPECT_TRUE((NWB_STRCMP(appendedText, "abcd") == 0));

    char formattedText[8] = {};
    EXPECT_TRUE((NWB_SPRINTF(formattedText, sizeof(formattedText), "%s", "ok") == 2));
    EXPECT_TRUE((NWB_STRCMP(formattedText, "ok") == 0));

    wchar wideText[8] = {};
    EXPECT_TRUE((NWB_WSTRCPY(wideText, sizeof(wideText) / sizeof(wideText[0]), L"wide") == 0));
    EXPECT_TRUE((NWB_WSTRCMP(wideText, L"wide") == 0));

    wchar formattedWideText[8] = {};
    EXPECT_TRUE((NWB_WSPRINTF(formattedWideText, sizeof(formattedWideText) / sizeof(formattedWideText[0]), L"%ls", L"ok") == 2));
    EXPECT_TRUE((NWB_WSTRCMP(formattedWideText, L"ok") == 0));

#if !defined(_MSC_VER)
    char truncatedText[4] = {};
    EXPECT_TRUE((NWB_STRCPY(truncatedText, sizeof(truncatedText), "abcdef") != 0));
    EXPECT_TRUE((NWB_STRCMP(truncatedText, "abc") == 0));

    char nullTerminatedText[4] = { 'a', 'b', 'c', 'd' };
    EXPECT_TRUE((NWB_STRCAT(nullTerminatedText, sizeof(nullTerminatedText), "e") != 0));
    EXPECT_TRUE((nullTerminatedText[sizeof(nullTerminatedText) - 1u] == '\0'));
#endif
}

static void TestLoggerMacrosBehaveAsSingleStatements(){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard guard(logger);

    bool elseBranchRan = false;
    if(false)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("unreachable"));
    else
        elseBranchRan = true;

    EXPECT_TRUE((elseBranchRan));
    EXPECT_TRUE((logger.messageCount() == 0u));

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("macro {}"), 42);

    EXPECT_TRUE((logger.messageCount() == 1u));
    EXPECT_TRUE((logger.lastType() == NWB::Core::Common::LogType::EssentialInfo));
    EXPECT_TRUE((logger.sawMessageContaining(NWB_TEXT("macro 42"))));

    const AString rawMessage("raw converted warning");
    NWB_LOGGER_WARNING(StringConvert(rawMessage));

#if NWB_OCCUR_WARNING
    EXPECT_TRUE((logger.messageCount() == 2u));
    EXPECT_TRUE((logger.lastType() == NWB::Core::Common::LogType::Warning));
    EXPECT_TRUE((logger.sawMessageContaining(NWB_TEXT("raw converted warning"))));
#else
    EXPECT_TRUE((logger.messageCount() == 1u));
    EXPECT_TRUE((logger.lastType() == NWB::Core::Common::LogType::EssentialInfo));
    EXPECT_TRUE((!logger.sawMessageContaining(NWB_TEXT("raw converted warning"))));
#endif
}

static void TestLoggerDiagnosticCaptureUsesFormattedMessage(){
    s_DiagnosticEventCaptureCount = 0u;
    s_DiagnosticEventName = nullptr;
    s_DiagnosticEventCategory = nullptr;
    s_DiagnosticEventExpression = nullptr;
    s_DiagnosticEventMessage = nullptr;
    s_DiagnosticEventFile = nullptr;
    s_DiagnosticEventLine = 0u;

    const DiagnosticEventCallback callback = [](const DiagnosticEventRecord& record)noexcept{
        ++s_DiagnosticEventCaptureCount;
        s_DiagnosticEventName = record.event;
        s_DiagnosticEventCategory = record.category;
        s_DiagnosticEventExpression = record.expression;
        s_DiagnosticEventMessage = record.message;
        s_DiagnosticEventFile = record.file;
        s_DiagnosticEventLine = record.line;
    };

    CapturingLogger logger;
    SetDiagnosticEventCallback(callback);
    NWB::Core::Common::LoggerDetail::EnqueueMessageAndCapture(
        logger,
        NWB::Core::Common::LogType::Error,
        NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
        "logger_diagnostic_test.cpp",
        77u,
        NWB_TEXT("recoverable error {}"),
        13
    );
    ClearDiagnosticEventCallback(callback);

    EXPECT_TRUE((logger.messageCount() == 1u));
    EXPECT_TRUE((logger.lastType() == NWB::Core::Common::LogType::Error));
    EXPECT_TRUE((logger.sawMessageContaining(NWB_TEXT("recoverable error 13"))));
    EXPECT_TRUE((s_DiagnosticEventCaptureCount == 1u));
    EXPECT_TRUE((s_DiagnosticEventName && NWB_STRCMP(s_DiagnosticEventName, DiagnosticEventName::s_Error) == 0));
    EXPECT_TRUE((s_DiagnosticEventCategory && NWB_STRCMP(s_DiagnosticEventCategory, NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError) == 0));
    EXPECT_TRUE((s_DiagnosticEventExpression && NWB_STRCMP(s_DiagnosticEventExpression, "") == 0));
    EXPECT_TRUE((s_DiagnosticEventMessage && NWB_STRCMP(s_DiagnosticEventMessage, "recoverable error 13") == 0));
    EXPECT_TRUE((s_DiagnosticEventFile && NWB_STRCMP(s_DiagnosticEventFile, "logger_diagnostic_test.cpp") == 0));
    EXPECT_TRUE((s_DiagnosticEventLine == 77u));
}

static void TestLoggerAssertTypeCapturesAssertDiagnostic(){
    s_DiagnosticEventCaptureCount = 0u;
    s_DiagnosticEventName = nullptr;
    s_DiagnosticEventCategory = nullptr;
    s_DiagnosticEventExpression = nullptr;
    s_DiagnosticEventMessage = nullptr;
    s_DiagnosticEventFile = nullptr;
    s_DiagnosticEventLine = 0u;

    const DiagnosticEventCallback callback = [](const DiagnosticEventRecord& record)noexcept{
        ++s_DiagnosticEventCaptureCount;
        s_DiagnosticEventName = record.event;
        s_DiagnosticEventCategory = record.category;
        s_DiagnosticEventExpression = record.expression;
        s_DiagnosticEventMessage = record.message;
        s_DiagnosticEventFile = record.file;
        s_DiagnosticEventLine = record.line;
    };

    CapturingLogger logger;
    SetDiagnosticEventCallback(callback);
    NWB::Core::Common::LoggerDetail::EnqueueMessageAndCapture(
        logger,
        NWB::Core::Common::LogType::Assert,
        NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryAssert,
        "logger_assert_test.cpp",
        91u,
        NWB_TEXT("assert log {}"),
        21
    );
    ClearDiagnosticEventCallback(callback);

    EXPECT_TRUE((logger.messageCount() == 1u));
    EXPECT_TRUE((logger.lastType() == NWB::Core::Common::LogType::Assert));
    EXPECT_TRUE((logger.sawMessageContaining(NWB_TEXT("assert log 21"))));
    EXPECT_TRUE((s_DiagnosticEventCaptureCount == 1u));
    EXPECT_TRUE((s_DiagnosticEventName && NWB_STRCMP(s_DiagnosticEventName, DiagnosticEventName::s_Assert) == 0));
    EXPECT_TRUE((s_DiagnosticEventCategory && NWB_STRCMP(s_DiagnosticEventCategory, NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryAssert) == 0));
    EXPECT_TRUE((s_DiagnosticEventMessage && NWB_STRCMP(s_DiagnosticEventMessage, "assert log 21") == 0));
    EXPECT_TRUE((s_DiagnosticEventFile && NWB_STRCMP(s_DiagnosticEventFile, "logger_assert_test.cpp") == 0));
    EXPECT_TRUE((s_DiagnosticEventLine == 91u));
}

static void TestDiagnosticEventHook(){
    s_DiagnosticEventCaptureCount = 0u;
    s_DiagnosticEventName = nullptr;
    s_DiagnosticEventCategory = nullptr;
    s_DiagnosticEventExpression = nullptr;
    s_DiagnosticEventMessage = nullptr;
    s_DiagnosticEventFile = nullptr;
    s_DiagnosticEventLine = 0u;

    const DiagnosticEventCallback callback = [](const DiagnosticEventRecord& record)noexcept{
        ++s_DiagnosticEventCaptureCount;
        s_DiagnosticEventName = record.event;
        s_DiagnosticEventCategory = record.category;
        s_DiagnosticEventExpression = record.expression;
        s_DiagnosticEventMessage = record.message;
        s_DiagnosticEventFile = record.file;
        s_DiagnosticEventLine = record.line;
        CaptureDiagnosticEvent("recursive", "ignored");
    };

    SetDiagnosticEventCallback(callback);
    CaptureDiagnosticEvent("unit", "message", "diagnostics_test.cpp", 42u);
    ClearDiagnosticEventCallback(callback);
    CaptureDiagnosticEvent("unit", "ignored");

    EXPECT_TRUE((s_DiagnosticEventCaptureCount == 1u));
    EXPECT_TRUE((s_DiagnosticEventName && NWB_STRCMP(s_DiagnosticEventName, "") == 0));
    EXPECT_TRUE((s_DiagnosticEventCategory && NWB_STRCMP(s_DiagnosticEventCategory, "unit") == 0));
    EXPECT_TRUE((s_DiagnosticEventExpression && NWB_STRCMP(s_DiagnosticEventExpression, "") == 0));
    EXPECT_TRUE((s_DiagnosticEventMessage && NWB_STRCMP(s_DiagnosticEventMessage, "message") == 0));
    EXPECT_TRUE((s_DiagnosticEventFile && NWB_STRCMP(s_DiagnosticEventFile, "diagnostics_test.cpp") == 0));
    EXPECT_TRUE((s_DiagnosticEventLine == 42u));
    EXPECT_TRUE((DiagnosticEventNameFromCategory(DiagnosticEventCategory::s_Assert) == DiagnosticEventName::s_Assert));
    EXPECT_TRUE((DiagnosticEventNameFromCategory(DiagnosticEventCategory::s_FatalAssert) == DiagnosticEventName::s_Assert));
    EXPECT_TRUE((DiagnosticEventNameFromCategory("unknown") == nullptr));
    EXPECT_TRUE((DiagnosticEventNameFromRecord(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error }) == DiagnosticEventName::s_Error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, PodRoundTrip){
    __hidden_tests::TestPodRoundTrip();
}

TEST(Global, LengthPrefixedStringRoundTrip){
    __hidden_tests::TestLengthPrefixedStringRoundTrip();
}

TEST(Global, RejectedStringReadsDoNotAdvanceCursor){
    __hidden_tests::TestRejectedStringReadsDoNotAdvanceCursor();
}

TEST(Global, RejectedACompactStringAssignResetsText){
    __hidden_tests::TestRejectedACompactStringAssignResetsText();
}

TEST(Global, BasicCompactStringTypes){
    __hidden_tests::TestBasicCompactStringTypes();
}

TEST(Global, TextUtilityHelpers){
    __hidden_tests::TestTextUtilityHelpers();
}

TEST(Global, FilesystemMovePathToDirectory){
    __hidden_tests::TestFilesystemMovePathToDirectory();
}

TEST(Global, StringTableText){
    __hidden_tests::TestStringTableText();
}

TEST(Global, InvalidStringTableReads){
    __hidden_tests::TestInvalidStringTableReads();
}

TEST(Global, BinaryVectorPayloadRoundTrip){
    __hidden_tests::TestBinaryVectorPayloadRoundTrip();
}

TEST(Global, FixedVectorBinaryPayloadRoundTrip){
    __hidden_tests::TestFixedVectorBinaryPayloadRoundTrip();
}

TEST(Global, FixedVectorBinaryStringWrites){
    __hidden_tests::TestFixedVectorBinaryStringWrites();
}

TEST(Global, RejectedBinaryVectorPayloadReadsDoNotAdvanceCursor){
    __hidden_tests::TestRejectedBinaryVectorPayloadReadsDoNotAdvanceCursor();
}

TEST(Global, AppendTriviallyCopyableVectorSelfAppend){
    __hidden_tests::TestAppendTriviallyCopyableVectorSelfAppend();
}

TEST(Global, TriviallyCopyableVectorAlias){
    __hidden_tests::TestTriviallyCopyableVectorAlias();
}

TEST(Global, CompressedPairSwapUsesMove){
    __hidden_tests::TestCompressedPairSwapUsesMove();
}

TEST(Global, BoundedRuntimeWrappers){
    __hidden_tests::TestBoundedRuntimeWrappers();
}

TEST(Global, LoggerMacrosBehaveAsSingleStatements){
    __hidden_tests::TestLoggerMacrosBehaveAsSingleStatements();
}

TEST(Global, LoggerDiagnosticCaptureUsesFormattedMessage){
    __hidden_tests::TestLoggerDiagnosticCaptureUsesFormattedMessage();
}

TEST(Global, LoggerAssertTypeCapturesAssertDiagnostic){
    __hidden_tests::TestLoggerAssertTypeCapturesAssertDiagnostic();
}

TEST(Global, DiagnosticEventHook){
    __hidden_tests::TestDiagnosticEventHook();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

