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
using AString = NWB::Tests::TestAString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceArenaTag{};
struct DestinationArenaTag{};

using SourceArena = NWB::Tests::TestArena<SourceArenaTag>;
using DestinationArena = NWB::Tests::TestArena<DestinationArenaTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static MStringView ViewOf(const AString& text)noexcept{
    return MStringView(text.data(), text.size());
}

template<usize N>
[[nodiscard]] static MStringView LiteralView(const char (&text)[N])noexcept{
    return MStringView(text, N > 0u ? N - 1u : 0u);
}

static void CheckStringListElement(const Value& value, const AString& expected){
    ASSERT_TRUE(value.isString());
    EXPECT_EQ(value.asString(), ViewOf(expected));
}

static void CheckParseFailsWithMessage(const AString& source, MStringView expectedMessage){
    DestinationArena arena;
    Document document(arena.arena);

    const bool parsed = document.parse(ViewOf(source));
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
    EXPECT_EQ(destination.asString(), ViewOf(text));
}

TEST(Metascript, CrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'c');

    Value source(sourceArena.arena);
    source.makeMap();
    Value& list = source.field(MStringView(s_ItemsField.data(), 5u));
    MakeSingleStringList(list, sourceArena, text);

    Value destination(destinationArena.arena);

    destination = source;

    EXPECT_TRUE(source.isMap());
    const Value* copiedList = destination.findField(MStringView(s_ItemsField.data(), 5u));
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

TEST(Metascript, NamedLookupDoesNotSelectAnAssetOrDependOnDeclarationOrder){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("number first = 9; texture asset; asset.source = \"albedo.png\"; number last = 2;")));
    ASSERT_EQ(document.declarations().size(), 3u);
    EXPECT_EQ(document.declarations()[0u].variable, "first");

    const Document& constDocument = document;
    const Value* first = constDocument.findField("first");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->asInteger(), 9);
    const auto* declaration = constDocument.findDeclaration("asset");
    ASSERT_NE(declaration, nullptr);
    EXPECT_EQ(declaration->type, "texture");
    EXPECT_EQ(declaration->variable, "asset");
    const Value* asset = constDocument.findField("asset");
    ASSERT_NE(asset, nullptr);
    ASSERT_TRUE(asset->isMap());
    const Value* source = asset->findField("source");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->asString(), "albedo.png");
    EXPECT_EQ(constDocument.findField("missing"), nullptr);
    EXPECT_EQ(document.findField("missing"), nullptr);
    EXPECT_EQ(constDocument.findDeclaration("missing"), nullptr);

    Value* last = document.findField("last");
    ASSERT_NE(last, nullptr);
    last->setInteger(5);
    EXPECT_EQ(constDocument.findField("last")->asInteger(), 5);
    EXPECT_EQ(document.findDeclaration("first")->type, "number");
}

TEST(Metascript, EmptyAndStructOnlyDocumentsHaveNoImplicitVariables){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_TRUE(document.structs().empty());
    EXPECT_EQ(document.findField("asset"), nullptr);

    ASSERT_TRUE(document.parse(LiteralView("[custom(\"argument\")] struct Description{ [field] float value; };")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_EQ(document.findField("asset"), nullptr);
    EXPECT_EQ(document.findField("Description"), nullptr);
    ASSERT_EQ(document.structs().size(), 1u);
    const auto structure = document.structs().find(LiteralView("Description"));
    ASSERT_NE(structure, document.structs().end());
    const Value* attributes = structure.value().findField("attributes");
    ASSERT_NE(attributes, nullptr);
    ASSERT_TRUE(attributes->isList());
    ASSERT_EQ(attributes->asList().size(), 1u);
    const Value* attributeName = attributes->asList()[0u].findField("name");
    ASSERT_NE(attributeName, nullptr);
    EXPECT_EQ(attributeName->asString(), "custom");
    const Value* arguments = attributes->asList()[0u].findField("arguments");
    ASSERT_NE(arguments, nullptr);
    ASSERT_EQ(arguments->asList().size(), 1u);
    EXPECT_EQ(arguments->asList()[0u].asString(), "argument");
    const Value* fields = structure.value().findField("fields");
    ASSERT_NE(fields, nullptr);
    ASSERT_EQ(fields->asList().size(), 1u);
    EXPECT_EQ(fields->asList()[0u].findField("type")->asString(), "float");
    EXPECT_EQ(fields->asList()[0u].findField("name")->asString(), "value");
    const Value* fieldAttributes = fields->asList()[0u].findField("attributes");
    ASSERT_NE(fieldAttributes, nullptr);
    ASSERT_EQ(fieldAttributes->asList().size(), 1u);
    EXPECT_EQ(fieldAttributes->asList()[0u].findField("name")->asString(), "field");
}

TEST(Metascript, StructNamesAndVariablesUseSeparateNamespaces){
    DestinationArena arena;
    Document document(arena.arena);
    ASSERT_TRUE(document.parse(LiteralView("number Parameters = 3; struct Parameters{ float value; }; Parameters runtime; runtime.value = 4;")));
    ASSERT_EQ(document.structs().size(), 1u);
    ASSERT_EQ(document.declarations().size(), 2u);
    EXPECT_EQ(document.findField("Parameters")->asInteger(), 3);
    const auto* declaration = document.findDeclaration("runtime");
    ASSERT_NE(declaration, nullptr);
    EXPECT_EQ(declaration->type, "Parameters");
    EXPECT_EQ(declaration->variable, "runtime");
    const Value* runtime = document.findField("runtime");
    ASSERT_NE(runtime, nullptr);
    ASSERT_TRUE(runtime->isMap());
    EXPECT_EQ(runtime->findField("value")->asInteger(), 4);
    EXPECT_EQ(document.findField("asset"), nullptr);
    EXPECT_EQ(document.findField("structs"), nullptr);
    EXPECT_EQ(document.findField("instances"), nullptr);
}

TEST(Metascript, StructMetadataOwnsSourceAndSurvivesVariableStorageGrowth){
    DestinationArena arena;
    Document document(arena.arena);
    {
        AString source("struct Description{ float value; }; Description runtime;\n");
        for(usize index = 0u; index < 128u; ++index){
            char indexText[TextDetail::s_DecimalTextBufferBytes] = {};
            const AStringView indexView = FormatDecimal(index, indexText);
            source.append("float extra");
            source.append(indexView.data(), indexView.size());
            source.append(";\n");
        }
        ASSERT_TRUE(document.parse(ViewOf(source)));
    }
    ASSERT_EQ(document.declarations().size(), 129u);
    const auto structure = document.structs().find(LiteralView("Description"));
    ASSERT_NE(structure, document.structs().end());
    const Value* fields = structure.value().findField("fields");
    ASSERT_NE(fields, nullptr);
    ASSERT_TRUE(fields->isList());
    ASSERT_EQ(fields->asList().size(), 1u);
    const Value* fieldName = fields->asList()[0u].findField(MStringView(s_NameField.data(), s_NameField.size()));
    ASSERT_NE(fieldName, nullptr);
    EXPECT_EQ(fieldName->asString(), "value");
    EXPECT_EQ(document.findDeclaration("runtime")->type, "Description");
    ASSERT_NE(document.findField("runtime"), nullptr);
    EXPECT_TRUE(document.findField("runtime")->isNull());
}

TEST(Metascript, StructAndVariableDuplicatesAreRejectedInTheirOwnNamespaces){
    CheckParseFailsWithMessage(AString("struct Duplicate{ float value; float value; };"), "duplicate struct field declaration");
    CheckParseFailsWithMessage(AString("struct Duplicate{}; struct Duplicate{};"), "duplicate struct declaration");
    CheckParseFailsWithMessage(AString("struct Description{}; Description runtime; Description runtime;"), "duplicate variable declaration");
    CheckParseFailsWithMessage(AString("number value; text value;"), "duplicate variable declaration");
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
    EXPECT_EQ(document.findField("previous"), nullptr);
    EXPECT_EQ(document.findField("value"), nullptr);
    EXPECT_EQ(document.findField("current")->asString(), "new");
    ASSERT_TRUE(document.parse(LiteralView("")));
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_EQ(document.findField("current"), nullptr);
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
        EXPECT_EQ(MStringView(document.errors()[0u].message.data(), document.errors()[0u].message.size()), testCase.message);
        const Value* asset = document.findField(s_AssetVariable);
        ASSERT_NE(asset, nullptr);
        ASSERT_TRUE(asset->isInteger());
        EXPECT_EQ(asset->asInteger(), testCase.retainedValue);
        EXPECT_EQ(document.findField("missing"), nullptr);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

