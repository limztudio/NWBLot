// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_STRUCT_NWB_DUP = "struct NwbDup{\n";
static constexpr AStringView s_FLOAT_VALUE_LINE = "    float value;\n";
static constexpr AStringView s_NWB_DUP_RUNTIME = "NwbDup runtime;\n";
static constexpr AStringView s_CLOSE_BRACE = "};\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Document = NWB::Core::Metascript::Document;
using Value = NWB::Core::Metascript::Value;
using MStringView = NWB::Core::Metascript::MStringView;
using AString = NWB::Tests::TestAString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceArenaTag{};
struct DestinationArenaTag{};

using SourceArena = NWB::Tests::TestArena<SourceArenaTag>;
using DestinationArena = NWB::Tests::TestArena<DestinationArenaTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static MStringView ViewOf(const AString& text){
    return MStringView(text.data(), text.size());
}

template<usize N>
[[nodiscard]] static MStringView LiteralView(const char (&text)[N]){
    return MStringView(text, N > 0u ? N - 1u : 0u);
}

[[nodiscard]] static bool ParseImplicitMaterialBind(Document& document, const AString& source){
    return document.parseWithImplicitAsset(ViewOf(source), LiteralView("material_bind"), LiteralView("asset"));
}

static void CheckStringListElement(const Value& value, const AString& expected){
    ASSERT_TRUE(value.isString());
    EXPECT_EQ(value.asString(), ViewOf(expected));
}

static void CheckImplicitMaterialBindParseFailsWithMessage(const AString& source, MStringView expectedMessage){
    DestinationArena arena;
    Document document(arena.arena);

    const bool parsed = ParseImplicitMaterialBind(document, source);
    EXPECT_FALSE(parsed);
    ASSERT_TRUE(document.hasErrors());
    ASSERT_FALSE(document.errors().empty());

    const auto& error = document.errors()[0u];
    const MStringView message(error.message.data(), error.message.size());
    EXPECT_EQ(message, expectedMessage);
}

static void CheckSingleStringListValue(const Value& value, const AString& text){
    ASSERT_TRUE(value.isList());
    ASSERT_EQ(value.asList().size(), 1u);
    CheckStringListElement(value.asList()[0u], text);
}

template<typename ArenaT>
static void MakeSingleStringList(Value& list, ArenaT& arena, const AString& text){
    list.makeList();
    list.append(Value(ViewOf(text), arena.arena));
}

TEST(Metascript, CrossArenaMoveAssignmentCopiesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'm');

    Value source(ViewOf(text), sourceArena.arena);
    Value destination(destinationArena.arena);

    destination = Move(source);

    EXPECT_TRUE(source.isNull());
    EXPECT_TRUE(destination.isString());
    EXPECT_EQ(destination.asString(), ViewOf(text));
}

TEST(Metascript, CrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'c');

    Value source(sourceArena.arena);
    source.makeMap();
    Value& list = source.field(MStringView("items", 5u));
    MakeSingleStringList(list, sourceArena, text);

    Value destination(destinationArena.arena);

    destination = source;

    EXPECT_TRUE(source.isMap());
    EXPECT_TRUE(destination.isMap());

    const Value* copiedList = destination.findField(MStringView("items", 5u));
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

    Value source(ViewOf(text), sourceArena.arena);
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

    ASSERT_EQ(list.asList().size(), s_ExpectedDualCount);
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
    ASSERT_EQ(list.asList().size(), s_ExpectedDualCount);
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
    ASSERT_EQ(list.asList().size(), s_ExpectedDualCount);
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
    ASSERT_EQ(list.asList().size(), s_ExpectedDualCount);
    CheckStringListElement(list.asList()[0u], text);
    CheckStringListElement(list.asList()[1u], text);
}

TEST(Metascript, AppendsFirstMiddleAndLastElementsAcrossReallocation){
    constexpr usize s_InitialCount = 8u;
    const usize sourceIndices[] = { 0u, 3u, 7u };
    for(const usize sourceIndex : sourceIndices){
        for(const bool moveSource : { false, true }){
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
    constexpr usize s_InitialCount = 8u;
    const usize sourceIndices[] = { 0u, 3u, 7u };
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

TEST(Metascript, LargeListAppendPreservesValues){
    constexpr usize s_ValueCount = 8192u;
    DestinationArena arena;
    Value list(arena.arena);
    list.makeList();
    const Timer appendBegin = TimerNow();
    for(usize index = 0u; index < s_ValueCount; ++index){
        Value value(static_cast<i64>(index), arena.arena);
        if((index & 1u) == 0u)
            list.append(Move(value));
        else
            list += value;
    }
    const u64 appendNanoseconds = DurationInNS<u64>(TimerNow(), appendBegin);
    char durationText[32u] = {};
    RecordProperty("list_append_ns", FormatDecimal(appendNanoseconds, durationText).data());
    ASSERT_EQ(list.asList().size(), s_ValueCount);
    for(usize index = 0u; index < s_ValueCount; ++index)
        EXPECT_EQ(list.asList()[index].asInteger(), static_cast<i64>(index));
}

TEST(Metascript, ParsesLargeNumericList){
    constexpr usize s_ValueCount = 8192u;
    DestinationArena arena;
    AString source("number asset; asset.values = [");
    source.reserve(source.size() + s_ValueCount * s_ExpectedDualCount + s_ExpectedDualCount);
    for(usize index = 0u; index < s_ValueCount; ++index)
        source += "1,";
    source += "];";

    Document document(arena.arena);
    const Timer parseBegin = TimerNow();
    const bool parsed = document.parse(ViewOf(source));
    const u64 parseNanoseconds = DurationInNS<u64>(TimerNow(), parseBegin);
    char durationText[32u] = {};
    RecordProperty("list_parse_ns", FormatDecimal(parseNanoseconds, durationText).data());
    ASSERT_TRUE(parsed);
    const Value* const values = document.asset().findField(LiteralView("values"));
    ASSERT_NE(values, nullptr);
    ASSERT_TRUE(values->isList());
    ASSERT_EQ(values->asList().size(), s_ValueCount);
    for(const Value& value : values->asList())
        EXPECT_EQ(value.asInteger(), 1);
}

TEST(Metascript, BindStyleStructDuplicateRejections){
    const AString duplicateFieldSource = AString(s_STRUCT_NWB_DUP) + s_FLOAT_VALUE_LINE.data()
        + s_FLOAT_VALUE_LINE.data() + s_CLOSE_BRACE.data();
    CheckImplicitMaterialBindParseFailsWithMessage(duplicateFieldSource, LiteralView("duplicate struct field declaration"));

    const AString duplicateInstanceSource = AString(s_STRUCT_NWB_DUP) + s_FLOAT_VALUE_LINE.data()
        + s_CLOSE_BRACE.data() + s_NWB_DUP_RUNTIME.data() + s_NWB_DUP_RUNTIME.data();
    CheckImplicitMaterialBindParseFailsWithMessage(duplicateInstanceSource, LiteralView("duplicate struct instance declaration"));

    const AString existingInstanceSource = AString("asset.instances = [{ \"type\": \"NwbDup\", \"name\": \"runtime\" }];\n")
        + s_STRUCT_NWB_DUP.data() + s_FLOAT_VALUE_LINE.data() + s_CLOSE_BRACE.data() + s_NWB_DUP_RUNTIME.data();
    CheckImplicitMaterialBindParseFailsWithMessage(existingInstanceSource, LiteralView("duplicate struct instance declaration"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

