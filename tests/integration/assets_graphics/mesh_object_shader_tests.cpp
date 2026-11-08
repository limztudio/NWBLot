// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <tests/integration/assets_graphics/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_object_shader_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ObjectGeometryMetadataRejectsRetiredVertexSourceSelectors){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "object_geometry_metadata");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path metadataPath = root / "shader.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    constexpr Array<AssetsGraphicsFixture::ShaderMetadataCase, 8u> s_Cases = {{
        { .shaderAssetType = "mesh_shader", .fields = "", .accepted = true },
        { .shaderAssetType = "pixel_shader", .fields = "", .accepted = true },
        { .shaderAssetType = "mesh_shader", .fields = "asset.mesh_object_vertex = \"object_vs.slang\";\n", .accepted = false },
        { .shaderAssetType = "mesh_shader", .fields = "asset.mesh_object_vertex = \"\";\n", .accepted = false },
        { .shaderAssetType = "compute_shader", .fields = "asset.mesh_object_vertex = \"object_vs.slang\";\n", .accepted = false },
        { .shaderAssetType = "vertex_shader", .fields = "asset.mesh_object_vertex = \"object_vs.slang\";\n", .accepted = false },
        { .shaderAssetType = "pixel_shader", .fields = "asset.mesh_object_vertex = \"object_vs.slang\";\n", .accepted = false },
        { .shaderAssetType = "mesh_shader", .fields = "asset.mesh_object_cull = \"object_cull_cs.slang\";\n", .accepted = false },
    }};
    Impl::ShaderCook shaderCook(testArena.arena);
    for(const AssetsGraphicsFixture::ShaderMetadataCase& testCase : s_Cases){
        SCOPED_TRACE(testCase.shaderAssetType);
        SCOPED_TRACE(testCase.fields);
        Impl::ShaderCook::CookString metadata(testCase.shaderAssetType, testArena.arena);
        metadata.append(" asset;\nasset.entry_point = \"main\";\n");
        metadata.append(testCase.fields.data(), testCase.fields.size());
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, AStringView(metadata.data(), metadata.size())));
        const u32 priorErrors = logger.errorCount();
        EXPECT_EQ(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook).has_value(), testCase.accepted);
        if(testCase.accepted)
            EXPECT_EQ(logger.errorCount(), priorErrors);
        else
            EXPECT_GT(logger.errorCount(), priorErrors);
    }
    EXPECT_TRUE(RemoveAllIfExists(root));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

