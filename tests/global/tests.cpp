// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/capturing_logger.h>
#include <tests/test_context.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;
using AString = NWB::Tests::TestAString;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


#define NWB_GLOBAL_TEST_CHECK NWB_TEST_CHECK


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestPodRoundTrip(TestContext& context){
    Vector<u8> binary;
    const u32 writtenValue = 0x11223344u;
    AppendPOD(binary, writtenValue);

    usize cursor = 0u;
    u32 readValue = 0u;
    NWB_GLOBAL_TEST_CHECK(context, ReadPOD(binary, cursor, readValue));
    NWB_GLOBAL_TEST_CHECK(context, readValue == writtenValue);
    NWB_GLOBAL_TEST_CHECK(context, cursor == sizeof(writtenValue));

    const usize failedCursor = cursor;
    u32 unchangedValue = 0xAABBCCDDu;
    NWB_GLOBAL_TEST_CHECK(context, !ReadPOD(binary, cursor, unchangedValue));
    NWB_GLOBAL_TEST_CHECK(context, cursor == failedCursor);
    NWB_GLOBAL_TEST_CHECK(context, unchangedValue == 0xAABBCCDDu);
}

static void TestLengthPrefixedStringRoundTrip(TestContext& context){
    Vector<u8> binary;
    const AString source("alpha");
    NWB_GLOBAL_TEST_CHECK(context, AppendString(binary, AStringView(source.data(), source.size())));

    usize cursor = 0u;
    AString parsed;
    NWB_GLOBAL_TEST_CHECK(context, ReadString(binary, cursor, parsed));
    NWB_GLOBAL_TEST_CHECK(context, parsed == source);
    NWB_GLOBAL_TEST_CHECK(context, cursor == binary.size());
}

static void TestRejectedStringReadsDoNotAdvanceCursor(TestContext& context){
    Vector<u8> truncated;
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));

    usize cursor = 0u;
    AString parsed("unchanged");
    NWB_GLOBAL_TEST_CHECK(context, !ReadString(truncated, cursor, parsed));
    NWB_GLOBAL_TEST_CHECK(context, cursor == 0u);
    NWB_GLOBAL_TEST_CHECK(context, parsed == "unchanged");

    Vector<u8> embeddedNull;
    const char textWithNull[] = { 'a', '\0', 'b' };
    NWB_GLOBAL_TEST_CHECK(context, AppendString(embeddedNull, AStringView(textWithNull, sizeof(textWithNull))));

    ACompactString compact("unchanged");
    NWB_GLOBAL_TEST_CHECK(context, !ReadString(embeddedNull, cursor, compact));
    NWB_GLOBAL_TEST_CHECK(context, cursor == 0u);
    NWB_GLOBAL_TEST_CHECK(context, compact.view() == AStringView("unchanged"));
}

static void TestRejectedACompactStringAssignResetsText(TestContext& context){
    ACompactString compact("seed");
    const char textWithNull[] = { 'a', '\0', 'b' };

    NWB_GLOBAL_TEST_CHECK(context, !compact.assign(AStringView(textWithNull, sizeof(textWithNull))));
    NWB_GLOBAL_TEST_CHECK(context, compact.empty());
    NWB_GLOBAL_TEST_CHECK(context, compact.view().empty());
    NWB_GLOBAL_TEST_CHECK(context, compact.c_str()[0] == '\0');
}

static void TestBasicCompactStringTypes(TestContext& context){
    static_assert(sizeof(ACompactString) == ACompactString::s_StorageBytes);
    static_assert(sizeof(WCompactString) == WCompactString::s_StorageBytes);

    ACompactString narrow("Folder\\File.TXT");
    NWB_GLOBAL_TEST_CHECK(context, narrow.view() == AStringView("folder/file.txt"));

    WCompactString wide(L"Folder\\File.TXT");
    NWB_GLOBAL_TEST_CHECK(context, wide.view() == WStringView(L"folder/file.txt"));

    wide += L"\\More";
    NWB_GLOBAL_TEST_CHECK(context, wide.view() == WStringView(L"folder/file.txt/more"));

    const WCompactString fileText = wide.substr(7u, 4u);
    NWB_GLOBAL_TEST_CHECK(context, fileText.view() == WStringView(L"file"));

    wchar oversized[WCompactString::s_MaxLength + 2u] = {};
    for(usize i = 0u; i < WCompactString::s_MaxLength + 1u; ++i)
        oversized[i] = L'a';

    WCompactString rejected(L"unchanged");
    NWB_GLOBAL_TEST_CHECK(context, !rejected.assign(WStringView(oversized, WCompactString::s_MaxLength + 1u)));
    NWB_GLOBAL_TEST_CHECK(context, rejected.empty());
}

static void TestStringTableText(TestContext& context){
    Vector<u8> stringTable;
    u32 alphaOffset = Limit<u32>::s_Max;
    u32 betaOffset = Limit<u32>::s_Max;

    NWB_GLOBAL_TEST_CHECK(context, AppendStringTableText(stringTable, AStringView("alpha"), alphaOffset));
    NWB_GLOBAL_TEST_CHECK(context, AppendStringTableText(stringTable, AStringView("beta"), betaOffset));
    NWB_GLOBAL_TEST_CHECK(context, alphaOffset == 0u);
    NWB_GLOBAL_TEST_CHECK(context, betaOffset == 6u);

    ACompactString parsed;
    NWB_GLOBAL_TEST_CHECK(context, ReadStringTableText(stringTable, 0u, stringTable.size(), alphaOffset, parsed));
    NWB_GLOBAL_TEST_CHECK(context, parsed.view() == AStringView("alpha"));

    Vector<u8> prefixedBinary;
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    NWB_GLOBAL_TEST_CHECK(context, ReadStringTableText(prefixedBinary, 1u, stringTable.size(), betaOffset, parsed));
    NWB_GLOBAL_TEST_CHECK(context, parsed.view() == AStringView("beta"));

    u32 emptyOffset = 0u;
    NWB_GLOBAL_TEST_CHECK(context, !AppendStringTableText(stringTable, AStringView(), emptyOffset));
    NWB_GLOBAL_TEST_CHECK(context, emptyOffset == Limit<u32>::s_Max);
}

static void TestInvalidStringTableReads(TestContext& context){
    Vector<u8> unterminated;
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));

    ACompactString parsed("unchanged");
    NWB_GLOBAL_TEST_CHECK(context, !ReadStringTableText(unterminated, 0u, unterminated.size(), 0u, parsed));
    NWB_GLOBAL_TEST_CHECK(context, parsed.empty());

    Vector<u8> emptyText;
    emptyText.push_back(0u);
    NWB_GLOBAL_TEST_CHECK(context, !ReadStringTableText(emptyText, 0u, emptyText.size(), 0u, parsed));
}

static void TestBinaryVectorPayloadRoundTrip(TestContext& context){
    Vector<u8> binary;
    Vector<u16> source;
    source.push_back(1u);
    source.push_back(2u);
    source.push_back(static_cast<u16>(0xBEEFu));

    NWB_GLOBAL_TEST_CHECK(context, AppendBinaryVectorPayload(binary, source) == BinaryVectorPayloadFailure::None);

    usize cursor = 0u;
    Vector<u16> parsed;
    NWB_GLOBAL_TEST_CHECK(context, ReadBinaryVectorPayload(binary, cursor, static_cast<u64>(source.size()), parsed) == BinaryVectorPayloadFailure::None);
    NWB_GLOBAL_TEST_CHECK(context, cursor == binary.size());
    NWB_GLOBAL_TEST_CHECK(context, parsed == source);
}

static void TestFixedVectorBinaryPayloadRoundTrip(TestContext& context){
    FixedVector<u8, 16u> fixedBinary;
    const u32 writtenValue = 0x55667788u;
    AppendPOD(fixedBinary, writtenValue);

    usize podCursor = 0u;
    u32 readValue = 0u;
    NWB_GLOBAL_TEST_CHECK(context, ReadPOD(fixedBinary, podCursor, readValue));
    NWB_GLOBAL_TEST_CHECK(context, readValue == writtenValue);
    NWB_GLOBAL_TEST_CHECK(context, podCursor == sizeof(writtenValue));

    Vector<u8> vectorBinary;
    const u16 values[] = { 4u, 5u, 6u };
    for(const u16 value : values)
        AppendPOD(vectorBinary, value);

    usize vectorCursor = 0u;
    FixedVector<u16, 4u> parsedValues;
    NWB_GLOBAL_TEST_CHECK(context, ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, parsedValues) == BinaryVectorPayloadFailure::None);
    NWB_GLOBAL_TEST_CHECK(context, vectorCursor == vectorBinary.size());
    NWB_GLOBAL_TEST_CHECK(context, parsedValues.size() == 3u);
    NWB_GLOBAL_TEST_CHECK(context, parsedValues[0u] == values[0u]);
    NWB_GLOBAL_TEST_CHECK(context, parsedValues[1u] == values[1u]);
    NWB_GLOBAL_TEST_CHECK(context, parsedValues[2u] == values[2u]);

    vectorCursor = 0u;
    FixedVector<u16, 2u> tooSmall;
    NWB_GLOBAL_TEST_CHECK(context, ReadBinaryVectorPayload(vectorBinary, vectorCursor, 3u, tooSmall) == BinaryVectorPayloadFailure::OutputOverflow);
    NWB_GLOBAL_TEST_CHECK(context, vectorCursor == 0u);
    NWB_GLOBAL_TEST_CHECK(context, tooSmall.empty());
}

static void TestFixedVectorBinaryStringWrites(TestContext& context){
    FixedVector<u8, 16u> fixedBinary;
    NWB_GLOBAL_TEST_CHECK(context, AppendString(fixedBinary, AStringView("fixed")));

    usize cursor = 0u;
    AString parsed;
    NWB_GLOBAL_TEST_CHECK(context, ReadString(fixedBinary, cursor, parsed));
    NWB_GLOBAL_TEST_CHECK(context, parsed == "fixed");
    NWB_GLOBAL_TEST_CHECK(context, cursor == fixedBinary.size());

    FixedVector<u8, 8u> fixedStringTable;
    u32 textOffset = Limit<u32>::s_Max;
    NWB_GLOBAL_TEST_CHECK(context, AppendStringTableText(fixedStringTable, AStringView("bind"), textOffset));
    NWB_GLOBAL_TEST_CHECK(context, textOffset == 0u);

    ACompactString tableText;
    NWB_GLOBAL_TEST_CHECK(context, ReadStringTableText(fixedStringTable, 0u, fixedStringTable.size(), textOffset, tableText));
    NWB_GLOBAL_TEST_CHECK(context, tableText.view() == AStringView("bind"));
}

static void TestRejectedBinaryVectorPayloadReadsDoNotAdvanceCursor(TestContext& context){
    Vector<u8> truncated;
    const u32 source = 0x12345678u;
    AppendPOD(truncated, source);

    usize cursor = 0u;
    Vector<u32> parsed;
    parsed.push_back(0xAABBCCDDu);
    NWB_GLOBAL_TEST_CHECK(context, ReadBinaryVectorPayload(truncated, cursor, 2u, parsed) == BinaryVectorPayloadFailure::SourceTruncated);
    NWB_GLOBAL_TEST_CHECK(context, cursor == 0u);
    NWB_GLOBAL_TEST_CHECK(context, parsed.empty());
}

static void TestAppendTriviallyCopyableVectorSelfAppend(TestContext& context){
    Vector<u32> values;
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);

    AppendTriviallyCopyableVector(values, values);

    NWB_GLOBAL_TEST_CHECK(context, values.size() == 6u);
    NWB_GLOBAL_TEST_CHECK(context, values[0u] == 1u);
    NWB_GLOBAL_TEST_CHECK(context, values[1u] == 2u);
    NWB_GLOBAL_TEST_CHECK(context, values[2u] == 3u);
    NWB_GLOBAL_TEST_CHECK(context, values[3u] == 1u);
    NWB_GLOBAL_TEST_CHECK(context, values[4u] == 2u);
    NWB_GLOBAL_TEST_CHECK(context, values[5u] == 3u);
}

static void TestTriviallyCopyableVectorAlias(TestContext& context){
    Vector<u32> values;
    values.push_back(1u);
    values.push_back(2u);
    values.push_back(3u);
    values.push_back(4u);

    const U32VectorView middle{ values.data() + 1u, 2u };
    AppendTriviallyCopyableVector(values, middle);

    NWB_GLOBAL_TEST_CHECK(context, values.size() == 6u);
    NWB_GLOBAL_TEST_CHECK(context, values[0u] == 1u);
    NWB_GLOBAL_TEST_CHECK(context, values[1u] == 2u);
    NWB_GLOBAL_TEST_CHECK(context, values[2u] == 3u);
    NWB_GLOBAL_TEST_CHECK(context, values[3u] == 4u);
    NWB_GLOBAL_TEST_CHECK(context, values[4u] == 2u);
    NWB_GLOBAL_TEST_CHECK(context, values[5u] == 3u);

    const U32VectorView assignedMiddle{ values.data() + 1u, 3u };
    AssignTriviallyCopyableVector(values, assignedMiddle);

    NWB_GLOBAL_TEST_CHECK(context, values.size() == 3u);
    NWB_GLOBAL_TEST_CHECK(context, values[0u] == 2u);
    NWB_GLOBAL_TEST_CHECK(context, values[1u] == 3u);
    NWB_GLOBAL_TEST_CHECK(context, values[2u] == 4u);
}

static void TestBoundedRuntimeWrappers(TestContext& context){
    u32 copiedValue = 0u;
    const u32 sourceValue = 0xAABBCCDDu;
    NWB_MEMCPY(&copiedValue, sizeof(copiedValue), &sourceValue, sizeof(sourceValue));
    NWB_GLOBAL_TEST_CHECK(context, copiedValue == sourceValue);

    char copiedText[8] = {};
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCPY(copiedText, sizeof(copiedText), "alpha") == 0);
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCMP(copiedText, "alpha") == 0);

    char copiedPrefix[8] = {};
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRNCPY(copiedPrefix, sizeof(copiedPrefix), "abcdef", 3u) == 0);
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCMP(copiedPrefix, "abc") == 0);

    char appendedText[8] = {};
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCPY(appendedText, sizeof(appendedText), "ab") == 0);
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCAT(appendedText, sizeof(appendedText), "cd") == 0);
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCMP(appendedText, "abcd") == 0);

    char formattedText[8] = {};
    NWB_GLOBAL_TEST_CHECK(context, NWB_SPRINTF(formattedText, sizeof(formattedText), "%s", "ok") == 2);
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCMP(formattedText, "ok") == 0);

    wchar wideText[8] = {};
    NWB_GLOBAL_TEST_CHECK(context, NWB_WSTRCPY(wideText, sizeof(wideText) / sizeof(wideText[0]), L"wide") == 0);
    NWB_GLOBAL_TEST_CHECK(context, NWB_WSTRCMP(wideText, L"wide") == 0);

    wchar formattedWideText[8] = {};
    NWB_GLOBAL_TEST_CHECK(context, NWB_WSPRINTF(formattedWideText, sizeof(formattedWideText) / sizeof(formattedWideText[0]), L"%ls", L"ok") == 2);
    NWB_GLOBAL_TEST_CHECK(context, NWB_WSTRCMP(formattedWideText, L"ok") == 0);

#if !defined(_MSC_VER)
    char truncatedText[4] = {};
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCPY(truncatedText, sizeof(truncatedText), "abcdef") != 0);
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCMP(truncatedText, "abc") == 0);

    char nullTerminatedText[4] = { 'a', 'b', 'c', 'd' };
    NWB_GLOBAL_TEST_CHECK(context, NWB_STRCAT(nullTerminatedText, sizeof(nullTerminatedText), "e") != 0);
    NWB_GLOBAL_TEST_CHECK(context, nullTerminatedText[sizeof(nullTerminatedText) - 1u] == '\0');
#endif
}

static void TestLoggerMacrosBehaveAsSingleStatements(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard guard(logger);

    bool elseBranchRan = false;
    if(false)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("unreachable"));
    else
        elseBranchRan = true;

    NWB_GLOBAL_TEST_CHECK(context, elseBranchRan);
    NWB_GLOBAL_TEST_CHECK(context, logger.messageCount() == 0u);

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("macro {}"), 42);

    NWB_GLOBAL_TEST_CHECK(context, logger.messageCount() == 1u);
    NWB_GLOBAL_TEST_CHECK(context, logger.lastType() == NWB::Core::Common::LogType::EssentialInfo);
    NWB_GLOBAL_TEST_CHECK(context, logger.sawMessageContaining(NWB_TEXT("macro 42")));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_GLOBAL_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("global", [](NWB::Tests::TestContext& context){
    __hidden_tests::TestPodRoundTrip(context);
    __hidden_tests::TestLengthPrefixedStringRoundTrip(context);
    __hidden_tests::TestRejectedStringReadsDoNotAdvanceCursor(context);
    __hidden_tests::TestRejectedACompactStringAssignResetsText(context);
    __hidden_tests::TestBasicCompactStringTypes(context);
    __hidden_tests::TestStringTableText(context);
    __hidden_tests::TestInvalidStringTableReads(context);
    __hidden_tests::TestBinaryVectorPayloadRoundTrip(context);
    __hidden_tests::TestFixedVectorBinaryPayloadRoundTrip(context);
    __hidden_tests::TestFixedVectorBinaryStringWrites(context);
    __hidden_tests::TestRejectedBinaryVectorPayloadReadsDoNotAdvanceCursor(context);
    __hidden_tests::TestAppendTriviallyCopyableVectorSelfAppend(context);
    __hidden_tests::TestTriviallyCopyableVectorAlias(context);
    __hidden_tests::TestBoundedRuntimeWrappers(context);
    __hidden_tests::TestLoggerMacrosBehaveAsSingleStatements(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

