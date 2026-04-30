// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_GLOBAL_TEST_CHECK NWB_TEST_CHECK


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

    CompactString compact("unchanged");
    NWB_GLOBAL_TEST_CHECK(context, !ReadString(embeddedNull, cursor, compact));
    NWB_GLOBAL_TEST_CHECK(context, cursor == 0u);
    NWB_GLOBAL_TEST_CHECK(context, compact.view() == AStringView("unchanged"));
}

static void TestStringTableText(TestContext& context){
    Vector<u8> stringTable;
    u32 alphaOffset = Limit<u32>::s_Max;
    u32 betaOffset = Limit<u32>::s_Max;

    NWB_GLOBAL_TEST_CHECK(context, AppendStringTableText(stringTable, AStringView("alpha"), alphaOffset));
    NWB_GLOBAL_TEST_CHECK(context, AppendStringTableText(stringTable, AStringView("beta"), betaOffset));
    NWB_GLOBAL_TEST_CHECK(context, alphaOffset == 0u);
    NWB_GLOBAL_TEST_CHECK(context, betaOffset == 6u);

    CompactString parsed;
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

    CompactString parsed("unchanged");
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_GLOBAL_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("global", [](NWB::Tests::TestContext& context){
        __hidden_global_tests::TestPodRoundTrip(context);
        __hidden_global_tests::TestLengthPrefixedStringRoundTrip(context);
        __hidden_global_tests::TestRejectedStringReadsDoNotAdvanceCursor(context);
        __hidden_global_tests::TestStringTableText(context);
        __hidden_global_tests::TestInvalidStringTableReads(context);
        __hidden_global_tests::TestBinaryVectorPayloadRoundTrip(context);
        __hidden_global_tests::TestRejectedBinaryVectorPayloadReadsDoNotAdvanceCursor(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

