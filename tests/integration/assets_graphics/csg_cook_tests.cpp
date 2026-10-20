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


constexpr AStringView s_CacheDirectory = "cache";
constexpr AStringView s_AuthoredDefine = "AUTHORED";
constexpr AStringView s_ModuleOptionDefine = "MODULE_OPTION";
constexpr AStringView s_PixelStage = "ps";
constexpr AStringView s_ModulePath = "project/csg/module";
constexpr AStringView s_GeneratedModulePath = "project/csg/generated/module.slangi";

using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendCsgShapeCookEntry(NWB::Impl::AssetsCsgCook::CsgShapeCookEntryVector& outEntries, NWB::Impl::AssetsCsgCook::CookArena& arena, const Name shapeName, const Name shaderModule, const AStringView moduleInclude){
    NWB::Impl::AssetsCsgCook::CsgShapeCookEntry entry(arena);
    entry.shapeName = shapeName;
    entry.shaderModule = shaderModule;
    entry.evalInclude.assign("project/csg/tests/eval.slangi");
    entry.moduleInclude.assign(moduleInclude.data(), moduleInclude.size());
    outEntries.push_back(Move(entry));
}

[[nodiscard]] static NWB::Impl::CsgShapeTypeId FindCookedCsgShapeTypeId(const NWB::Impl::AssetsCsgCook::CsgShapeCookEntryVector& entries, const Name shapeName)noexcept{
    for(const NWB::Impl::AssetsCsgCook::CsgShapeCookEntry& entry : entries){
        if(entry.shapeName == shapeName)
            return entry.shapeTypeId;
    }
    return NWB::Impl::s_InvalidCsgShapeTypeId;
}

Expected<NWB::Impl::CsgShapeBounds> CsgCookTestBounds(const SIMDMatrix& shapeToWorld, const u8* parameterBytes, const usize parameterByteSize)noexcept{
    static_cast<void>(shapeToWorld);
    static_cast<void>(parameterBytes);
    static_cast<void>(parameterByteSize);
    return NWB::Impl::CsgShapeBounds{ VectorSet(-1.0f, -1.0f, -1.0f, 0.0f), VectorSet(1.0f, 1.0f, 1.0f, 0.0f), true };
}

[[nodiscard]] static NWB::Impl::CsgShapeTypeDesc CsgCookTestShapeDesc(Core::Alloc::GlobalArena& arena, const Name shapeName, const Name shaderModule, const AStringView moduleInclude){
    NWB::Impl::CsgShapeTypeDesc desc(arena);
    desc.name = shapeName;
    desc.shaderModule = shaderModule;
    desc.shaderModuleInclude = moduleInclude;
    desc.boundsCallback = &CsgCookTestBounds;
    return desc;
}

TEST(AssetsGraphics, ShaderPlanMergesEvaluatorDependenciesOnceAndKeepsInheritedDefines){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "evaluator_dependency_plan");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

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
    paths.cacheDirectory = root / s_CacheDirectory;
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
        addDefine(entry.defineValues, s_AuthoredDefine, { "inherited" });
        auto key = PathToString(testArena.arena, path);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(key);
#endif
        ASSERT_TRUE(includeMetadata.emplace(Move(key), Move(entry)).second);
    };
    addIncludeMetadata(root / "common.slangi", "SHARED_OPTION", { "0", "1" });
    addIncludeMetadata(csgRoot / "first.slangi", s_ModuleOptionDefine, { "0", "1" });
    addIncludeMetadata(csgRoot / "second.slangi", s_ModuleOptionDefine, { "2" });
    ShaderCook::CookVector<Impl::MaterialCookEntry> materials(testArena.arena);
    const auto prepare = [&](const u32 mode){
        Plan::ShaderEntryVector entries(testArena.arena);
        ShaderCook::ShaderEntry entry(testArena.arena);
        entry.name = "project/shaders/dependency_plan";
        entry.source = "shader.slang";
        entry.stage = s_PixelStage.data();
        entry.archiveStage = s_PixelStage.data();
        addDefine(entry.defineValues, s_AuthoredDefine, { "authored" });
        if(mode == 1u)
            addDefine(entry.defineValues, Impl::AssetsGraphicsCsgShaderVariants::s_ProjectEvaluatorModuleDefineName, { "\"../common.slangi\"" });
        else if(mode == 2u)
            addDefine(entry.defineValues, Impl::AssetsGraphicsCsgShaderVariants::s_ProjectEvaluatorModuleDefineName, { "\"first.slangi\"", "\"second.slangi\"" });
        entries.push_back(Move(entry));
        const Path emptyPath(testArena.arena);
        return Plan::PrepareShaderEntriesForCook(testArena.arena, shaderCook, paths, bindRoot, csgRoot, emptyPath, emptyPath, includeMetadata, entries, materials, scratchArena);
    };

    for(u32 mode = 0u; mode < 3u; ++mode){
        SCOPED_TRACE(mode);
        auto planResult = prepare(mode);
        ASSERT_TRUE(planResult);
        const Plan::PreparedShaderPlan& plan = *planResult;
        ASSERT_EQ(plan.preparedEntries.size(), 1u);
        const auto& prepared = plan.preparedEntries.front();
        ASSERT_EQ(prepared.dependencies.size(), mode == 2u ? 4u : 2u);
        EXPECT_EQ(prepared.dependencies[0], root / "shader.slang");
        EXPECT_EQ(prepared.dependencies[1], root / "common.slangi");
        const auto authored = prepared.entry.defineValues.find(AStringView(s_AuthoredDefine));
        ASSERT_NE(authored, prepared.entry.defineValues.end());
        ASSERT_EQ(authored.value().values.size(), 1u);
        EXPECT_EQ(authored.value().values.front(), "authored");
        EXPECT_EQ(prepared.variantCount, mode == 2u ? 8u : 2u);
        EXPECT_EQ(plan.plannedFileCount, prepared.variantCount + 1u);
        if(mode == 2u){
            EXPECT_EQ(prepared.dependencies[2], csgRoot / "first.slangi");
            EXPECT_EQ(prepared.dependencies[3], csgRoot / "second.slangi");
            const auto inherited = prepared.entry.defineValues.find(AStringView(s_ModuleOptionDefine));
            ASSERT_NE(inherited, prepared.entry.defineValues.end());
            ASSERT_EQ(inherited.value().values.size(), 2u);
            EXPECT_EQ(inherited.value().values[0], "0");
            EXPECT_EQ(inherited.value().values[1], "1");
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(csgRoot / "second.slangi", "#include \"../common.slangi\"\n// Changed module.\n"));
            const auto changedPlan = prepare(mode);
            ASSERT_TRUE(changedPlan);
            ASSERT_EQ(changedPlan->preparedEntries.size(), 1u);
            EXPECT_NE(changedPlan->preparedEntries.front().dependencyChecksum, prepared.dependencyChecksum);
            EXPECT_EQ(changedPlan->preparedEntries.front().variantCount, prepared.variantCount);
        }
    }
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, CsgShapeCookAndRuntimeUseCanonicalIdsRegardlessOfRegistrationOrder){
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
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
    ASSERT_TRUE(AssignCsgShapeCookIds(cookedEntries, scratchArena));

    const NWB::Impl::CsgShapeTypeId cookedAlphaId = FindCookedCsgShapeTypeId(cookedEntries, alphaShape);
    const NWB::Impl::CsgShapeTypeId cookedZebraId = FindCookedCsgShapeTypeId(cookedEntries, zebraShape);
    ASSERT_NE(cookedAlphaId, NWB::Impl::s_InvalidCsgShapeTypeId);
    ASSERT_NE(cookedZebraId, NWB::Impl::s_InvalidCsgShapeTypeId);
    EXPECT_NE(cookedAlphaId, cookedZebraId);

    NWB::Impl::CsgShapeRegistry registry(testArena.arena);
    const auto runtimeZebraId = registry.registerShapeType(CsgCookTestShapeDesc(testArena.arena, zebraShape, zebraModule, zebraInclude));
    ASSERT_TRUE(runtimeZebraId);
    const auto runtimeAlphaId = registry.registerShapeType(CsgCookTestShapeDesc(testArena.arena, alphaShape, alphaModule, alphaInclude));
    ASSERT_TRUE(runtimeAlphaId);
    EXPECT_EQ(*runtimeAlphaId, cookedAlphaId);
    EXPECT_EQ(*runtimeZebraId, cookedZebraId);
}

TEST(AssetsGraphics, CsgShapeIdsRejectDuplicateCanonicalNamesBeforePublishingTheDuplicate){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    using namespace NWB::Impl::AssetsCsgCook;

    CsgShapeCookEntryVector entries(testArena.arena);
    AppendCsgShapeCookEntry(entries, testArena.arena, Name("project/csg/duplicate"), Name(s_ModulePath), s_GeneratedModulePath);
    AppendCsgShapeCookEntry(entries, testArena.arena, Name("PROJECT/CSG/DUPLICATE"), Name(s_ModulePath), s_GeneratedModulePath);

    EXPECT_FALSE(AssignCsgShapeCookIds(entries, scratchArena));
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_NE(entries[0].shapeTypeId, NWB::Impl::s_InvalidCsgShapeTypeId);
    EXPECT_EQ(entries[1].shapeTypeId, NWB::Impl::s_InvalidCsgShapeTypeId);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("collide on canonical GPU id")));
    EXPECT_EQ(logger.errorCount(), 1u);
}

TEST(AssetsGraphics, CsgMixedCaseModuleIncludeRetainsCookedPathAndRuntimeIdentity){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "csg_mixed_case_module_include");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(R"(csg_shape asset;
asset.shape = "project/csg/mixed_case_shape";
asset.module = "project/csg/mixed_case_module";
asset.module_include = "project/csg/Generated/MixedCaseEvaluator.slangi";
asset.eval = "project/csg/MixedCaseEval.slangi";
)"
    )
    );
    auto cooked = Impl::AssetsCsgCook::ParseCsgShapeCookMetadata(testArena.arena, root / "shape.nwb", document, scratchArena);
    ASSERT_TRUE(cooked);
    const AStringView moduleInclude = "project/csg/Generated/MixedCaseEvaluator.slangi";
    EXPECT_EQ(cooked->moduleInclude, moduleInclude);
    Impl::AssetsCsgCook::CsgShapeCookEntryVector entries(testArena.arena);
    entries.push_back(Move(*cooked));
    ASSERT_TRUE(Impl::AssetsCsgCook::AssignCsgShapeCookIds(entries, scratchArena));
    const auto generatedRoot = Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(root / s_CacheDirectory, "tests", entries, scratchArena);
    ASSERT_TRUE(generatedRoot);
    const auto generatedExists = FileExists(*generatedRoot / moduleInclude);
    ASSERT_TRUE(generatedExists);
    EXPECT_TRUE(*generatedExists);
    Impl::CsgShapeRegistry registry(testArena.arena);
    const auto registered = registry.registerShapeType(CsgCookTestShapeDesc(testArena.arena, entries.front().shapeName, entries.front().shaderModule, entries.front().moduleInclude));
    ASSERT_TRUE(registered);
    const auto selectedInclude = registry.findShaderModuleInclude(entries.front().shaderModule, testArena.arena);
    ASSERT_TRUE(selectedInclude);
    EXPECT_EQ(*selectedInclude, entries.front().moduleInclude);
    EXPECT_EQ(selectedInclude->get_allocator().arenaPtr(), &testArena.arena);
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, CsgRegistryOwnsExactModuleTextAfterSourceRetirementAndReplacement){
    TestArena registryArena;
    Impl::CsgShapeRegistry registry(registryArena.arena);
    const Name shapeName("project/csg/exact_text_shape");
    const Name moduleName("project/csg/exact_text_module");
    const AStringView originalInclude = "project/csg/Generated/LongMixedCaseEvaluatorOwnedByTheRegistry.slangi";
    {
        TestArena sourceArena;
        auto source = CsgCookTestShapeDesc(sourceArena.arena, shapeName, moduleName, originalInclude);
        ASSERT_TRUE(registry.registerShapeType(source));
    }
    auto selected = registry.findShaderModuleInclude(moduleName, registryArena.arena);
    ASSERT_TRUE(selected);
    EXPECT_EQ(*selected, originalInclude);
    for(u32 index = 0u; index < 32u; ++index){
        char indexText[32u] = {};
        const auto derivedNameAt278Col31 = DeriveName(Name("project/csg/exact_text_extra/"), FormatDecimal(index, indexText));
        ASSERT_TRUE(derivedNameAt278Col31);
        const Name extraName = *derivedNameAt278Col31;
        auto extra = CsgCookTestShapeDesc(registryArena.arena, extraName, s_NameNone, AStringView{});
        ASSERT_TRUE(registry.registerShapeType(extra));
    }
    const auto stored = registry.findShapeType(shapeName);
    ASSERT_TRUE(stored);
    EXPECT_EQ(stored->desc.shaderModuleInclude, originalInclude);
    EXPECT_EQ(stored->desc.shaderModuleInclude.get_allocator().arenaPtr(), &registryArena.arena);
    const AStringView replacementInclude = "project/csg/Generated/ReplacementMixedCaseEvaluatorOwnedByTheRegistry.slangi";
    auto replacement = CsgCookTestShapeDesc(registryArena.arena, shapeName, moduleName, replacementInclude);
    ASSERT_TRUE(registry.registerShapeType(replacement, true));
    const auto current = registry.findShaderModuleInclude(moduleName, registryArena.arena);
    ASSERT_TRUE(current);
    EXPECT_EQ(*current, replacementInclude);
    EXPECT_EQ(*selected, originalInclude);
    EXPECT_EQ(stored->desc.shaderModuleInclude, originalInclude);
}

TEST(AssetsGraphics, CsgRegistryRejectsQuotedIncludeControlBytesWithoutReplacingPublishedState){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Impl::CsgShapeRegistry registry(testArena.arena);
    const Name shapeName("project/csg/control_byte_shape");
    const Name moduleName("project/csg/control_byte_module");
    const AStringView originalInclude = "project/csg/Generated/OriginalEvaluator.slangi";
    const auto original = registry.registerShapeType(CsgCookTestShapeDesc(testArena.arena, shapeName, moduleName, originalInclude));
    ASSERT_TRUE(original);
    const u64 revision = registry.revision();
    const usize count = registry.shapeTypeCount();
    for(const char forbidden : Array<char, 6u>{ '\0', '\r', '\n', '"', ';', '=' }){
        auto replacement = CsgCookTestShapeDesc(testArena.arena, shapeName, moduleName, originalInclude);
        replacement.shaderModuleInclude.push_back(forbidden);
        replacement.shaderModuleInclude += "InjectedEvaluator.slangi";
        EXPECT_FALSE(registry.registerShapeType(replacement, true));
        EXPECT_EQ(registry.revision(), revision);
        EXPECT_EQ(registry.shapeTypeCount(), count);
        const auto retainedShape = registry.findShapeType(shapeName);
        ASSERT_TRUE(retainedShape);
        EXPECT_EQ(retainedShape->id, *original);
        EXPECT_EQ(retainedShape->desc.shaderModuleInclude, originalInclude);
        const auto retainedInclude = registry.findShaderModuleInclude(moduleName, testArena.arena);
        ASSERT_TRUE(retainedInclude);
        EXPECT_EQ(*retainedInclude, originalInclude);
    }
    EXPECT_EQ(logger.errorCount(), 6u);
}

#if defined(NWB_FINAL)
TEST(AssetsGraphics, CsgShapeCookRejectsGeneratedModuleIncludeCollisions){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    using namespace NWB::Impl::AssetsCsgCook;

    CsgShapeCookEntryVector entries(testArena.arena);
    AppendCsgShapeCookEntry(entries, testArena.arena, Name("project/csg/first_shape"), Name("project/csg/first_module"), "project/csg/generated/shared.slangi");
    AppendCsgShapeCookEntry(entries, testArena.arena, Name("project/csg/second_shape"), Name("project/csg/second_module"), "project/csg/generated/SHARED.slangi");

    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "csg_module_include_collision");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);


    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    EXPECT_FALSE(EmitCsgShapeModuleIncludes(root / "cache", "tests", entries, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("generated include")));

    EXPECT_TRUE(RemoveAllIfExists(root));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

