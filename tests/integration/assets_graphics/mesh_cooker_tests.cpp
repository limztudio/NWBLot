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


#if defined(NWB_FINAL)
using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using CookSingleMetaFn = decltype(&AssetsGraphicsFixture::CookSingleMeshMeta);
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;
#endif


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

