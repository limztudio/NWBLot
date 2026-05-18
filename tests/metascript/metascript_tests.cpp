// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <tests/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using Document = NWB::Core::Metascript::Document;
using Value = NWB::Core::Metascript::Value;
using MStringView = NWB::Core::Metascript::MStringView;


#define NWB_METASCRIPT_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceAllocatorTag{};
struct DestinationAllocatorTag{};

using SourceAllocator = NWB::Tests::CountingTestAllocator<SourceAllocatorTag>;
using DestinationAllocator = NWB::Tests::CountingTestAllocator<DestinationAllocatorTag>;
using SourceArena = NWB::Tests::TestArena<SourceAllocator>;
using DestinationArena = NWB::Tests::TestArena<DestinationAllocator>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static MStringView ViewOf(const AString& text){
    return MStringView(text.data(), text.size());
}

static void ResetAllocationCounts(){
    SourceAllocator::resetAllocationCalls();
    DestinationAllocator::resetAllocationCalls();
}

static void CheckDestinationOnlyAllocation(TestContext& context){
    NWB_METASCRIPT_TEST_CHECK(context, SourceAllocator::allocationCallCount() == 0u);
    NWB_METASCRIPT_TEST_CHECK(context, DestinationAllocator::allocationCallCount() > 0u);
}

static void CheckSingleStringListValue(TestContext& context, const Value& value, const AString& text){
    NWB_METASCRIPT_TEST_CHECK(context, value.isList());
    NWB_METASCRIPT_TEST_CHECK(context, value.isList() && value.asList().size() == 1u);
    if(value.isList() && value.asList().size() == 1u){
        const Value& copiedText = value.asList()[0u];
        NWB_METASCRIPT_TEST_CHECK(context, copiedText.isString());
        NWB_METASCRIPT_TEST_CHECK(context, copiedText.asString() == ViewOf(text));
    }
}

static void TestCrossArenaMoveAssignmentCopiesIntoDestinationArena(TestContext& context){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'm');

    Value source(ViewOf(text), sourceArena.arena);
    Value destination(destinationArena.arena);

    ResetAllocationCounts();
    destination = Move(source);

    NWB_METASCRIPT_TEST_CHECK(context, source.isNull());
    NWB_METASCRIPT_TEST_CHECK(context, destination.isString());
    NWB_METASCRIPT_TEST_CHECK(context, destination.asString() == ViewOf(text));
    CheckDestinationOnlyAllocation(context);
}

static void TestCrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena(TestContext& context){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'c');

    Value source(sourceArena.arena);
    source.makeMap();
    Value& list = source.field(MStringView("items", 5u));
    list.makeList();
    list.append(Value(ViewOf(text), sourceArena.arena));

    Value destination(destinationArena.arena);

    ResetAllocationCounts();
    destination = source;

    NWB_METASCRIPT_TEST_CHECK(context, source.isMap());
    NWB_METASCRIPT_TEST_CHECK(context, destination.isMap());
    CheckDestinationOnlyAllocation(context);

    const Value* copiedList = destination.findField(MStringView("items", 5u));
    NWB_METASCRIPT_TEST_CHECK(context, copiedList != nullptr);
    if(copiedList)
        CheckSingleStringListValue(context, *copiedList, text);
}

static void TestCrossArenaListConcatCopiesIntoResultArena(TestContext& context){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'p');

    Value source(sourceArena.arena);
    source.makeList();
    source.append(Value(ViewOf(text), sourceArena.arena));

    Value destination(destinationArena.arena);
    destination.makeList();

    ResetAllocationCounts();
    Value result = destination + source;

    CheckSingleStringListValue(context, result, text);
    CheckDestinationOnlyAllocation(context);
}

static void TestCrossArenaAppendCopiesIntoDestinationArena(TestContext& context){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'a');

    Value source(ViewOf(text), sourceArena.arena);
    Value destination(destinationArena.arena);
    destination.makeList();

    ResetAllocationCounts();
    destination.append(Move(source));

    NWB_METASCRIPT_TEST_CHECK(context, source.isNull());
    CheckSingleStringListValue(context, destination, text);
    CheckDestinationOnlyAllocation(context);
}

static void TestListSelfAppendCopiesOriginalValues(TestContext& context){
    DestinationArena arena;
    const AString text(128u, 's');

    Value list(arena.arena);
    list.makeList();
    list.append(Value(ViewOf(text), arena.arena));

    ResetAllocationCounts();
    list += list;

    NWB_METASCRIPT_TEST_CHECK(context, list.asList().size() == 2u);
    if(list.asList().size() == 2u){
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[0u].isString());
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[1u].isString());
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[0u].asString() == ViewOf(text));
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[1u].asString() == ViewOf(text));
    }
    CheckDestinationOnlyAllocation(context);
}

static void TestAppendSelfMoveCopiesOriginalValue(TestContext& context){
    DestinationArena arena;
    const AString text(128u, 'v');

    Value list(arena.arena);
    list.makeList();
    list.append(Value(ViewOf(text), arena.arena));

    list.append(Move(list));

    NWB_METASCRIPT_TEST_CHECK(context, list.isList());
    NWB_METASCRIPT_TEST_CHECK(context, list.asList().size() == 2u);
    if(list.isList() && list.asList().size() == 2u){
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[0u].isString());
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[0u].asString() == ViewOf(text));
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[1u].isList());
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[1u].asList().size() == 1u);
        if(list.asList()[1u].isList() && list.asList()[1u].asList().size() == 1u){
            const Value& nestedText = list.asList()[1u].asList()[0u];
            NWB_METASCRIPT_TEST_CHECK(context, nestedText.isString());
            NWB_METASCRIPT_TEST_CHECK(context, nestedText.asString() == ViewOf(text));
        }
    }
}

static void TestExponentDoubleLiterals(TestContext& context){
    DestinationArena arena;
    Document document(arena.arena);
    const AString source =
        "number asset;\n"
        "asset.values = [2.89785676e-05, -3.66009772E-07, 1e+03, 2E2];\n"
    ;

    NWB_METASCRIPT_TEST_CHECK(context, document.parse(ViewOf(source)));
    const Value* values = document.asset().findField(MStringView("values", 6u));
    NWB_METASCRIPT_TEST_CHECK(context, values != nullptr);
    if(!values || !values->isList())
        return;

    const auto& list = values->asList();
    NWB_METASCRIPT_TEST_CHECK(context, list.size() == 4u);
    if(list.size() != 4u)
        return;

    NWB_METASCRIPT_TEST_CHECK(context, list[0u].isDouble());
    NWB_METASCRIPT_TEST_CHECK(context, list[1u].isDouble());
    NWB_METASCRIPT_TEST_CHECK(context, list[2u].isDouble());
    NWB_METASCRIPT_TEST_CHECK(context, list[3u].isDouble());
    NWB_METASCRIPT_TEST_CHECK(context, Abs(list[0u].asDouble() - 0.0000289785676) < 0.0000000001);
    NWB_METASCRIPT_TEST_CHECK(context, Abs(list[1u].asDouble() + 0.000000366009772) < 0.0000000001);
    NWB_METASCRIPT_TEST_CHECK(context, Abs(list[2u].asDouble() - 1000.0) < 0.0000000001);
    NWB_METASCRIPT_TEST_CHECK(context, Abs(list[3u].asDouble() - 200.0) < 0.0000000001);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_METASCRIPT_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("metascript", [](NWB::Tests::TestContext& context){
    __hidden_metascript_tests::TestCrossArenaMoveAssignmentCopiesIntoDestinationArena(context);
    __hidden_metascript_tests::TestCrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena(context);
    __hidden_metascript_tests::TestCrossArenaListConcatCopiesIntoResultArena(context);
    __hidden_metascript_tests::TestCrossArenaAppendCopiesIntoDestinationArena(context);
    __hidden_metascript_tests::TestListSelfAppendCopiesOriginalValues(context);
    __hidden_metascript_tests::TestAppendSelfMoveCopiesOriginalValue(context);
    __hidden_metascript_tests::TestExponentDoubleLiterals(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

