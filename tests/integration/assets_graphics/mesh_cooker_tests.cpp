// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_mesh_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using CookSingleMetaFn = AssetsGraphicsFixture::CookSingleMetaFn;
using MinimalAssetKind = AssetsGraphicsFixture::MinimalAssetKind;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
static void ExpectCookFailure(
    TestArena& testArena,
    const CookSingleMetaFn cookSingleMeta,
    const AStringView metaText,
    const AStringView caseName
){
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_FALSE(cookSingleMeta(
        metaText,
        caseName,
        testArena,
        root,
        outputDirectory
    ));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

static void ExpectCookFailure(
    TestArena& testArena,
    const CookSingleMetaFn cookSingleMeta,
    const AString& metaText,
    const AStringView caseName
){
    ExpectCookFailure(testArena, cookSingleMeta, AStringView(metaText.data(), metaText.size()), caseName);
}
#endif

TEST(AssetsGraphics, MeshCookerTypedStreams){
    AssetsGraphicsFixture::CookAndCheckMinimalTypedAsset<NWB::Impl::Mesh>(
        AssetsGraphicsFixture::s_MinimalMeshMeta,
        "minimal_mesh",
        MinimalAssetKind::Mesh,
        [&](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.meshClass(), NWB::Core::Mesh::MeshClass::Static);
            EXPECT_EQ(loadedMesh.positionStream().size(), 3u);
            AssetsGraphicsFixture::CheckMinimalRuntimeMeshletPayload(loadedMesh);
            EXPECT_EQ(loadedMesh.positionStream()[0].x, -0.5f);
            EXPECT_EQ(LoadHalf4U(loadedMesh.normalStream()[0]).z, 1.f);
            EXPECT_EQ(LoadHalf4U(loadedMesh.colorStream()[2]).z, 1.f);
        }
    );
}

TEST(AssetsGraphics, MeshCookerDefaultColors){
    AssetsGraphicsFixture::CookAndCheckMinimalTypedAsset<NWB::Impl::Mesh>(
        AssetsGraphicsFixture::s_DefaultColorMeshMeta,
        "default_color_mesh",
        MinimalAssetKind::Mesh,
        [&](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.positionStream().size(), 3u);
            const Float4U color0 = LoadHalf4U(loadedMesh.colorStream()[0]);
            EXPECT_EQ(color0.x, 1.f);
            EXPECT_EQ(color0.w, 1.f);
        }
    );
}


TEST(AssetsGraphics, MeshCookerValidationFailures){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    ExpectCookFailure(testArena, AssetsGraphicsFixture::CookSingleMeshMeta, AssetsGraphicsFixture::s_UnsupportedMeshFieldsMeta, "unsupported_mesh_fields");
    ExpectCookFailure(testArena, AssetsGraphicsFixture::CookSingleMeshMeta, AssetsGraphicsFixture::s_MismatchedMeshMeta, "mismatched_mesh_streams");
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta("", AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "missing_mesh_normal_field"
    );
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta(
            AssetsGraphicsFixture::s_TriangleNormalField,
            AssetsGraphicsFixture::s_TriangleTangentField,
            AssetsGraphicsFixture::s_TriangleMissingNormalVertexRefsField
        ),
        "missing_mesh_normal_vertex_ref"
    );
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_EmptyNormalListField, AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "empty_list_mesh_normal"
    );
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_EmptyNormalMapField, AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "empty_map_mesh_normal"
    );
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_TriangleNormalField, "", AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "missing_mesh_tangent_field"
    );
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta(
            AssetsGraphicsFixture::s_TriangleNormalField,
            AssetsGraphicsFixture::s_TriangleTangentField,
            AssetsGraphicsFixture::s_TriangleMissingTangentVertexRefsField
        ),
        "missing_mesh_tangent_vertex_ref"
    );
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_TriangleNormalField, AssetsGraphicsFixture::s_EmptyTangentListField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "empty_list_mesh_tangent"
    );
    ExpectCookFailure(
        testArena,
        AssetsGraphicsFixture::CookSingleMeshMeta,
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_TriangleNormalField, AssetsGraphicsFixture::s_EmptyTangentMapField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "empty_map_mesh_tangent"
    );
    EXPECT_GE(logger.errorCount(), 10u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("'uv0' must be a list")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("'normals' must be a list")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("vertex_ref normal index is out of range")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("'normals' must not be empty")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("'tangents' must be a list")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("vertex_ref tangent index is out of range")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("'tangents' must not be empty")));
#else
#endif
}

TEST(AssetsGraphics, MeshClassPolicyHelpers){
    using namespace NWB::Core::Mesh;

    EXPECT_TRUE(MeshClassMatchesSkinPayload(MeshClass::Static, false));
    EXPECT_FALSE(MeshClassMatchesSkinPayload(MeshClass::Static, true));
    EXPECT_TRUE(MeshClassMatchesSkinPayload(MeshClass::Skinned, true));
    EXPECT_FALSE(MeshClassMatchesSkinPayload(MeshClass::Skinned, false));
}

TEST(AssetsGraphics, FormatBlockDimensions){
    const NWB::Core::FormatInfo& rgba8 = NWB::Core::GetFormatInfo(NWB::Core::Format::RGBA8_UNORM);
    EXPECT_EQ(NWB::Core::GetFormatBlockWidth(rgba8), 1u);
    EXPECT_EQ(NWB::Core::GetFormatBlockHeight(rgba8), 1u);

    const NWB::Core::FormatInfo& bc1 = NWB::Core::GetFormatInfo(NWB::Core::Format::BC1_UNORM);
    EXPECT_EQ(NWB::Core::GetFormatBlockWidth(bc1), 4u);
    EXPECT_EQ(NWB::Core::GetFormatBlockHeight(bc1), 4u);

    const NWB::Core::FormatInfo& astc8x5 = NWB::Core::GetFormatInfo(NWB::Core::Format::ASTC_8x5_UNORM);
    EXPECT_EQ(NWB::Core::GetFormatBlockWidth(astc8x5), 8u);
    EXPECT_EQ(NWB::Core::GetFormatBlockHeight(astc8x5), 5u);

    const NWB::Core::FormatInfo& astc12x10 = NWB::Core::GetFormatInfo(NWB::Core::Format::ASTC_12x10_FLOAT);
    EXPECT_EQ(NWB::Core::GetFormatBlockWidth(astc12x10), 12u);
    EXPECT_EQ(NWB::Core::GetFormatBlockHeight(astc12x10), 10u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

