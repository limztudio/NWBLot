// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <tests/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using Document = NWB::Core::Metascript::Document;
using Value = NWB::Core::Metascript::Value;
using MStringView = NWB::Core::Metascript::MStringView;
using AString = NWB::Tests::TestAString;


#define NWB_METASCRIPT_TEST_CHECK NWB_TEST_CHECK


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

static void CheckStringValue(TestContext& context, const Value* value, MStringView expected){
    NWB_METASCRIPT_TEST_CHECK(context, value != nullptr);
    if(!value)
        return;

    NWB_METASCRIPT_TEST_CHECK(context, value->isString());
    if(value->isString())
        NWB_METASCRIPT_TEST_CHECK(context, value->asString() == expected);
}

static void CheckReferenceListElement(TestContext& context, const Value& value, MStringView expected){
    NWB_METASCRIPT_TEST_CHECK(context, value.isReference());
    if(value.isReference())
        NWB_METASCRIPT_TEST_CHECK(context, value.asReference() == expected);
}

static void CheckStringListElement(TestContext& context, const Value& value, const AString& expected){
    NWB_METASCRIPT_TEST_CHECK(context, value.isString());
    if(value.isString())
        NWB_METASCRIPT_TEST_CHECK(context, value.asString() == ViewOf(expected));
}

static void CheckStringField(TestContext& context, const Value& value, MStringView fieldName, MStringView expected){
    CheckStringValue(context, FindTestField(value, fieldName), expected);
}

static void CheckImplicitMaterialBindParseFailsWithMessage(TestContext& context, const AString& source, MStringView expectedMessage){
    DestinationArena arena;
    Document document(arena.arena);

    const bool parsed = ParseImplicitMaterialBind(document, source);
    NWB_METASCRIPT_TEST_CHECK(context, !parsed);
    NWB_METASCRIPT_TEST_CHECK(context, document.hasErrors());
    if(document.errors().empty())
        return;

    const auto& error = document.errors()[0u];
    const MStringView message(error.message.data(), error.message.size());
    NWB_METASCRIPT_TEST_CHECK(context, message == expectedMessage);
}

static void CheckSingleStringListValue(TestContext& context, const Value& value, const AString& text){
    NWB_METASCRIPT_TEST_CHECK(context, value.isList());
    NWB_METASCRIPT_TEST_CHECK(context, value.isList() && value.asList().size() == 1u);
    if(value.isList() && value.asList().size() == 1u)
        CheckStringListElement(context, value.asList()[0u], text);
}

template<typename ArenaT>
static void MakeSingleStringList(Value& list, ArenaT& arena, const AString& text){
    list.makeList();
    list.append(Value(ViewOf(text), arena.arena));
}

static void TestCrossArenaMoveAssignmentCopiesIntoDestinationArena(TestContext& context){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'm');

    Value source(ViewOf(text), sourceArena.arena);
    Value destination(destinationArena.arena);

    destination = Move(source);

    NWB_METASCRIPT_TEST_CHECK(context, source.isNull());
    NWB_METASCRIPT_TEST_CHECK(context, destination.isString());
    NWB_METASCRIPT_TEST_CHECK(context, destination.asString() == ViewOf(text));
}

static void TestCrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena(TestContext& context){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'c');

    Value source(sourceArena.arena);
    source.makeMap();
    Value& list = source.field(MStringView("items", 5u));
    MakeSingleStringList(list, sourceArena, text);

    Value destination(destinationArena.arena);

    destination = source;

    NWB_METASCRIPT_TEST_CHECK(context, source.isMap());
    NWB_METASCRIPT_TEST_CHECK(context, destination.isMap());

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
    MakeSingleStringList(source, sourceArena, text);

    Value destination(destinationArena.arena);
    destination.makeList();

    Value result = destination + source;

    CheckSingleStringListValue(context, result, text);
}

static void TestCrossArenaAppendCopiesIntoDestinationArena(TestContext& context){
    SourceArena sourceArena;
    DestinationArena destinationArena;
    const AString text(128u, 'a');

    Value source(ViewOf(text), sourceArena.arena);
    Value destination(destinationArena.arena);
    destination.makeList();

    destination.append(Move(source));

    NWB_METASCRIPT_TEST_CHECK(context, source.isNull());
    CheckSingleStringListValue(context, destination, text);
}

static void TestListSelfAppendCopiesOriginalValues(TestContext& context){
    DestinationArena arena;
    const AString text(128u, 's');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list += list;

    NWB_METASCRIPT_TEST_CHECK(context, list.asList().size() == 2u);
    if(list.asList().size() == 2u){
        CheckStringListElement(context, list.asList()[0u], text);
        CheckStringListElement(context, list.asList()[1u], text);
    }
}

static void TestAppendSelfMoveCopiesOriginalValue(TestContext& context){
    DestinationArena arena;
    const AString text(128u, 'v');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list.append(Move(list));

    NWB_METASCRIPT_TEST_CHECK(context, list.isList());
    NWB_METASCRIPT_TEST_CHECK(context, list.asList().size() == 2u);
    if(list.isList() && list.asList().size() == 2u){
        CheckStringListElement(context, list.asList()[0u], text);
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[1u].isList());
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[1u].asList().size() == 1u);
        if(list.asList()[1u].isList() && list.asList()[1u].asList().size() == 1u){
            const Value& nestedText = list.asList()[1u].asList()[0u];
            CheckStringListElement(context, nestedText, text);
        }
    }
}

static void TestAppendExistingListElementMoveCopiesBeforeDestroy(TestContext& context){
    DestinationArena arena;
    const AString text(128u, 'e');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list.append(Move(list.asList()[0u]));

    NWB_METASCRIPT_TEST_CHECK(context, list.isList());
    NWB_METASCRIPT_TEST_CHECK(context, list.asList().size() == 2u);
    if(list.isList() && list.asList().size() == 2u){
        NWB_METASCRIPT_TEST_CHECK(context, list.asList()[0u].isNull());
        CheckStringListElement(context, list.asList()[1u], text);
    }
}

static void TestListAppendExistingElementCopiesBeforeReallocation(TestContext& context){
    DestinationArena arena;
    const AString text(128u, 'r');

    Value list(arena.arena);
    MakeSingleStringList(list, arena, text);

    list += list.asList()[0u];

    NWB_METASCRIPT_TEST_CHECK(context, list.isList());
    NWB_METASCRIPT_TEST_CHECK(context, list.asList().size() == 2u);
    if(list.isList() && list.asList().size() == 2u){
        CheckStringListElement(context, list.asList()[0u], text);
        CheckStringListElement(context, list.asList()[1u], text);
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

static void TestBindStyleStructDeclarations(TestContext& context){
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
    NWB_METASCRIPT_TEST_CHECK(context, parsed);
    if(!parsed)
        return;

    NWB_METASCRIPT_TEST_CHECK(context, document.assetType() == LiteralView("material_bind"));
    NWB_METASCRIPT_TEST_CHECK(context, document.assetVariable() == LiteralView("asset"));

    const Value& asset = document.asset();
    const Value* structs = FindTestField(asset, LiteralView("structs"));
    NWB_METASCRIPT_TEST_CHECK(context, structs != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, structs && structs->isMap());
    if(!structs || !structs->isMap())
        return;

    const Value* surfaceStruct = structs->findField(LiteralView("NwbProjectBxdfSurfaceMaterial"));
    const Value* runtimeStruct = structs->findField(LiteralView("NwbProjectBxdfRuntimeMaterial"));
    NWB_METASCRIPT_TEST_CHECK(context, surfaceStruct != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, runtimeStruct != nullptr);
    if(!surfaceStruct || !runtimeStruct)
        return;

    const Value* surfaceAttributes = FindTestField(*surfaceStruct, LiteralView("attributes"));
    NWB_METASCRIPT_TEST_CHECK(context, surfaceAttributes != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, surfaceAttributes && surfaceAttributes->isList());
    NWB_METASCRIPT_TEST_CHECK(context, surfaceAttributes && surfaceAttributes->isList() && surfaceAttributes->asList().size() == 1u);
    if(surfaceAttributes && surfaceAttributes->isList() && surfaceAttributes->asList().size() == 1u)
        CheckStringField(context, surfaceAttributes->asList()[0u], LiteralView("name"), LiteralView("material_constant"));

    const Value* surfaceFields = FindTestField(*surfaceStruct, LiteralView("fields"));
    NWB_METASCRIPT_TEST_CHECK(context, surfaceFields != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, surfaceFields && surfaceFields->isList());
    NWB_METASCRIPT_TEST_CHECK(context, surfaceFields && surfaceFields->isList() && surfaceFields->asList().size() == 2u);
    if(!surfaceFields || !surfaceFields->isList() || surfaceFields->asList().size() != 2u)
        return;

    const Value& baseColorField = surfaceFields->asList()[0u];
    CheckStringField(context, baseColorField, LiteralView("type"), LiteralView("float4"));
    CheckStringField(context, baseColorField, LiteralView("name"), LiteralView("base_color"));

    const Value* baseColorAttributes = FindTestField(baseColorField, LiteralView("attributes"));
    NWB_METASCRIPT_TEST_CHECK(context, baseColorAttributes != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, baseColorAttributes && baseColorAttributes->isList());
    NWB_METASCRIPT_TEST_CHECK(context, baseColorAttributes && baseColorAttributes->isList() && baseColorAttributes->asList().size() == 1u);
    if(baseColorAttributes && baseColorAttributes->isList() && baseColorAttributes->asList().size() == 1u){
        const Value& defaultAttribute = baseColorAttributes->asList()[0u];
        CheckStringField(context, defaultAttribute, LiteralView("name"), LiteralView("default"));

        const Value* arguments = FindTestField(defaultAttribute, LiteralView("arguments"));
        NWB_METASCRIPT_TEST_CHECK(context, arguments != nullptr);
        NWB_METASCRIPT_TEST_CHECK(context, arguments && arguments->isList());
        NWB_METASCRIPT_TEST_CHECK(context, arguments && arguments->isList() && arguments->asList().size() == 1u);
        if(arguments && arguments->isList() && arguments->asList().size() == 1u)
            CheckStringValue(context, &arguments->asList()[0u], LiteralView("float4(1.0, 1.0, 1.0, 1.0)"));
    }

    const Value* instances = FindTestField(asset, LiteralView("instances"));
    NWB_METASCRIPT_TEST_CHECK(context, instances != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, instances && instances->isList());
    NWB_METASCRIPT_TEST_CHECK(context, instances && instances->isList() && instances->asList().size() == 2u);
    if(!instances || !instances->isList() || instances->asList().size() != 2u)
        return;

    CheckStringField(context, instances->asList()[0u], LiteralView("type"), LiteralView("NwbProjectBxdfSurfaceMaterial"));
    CheckStringField(context, instances->asList()[0u], LiteralView("name"), LiteralView("surface"));
    CheckStringField(context, instances->asList()[1u], LiteralView("type"), LiteralView("NwbProjectBxdfRuntimeMaterial"));
    CheckStringField(context, instances->asList()[1u], LiteralView("name"), LiteralView("runtime"));
}

static void TestBindStyleStructDuplicateRejections(TestContext& context){
    const AString duplicateFieldSource =
        "struct NwbDup{\n"
        "    float value;\n"
        "    float value;\n"
        "};\n"
    ;
    CheckImplicitMaterialBindParseFailsWithMessage(context, duplicateFieldSource, LiteralView("duplicate struct field declaration"));

    const AString duplicateInstanceSource =
        "struct NwbDup{\n"
        "    float value;\n"
        "};\n"
        "NwbDup runtime;\n"
        "NwbDup runtime;\n"
    ;
    CheckImplicitMaterialBindParseFailsWithMessage(context, duplicateInstanceSource, LiteralView("duplicate struct instance declaration"));

    const AString existingInstanceSource =
        "asset.instances = [{ \"type\": \"NwbDup\", \"name\": \"runtime\" }];\n"
        "struct NwbDup{\n"
        "    float value;\n"
        "};\n"
        "NwbDup runtime;\n"
    ;
    CheckImplicitMaterialBindParseFailsWithMessage(context, existingInstanceSource, LiteralView("duplicate struct instance declaration"));
}

static void TestGenericDeclarationsAndReferences(TestContext& context){
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
    NWB_METASCRIPT_TEST_CHECK(context, parsed);
    if(!parsed)
        return;

    NWB_METASCRIPT_TEST_CHECK(context, document.declarations().size() == 3u);
    if(document.declarations().size() != 3u)
        return;

    NWB_METASCRIPT_TEST_CHECK(context, document.declarations()[0u].type == LiteralView("model"));
    NWB_METASCRIPT_TEST_CHECK(context, document.declarations()[0u].variable == LiteralView("model"));
    NWB_METASCRIPT_TEST_CHECK(context, document.declarations()[1u].type == LiteralView("mesh"));
    NWB_METASCRIPT_TEST_CHECK(context, document.declarations()[1u].variable == LiteralView("mesh"));
    NWB_METASCRIPT_TEST_CHECK(context, document.declarations()[2u].type == LiteralView("asset_bunch"));
    NWB_METASCRIPT_TEST_CHECK(context, document.declarations()[2u].variable == LiteralView("bunch"));

    const Value* bunch = document.findVariable(LiteralView("bunch"));
    NWB_METASCRIPT_TEST_CHECK(context, bunch != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, bunch && bunch->isList());
    if(!bunch || !bunch->isList() || bunch->asList().size() != 2u)
        return;

    CheckReferenceListElement(context, bunch->asList()[0u], LiteralView("model"));
    CheckReferenceListElement(context, bunch->asList()[1u], LiteralView("mesh"));

    const Value* modelValue = document.findVariable(LiteralView("model"));
    NWB_METASCRIPT_TEST_CHECK(context, modelValue != nullptr);
    if(modelValue)
        CheckStringField(context, *modelValue, LiteralView("mesh"), LiteralView("project/body/mesh"));

    const Value* meshValue = document.findVariable(LiteralView("mesh"));
    NWB_METASCRIPT_TEST_CHECK(context, meshValue != nullptr);
    if(!meshValue)
        return;

    const Value* indices = FindTestField(*meshValue, LiteralView("indices"));
    NWB_METASCRIPT_TEST_CHECK(context, indices != nullptr);
    NWB_METASCRIPT_TEST_CHECK(context, indices && indices->isList());
    NWB_METASCRIPT_TEST_CHECK(context, indices && indices->isList() && indices->asList().size() == 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_METASCRIPT_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("metascript", [](NWB::Tests::TestContext& context){
    __hidden_tests::TestCrossArenaMoveAssignmentCopiesIntoDestinationArena(context);
    __hidden_tests::TestCrossArenaCopyAssignmentCopiesNestedValuesIntoDestinationArena(context);
    __hidden_tests::TestCrossArenaListConcatCopiesIntoResultArena(context);
    __hidden_tests::TestCrossArenaAppendCopiesIntoDestinationArena(context);
    __hidden_tests::TestListSelfAppendCopiesOriginalValues(context);
    __hidden_tests::TestAppendSelfMoveCopiesOriginalValue(context);
    __hidden_tests::TestAppendExistingListElementMoveCopiesBeforeDestroy(context);
    __hidden_tests::TestListAppendExistingElementCopiesBeforeReallocation(context);
    __hidden_tests::TestExponentDoubleLiterals(context);
    __hidden_tests::TestBindStyleStructDeclarations(context);
    __hidden_tests::TestBindStyleStructDuplicateRejections(context);
    __hidden_tests::TestGenericDeclarationsAndReferences(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

