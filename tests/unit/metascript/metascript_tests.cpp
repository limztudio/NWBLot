// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <global/text_utils.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_AssetVariable = "asset";
constexpr AStringView s_ItemsField = "items";
constexpr AStringView s_NameField = "name";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Document = NWB::Core::Metascript::Document;
using Value = NWB::Core::Metascript::Value;
using MStringView = NWB::Core::Metascript::MStringView;
using NWB::Core::Metascript::LiteralView;
using AString = NWB::Tests::TestAString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceArenaTag{};
struct DestinationArenaTag{};

using SourceArena = NWB::Tests::TestArena<SourceArenaTag>;
using DestinationArena = NWB::Tests::TestArena<DestinationArenaTag>;

static void CheckStringListElement(const Value& value, const MStringView expected){
    ASSERT_TRUE(value.isString());
    EXPECT_EQ(value.asString(), expected);
}

static void CheckParseFailsWithMessage(const MStringView source, MStringView expectedMessage){
    DestinationArena arena;
    Document document(arena.arena);

    const b8 parsed = document.parse(source);
    EXPECT_FALSE(parsed);
    ASSERT_TRUE(document.hasErrors());
    ASSERT_FALSE(document.errors().empty());

    const auto& error = document.errors()[0u];
    EXPECT_EQ(MStringView(error.message), expectedMessage);
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

TEST(Metascript, EmptyAndStructOnlyDocumentsHaveNoImplicitVariables){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_TRUE(document.structs().empty());
    EXPECT_EQ(document.findVariable(s_AssetVariable), nullptr);

    ASSERT_TRUE(document.parse(LiteralView("[custom(\"argument\")] struct Description{ [field] float value; };")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_EQ(document.findVariable(s_AssetVariable), nullptr);
    EXPECT_EQ(document.findVariable("Description"), nullptr);
    ASSERT_EQ(document.structs().size(), 1u);
    EXPECT_NE(document.structs().find(LiteralView("Description")), document.structs().end());
}

TEST(Metascript, StructNamesAndVariablesUseSeparateNamespaces){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("number Parameters = 3; struct Parameters{ float value; }; Parameters runtime; runtime.value = 4;")));
    ASSERT_EQ(document.structs().size(), 1u);
    ASSERT_EQ(document.declarations().size(), 2u);
    EXPECT_EQ(document.findVariable("Parameters")->asInteger(), 3);
    const auto* declaration = document.findDeclaration("runtime");
    ASSERT_NE(declaration, nullptr);
    EXPECT_EQ(declaration->type, "Parameters");
    EXPECT_EQ(declaration->variable, "runtime");
    const Value* runtime = document.findVariable("runtime");
    ASSERT_NE(runtime, nullptr);
    ASSERT_TRUE(runtime->isMap());
    EXPECT_EQ(runtime->findField("value")->asInteger(), 4);
    EXPECT_EQ(document.findVariable(s_AssetVariable), nullptr);
    EXPECT_EQ(document.findVariable("structs"), nullptr);
    EXPECT_EQ(document.findVariable("instances"), nullptr);
}

TEST(Metascript, StructMetadataOwnsSourceAndSurvivesVariableStorageGrowth){
    DestinationArena arena;
    Document document(arena.arena);
    {
        AString source("struct Description{ float value; }; Description runtime;\n");
        for(usize index = 0u; index < 128u; ++index){
            char indexText[s_DecimalTextBufferBytes] = {};
            const AStringView indexView = FormatDecimal(index, indexText);
            source.append("float extra");
            source.append(indexView.data(), indexView.size());
            source.append(";\n");
        }
        ASSERT_TRUE(document.parse(source));
    }
    ASSERT_EQ(document.declarations().size(), 129u);
    const auto structure = document.structs().find(LiteralView("Description"));
    ASSERT_NE(structure, document.structs().end());
    const Value* fields = structure.value().findField("fields");
    ASSERT_NE(fields, nullptr);
    ASSERT_TRUE(fields->isList());
    ASSERT_EQ(fields->asList().size(), 1u);
    const Value* fieldName = fields->asList()[0u].findField(s_NameField);
    ASSERT_NE(fieldName, nullptr);
    EXPECT_EQ(fieldName->asString(), "value");
    EXPECT_EQ(document.findDeclaration("runtime")->type, "Description");
    ASSERT_NE(document.findVariable("runtime"), nullptr);
    EXPECT_TRUE(document.findVariable("runtime")->isNull());
}

TEST(Metascript, StructAndVariableDuplicatesAreRejectedInTheirOwnNamespaces){
    CheckParseFailsWithMessage("struct Duplicate{ float value; float value; };", "duplicate struct field declaration");
    CheckParseFailsWithMessage("struct Duplicate{}; struct Duplicate{};", "duplicate struct declaration");
    CheckParseFailsWithMessage("struct Description{}; Description runtime; Description runtime;", "duplicate variable declaration");
    CheckParseFailsWithMessage("number value; text value;", "duplicate variable declaration");
}

TEST(Metascript, ReparsingClearsStructsVariablesDeclarationsAndErrors){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("struct Old{ float value; }; Old previous;")));
    ASSERT_FALSE(document.parse(LiteralView("struct Partial{}; number value; number value;")));
    ASSERT_TRUE(document.hasErrors());
    ASSERT_TRUE(document.parse(LiteralView("text current = \"new\";")));
    EXPECT_FALSE(document.hasErrors());
    EXPECT_TRUE(document.structs().empty());
    ASSERT_EQ(document.declarations().size(), 1u);
    EXPECT_EQ(document.findDeclaration("previous"), nullptr);
    EXPECT_EQ(document.findVariable("previous"), nullptr);
    EXPECT_EQ(document.findVariable("value"), nullptr);
    EXPECT_EQ(document.findVariable("current")->asString(), "new");
    ASSERT_TRUE(document.parse(LiteralView("")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_EQ(document.findVariable("current"), nullptr);
}

TEST(Metascript, InvalidAssignmentTargetsPreserveDiagnosticsAndDeclaredValue){
    struct InvalidTargetCase{
        MStringView source;
        MStringView message;
        i64 retainedValue;
    };
    const InvalidTargetCase cases[] = {
        { "float asset = 7;\nmissing.value = 2;", "assignments must target a declared variable", 7 },
        { "float asset = 9;\nasset.value = 2;", "cannot access field on non-map value", 9 },
    };
    for(const auto& testCase : cases){
        SCOPED_TRACE(testCase.message.data());
        DestinationArena arena;
        Document document(arena.arena);
        EXPECT_FALSE(document.parse(testCase.source));
        ASSERT_EQ(document.errors().size(), 1u);
        EXPECT_EQ(document.errors()[0u].line, 2u);
        EXPECT_EQ(document.errors()[0u].column, 1u);
        EXPECT_EQ(MStringView(document.errors()[0u].message), testCase.message);
        const Value* asset = document.findVariable(s_AssetVariable);
        ASSERT_NE(asset, nullptr);
        ASSERT_TRUE(asset->isInteger());
        EXPECT_EQ(asset->asInteger(), testCase.retainedValue);
        EXPECT_EQ(document.findVariable("missing"), nullptr);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

