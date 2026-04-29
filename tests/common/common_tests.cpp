// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/common.h>

#include <tests/test_context.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_COMMON_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestPodRoundTrip(TestContext& context){
    Vector<u8> binary;
    const u32 writtenValue = 0x11223344u;
    AppendPOD(binary, writtenValue);

    usize cursor = 0u;
    u32 readValue = 0u;
    NWB_COMMON_TEST_CHECK(context, ReadPOD(binary, cursor, readValue));
    NWB_COMMON_TEST_CHECK(context, readValue == writtenValue);
    NWB_COMMON_TEST_CHECK(context, cursor == sizeof(writtenValue));

    const usize failedCursor = cursor;
    u32 unchangedValue = 0xAABBCCDDu;
    NWB_COMMON_TEST_CHECK(context, !ReadPOD(binary, cursor, unchangedValue));
    NWB_COMMON_TEST_CHECK(context, cursor == failedCursor);
    NWB_COMMON_TEST_CHECK(context, unchangedValue == 0xAABBCCDDu);
}

static void TestLengthPrefixedStringRoundTrip(TestContext& context){
    Vector<u8> binary;
    const AString source("alpha");
    NWB_COMMON_TEST_CHECK(context, AppendString(binary, AStringView(source.data(), source.size())));

    usize cursor = 0u;
    AString parsed;
    NWB_COMMON_TEST_CHECK(context, ReadString(binary, cursor, parsed));
    NWB_COMMON_TEST_CHECK(context, parsed == source);
    NWB_COMMON_TEST_CHECK(context, cursor == binary.size());
}

static void TestRejectedStringReadsDoNotAdvanceCursor(TestContext& context){
    Vector<u8> truncated;
    AppendPOD(truncated, static_cast<u32>(4u));
    truncated.push_back(static_cast<u8>('x'));

    usize cursor = 0u;
    AString parsed("unchanged");
    NWB_COMMON_TEST_CHECK(context, !ReadString(truncated, cursor, parsed));
    NWB_COMMON_TEST_CHECK(context, cursor == 0u);
    NWB_COMMON_TEST_CHECK(context, parsed == "unchanged");

    Vector<u8> embeddedNull;
    const char textWithNull[] = { 'a', '\0', 'b' };
    NWB_COMMON_TEST_CHECK(context, AppendString(embeddedNull, AStringView(textWithNull, sizeof(textWithNull))));

    CompactString compact("unchanged");
    NWB_COMMON_TEST_CHECK(context, !ReadString(embeddedNull, cursor, compact));
    NWB_COMMON_TEST_CHECK(context, cursor == 0u);
    NWB_COMMON_TEST_CHECK(context, compact.view() == AStringView("unchanged"));
}

static void TestStringTableText(TestContext& context){
    Vector<u8> stringTable;
    u32 alphaOffset = Limit<u32>::s_Max;
    u32 betaOffset = Limit<u32>::s_Max;

    NWB_COMMON_TEST_CHECK(context, AppendStringTableText(stringTable, AStringView("alpha"), alphaOffset));
    NWB_COMMON_TEST_CHECK(context, AppendStringTableText(stringTable, AStringView("beta"), betaOffset));
    NWB_COMMON_TEST_CHECK(context, alphaOffset == 0u);
    NWB_COMMON_TEST_CHECK(context, betaOffset == 6u);

    CompactString parsed;
    NWB_COMMON_TEST_CHECK(context, ReadStringTableText(stringTable, 0u, stringTable.size(), alphaOffset, parsed));
    NWB_COMMON_TEST_CHECK(context, parsed.view() == AStringView("alpha"));

    Vector<u8> prefixedBinary;
    prefixedBinary.push_back(0xFFu);
    prefixedBinary.insert(prefixedBinary.end(), stringTable.begin(), stringTable.end());
    NWB_COMMON_TEST_CHECK(context, ReadStringTableText(prefixedBinary, 1u, stringTable.size(), betaOffset, parsed));
    NWB_COMMON_TEST_CHECK(context, parsed.view() == AStringView("beta"));

    u32 emptyOffset = 0u;
    NWB_COMMON_TEST_CHECK(context, !AppendStringTableText(stringTable, AStringView(), emptyOffset));
    NWB_COMMON_TEST_CHECK(context, emptyOffset == Limit<u32>::s_Max);
}

static void TestInvalidStringTableReads(TestContext& context){
    Vector<u8> unterminated;
    unterminated.push_back(static_cast<u8>('a'));
    unterminated.push_back(static_cast<u8>('b'));

    CompactString parsed("unchanged");
    NWB_COMMON_TEST_CHECK(context, !ReadStringTableText(unterminated, 0u, unterminated.size(), 0u, parsed));
    NWB_COMMON_TEST_CHECK(context, parsed.empty());

    Vector<u8> emptyText;
    emptyText.push_back(0u);
    NWB_COMMON_TEST_CHECK(context, !ReadStringTableText(emptyText, 0u, emptyText.size(), 0u, parsed));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_COMMON_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "common tests failed: common initialization failed\n";
        return -1;
    }

    __hidden_common_tests::TestContext context;
    __hidden_common_tests::TestPodRoundTrip(context);
    __hidden_common_tests::TestLengthPrefixedStringRoundTrip(context);
    __hidden_common_tests::TestRejectedStringReadsDoNotAdvanceCursor(context);
    __hidden_common_tests::TestStringTableText(context);
    __hidden_common_tests::TestInvalidStringTableReads(context);

    if(context.failed != 0){
        NWB_CERR << "common tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return -1;
    }

    NWB_COUT << "common tests passed: " << context.passed << '\n';
    return 0;
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
