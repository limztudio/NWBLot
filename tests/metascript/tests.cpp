// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <tests/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_tests{


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

[[nodiscard]] static const Value* FindTestField(const Value& value, MStringView name){
    if(!value.isMap())
        return nullptr;
    return value.findField(name);
}

static void CheckStringValue(const Value* value, MStringView expected){
    ASSERT_NE(value, nullptr);
    ASSERT_TRUE(value->isString());
    EXPECT_EQ(value->asString(), expected);
}

static void CheckReferenceListElement(const Value& value, MStringView expected){
    ASSERT_TRUE(value.isReference());
    EXPECT_EQ(value.asReference(), expected);
}

static void CheckStringListElement(const Value& value, const AString& expected){
    ASSERT_TRUE(value.isString());
    EXPECT_EQ(value.asString(), ViewOf(expected));
}

static void CheckStringField(const Value& value, MStringView fieldName, MStringView expected){
    CheckStringValue(FindTestField(value, fieldName), expected);
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

TEST(Metascript, ExponentDoubleLiterals){
    DestinationArena arena;
    Document document(arena.arena);
    const AString source =
        "number asset;\n"
        "asset.values = [2.89785676e-05, -3.66009772E-07, 1e+03, 2E2];\n"
    ;

    ASSERT_TRUE(document.parse(ViewOf(source)));
    const Value* values = document.asset().findField(MStringView("values", 6u));
    ASSERT_NE(values, nullptr);
    ASSERT_TRUE(values->isList());

    const auto& list = values->asList();
    ASSERT_EQ(list.size(), 4u);

    EXPECT_TRUE(list[0u].isDouble());
    EXPECT_TRUE(list[1u].isDouble());
    EXPECT_TRUE(list[2u].isDouble());
    EXPECT_TRUE(list[3u].isDouble());
    EXPECT_LT(Abs(list[0u].asDouble() - 0.0000289785676), 0.0000000001);
    EXPECT_LT(Abs(list[1u].asDouble() + 0.000000366009772), 0.0000000001);
    EXPECT_LT(Abs(list[2u].asDouble() - 1000.0), 0.0000000001);
    EXPECT_LT(Abs(list[3u].asDouble() - 200.0), 0.0000000001);
}

TEST(Metascript, BindStyleStructDeclarations){
    DestinationArena arena;
    Document document(arena.arena);
    const AString source =
        "[material_constant]\n"
        "struct NwbProjectBxdfSurfaceMaterial{\n"
        "    [default(\"float4(1.0, 1.0, 1.0, 1.0)\")]\n"
        "    float4 base_color;\n"
        "\n"
        "    [default(\"float(0.5)\")]\n"
        "    float roughness;\n"
        "};\n"
        "\n"
        "[material_mutable]\n"
        "struct NwbProjectBxdfRuntimeMaterial{\n"
        "    [default(\"float(1.0)\")]\n"
        "    float fade_alpha;\n"
        "};\n"
        "\n"
        "NwbProjectBxdfSurfaceMaterial surface;\n"
        "NwbProjectBxdfRuntimeMaterial runtime;\n"
    ;

    const bool parsed = ParseImplicitMaterialBind(document, source);
    ASSERT_TRUE(parsed);

    EXPECT_EQ(document.assetType(), LiteralView("material_bind"));
    EXPECT_EQ(document.assetVariable(), LiteralView("asset"));

    const Value& asset = document.asset();
    const Value* structs = FindTestField(asset, LiteralView("structs"));
    ASSERT_NE(structs, nullptr);
    ASSERT_TRUE(structs->isMap());

    const Value* surfaceStruct = structs->findField(LiteralView("NwbProjectBxdfSurfaceMaterial"));
    const Value* runtimeStruct = structs->findField(LiteralView("NwbProjectBxdfRuntimeMaterial"));
    ASSERT_NE(surfaceStruct, nullptr);
    ASSERT_NE(runtimeStruct, nullptr);

    const Value* surfaceAttributes = FindTestField(*surfaceStruct, LiteralView("attributes"));
    ASSERT_NE(surfaceAttributes, nullptr);
    ASSERT_TRUE(surfaceAttributes->isList());
    ASSERT_EQ(surfaceAttributes->asList().size(), 1u);
    CheckStringField(surfaceAttributes->asList()[0u], LiteralView("name"), LiteralView("material_constant"));

    const Value* surfaceFields = FindTestField(*surfaceStruct, LiteralView("fields"));
    ASSERT_NE(surfaceFields, nullptr);
    ASSERT_TRUE(surfaceFields->isList());
    ASSERT_EQ(surfaceFields->asList().size(), 2u);

    const Value& baseColorField = surfaceFields->asList()[0u];
    CheckStringField(baseColorField, LiteralView("type"), LiteralView("float4"));
    CheckStringField(baseColorField, LiteralView("name"), LiteralView("base_color"));

    const Value* baseColorAttributes = FindTestField(baseColorField, LiteralView("attributes"));
    ASSERT_NE(baseColorAttributes, nullptr);
    ASSERT_TRUE(baseColorAttributes->isList());
    ASSERT_EQ(baseColorAttributes->asList().size(), 1u);
    const Value& defaultAttribute = baseColorAttributes->asList()[0u];
    CheckStringField(defaultAttribute, LiteralView("name"), LiteralView("default"));

    const Value* arguments = FindTestField(defaultAttribute, LiteralView("arguments"));
    ASSERT_NE(arguments, nullptr);
    ASSERT_TRUE(arguments->isList());
    ASSERT_EQ(arguments->asList().size(), 1u);
    CheckStringValue(&arguments->asList()[0u], LiteralView("float4(1.0, 1.0, 1.0, 1.0)"));

    const Value* instances = FindTestField(asset, LiteralView("instances"));
    ASSERT_NE(instances, nullptr);
    ASSERT_TRUE(instances->isList());
    ASSERT_EQ(instances->asList().size(), 2u);

    CheckStringField(instances->asList()[0u], LiteralView("type"), LiteralView("NwbProjectBxdfSurfaceMaterial"));
    CheckStringField(instances->asList()[0u], LiteralView("name"), LiteralView("surface"));
    CheckStringField(instances->asList()[1u], LiteralView("type"), LiteralView("NwbProjectBxdfRuntimeMaterial"));
    CheckStringField(instances->asList()[1u], LiteralView("name"), LiteralView("runtime"));
}

TEST(Metascript, BindStyleStructDuplicateRejections){
    const AString duplicateFieldSource =
        "struct NwbDup{\n"
        "    float value;\n"
        "    float value;\n"
        "};\n"
    ;
    CheckImplicitMaterialBindParseFailsWithMessage(duplicateFieldSource, LiteralView("duplicate struct field declaration"));

    const AString duplicateInstanceSource =
        "struct NwbDup{\n"
        "    float value;\n"
        "};\n"
        "NwbDup runtime;\n"
        "NwbDup runtime;\n"
    ;
    CheckImplicitMaterialBindParseFailsWithMessage(duplicateInstanceSource, LiteralView("duplicate struct instance declaration"));

    const AString existingInstanceSource =
        "asset.instances = [{ \"type\": \"NwbDup\", \"name\": \"runtime\" }];\n"
        "struct NwbDup{\n"
        "    float value;\n"
        "};\n"
        "NwbDup runtime;\n"
    ;
    CheckImplicitMaterialBindParseFailsWithMessage(existingInstanceSource, LiteralView("duplicate struct instance declaration"));
}

TEST(Metascript, GenericDeclarationsAndReferences){
    DestinationArena arena;
    Document document(arena.arena);
    const AString source =
        "model model;\n"
        "model.mesh = \"project/body/mesh\";\n"
        "\n"
        "mesh mesh;\n"
        "mesh.indices = [0, 1, 2];\n"
        "\n"
        "asset_bunch bunch = [\n"
        "    model,\n"
        "    mesh,\n"
        "];\n"
    ;

    const bool parsed = document.parse(ViewOf(source));
    ASSERT_TRUE(parsed);

    ASSERT_EQ(document.declarations().size(), 3u);

    EXPECT_EQ(document.declarations()[0u].type, LiteralView("model"));
    EXPECT_EQ(document.declarations()[0u].variable, LiteralView("model"));
    EXPECT_EQ(document.declarations()[1u].type, LiteralView("mesh"));
    EXPECT_EQ(document.declarations()[1u].variable, LiteralView("mesh"));
    EXPECT_EQ(document.declarations()[2u].type, LiteralView("asset_bunch"));
    EXPECT_EQ(document.declarations()[2u].variable, LiteralView("bunch"));

    const Value* bunch = document.findVariable(LiteralView("bunch"));
    ASSERT_NE(bunch, nullptr);
    ASSERT_TRUE(bunch->isList());
    ASSERT_EQ(bunch->asList().size(), 2u);

    CheckReferenceListElement(bunch->asList()[0u], LiteralView("model"));
    CheckReferenceListElement(bunch->asList()[1u], LiteralView("mesh"));

    const Value* modelValue = document.findVariable(LiteralView("model"));
    ASSERT_NE(modelValue, nullptr);
    CheckStringField(*modelValue, LiteralView("mesh"), LiteralView("project/body/mesh"));

    const Value* meshValue = document.findVariable(LiteralView("mesh"));
    ASSERT_NE(meshValue, nullptr);

    const Value* indices = FindTestField(*meshValue, LiteralView("indices"));
    ASSERT_NE(indices, nullptr);
    ASSERT_TRUE(indices->isList());
    EXPECT_EQ(indices->asList().size(), 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

