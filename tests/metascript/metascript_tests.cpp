// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/value.h>

#include <tests/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
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
    NWB_METASCRIPT_TEST_CHECK(context, SourceAllocator::allocationCallCount() == 0u);
    NWB_METASCRIPT_TEST_CHECK(context, DestinationAllocator::allocationCallCount() > 0u);
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
    NWB_METASCRIPT_TEST_CHECK(context, SourceAllocator::allocationCallCount() == 0u);
    NWB_METASCRIPT_TEST_CHECK(context, DestinationAllocator::allocationCallCount() > 0u);

    const Value* copiedList = destination.findField(MStringView("items", 5u));
    NWB_METASCRIPT_TEST_CHECK(context, copiedList != nullptr);
    if(copiedList){
        NWB_METASCRIPT_TEST_CHECK(context, copiedList->isList());
        NWB_METASCRIPT_TEST_CHECK(context, copiedList->asList().size() == 1u);
        if(copiedList->isList() && copiedList->asList().size() == 1u){
            const Value& copiedText = copiedList->asList()[0u];
            NWB_METASCRIPT_TEST_CHECK(context, copiedText.isString());
            NWB_METASCRIPT_TEST_CHECK(context, copiedText.asString() == ViewOf(text));
        }
    }
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

    NWB_METASCRIPT_TEST_CHECK(context, result.isList());
    NWB_METASCRIPT_TEST_CHECK(context, result.asList().size() == 1u);
    if(result.isList() && result.asList().size() == 1u){
        const Value& copiedText = result.asList()[0u];
        NWB_METASCRIPT_TEST_CHECK(context, copiedText.isString());
        NWB_METASCRIPT_TEST_CHECK(context, copiedText.asString() == ViewOf(text));
    }
    NWB_METASCRIPT_TEST_CHECK(context, SourceAllocator::allocationCallCount() == 0u);
    NWB_METASCRIPT_TEST_CHECK(context, DestinationAllocator::allocationCallCount() > 0u);
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
    NWB_METASCRIPT_TEST_CHECK(context, destination.asList().size() == 1u);
    if(destination.asList().size() == 1u){
        const Value& copiedText = destination.asList()[0u];
        NWB_METASCRIPT_TEST_CHECK(context, copiedText.isString());
        NWB_METASCRIPT_TEST_CHECK(context, copiedText.asString() == ViewOf(text));
    }
    NWB_METASCRIPT_TEST_CHECK(context, SourceAllocator::allocationCallCount() == 0u);
    NWB_METASCRIPT_TEST_CHECK(context, DestinationAllocator::allocationCallCount() > 0u);
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
    NWB_METASCRIPT_TEST_CHECK(context, SourceAllocator::allocationCallCount() == 0u);
    NWB_METASCRIPT_TEST_CHECK(context, DestinationAllocator::allocationCallCount() > 0u);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_METASCRIPT_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("metascript", [](NWB::Tests::TestContext& context){
        __hidden_metascript_tests::TestCrossArenaMoveAssignmentCopiesIntoDestinationArena(context);
        __hidden_metascript_tests::TestCrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena(context);
        __hidden_metascript_tests::TestCrossArenaListConcatCopiesIntoResultArena(context);
        __hidden_metascript_tests::TestCrossArenaAppendCopiesIntoDestinationArena(context);
        __hidden_metascript_tests::TestListSelfAppendCopiesOriginalValues(context);
        __hidden_metascript_tests::TestAppendSelfMoveCopiesOriginalValue(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

