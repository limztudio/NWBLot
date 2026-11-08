// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/ecs_test_world.h>
#include <tests/unit/csg/name_cases.adhd.h>

#include <impl/ecs_csg/module.h>
#include <impl/ecs_csg/deform_edit.h>

#include <core/common/module.h>
#include <core/ecs/module.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_deform_cut_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_EngineCsgPlane = NWB::Tests::CsgCases::s_CsgEnginePlanePath;
inline constexpr Name s_ScratchArena = NWB::Tests::CsgCases::s_OneOffNameTestsCsgScratch;


TEST(Csg, SequentialCutsPreserveEarlierHalfSpaceAndUnweldedSourceVertices){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Core::Alloc::GlobalArena commitArena(s_ScratchArena);

    auto makeVertex = [](const f32 x, const f32 y, const f32 z)noexcept{
        NWB::Impl::CsgDeformVertex vertex;
        vertex.position = Float3U(x, y, z);
        vertex.normal = Float4(0.0f, 0.0f, 1.0f, 0.0f);
        vertex.tangent = Float4(1.0f, 0.0f, 0.0f, 1.0f);
        vertex.uv0 = Float2U(0.5f, 0.5f);
        vertex.color = Float4(1.0f, 1.0f, 1.0f, 1.0f);
        return vertex;
    };

    // Closed unit cube so each planar cut leaves a closable boundary loop for caps.
    const Array<NWB::Impl::CsgDeformVertex, 8u> inputVertices = {
        makeVertex(-1.0f, -1.0f, -1.0f),
        makeVertex(1.0f, -1.0f, -1.0f),
        makeVertex(1.0f, 1.0f, -1.0f),
        makeVertex(-1.0f, 1.0f, -1.0f),
        makeVertex(-1.0f, -1.0f, 1.0f),
        makeVertex(1.0f, -1.0f, 1.0f),
        makeVertex(1.0f, 1.0f, 1.0f),
        makeVertex(-1.0f, 1.0f, 1.0f),
    };
    const Array<NWB::Impl::CsgDeformTriangle, 12u> inputTriangles = {
        NWB::Impl::CsgDeformTriangle{ { 0u, 1u, 2u } }, NWB::Impl::CsgDeformTriangle{ { 0u, 2u, 3u } },
        NWB::Impl::CsgDeformTriangle{ { 4u, 6u, 5u } }, NWB::Impl::CsgDeformTriangle{ { 4u, 7u, 6u } },
        NWB::Impl::CsgDeformTriangle{ { 0u, 4u, 5u } }, NWB::Impl::CsgDeformTriangle{ { 0u, 5u, 1u } },
        NWB::Impl::CsgDeformTriangle{ { 2u, 6u, 7u } }, NWB::Impl::CsgDeformTriangle{ { 2u, 7u, 3u } },
        NWB::Impl::CsgDeformTriangle{ { 0u, 3u, 7u } }, NWB::Impl::CsgDeformTriangle{ { 0u, 7u, 4u } },
        NWB::Impl::CsgDeformTriangle{ { 1u, 5u, 6u } }, NWB::Impl::CsgDeformTriangle{ { 1u, 6u, 2u } },
    };

    // Cut x >= -0.5 then y >= -0.5 using plane normal and distance.
    Array<NWB::Impl::CsgDeformCutDesc, 2u> cuts;
    cuts[0u].active = true;
    cuts[0u].shape.shapeType = Name(s_EngineCsgPlane);
    cuts[0u].shape.worldToShape = ::MatrixIdentity<Float34>();
    cuts[0u].shape.parameter0 = Float4(1.0f, 0.0f, 0.0f, 0.5f);
    cuts[1u].active = true;
    cuts[1u].shape.shapeType = Name(s_EngineCsgPlane);
    cuts[1u].shape.worldToShape = ::MatrixIdentity<Float34>();
    cuts[1u].shape.parameter0 = Float4(0.0f, 1.0f, 0.0f, 0.5f);

    const NWB::Impl::CsgDeformBuildOptions options;

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> commitVertices(commitArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> commitTriangles(commitArena);
    const auto commit = NWB::Impl::CommitCsgDeformCuts(scratchArena, commitArena, inputVertices, inputTriangles, cuts, options, nullptr, commitVertices, commitTriangles);
    ASSERT_TRUE(commit);
    EXPECT_EQ(commit->appliedCutCount, 2u);
    ASSERT_FALSE(commitTriangles.empty());
    ASSERT_GE(commitVertices.size(), 8u);

    // Referenced vertices must satisfy both cuts while unreferenced source vertices stay unchanged.
    for(const NWB::Impl::CsgDeformTriangle& triangle : commitTriangles){
        for(const u32 index : triangle.indices){
            ASSERT_LT(index, commitVertices.size());
            EXPECT_GE(commitVertices[index].position.x, -0.5001f);
            EXPECT_GE(commitVertices[index].position.y, -0.5001f);
        }
    }
    // Seam-safe rebuild never welds or drops source verts: originals survive verbatim.
    EXPECT_EQ(commitVertices[0u].position.x, -1.0f);
    EXPECT_EQ(commitVertices[0u].position.y, -1.0f);
    EXPECT_EQ(commitVertices[2u].position.x, 1.0f);
    EXPECT_EQ(commitVertices[2u].position.y, 1.0f);
    // The second cut refines the first cut's output: at least one split vertex sits on x == -0.5.
    b8 foundCutWall = false;
    for(const NWB::Impl::CsgDeformVertex& vertex : commitVertices){
        if(vertex.position.x > -0.5001f && vertex.position.x < -0.4999f)
            foundCutWall = true;
    }
    EXPECT_TRUE(foundCutWall);
}

TEST(Csg, CsgDeformCutViabilityRejectsDegenerateCommit){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Core::Alloc::GlobalArena commitArena(s_ScratchArena);

    NWB::Impl::CsgDeformVertex vertex;
    vertex.position = Float3U(-5.0f, 0.0f, 0.0f);
    vertex.normal = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    vertex.tangent = Float4(1.0f, 0.0f, 0.0f, 1.0f);
    vertex.uv0 = Float2U(0.0f, 0.0f);
    vertex.color = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    const Array<NWB::Impl::CsgDeformVertex, 3u> inputVertices = { vertex, vertex, vertex };
    const Array<NWB::Impl::CsgDeformTriangle, 1u> inputTriangles = { NWB::Impl::CsgDeformTriangle{ { 0u, 1u, 2u } } };

    // Triangle at x == -5 lies outside x >= 0 and fails with NoKeptGeometry.
    NWB::Impl::CsgDeformCutDesc cut;
    cut.active = true;
    cut.shape.shapeType = Name(s_EngineCsgPlane);
    cut.shape.worldToShape = ::MatrixIdentity<Float34>();
    cut.shape.parameter0 = Float4(1.0f, 0.0f, 0.0f, 0.0f);

    const NWB::Impl::CsgDeformBuildOptions options;
    const Span<const NWB::Impl::CsgDeformCutDesc> cuts(&cut, 1u);
    const NWB::Impl::CsgDeformViability viability = NWB::Impl::CheckCsgDeformCutsViability(scratchArena, inputVertices, inputTriangles, cuts, options);
    EXPECT_FALSE(viability.viable);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::ScratchArena> previewVertices(scratchArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::ScratchArena> previewTriangles(scratchArena);
    const auto preview = NWB::Impl::PreviewCsgDeformCuts(scratchArena, inputVertices, inputTriangles, cuts, options, nullptr, previewVertices, previewTriangles);
    ASSERT_FALSE(preview);
    EXPECT_EQ(preview.error().reason, NWB::Impl::CsgDeformViabilityReason::NoKeptGeometry);
    EXPECT_EQ(preview.error().stats.inputVertexCount, 3u);
    EXPECT_EQ(preview.error().stats.inputTriangleCount, 1u);
    EXPECT_EQ(preview.error().stats.outputVertexCount, 0u);
    EXPECT_EQ(preview.error().stats.outputTriangleCount, 0u);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> commitVertices(commitArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> commitTriangles(commitArena);
    const auto commit = NWB::Impl::CommitCsgDeformCuts(scratchArena, commitArena, inputVertices, inputTriangles, cuts, options, nullptr, commitVertices, commitTriangles);
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().reason, NWB::Impl::CsgDeformViabilityReason::NoKeptGeometry);
    EXPECT_EQ(commit.error().stats.inputVertexCount, 3u);
    EXPECT_EQ(commit.error().stats.inputTriangleCount, 1u);
    EXPECT_EQ(commit.error().stats.outputVertexCount, 0u);
    EXPECT_EQ(commit.error().stats.outputTriangleCount, 0u);
    EXPECT_TRUE(commitVertices.empty());
    EXPECT_TRUE(commitTriangles.empty());

    NWB::Core::Alloc::GlobalArena otherArena(s_ScratchArena);
    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> wrongVertices(otherArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> wrongTriangles(otherArena);
    wrongVertices.push_back(vertex);
    wrongTriangles.push_back(inputTriangles[0u]);
    const auto wrongArena = NWB::Impl::CommitCsgDeformCuts(scratchArena, commitArena, inputVertices, inputTriangles, cuts, options, nullptr, wrongVertices, wrongTriangles);
    ASSERT_FALSE(wrongArena);
    EXPECT_EQ(wrongArena.error().reason, NWB::Impl::CsgDeformViabilityReason::InvalidOutputArena);
    EXPECT_EQ(wrongArena.error().stats.inputVertexCount, 0u);
    EXPECT_EQ(wrongArena.error().stats.inputTriangleCount, 0u);
    ASSERT_EQ(wrongVertices.size(), 1u);
    ASSERT_EQ(wrongTriangles.size(), 1u);
    EXPECT_EQ(wrongVertices.front().position.x, vertex.position.x);
    EXPECT_EQ(wrongTriangles.front().indices[0u], inputTriangles[0u].indices[0u]);
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

