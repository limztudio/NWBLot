// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_mesh/meshlet_triangle_indices.h>

#include <global/not_null.h>

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
using TestArena = AssetsGraphicsFixture::TestArena;
#if defined(NWB_FINAL)
using CookSingleMetaFn = decltype(&AssetsGraphicsFixture::CookSingleMeshMeta);
using Path = AssetsGraphicsFixture::Path;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AString BuildMeshIndexMeta(const AStringView indexList){
    static constexpr usize s_IndexFieldOffset = AssetsGraphicsFixture::s_MinimalMeshMeta.find("asset.indices");
    static_assert(s_IndexFieldOffset != AStringView::npos);
    AString meta(AssetsGraphicsFixture::s_MinimalMeshMeta.data(), s_IndexFieldOffset);
    meta += "asset.indices = ";
    AssetsGraphicsFixture::AppendTestMeta(meta, indexList);
    meta += ";\n";
    return meta;
}


#if defined(NWB_FINAL)
static void ExpectCookFailure(TestArena& testArena, const NotNull<CookSingleMetaFn> cookSingleMeta, const AStringView metaText, const AStringView caseName){
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, caseName);
    ASSERT_TRUE(cookCase);
    EXPECT_FALSE(cookSingleMeta.get()(metaText, testArena, *cookCase));

    EXPECT_TRUE(RemoveAllIfExists(cookCase->root));
}
#endif


TEST(AssetsGraphics, MeshCookerPreservesFlatAndNestedIndexCornerOrder){
    static constexpr AStringView s_IndexLists[] = {
        "[2, 0, 1]",
        "[[], [2, [], [0]], [[], 1], []]",
    };
    const Float3U expectedPositions[] = {
        Float3U(0.0f, 0.5f, 0.0f),
        Float3U(-0.5f, -0.5f, 0.0f),
        Float3U(0.5f, -0.5f, 0.0f),
    };
    for(const AStringView indexList : s_IndexLists){
        SCOPED_TRACE(indexList);
        CapturingLogger logger;
        Core::Common::LoggerRegistrationGuard loggerGuard(logger);
        TestArena testArena;
        auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "mesh_index_corner_order");
        ASSERT_TRUE(cookCase);
        const AString meta = BuildMeshIndexMeta(indexList);
        ASSERT_TRUE(AssetsGraphicsFixture::CookSingleMeshMeta(AStringView(meta.data(), meta.size()), testArena, *cookCase));
        auto loadedAsset = AssetsGraphicsFixture::LoadCookedMinimalMesh(testArena, cookCase->outputDirectory);
        ASSERT_TRUE(loadedAsset);
        const auto& mesh = static_cast<const Impl::Mesh&>(**loadedAsset);
        Core::Alloc::ScratchArena scratchArena(Name("tests/integration/assets_graphics/mesh_index_corner_order"));
        const auto triangleIndices = Impl::BuildMeshletTriangleIndices(scratchArena, mesh);
        ASSERT_TRUE(triangleIndices);
        ASSERT_EQ(triangleIndices->size(), 3u);
        for(usize corner = 0u; corner < 3u; ++corner){
            ASSERT_LT((*triangleIndices)[corner], mesh.positionStream().size());
            const Float3U& position = mesh.positionStream()[(*triangleIndices)[corner]];
            EXPECT_EQ(position.x, expectedPositions[corner].x);
            EXPECT_EQ(position.y, expectedPositions[corner].y);
            EXPECT_EQ(position.z, expectedPositions[corner].z);
        }
        EXPECT_EQ(logger.errorCount(), 0u);
        EXPECT_TRUE(RemoveAllIfExists(cookCase->root));
    }
}

TEST(AssetsGraphics, MeshCookerReportsFirstInvalidNestedIndex){
#if defined(NWB_FINAL)
    static constexpr struct{
        AStringView indices;
        TStringView diagnostic;
    } s_Cases[] = {
        { R"([[], [2, ["bad", -1]], 4294967296])", NWB_TEXT("'indices[1][1][0]' must contain only integer values") },
        { R"([[], [2, [-1, "later"]], 4294967296])", NWB_TEXT("'indices[1][1][0]' contains a non-integer or negative value") },
        { R"([[], [2, [4294967296, "later"]], -1])", NWB_TEXT("'indices[1][1][0]' contains a value that exceeds u32") },
    };
    for(const auto& testCase : s_Cases){
        SCOPED_TRACE(testCase.indices);
        CapturingLogger logger;
        Core::Common::LoggerRegistrationGuard loggerGuard(logger);
        TestArena testArena;
        ExpectCookFailure(testArena, MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta), BuildMeshIndexMeta(testCase.indices), "invalid_nested_mesh_index");
        EXPECT_TRUE(logger.sawErrorContaining(testCase.diagnostic));
        EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("'indices[1][1][1]'")));
        EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("'indices[2]'")));
    }
#endif
}


TEST(AssetsGraphics, MeshCookerValidationFailures){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    ExpectCookFailure(testArena, MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta), AssetsGraphicsFixture::s_UnsupportedMeshFieldsMeta, "unsupported_mesh_fields");
    ExpectCookFailure(testArena, MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta), AssetsGraphicsFixture::s_MismatchedMeshMeta, "mismatched_mesh_streams");
    ExpectCookFailure(testArena, MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta), AssetsGraphicsFixture::BuildMeshTriangleMeta("", AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleVertexRefsField), "missing_mesh_normal_field");
    ExpectCookFailure(
        testArena,
        MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta),
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_TriangleNormalField, AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleMissingNormalVertexRefsField),
        "missing_mesh_normal_vertex_ref"
    );
    ExpectCookFailure(
        testArena,
        MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta),
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_EmptyNormalListField, AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "empty_list_mesh_normal"
    );
    ExpectCookFailure(
        testArena,
        MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta),
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_EmptyNormalMapField, AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "empty_map_mesh_normal"
    );
    ExpectCookFailure(testArena, MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta), AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_TriangleNormalField, "", AssetsGraphicsFixture::s_TriangleVertexRefsField), "missing_mesh_tangent_field");
    ExpectCookFailure(
        testArena,
        MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta),
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_TriangleNormalField, AssetsGraphicsFixture::s_TriangleTangentField, AssetsGraphicsFixture::s_TriangleMissingTangentVertexRefsField),
        "missing_mesh_tangent_vertex_ref"
    );
    ExpectCookFailure(
        testArena,
        MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta),
        AssetsGraphicsFixture::BuildMeshTriangleMeta(AssetsGraphicsFixture::s_TriangleNormalField, AssetsGraphicsFixture::s_EmptyTangentListField, AssetsGraphicsFixture::s_TriangleVertexRefsField),
        "empty_list_mesh_tangent"
    );
    ExpectCookFailure(
        testArena,
        MakeNotNull<CookSingleMetaFn>(AssetsGraphicsFixture::CookSingleMeshMeta),
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

