// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <tests/common/test_context.h>
#include <tests/unit/metascript/generated/compact_string.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_value_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ItemsField = NWB::Tests::s_MetascriptValueItemsField;
using Value = NWB::Core::Metascript::Value;
using MStringView = NWB::Core::Metascript::MStringView;
using AString = NWB::Tests::TestAString;
struct SourceArenaTag{};
struct DestinationArenaTag{};
using SourceArena = NWB::Tests::TestArena<SourceArenaTag>;
using DestinationArena = NWB::Tests::TestArena<DestinationArenaTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CheckStringListElement(const Value& value, const MStringView expected){
    ASSERT_TRUE(value.isString());
    EXPECT_EQ(value.asString(), expected);
}

static void CheckSingleStringListValue(const Value& value, const MStringView text){
    ASSERT_TRUE(value.isList());
    ASSERT_EQ(value.asList().size(), 1u);
    CheckStringListElement(value.asList()[0u], text);
}

template<typename ArenaT>
static void MakeSingleStringList(Value& list, ArenaT& arena, const MStringView text){
    list.makeList();
    list.append(Value(text, arena.arena));
}

TEST(Metascript, CrossArenaMoveAssignmentCopiesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'm');

    Value source(text, sourceArena.arena);
    Value destination(destinationArena.arena);

    destination = Move(source);

    EXPECT_TRUE(source.isNull());
    EXPECT_EQ(destination.asString(), text);
}

TEST(Metascript, CrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'c');

    Value source(sourceArena.arena);
    source.makeMap();
    Value& list = source.field(s_ItemsField);
    MakeSingleStringList(list, sourceArena, text);

    Value destination(destinationArena.arena);

    destination = source;

    EXPECT_TRUE(source.isMap());
    const Value* copiedList = destination.findField(s_ItemsField);
    ASSERT_NE(copiedList, nullptr);
    CheckSingleStringListValue(*copiedList, text);
}

TEST(Metascript, CrossArenaListConcatCopiesIntoResultArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'p');

    Value source(sourceArena.arena);
    MakeSingleStringList(source, sourceArena, text);

    Value destination(destinationArena.arena);
    destination.makeList();

    Value result = destination + source;

    CheckSingleStringListValue(result, text);
}

TEST(Metascript, CrossArenaAppendCopiesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'a');

    Value source(text, sourceArena.arena);
    Value destination(destinationArena.arena);
    destination.makeList();

    destination.append(Move(source));

    EXPECT_TRUE(source.isNull());
    CheckSingleStringListValue(destination, text);
}

TEST(Metascript, ListSelfAppendCopiesOriginalValues){
    DestinationArena arena;
    const AString text(128u, 's');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list += list;

    ASSERT_EQ(list.asList().size(), 2u);
    CheckStringListElement(list.asList()[0u], text);
    CheckStringListElement(list.asList()[1u], text);
}

TEST(Metascript, AppendSelfMoveCopiesOriginalValue){
    DestinationArena arena;
    const AString text(128u, 'v');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list.append(Move(list));

    ASSERT_TRUE(list.isList());
    ASSERT_EQ(list.asList().size(), 2u);
    CheckStringListElement(list.asList()[0u], text);
    ASSERT_TRUE(list.asList()[1u].isList());
    ASSERT_EQ(list.asList()[1u].asList().size(), 1u);
    const Value& nestedText = list.asList()[1u].asList()[0u];
    CheckStringListElement(nestedText, text);
}

TEST(Metascript, AppendExistingListElementMoveCopiesBeforeDestroy){
    DestinationArena arena;
    const AString text(128u, 'e');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list.append(Move(list.asList()[0u]));

    ASSERT_TRUE(list.isList());
    ASSERT_EQ(list.asList().size(), 2u);
    EXPECT_TRUE(list.asList()[0u].isNull());
    CheckStringListElement(list.asList()[1u], text);
}

TEST(Metascript, ListAppendExistingElementCopiesBeforeReallocation){
    DestinationArena arena;
    const AString text(128u, 'r');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list += list.asList()[0u];

    ASSERT_TRUE(list.isList());
    ASSERT_EQ(list.asList().size(), 2u);
    CheckStringListElement(list.asList()[0u], text);
    CheckStringListElement(list.asList()[1u], text);
}

TEST(Metascript, AppendsFirstMiddleAndLastElementsAcrossReallocation){
    constexpr usize s_InitialCount = NWB::Tests::s_MetascriptValueInitialCount;
    const Array<usize, 3u> sourceIndices = { 0u, 3u, 7u };
    for(const usize sourceIndex : sourceIndices){
        for(const b8 moveSource : { false, true }){
            DestinationArena arena;
            Value list(arena.arena);
            list.makeList();
            list.asList().reserve(s_InitialCount);
            const usize originalCount = list.asList().capacity();
            for(usize index = 0u; index < originalCount; ++index)
                list.append(Value(static_cast<i64>(index), arena.arena));

            if(moveSource)
                list.append(Move(list.asList()[sourceIndex]));
            else
                list += list.asList()[sourceIndex];

            ASSERT_EQ(list.asList().size(), originalCount + 1u);
            EXPECT_EQ(list.asList().back().asInteger(), static_cast<i64>(sourceIndex));
            for(usize index = 0u; index < originalCount; ++index){
                if(moveSource && index == sourceIndex)
                    EXPECT_TRUE(list.asList()[index].isNull());
                else
                    EXPECT_EQ(list.asList()[index].asInteger(), static_cast<i64>(index));
            }
        }
    }
}

TEST(Metascript, ConcatenatesNestedListElementsAcrossReallocation){
    constexpr usize s_InitialCount = NWB::Tests::s_MetascriptValueInitialCount;
    const Array<usize, 3u> sourceIndices = { 0u, 3u, 7u };
    const AString text(128u, 'n');
    for(const usize sourceIndex : sourceIndices){
        DestinationArena arena;
        Value list(arena.arena);
        list.makeList();
        list.asList().reserve(s_InitialCount);
        const usize originalCount = list.asList().capacity();
        for(usize index = 0u; index < originalCount; ++index)
            list.append(Value(static_cast<i64>(index), arena.arena));
        MakeSingleStringList(list.asList()[sourceIndex], arena, text);

        list += list.asList()[sourceIndex];

        ASSERT_EQ(list.asList().size(), originalCount + 1u);
        EXPECT_GT(list.asList().capacity(), originalCount);
        CheckStringListElement(list.asList().back(), text);
        for(usize index = 0u; index < originalCount; ++index){
            if(index == sourceIndex)
                CheckSingleStringListValue(list.asList()[index], text);
            else
                EXPECT_EQ(list.asList()[index].asInteger(), static_cast<i64>(index));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

