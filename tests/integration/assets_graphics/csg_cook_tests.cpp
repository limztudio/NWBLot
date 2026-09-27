// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_graphics/csg_shader_variants.h>
#include <impl/assets_graphics/shader_cook_plan.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_csg_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendCsgShapeCookEntry(
    NWB::Impl::AssetsCsgCook::CsgShapeCookEntryVector& outEntries,
    NWB::Impl::AssetsCsgCook::CookArena& arena,
    const Name shapeName,
    const Name shaderModule,
    const AStringView moduleInclude
){
    NWB::Impl::AssetsCsgCook::CsgShapeCookEntry entry(arena);
    entry.shapeName = shapeName;
    entry.shaderModule = shaderModule;
    entry.evalInclude.assign("project/csg/tests/eval.slangi");
    entry.moduleInclude.assign(moduleInclude.data(), moduleInclude.size());
    outEntries.push_back(Move(entry));
}

[[nodiscard]] static NWB::Impl::CsgShapeTypeId FindCookedCsgShapeTypeId(
    const NWB::Impl::AssetsCsgCook::CsgShapeCookEntryVector& entries,
    const Name shapeName
){
    for(const NWB::Impl::AssetsCsgCook::CsgShapeCookEntry& entry : entries){
        if(entry.shapeName == shapeName)
            return entry.shapeTypeId;
    }
    return NWB::Impl::s_InvalidCsgShapeTypeId;
}

static bool CsgCookTestBounds(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    static_cast<void>(shapeToWorld);
    static_cast<void>(parameterBytes);
    static_cast<void>(parameterByteSize);
    outMinBounds = VectorSet(-1.0f, -1.0f, -1.0f, 0.0f);
    outMaxBounds = VectorSet(1.0f, 1.0f, 1.0f, 0.0f);
    outFiniteBounds = true;
    return true;
}

[[nodiscard]] static NWB::Impl::CsgShapeTypeDesc CsgCookTestShapeDesc(
    const Name shapeName,
    const Name shaderModule,
    const AStringView moduleInclude
){
    NWB::Impl::CsgShapeTypeDesc desc;
    desc.name = shapeName;
    desc.shaderModule = shaderModule;
    desc.shaderModuleInclude = ACompactString(moduleInclude);
    desc.boundsCallback = &CsgCookTestBounds;
    return desc;
}

TEST(AssetsGraphics, ShaderPlanMergesEvaluatorDependenciesOnceAndKeepsInheritedDefines){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "evaluator_dependency_plan", root));
    const Path bindRoot = root / "bind";
    const Path csgRoot = root / "csg";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(bindRoot / "unused.slangi", "// Empty bind root.\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "#include \"common.slangi\"\nvoid main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "common.slangi", "static const uint sharedValue = 1u;\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(csgRoot / "first.slangi", "#include \"../common.slangi\"\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(csgRoot / "second.slangi", "#include \"../common.slangi\"\n"));

    namespace Plan = Impl::AssetsGraphicsCookDetail;
    using ShaderCook = Impl::ShaderCook;
    Plan::ResolvedCookPaths paths(testArena.arena);
    paths.repoRoot = root;
    paths.cacheDirectory = root / "cache";
    ShaderCook shaderCook(testArena.arena);
    Plan::IncludeMetadataMap includeMetadata(testArena.arena);
    const auto addDefine = [&](auto& defines, AStringView name, InitializerList<AStringView> values){
        ShaderCook::DefineEntry definition(testArena.arena);
        for(const AStringView value : values)
            definition.values.emplace_back(value, testArena.arena);
        ASSERT_TRUE(defines.emplace(ShaderCook::CookString(name, testArena.arena), Move(definition)).second);
    };
    const auto addIncludeMetadata = [&](const Path& path, AStringView option, InitializerList<AStringView> values){
        ShaderCook::IncludeEntry entry(testArena.arena);
        addDefine(entry.defineValues, option, values);
        addDefine(entry.defineValues, "AUTHORED", { "inherited" });
        auto key = PathToString(testArena.arena, path);
        CanonicalizeTextInPlace(key);
        ASSERT_TRUE(includeMetadata.emplace(Move(key), Move(entry)).second);
    };
    addIncludeMetadata(root / "common.slangi", "SHARED_OPTION", { "0", "1" });
    addIncludeMetadata(csgRoot / "first.slangi", "MODULE_OPTION", { "0", "1" });
    addIncludeMetadata(csgRoot / "second.slangi", "MODULE_OPTION", { "2" });
    ShaderCook::CookVector<Impl::MaterialCookEntry> materials(testArena.arena);
    const auto prepare = [&](const u32 mode, Plan::PreparedShaderPlan& plan){
        Plan::ShaderEntryVector entries(testArena.arena);
        ShaderCook::ShaderEntry entry(testArena.arena);
        entry.name = "project/shaders/dependency_plan";
        entry.source = "shader.slang";
        entry.stage = "ps";
        entry.archiveStage = "ps";
        entry.targetProfile = "spirv_1_5";
        addDefine(entry.defineValues, "AUTHORED", { "authored" });
        if(mode == 1u)
            addDefine(entry.defineValues, Impl::AssetsGraphicsCsgShaderVariants::s_ProjectEvaluatorModuleDefineName, { "\"../common.slangi\"" });
        else if(mode == 2u)
            addDefine(entry.defineValues, Impl::AssetsGraphicsCsgShaderVariants::s_ProjectEvaluatorModuleDefineName, { "\"first.slangi\"", "\"second.slangi\"" });
        entries.push_back(Move(entry));
        const Path emptyPath(testArena.arena);
        return Plan::PrepareShaderEntriesForCook(
            testArena.arena,
            shaderCook,
            paths,
            bindRoot,
            csgRoot,
            emptyPath,
            emptyPath,
            includeMetadata,
            entries,
            materials,
            plan,
            scratchArena
        );
    };

    for(u32 mode = 0u; mode < 3u; ++mode){
        SCOPED_TRACE(mode);
        Plan::PreparedShaderPlan plan(testArena.arena);
        ASSERT_TRUE(prepare(mode, plan));
        ASSERT_EQ(plan.preparedEntries.size(), 1u);
        const auto& prepared = plan.preparedEntries.front();
        ASSERT_EQ(prepared.dependencies.size(), mode == 2u ? 4u : 2u);
        EXPECT_EQ(prepared.dependencies[0], root / "shader.slang");
        EXPECT_EQ(prepared.dependencies[1], root / "common.slangi");
        const auto authored = prepared.entry.defineValues.find(ShaderCook::CookString("AUTHORED", testArena.arena));
        ASSERT_NE(authored, prepared.entry.defineValues.end());
        ASSERT_EQ(authored.value().values.size(), 1u);
        EXPECT_EQ(authored.value().values.front(), "authored");
        EXPECT_EQ(prepared.variantCount, mode == 2u ? 8u : 2u);
        EXPECT_EQ(plan.plannedFileCount, prepared.variantCount + 1u);
        if(mode == 2u){
            EXPECT_EQ(prepared.dependencies[2], csgRoot / "first.slangi");
            EXPECT_EQ(prepared.dependencies[3], csgRoot / "second.slangi");
            const auto inherited = prepared.entry.defineValues.find(ShaderCook::CookString("MODULE_OPTION", testArena.arena));
            ASSERT_NE(inherited, prepared.entry.defineValues.end());
            ASSERT_EQ(inherited.value().values.size(), 2u);
            EXPECT_EQ(inherited.value().values[0], "0");
            EXPECT_EQ(inherited.value().values[1], "1");
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(csgRoot / "second.slangi", "#include \"../common.slangi\"\n// Changed module.\n"));
            Plan::PreparedShaderPlan changedPlan(testArena.arena);
            ASSERT_TRUE(prepare(mode, changedPlan));
            ASSERT_EQ(changedPlan.preparedEntries.size(), 1u);
            EXPECT_NE(changedPlan.preparedEntries.front().dependencyChecksum, prepared.dependencyChecksum);
            EXPECT_EQ(changedPlan.preparedEntries.front().variantCount, prepared.variantCount);
        }
    }
    EXPECT_EQ(logger.errorCount(), 0u);
    ErrorCode error;
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

TEST(AssetsGraphics, CsgShapeCookAndRuntimeUseCanonicalIdsRegardlessOfRegistrationOrder){
    TestArena testArena;
    using namespace NWB::Impl::AssetsCsgCook;

    const Name alphaShape("project/csg/alpha_shape");
    const Name zebraShape("project/csg/zebra_shape");
    const Name alphaModule("project/csg/alpha_module");
    const Name zebraModule("project/csg/zebra_module");
    const AStringView alphaInclude("project/csg/generated/alpha.slangi");
    const AStringView zebraInclude("project/csg/generated/zebra.slangi");

    CsgShapeCookEntryVector cookedEntries(testArena.arena);
    // The cooker normalizes its input by name, while the runtime deliberately receives the inverse order.
    AppendCsgShapeCookEntry(cookedEntries, testArena.arena, zebraShape, zebraModule, zebraInclude);
    AppendCsgShapeCookEntry(cookedEntries, testArena.arena, alphaShape, alphaModule, alphaInclude);
    ASSERT_TRUE(AssignCsgShapeCookIds(cookedEntries));

    const NWB::Impl::CsgShapeTypeId cookedAlphaId = FindCookedCsgShapeTypeId(cookedEntries, alphaShape);
    const NWB::Impl::CsgShapeTypeId cookedZebraId = FindCookedCsgShapeTypeId(cookedEntries, zebraShape);
    ASSERT_NE(cookedAlphaId, NWB::Impl::s_InvalidCsgShapeTypeId);
    ASSERT_NE(cookedZebraId, NWB::Impl::s_InvalidCsgShapeTypeId);
    EXPECT_NE(cookedAlphaId, cookedZebraId);
    EXPECT_EQ(cookedAlphaId, NWB::Impl::CsgShapeTypeIdFromName(alphaShape));
    EXPECT_EQ(cookedZebraId, NWB::Impl::CsgShapeTypeIdFromName(zebraShape));

    NWB::Impl::CsgShapeRegistry registry(testArena.arena);
    NWB::Impl::CsgShapeTypeId runtimeZebraId = NWB::Impl::s_InvalidCsgShapeTypeId;
    NWB::Impl::CsgShapeTypeId runtimeAlphaId = NWB::Impl::s_InvalidCsgShapeTypeId;
    ASSERT_TRUE(registry.registerShapeType(CsgCookTestShapeDesc(zebraShape, zebraModule, zebraInclude), runtimeZebraId));
    ASSERT_TRUE(registry.registerShapeType(CsgCookTestShapeDesc(alphaShape, alphaModule, alphaInclude), runtimeAlphaId));
    EXPECT_EQ(runtimeAlphaId, cookedAlphaId);
    EXPECT_EQ(runtimeZebraId, cookedZebraId);
}

#if defined(NWB_FINAL)
TEST(AssetsGraphics, CsgShapeCookRejectsGeneratedModuleIncludeCollisions){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    using namespace NWB::Impl::AssetsCsgCook;

    CsgShapeCookEntryVector entries(testArena.arena);
    AppendCsgShapeCookEntry(
        entries,
        testArena.arena,
        Name("project/csg/first_shape"),
        Name("project/csg/first_module"),
        "project/csg/generated/shared.slangi"
    );
    AppendCsgShapeCookEntry(
        entries,
        testArena.arena,
        Name("project/csg/second_shape"),
        Name("project/csg/second_module"),
        "project/csg/generated/SHARED.slangi"
    );

    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "csg_module_include_collision", root));

    Path includeRoot(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    EXPECT_FALSE(EmitCsgShapeModuleIncludes(
        root / "cache",
        "tests",
        entries,
        includeRoot,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("generated include")));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

