// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_model_fixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLB_FINAL)
static constexpr AStringView s_MODEL_FIXTURE_NWB = "model_fixture.nwb";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ModelFixtureMeshMeta =
R"(mesh mesh;

mesh.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];
mesh.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];
mesh.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];
mesh.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];
mesh.colors = [
    [1.0, 1.0, 1.0, 1.0],
];
mesh.vertex_refs = [
    [0, 0, 0, 0, 0],
    [1, 1, 1, 1, 0],
    [2, 2, 2, 2, 0],
];
mesh.indices = [
    [0, 1, 2],
];

)";
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ModelBunchRejectsFourRowTransform){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    AString meta;
    meta.reserve(2048u);
    AssetsGraphicsFixture::AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AssetsGraphicsFixture::AppendTestMeta(meta, R"(model model;

model.static_meshes = {
    "tool": {
        "mesh": mesh,
        "transform": [
            [1, 0, 0, 0.5],
            [0, 1, 0, 0.125],
            [0, 0, 1, -0.25],
            [0, 0, 0, 1],
        ],
    },
};

asset_bunch bunch = [
    mesh,
    model,
];
)");

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookSingleGraphicsMeta(
        AStringView(meta.data(), meta.size()),
        "model_bunch_four_row_transform",
        "characters",
        s_MODEL_FIXTURE_NWB.data(),
        testArena,
        root,
        outputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("must be a 3x4 affine matrix")));
#endif
}

#if defined(GLB_FINAL)
static bool ExpandModelBunchFixture(
    TestArena& testArena,
    const AStringView meta,
    const AStringView caseName,
    NWB::Core::Metascript::Document& doc,
    NWB::Core::Assets::ExpandedAssetMetadataVector& outAssets,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    if(!doc.parse(meta))
        return false;

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, caseName) / "assets";
    const Path nwbFilePath = assetRoot / "characters" / s_MODEL_FIXTURE_NWB;
    return NWB::Core::Assets::AssetsBunchCook::ExpandAssetBunch(
        assetRoot,
        "project",
        nwbFilePath,
        doc,
        outAssets,
        scratchArena
    );
}
#endif

TEST(AssetsGraphics, ModelBunchRejectsDuplicateLocalReference){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    AString meta;
    meta.reserve(2048u);
    AssetsGraphicsFixture::AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AssetsGraphicsFixture::AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": {
        "skeleton": "project/characters/shared/skeleton",
    },
};

asset_bunch bunch = [
    mesh,
    mesh,
    model,
];
)");

    TestArena testArena;
    NWB::Core::Metascript::Document doc(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ModelFixtureScratchArena);
    NWB::Core::Assets::ExpandedAssetMetadataVector expandedAssets(scratchArena);
    const bool expanded = ExpandModelBunchFixture(
        testArena,
        AStringView(meta.data(), meta.size()),
        "model_bunch_duplicate_local_reference",
        doc,
        expandedAssets,
        scratchArena
    );
    EXPECT_FALSE(expanded);
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("variable 'mesh' is listed more than once")));
#else
#endif
}

TEST(AssetsGraphics, ModelBunchRejectsMissingLocalReference){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    AString meta;
    meta.reserve(2048u);
    AssetsGraphicsFixture::AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AssetsGraphicsFixture::AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": {
        "skeleton": missing_skeleton,
    },
};

asset_bunch bunch = [
    mesh,
    model,
];
)");

    TestArena testArena;
    NWB::Core::Metascript::Document doc(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ModelFixtureScratchArena);
    NWB::Core::Assets::ExpandedAssetMetadataVector expandedAssets(scratchArena);
    const bool expanded = ExpandModelBunchFixture(
        testArena,
        AStringView(meta.data(), meta.size()),
        "model_bunch_missing_local_reference",
        doc,
        expandedAssets,
        scratchArena
    );
    EXPECT_FALSE(expanded);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

