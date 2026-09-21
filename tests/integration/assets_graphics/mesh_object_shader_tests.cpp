// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_graphics/mesh_object_shader_plan.h>
#include <impl/assets_material/shader_stage_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_SharedSlangiValueSnippet = "static const uint value = 2u;\n";
constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_object_shader_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
namespace Plan = Impl::AssetsGraphicsCookDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ObjectGeometryMetadataRequiresExplicitNonemptyVertexSourceOnlyForMeshStages){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "object_geometry_metadata", root));
    const Path metadataPath = root / "shader.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    struct MetadataCase{
        AStringView stage;
        AStringView fields;
        bool accepted;
        bool objectGeometry;
    };
    constexpr MetadataCase cases[] = {
        { "mesh", "asset.mesh_object_vertex = \"object_vs.slang\";\n", true, true },
        { "mesh", "", true, false },
        { "ps", "", true, false },
        { "mesh", "asset.mesh_object_vertex = \"\";\n", false, false },
        { "cs", "asset.mesh_object_vertex = \"object_vs.slang\";\n", false, false },
        { "vs", "asset.mesh_object_vertex = \"object_vs.slang\";\n", false, false },
        { "ps", "asset.mesh_object_vertex = \"object_vs.slang\";\n", false, false },
        { "mesh", "asset.mesh_object_cull = \"object_cull_cs.slang\";\n", false, false },
    };
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry entry(testArena.arena);
    for(const MetadataCase& testCase : cases){
        SCOPED_TRACE(testCase.stage);
        SCOPED_TRACE(testCase.fields);
        Impl::ShaderCook::CookString metadata("shader asset;\nasset.stage = \"", testArena.arena);
        metadata.append(testCase.stage.data(), testCase.stage.size());
        metadata.append("\";\nasset.target_profile = \"spirv_1_5\";\nasset.entry_point = \"main\";\n");
        metadata.append(testCase.fields.data(), testCase.fields.size());
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, AStringView(metadata.data(), metadata.size())));
        const u32 priorErrors = logger.errorCount();
        EXPECT_EQ(shaderCook.parseShaderMeta(metadataPath, entry, scratchArena), testCase.accepted);
        if(testCase.accepted){
            EXPECT_EQ(logger.errorCount(), priorErrors);
            EXPECT_EQ(entry.meshObjectVertexSource, testCase.objectGeometry ? "object_vs.slang" : "");
        }
        else{
            EXPECT_GT(logger.errorCount(), priorErrors);
        }
    }
    ErrorCode error;
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

TEST(AssetsGraphics, ObjectGeometryCookPlanRestrictsIdentityAndKeepsAuxiliaryStagesIndependent){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "object_geometry_plan", root));
    const Path meshRoot = root / "impl" / "assets" / "graphics" / "mesh";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(meshRoot / "shared_ms.slang", "void main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(meshRoot / "object_shared.slangi", "static const uint value = 1u;\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(meshRoot / "object_vs.slang", "#include \"object_shared.slangi\"\nvoid main(){}\n"));
    Plan::ResolvedCookPaths paths(testArena.arena);
    paths.repoRoot = root;
    paths.cacheDirectory = root / "cache";
    Impl::ShaderCook shaderCook(testArena.arena);
    Plan::PreparedShaderEntry mesh(testArena.arena);
    mesh.entry.name = "engine/graphics/mesh/shared_ms";
    mesh.entry.stage = "mesh";
    mesh.entry.archiveStage = "mesh";
    mesh.entry.targetProfile = "spirv_1_6";
    mesh.entry.entryPoint = "authoredEntry";
    mesh.entry.meshObjectVertexSource = "object_vs.slang";
    mesh.entry.includeRoots.emplace_back("engine/graphics", testArena.arena);
    mesh.entry.implicitDefines.emplace(
        Impl::ShaderCook::CookString("NWB_CSG_ENABLED", testArena.arena), Impl::ShaderCook::CookString("1", testArena.arena)
    );
    Impl::ShaderCook::DefineEntry authoredDefine(testArena.arena);
    authoredDefine.values.emplace_back("0", testArena.arena);
    authoredDefine.values.emplace_back("1", testArena.arena);
    mesh.entry.defineValues.emplace(Impl::ShaderCook::CookString("AUTHORED_OPTION", testArena.arena), Move(authoredDefine));
    mesh.sourcePath = meshRoot / "shared_ms.slang";
    mesh.includeDirectories.push_back(meshRoot);
    mesh.variantCount = s_ExpectedDualCount;
    mesh.usesMaterialTypedBinding = true;
    mesh.supportsCsgClipVariant = true;
    Plan::PreparedShaderPlan plan(testArena.arena);
    plan.plannedFileCount = mesh.variantCount;
    ASSERT_TRUE(Plan::AppendMeshObjectShaderEntries(testArena.arena, shaderCook, paths, mesh, plan, scratchArena));
    ASSERT_EQ(plan.preparedEntries.size(), 1u);
    EXPECT_EQ(plan.plannedFileCount, 3u);
    constexpr AStringView stages[] = { "vs" };
    const AStringView archiveStages[] = { Impl::MaterialShaderStageNames::MeshObjectVertexArchiveStageText() };
    const Path expectedSources[] = { meshRoot / "object_vs.slang" };
    for(u32 index = 0u; index < 1u; ++index){
        const auto& auxiliary = plan.preparedEntries[index];
        EXPECT_EQ(auxiliary.entry.name, mesh.entry.name);
        EXPECT_EQ(auxiliary.entry.stage.view(), stages[index]);
        EXPECT_EQ(auxiliary.entry.archiveStage.view(), archiveStages[index]);
        EXPECT_EQ(auxiliary.entry.targetProfile.view(), "spirv_1_5");
        EXPECT_EQ(auxiliary.entry.entryPoint, "main");
        EXPECT_EQ(auxiliary.sourcePath.lexically_normal(), expectedSources[index].lexically_normal());
        EXPECT_EQ(auxiliary.includeDirectories, mesh.includeDirectories);
        EXPECT_EQ(auxiliary.variantCount, 1u);
        EXPECT_TRUE(auxiliary.entry.defineValues.empty());
        EXPECT_TRUE(auxiliary.entry.implicitDefines.empty());
        Impl::ShaderCook::CookVector<Impl::ShaderCook::DefineCombo> combinations(testArena.arena);
        ASSERT_TRUE(shaderCook.expandDefineCombinations(auxiliary.entry.defineValues, combinations, scratchArena));
        ASSERT_EQ(combinations.size(), 1u);
        EXPECT_EQ(shaderCook.buildVariantName(combinations[0], scratchArena), Core::ShaderArchive::s_DefaultVariant);
        EXPECT_TRUE(auxiliary.entry.meshObjectVertexSource.empty());
        EXPECT_FALSE(auxiliary.entry.emitMeshComputeShadow);
        EXPECT_FALSE(auxiliary.usesMaterialTypedBinding);
        EXPECT_FALSE(auxiliary.supportsCsgClipVariant);
        EXPECT_FALSE(auxiliary.supportsAvboitCsgClipVariant);
        EXPECT_NE(auxiliary.dependencyChecksum, 0u);
        EXPECT_GE(auxiliary.dependencies.size(), s_ExpectedDualCount);
    }
    EXPECT_EQ(logger.errorCount(), 0u);
    for(u32 mismatch = 0u; mismatch < 6u; ++mismatch){
        SCOPED_TRACE(mismatch);
        Plan::PreparedShaderEntry rejected = mesh;
        switch(mismatch){
        case 0u: rejected.entry.name = "project/mesh/shared_ms"; break;
        case 1u: rejected.sourcePath = root / "project" / "shared_ms.slang"; break;
        case s_ExpectedDualCount: rejected.entry.archiveStage = "mesh_compute"; break;
        case 3u: rejected.entry.stage = "cs"; break;
        case 4u: rejected.entry.meshObjectVertexSource = "custom_vs.slang"; break;
        case 5u: rejected.entry.emitMeshComputeShadow = false; break;
        }
        Plan::PreparedShaderPlan rejectedPlan(testArena.arena);
        rejectedPlan.plannedFileCount = 7u;
        EXPECT_FALSE(Plan::AppendMeshObjectShaderEntries(testArena.arena, shaderCook, paths, rejected, rejectedPlan, scratchArena));
        EXPECT_TRUE(rejectedPlan.preparedEntries.empty());
        EXPECT_EQ(rejectedPlan.plannedFileCount, 7u);
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("require the fixed engine shared mesh program")));
    Plan::PreparedShaderEntry legacy(testArena.arena);
    legacy.entry.name = "project/custom_mesh";
    Plan::PreparedShaderPlan legacyPlan(testArena.arena);
    legacyPlan.plannedFileCount = 3u;
    EXPECT_TRUE(Plan::AppendMeshObjectShaderEntries(testArena.arena, shaderCook, paths, legacy, legacyPlan, scratchArena));
    EXPECT_TRUE(legacyPlan.preparedEntries.empty());
    EXPECT_EQ(legacyPlan.plannedFileCount, 3u);
    const u64 oldChecksum = plan.preparedEntries[0].dependencyChecksum;
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(meshRoot / "object_shared.slangi", s_SharedSlangiValueSnippet));
    Plan::PreparedShaderPlan changedPlan(testArena.arena);
    ASSERT_TRUE(Plan::AppendMeshObjectShaderEntries(testArena.arena, shaderCook, paths, mesh, changedPlan, scratchArena));
    ASSERT_EQ(changedPlan.preparedEntries.size(), 1u);
    EXPECT_NE(changedPlan.preparedEntries[0].dependencyChecksum, oldChecksum);
    EXPECT_EQ(changedPlan.preparedEntries[0].variantCount, 1u);
    ErrorCode error;
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

