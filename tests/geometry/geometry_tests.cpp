// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/geometry/attribute_transfer.h>
#include <core/geometry/meshlet_cluster.h>
#include <core/geometry/mesh_topology.h>
#include <core/geometry/surface_patch_edit.h>
#include <core/geometry/tangent_frame_rebuild.h>
#include <core/geometry/frame_math.h>

#include <tests/test_context.h>

#include <core/common/common.h>

#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using MeshletBuildConfig = NWB::Core::Geometry::MeshletBuildConfig;
using MeshletBounds = NWB::Core::Geometry::MeshletBounds;
using MeshletCluster = NWB::Core::Geometry::MeshletCluster;
using MeshTopologyEdge = NWB::Core::Geometry::MeshTopologyEdge;
using Float4BlendSource = NWB::Core::Geometry::AttributeTransferFloat4BlendSource;
using MorphBlendSource = NWB::Core::Geometry::AttributeTransferMorphBlendSource;
using MorphDelta = NWB::Core::Geometry::AttributeTransferMorphDelta;
using SkinBlendSource = NWB::Core::Geometry::AttributeTransferSkinBlendSource;
using SkinInfluence4 = NWB::Core::Geometry::AttributeTransferSkinInfluence4;
using SurfacePatchWallVertex = NWB::Core::Geometry::SurfacePatchWallVertex;
using TangentFrameRebuildVertex = NWB::Core::Geometry::TangentFrameRebuildVertex;


#define NWB_GEOMETRY_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    return Abs(lhs - rhs) <= epsilon;
}

static bool NearlyEqual3(const Float3U& value, const f32 x, const f32 y, const f32 z){
    return
        NearlyEqual(value.x, x)
        && NearlyEqual(value.y, y)
        && NearlyEqual(value.z, z)
    ;
}

static bool NearlyEqual4(const Float4U& value, const f32 x, const f32 y, const f32 z, const f32 w){
    return
        NearlyEqual(value.x, x)
        && NearlyEqual(value.y, y)
        && NearlyEqual(value.z, z)
        && NearlyEqual(value.w, w)
    ;
}

static bool NearlyEqualBounds(const MeshletBounds& lhs, const MeshletBounds& rhs){
    return
        NearlyEqual3(lhs.minimum, rhs.minimum.x, rhs.minimum.y, rhs.minimum.z)
        && NearlyEqual3(lhs.maximum, rhs.maximum.x, rhs.maximum.y, rhs.maximum.z)
        && NearlyEqual3(lhs.center, rhs.center.x, rhs.center.y, rhs.center.z)
        && NearlyEqual(lhs.radius, rhs.radius)
    ;
}

static void TestResolvesCoreFrameMath(TestContext& context){
    SIMDVector normal = VectorSet(0.0f, 0.0f, 5.0f, 0.0f);
    SIMDVector tangent = VectorSet(2.0f, 1.0f, 0.0f, -0.25f);
    NWB::Core::Geometry::FrameOrthonormalize(
        normal,
        tangent,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        VectorSet(1.0f, 0.0f, 0.0f, -1.0f)
    );

    const SIMDVector bitangent = NWB::Core::Geometry::FrameResolveBitangent(
        normal,
        tangent,
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );
    Float4U normalValue;
    Float4U tangentValue;
    Float4U bitangentValue;
    StoreFloat(normal, &normalValue);
    StoreFloat(tangent, &tangentValue);
    StoreFloat(bitangent, &bitangentValue);

    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual4(normalValue, 0.0f, 0.0f, 1.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(tangent)), 1.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(bitangent)), 1.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(normal, tangent)), 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(normal, bitangent)), 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(tangent, bitangent)), 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(tangentValue.w, -1.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(bitangentValue.w, 0.0f));
}

static TangentFrameRebuildVertex MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u, const f32 v){
    TangentFrameRebuildVertex vertex;
    vertex.position = Float3U(x, y, z);
    vertex.uv0 = Float2U(u, v);
    vertex.normal = Float3U(0.0f, 0.0f, 0.0f);
    vertex.tangent = Float4U(0.0f, 0.0f, 0.0f, 0.0f);
    return vertex;
}

static SkinInfluence4 MakeSkinInfluence(
    const u16 joint0,
    const f32 weight0,
    const u16 joint1,
    const f32 weight1){
    SkinInfluence4 influence;
    influence.joint[0] = joint0;
    influence.weight[0] = weight0;
    influence.joint[1] = joint1;
    influence.weight[1] = weight1;
    return influence;
}

static void TestBlendsSkinInfluenceWeights(TestContext& context){
    SkinBlendSource sources[2] = {};
    sources[0].influence = MakeSkinInfluence(0u, 0.5f, 1u, 0.5f);
    sources[0].weight = 0.5f;
    sources[1].influence = MakeSkinInfluence(1u, 0.5f, 2u, 0.5f);
    sources[1].weight = 0.5f;

    SkinInfluence4 blended;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::BlendSkinInfluence4(sources, LengthOf(sources), blended));
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::ValidSkinInfluence4(blended));
    NWB_GEOMETRY_TEST_CHECK(context, blended.joint[0] == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(blended.weight[0], 0.5f));
    NWB_GEOMETRY_TEST_CHECK(context, blended.joint[1] == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(blended.weight[1], 0.25f));
    NWB_GEOMETRY_TEST_CHECK(context, blended.joint[2] == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(blended.weight[2], 0.25f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(blended.weight[3], 0.0f));
}

static void TestBlendSkinInfluenceRejectsInvalidInput(TestContext& context){
    SkinBlendSource sources[2] = {};
    sources[0].influence = MakeSkinInfluence(0u, 0.5f, 1u, 0.5f);
    sources[0].weight = 1.0f;
    sources[1].influence = MakeSkinInfluence(2u, 0.75f, 3u, 0.75f);
    sources[1].weight = 1.0f;

    SkinInfluence4 blended;
    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::BlendSkinInfluence4(sources, LengthOf(sources), blended));

    sources[1].influence = MakeSkinInfluence(2u, 0.5f, 3u, 0.5f);
    sources[1].weight = -0.25f;
    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::BlendSkinInfluence4(sources, LengthOf(sources), blended));
}

static void TestBlendsFloat4Weights(TestContext& context){
    Float4BlendSource sources[3] = {};
    sources[0].value = Float4U(1.0f, 0.0f, 0.0f, 1.0f);
    sources[0].weight = 0.25f;
    sources[1].value = Float4U(0.0f, 1.0f, 0.0f, 1.0f);
    sources[1].weight = 0.5f;
    sources[2].value = Float4U(0.0f, 0.0f, 1.0f, 1.0f);
    sources[2].weight = 0.25f;

    Float4U blended;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::BlendFloat4(sources, LengthOf(sources), blended));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual4(blended, 0.25f, 0.5f, 0.25f, 1.0f));
}

static void TestBlendFloat4RejectsInvalidInput(TestContext& context){
    Float4BlendSource sources[2] = {};
    sources[0].value = Float4U(1.0f, 0.0f, 0.0f, 1.0f);
    sources[0].weight = 1.0f;
    sources[1].value = Float4U(0.0f, Limit<f32>::s_QuietNaN, 0.0f, 1.0f);
    sources[1].weight = 1.0f;

    Float4U blended;
    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::BlendFloat4(sources, LengthOf(sources), blended));

    sources[1].value = Float4U(0.0f, 1.0f, 0.0f, 1.0f);
    sources[1].weight = -0.25f;
    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::BlendFloat4(sources, LengthOf(sources), blended));
}

static MorphDelta MakeMorphDelta(
    const u32 vertexId,
    const f32 positionZ,
    const f32 normalY,
    const f32 tangentX,
    const f32 tangentW){
    MorphDelta delta;
    delta.vertexId = vertexId;
    delta.deltaPosition = Float3U(0.0f, 0.0f, positionZ);
    delta.deltaNormal = Float3U(0.0f, normalY, 0.0f);
    delta.deltaTangent = Float4U(tangentX, 0.0f, 0.0f, tangentW);
    return delta;
}

static void TestBlendsMorphDeltaWeights(TestContext& context){
    const MorphDelta first = MakeMorphDelta(4u, 0.4f, 0.2f, 0.6f, -0.4f);
    const MorphDelta second = MakeMorphDelta(8u, 0.2f, 0.4f, 0.2f, 0.2f);
    MorphBlendSource sources[2] = {};
    sources[0].delta = &first;
    sources[0].weight = 0.25f;
    sources[1].delta = &second;
    sources[1].weight = 0.5f;

    MorphDelta blended;
    bool hasDelta = false;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::BlendMorphDelta(sources, LengthOf(sources), 19u, blended, hasDelta));
    NWB_GEOMETRY_TEST_CHECK(context, hasDelta);
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::ActiveMorphDelta(blended));
    NWB_GEOMETRY_TEST_CHECK(context, blended.vertexId == 19u);
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(blended.deltaPosition, 0.0f, 0.0f, 0.2f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(blended.deltaNormal, 0.0f, 0.25f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual4(blended.deltaTangent, 0.25f, 0.0f, 0.0f, 0.0f));
}

static void TestBlendMorphDeltaReportsInactiveOutput(TestContext& context){
    const MorphDelta zeroDelta = MakeMorphDelta(3u, 0.0f, 0.0f, 0.0f, 0.0f);
    MorphBlendSource sources[2] = {};
    sources[0].delta = nullptr;
    sources[0].weight = 1.0f;
    sources[1].delta = &zeroDelta;
    sources[1].weight = 1.0f;

    MorphDelta blended;
    bool hasDelta = true;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::BlendMorphDelta(sources, LengthOf(sources), 11u, blended, hasDelta));
    NWB_GEOMETRY_TEST_CHECK(context, !hasDelta);
    NWB_GEOMETRY_TEST_CHECK(context, blended.vertexId == 11u);
}

static void TestBlendMorphDeltaRejectsInvalidInput(TestContext& context){
    MorphDelta invalidDelta = MakeMorphDelta(3u, 0.0f, 0.0f, 0.0f, 0.0f);
    invalidDelta.deltaPosition.z = Limit<f32>::s_QuietNaN;
    MorphBlendSource sources[1] = {};
    sources[0].delta = &invalidDelta;
    sources[0].weight = 1.0f;

    MorphDelta blended;
    bool hasDelta = false;
    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::BlendMorphDelta(sources, LengthOf(sources), 11u, blended, hasDelta));

    invalidDelta.deltaPosition.z = 0.25f;
    sources[0].weight = -0.25f;
    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::BlendMorphDelta(sources, LengthOf(sources), 11u, blended, hasDelta));
}

static void TestRebuildsFlatQuadFrame(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, 1.0f, 0.0f, 1.0f, 1.0f));
    vertices.push_back(MakeVertex(-1.0f, 1.0f, 0.0f, 0.0f, 1.0f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(0u);
    indices.push_back(2u);
    indices.push_back(3u);

    NWB::Core::Geometry::TangentFrameRebuildResult result;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::RebuildTangentFrames(vertices, indices, &result));
    NWB_GEOMETRY_TEST_CHECK(context, result.rebuiltVertexCount == 4u);
    NWB_GEOMETRY_TEST_CHECK(context, result.degenerateUvTriangleCount == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, result.fallbackTangentVertexCount == 0u);
    for(const TangentFrameRebuildVertex& vertex : vertices){
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual4(vertex.tangent, 1.0f, 0.0f, 0.0f, 1.0f));
    }
}

static void TestDegenerateUvsUseStableTangentFallback(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 1.0f, 0.0f, 0.0f, 0.0f));
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.tangent = Float4U(0.0f, 1.0f, 0.0f, -1.0f);

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);

    NWB::Core::Geometry::TangentFrameRebuildResult result;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::RebuildTangentFrames(vertices, indices, &result));
    NWB_GEOMETRY_TEST_CHECK(context, result.rebuiltVertexCount == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, result.degenerateUvTriangleCount == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, result.fallbackTangentVertexCount == 3u);
    for(const TangentFrameRebuildVertex& vertex : vertices){
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual4(vertex.tangent, 0.0f, 1.0f, 0.0f, -1.0f));
    }
}

static void TestRejectsDegenerateTriangle(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(2.0f, 0.0f, 0.0f, 1.0f, 1.0f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);

    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::RebuildTangentFrames(vertices, indices));
}

static MeshTopologyEdge MakeEdge(const u32 a, const u32 b){
    MeshTopologyEdge edge;
    edge.a = a;
    edge.b = b;
    edge.fullCount = 2u;
    edge.removedCount = 1u;
    return edge;
}

static void TestOrdersBoundaryLoopCounterClockwise(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(-1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(-1.0f, 1.0f, 0.0f));

    Vector<MeshTopologyEdge> boundaryEdges;
    boundaryEdges.push_back(MakeEdge(2u, 3u));
    boundaryEdges.push_back(MakeEdge(1u, 0u));
    boundaryEdges.push_back(MakeEdge(3u, 0u));
    boundaryEdges.push_back(MakeEdge(1u, 2u));

    NWB::Core::Geometry::MeshTopologyBoundaryLoopFrame frame;
    frame.center = Float3U(0.0f, 0.0f, 0.0f);
    frame.tangent = Float3U(1.0f, 0.0f, 0.0f);
    frame.bitangent = Float3U(0.0f, 1.0f, 0.0f);

    Vector<MeshTopologyEdge> orderedEdges;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildOrderedBoundaryLoop(boundaryEdges, positions, frame, orderedEdges)
    );
    NWB_GEOMETRY_TEST_CHECK(context, orderedEdges.size() == 4u);
    NWB_GEOMETRY_TEST_CHECK(context, orderedEdges[0].a == 0u && orderedEdges[0].b == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, orderedEdges[1].a == 1u && orderedEdges[1].b == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, orderedEdges[2].a == 2u && orderedEdges[2].b == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, orderedEdges[3].a == 3u && orderedEdges[3].b == 0u);
}

static void TestRejectsBranchedBoundaryLoop(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(0.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(0.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(-1.0f, 0.0f, 0.0f));

    Vector<MeshTopologyEdge> boundaryEdges;
    boundaryEdges.push_back(MakeEdge(0u, 1u));
    boundaryEdges.push_back(MakeEdge(1u, 2u));
    boundaryEdges.push_back(MakeEdge(2u, 0u));
    boundaryEdges.push_back(MakeEdge(0u, 3u));

    NWB::Core::Geometry::MeshTopologyBoundaryLoopFrame frame;
    frame.center = Float3U(0.0f, 0.0f, 0.0f);
    frame.tangent = Float3U(1.0f, 0.0f, 0.0f);
    frame.bitangent = Float3U(0.0f, 1.0f, 0.0f);

    Vector<MeshTopologyEdge> orderedEdges;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildOrderedBoundaryLoop(boundaryEdges, positions, frame, orderedEdges)
    );
}

static bool ContainsUndirectedEdge(const Vector<MeshTopologyEdge>& edges, const u32 a, const u32 b){
    for(const MeshTopologyEdge& edge : edges){
        if((edge.a == a && edge.b == b) || (edge.a == b && edge.b == a))
            return true;
    }
    return false;
}

static Vector<u32> MakeTetrahedronIndices(){
    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(0u);
    indices.push_back(3u);
    indices.push_back(1u);
    indices.push_back(1u);
    indices.push_back(3u);
    indices.push_back(2u);
    indices.push_back(2u);
    indices.push_back(3u);
    indices.push_back(0u);
    return indices;
}

static void TestBuildsBoundaryEdgesFromRemovedTriangles(TestContext& context){
    Vector<u32> indices = MakeTetrahedronIndices();
    Vector<u8> removedTriangles;
    removedTriangles.resize(4u, 0u);
    removedTriangles[0u] = 1u;

    Vector<MeshTopologyEdge> boundaryEdges;
    u32 removedTriangleCount = 0u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildBoundaryEdgesFromRemovedTriangles(
            indices,
            removedTriangles,
            boundaryEdges,
            &removedTriangleCount
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, removedTriangleCount == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, boundaryEdges.size() == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, ContainsUndirectedEdge(boundaryEdges, 0u, 1u));
    NWB_GEOMETRY_TEST_CHECK(context, ContainsUndirectedEdge(boundaryEdges, 1u, 2u));
    NWB_GEOMETRY_TEST_CHECK(context, ContainsUndirectedEdge(boundaryEdges, 2u, 0u));
    for(const MeshTopologyEdge& edge : boundaryEdges){
        NWB_GEOMETRY_TEST_CHECK(context, edge.fullCount == 2u);
        NWB_GEOMETRY_TEST_CHECK(context, edge.removedCount == 1u);
    }
}

static void TestRejectsMalformedRemovedTriangleBoundaries(TestContext& context){
    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(0u);
    indices.push_back(2u);
    indices.push_back(3u);

    Vector<u8> removedTriangles;
    removedTriangles.resize(2u, 0u);
    removedTriangles[0u] = 1u;

    Vector<MeshTopologyEdge> boundaryEdges;
    u32 removedTriangleCount = 7u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildBoundaryEdgesFromRemovedTriangles(
            indices,
            removedTriangles,
            boundaryEdges,
            &removedTriangleCount
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, boundaryEdges.empty());
    NWB_GEOMETRY_TEST_CHECK(context, removedTriangleCount == 0u);

    indices = MakeTetrahedronIndices();
    removedTriangles.clear();
    removedTriangles.resize(4u, 1u);
    removedTriangleCount = 7u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildBoundaryEdgesFromRemovedTriangles(
            indices,
            removedTriangles,
            boundaryEdges,
            &removedTriangleCount
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, boundaryEdges.empty());
    NWB_GEOMETRY_TEST_CHECK(context, removedTriangleCount == 0u);

    removedTriangles.resize(3u, 0u);
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildBoundaryEdgesFromRemovedTriangles(indices, removedTriangles, boundaryEdges)
    );

    removedTriangles.resize(4u, 0u);
    removedTriangles[0u] = 1u;
    indices[2u] = indices[1u];
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildBoundaryEdgesFromRemovedTriangles(indices, removedTriangles, boundaryEdges)
    );
}

static void TestAppendsWallTrianglePairs(TestContext& context){
    Vector<MeshTopologyEdge> orderedEdges;
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(1u, 2u));
    orderedEdges.push_back(MakeEdge(2u, 0u));

    Vector<u32> innerVertices;
    innerVertices.push_back(3u);
    innerVertices.push_back(4u);
    innerVertices.push_back(5u);

    Vector<u32> indices;
    u32 addedTriangleCount = 0u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::AppendWallTrianglePairs(
            orderedEdges,
            innerVertices,
            indices,
            &addedTriangleCount
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, addedTriangleCount == 6u);
    NWB_GEOMETRY_TEST_CHECK(context, indices.size() == 18u);
    NWB_GEOMETRY_TEST_CHECK(context, indices[0] == 0u && indices[1] == 1u && indices[2] == 4u);
    NWB_GEOMETRY_TEST_CHECK(context, indices[3] == 0u && indices[4] == 4u && indices[5] == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, indices[12] == 2u && indices[13] == 0u && indices[14] == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, indices[15] == 2u && indices[16] == 3u && indices[17] == 5u);
}

static void TestRejectsMalformedWallTrianglePairs(TestContext& context){
    Vector<MeshTopologyEdge> orderedEdges;
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(2u, 0u));
    orderedEdges.push_back(MakeEdge(1u, 2u));

    Vector<u32> innerVertices;
    innerVertices.push_back(3u);
    innerVertices.push_back(4u);
    innerVertices.push_back(5u);

    Vector<u32> indices;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::AppendWallTrianglePairs(
            orderedEdges,
            innerVertices,
            indices
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, indices.empty());

    orderedEdges.clear();
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(1u, 2u));
    orderedEdges.push_back(MakeEdge(2u, 0u));
    innerVertices[1u] = 1u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::AppendWallTrianglePairs(
            orderedEdges,
            innerVertices,
            indices
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, indices.empty());
}

static void TestBuildsBoundaryLoopVertexFrame(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(-1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(-1.0f, 1.0f, 0.0f));

    NWB::Core::Geometry::MeshTopologyBoundaryLoopFrame loopFrame;
    loopFrame.center = Float3U(0.0f, 0.0f, 0.0f);
    loopFrame.tangent = Float3U(1.0f, 0.0f, 0.0f);
    loopFrame.bitangent = Float3U(0.0f, 1.0f, 0.0f);

    NWB::Core::Geometry::MeshTopologyLoopVertexFrame vertexFrame;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildBoundaryLoopVertexFrame(
            positions,
            loopFrame,
            MakeEdge(3u, 0u),
            MakeEdge(0u, 1u),
            vertexFrame
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(vertexFrame.normal.x, 0.7071067f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(vertexFrame.normal.y, 0.7071067f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(vertexFrame.normal.z, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(vertexFrame.tangent.x, -0.7071067f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(vertexFrame.tangent.y, 0.7071067f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(vertexFrame.tangent.z, 0.0f));

    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildBoundaryLoopVertexFrame(
            positions,
            loopFrame,
            MakeEdge(2u, 3u),
            MakeEdge(0u, 1u),
            vertexFrame
        )
    );
}

static void TestBuildsSurfacePatchLoopDistances(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(-1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(-1.0f, 1.0f, 0.0f));

    Vector<MeshTopologyEdge> orderedEdges;
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(1u, 2u));
    orderedEdges.push_back(MakeEdge(2u, 3u));
    orderedEdges.push_back(MakeEdge(3u, 0u));

    f32 distances[4] = {};
    f32 loopLength = 0.0f;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildSurfacePatchLoopDistances(
            orderedEdges,
            positions,
            Float3U(0.0f, 0.0f, 1.0f),
            distances,
            LengthOf(distances),
            loopLength
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(distances[0], 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(distances[1], 2.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(distances[2], 4.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(distances[3], 6.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(loopLength, 8.0f));
}

static void TestRejectsMalformedSurfacePatchLoopDistances(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(0.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(0.0f, 1.0f, 0.0f));

    Vector<MeshTopologyEdge> orderedEdges;
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(2u, 0u));
    orderedEdges.push_back(MakeEdge(1u, 2u));

    f32 distances[3] = {};
    f32 loopLength = 7.0f;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildSurfacePatchLoopDistances(
            orderedEdges,
            positions,
            Float3U(0.0f, 0.0f, 1.0f),
            distances,
            LengthOf(distances),
            loopLength
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(loopLength, 0.0f));

    orderedEdges.clear();
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(1u, 2u));
    orderedEdges.push_back(MakeEdge(2u, 0u));
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildSurfacePatchLoopDistances(
            orderedEdges,
            positions,
            Float3U(0.0f, 0.0f, 0.0f),
            distances,
            LengthOf(distances),
            loopLength
        )
    );
}

static void TestBuildsSurfacePatchRingEdges(TestContext& context){
    const u32 ringVertices[] = { 10u, 11u, 12u, 13u };
    Vector<MeshTopologyEdge> ringEdges;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildSurfacePatchRingEdges(
            ringVertices,
            LengthOf(ringVertices),
            ringEdges
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, ringEdges.size() == 4u);
    NWB_GEOMETRY_TEST_CHECK(context, ringEdges[0].a == 10u && ringEdges[0].b == 11u);
    NWB_GEOMETRY_TEST_CHECK(context, ringEdges[3].a == 13u && ringEdges[3].b == 10u);
    for(const MeshTopologyEdge& edge : ringEdges){
        NWB_GEOMETRY_TEST_CHECK(context, edge.fullCount == 2u);
        NWB_GEOMETRY_TEST_CHECK(context, edge.removedCount == 1u);
    }
}

static void TestRejectsMalformedSurfacePatchRingEdges(TestContext& context){
    const u32 ringVertices[] = { 10u, 11u, 11u };
    Vector<MeshTopologyEdge> ringEdges;
    ringEdges.push_back(MakeEdge(0u, 1u));
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildSurfacePatchRingEdges(
            ringVertices,
            LengthOf(ringVertices),
            ringEdges
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, ringEdges.empty());
}

static void TestBuildsSurfacePatchWallVertices(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(-1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(-1.0f, 1.0f, 0.0f));

    Vector<MeshTopologyEdge> orderedEdges;
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(1u, 2u));
    orderedEdges.push_back(MakeEdge(2u, 3u));
    orderedEdges.push_back(MakeEdge(3u, 0u));

    NWB::Core::Geometry::MeshTopologyBoundaryLoopFrame loopFrame;
    loopFrame.center = Float3U(0.0f, 0.0f, 0.0f);
    loopFrame.tangent = Float3U(1.0f, 0.0f, 0.0f);
    loopFrame.bitangent = Float3U(0.0f, 1.0f, 0.0f);

    Vector<SurfacePatchWallVertex> wallVertices;
    wallVertices.resize(8u);
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildSurfacePatchWallVertices(
            orderedEdges,
            positions,
            loopFrame,
            Float3U(0.0f, 0.0f, 1.0f),
            0.5f,
            2u,
            wallVertices.data(),
            wallVertices.size()
        )
    );

    NWB_GEOMETRY_TEST_CHECK(context, wallVertices[0u].sourceVertex == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, wallVertices[0u].attributeVertices[0u] == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, wallVertices[0u].attributeVertices[1u] == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, wallVertices[0u].attributeVertices[2u] == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(wallVertices[0u].position, -1.0f, -1.0f, -0.25f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(wallVertices[0u].normal, 0.7071067f, 0.7071067f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(wallVertices[0u].tangent, -0.7071067f, 0.7071067f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(wallVertices[0u].uv0.x, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(wallVertices[0u].uv0.y, 0.5f));
    NWB_GEOMETRY_TEST_CHECK(context, wallVertices[1u].sourceVertex == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(wallVertices[1u].uv0.x, 0.25f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(wallVertices[4u].position, -1.0f, -1.0f, -0.5f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(wallVertices[4u].uv0.y, 1.0f));
}

static void TestRejectsMalformedSurfacePatchWallVertices(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(0.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(0.0f, 1.0f, 0.0f));

    Vector<MeshTopologyEdge> orderedEdges;
    orderedEdges.push_back(MakeEdge(0u, 1u));
    orderedEdges.push_back(MakeEdge(1u, 2u));
    orderedEdges.push_back(MakeEdge(2u, 0u));

    NWB::Core::Geometry::MeshTopologyBoundaryLoopFrame loopFrame;
    loopFrame.center = Float3U(0.0f, 0.0f, 0.0f);
    loopFrame.tangent = Float3U(1.0f, 0.0f, 0.0f);
    loopFrame.bitangent = Float3U(0.0f, 1.0f, 0.0f);

    SurfacePatchWallVertex wallVertices[3] = {};
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildSurfacePatchWallVertices(
            orderedEdges,
            positions,
            loopFrame,
            Float3U(0.0f, 0.0f, 1.0f),
            0.0f,
            1u,
            wallVertices,
            LengthOf(wallVertices)
        )
    );
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildSurfacePatchWallVertices(
            orderedEdges,
            positions,
            loopFrame,
            Float3U(0.0f, 0.0f, 1.0f),
            0.5f,
            1u,
            wallVertices,
            LengthOf(wallVertices) - 1u
        )
    );
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildSurfacePatchWallVertices(
            orderedEdges,
            positions,
            loopFrame,
            Float3U(0.0f, 0.0f, 0.0f),
            0.5f,
            1u,
            wallVertices,
            LengthOf(wallVertices)
        )
    );
}

static void TestBuildsSingleQuadMeshlet(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(-1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(-1.0f, 1.0f, 0.0f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(0u);
    indices.push_back(2u);
    indices.push_back(3u);

    Vector<MeshletCluster> meshlets;
    Vector<u32> vertexIndices;
    Vector<u32> localIndices;
    MeshletBuildConfig config;
    config.maxVertices = 4u;
    config.maxTriangles = 2u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildMeshlets(positions, indices, config, meshlets, vertexIndices, localIndices)
    );
    NWB_GEOMETRY_TEST_CHECK(context, meshlets.size() == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, vertexIndices.size() == 4u);
    NWB_GEOMETRY_TEST_CHECK(context, localIndices.size() == 6u);
    NWB_GEOMETRY_TEST_CHECK(context, vertexIndices[0] == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, vertexIndices[1] == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, vertexIndices[2] == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, vertexIndices[3] == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, localIndices[0] == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, localIndices[1] == 1u);
    NWB_GEOMETRY_TEST_CHECK(context, localIndices[2] == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, localIndices[3] == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, localIndices[4] == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, localIndices[5] == 3u);

    const MeshletCluster& meshlet = meshlets[0];
    NWB_GEOMETRY_TEST_CHECK(context, meshlet.firstVertex == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlet.vertexCount == 4u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlet.firstIndex == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlet.indexCount == 6u);
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(meshlet.bounds.minimum, -1.0f, -1.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(meshlet.bounds.maximum, 1.0f, 1.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(meshlet.bounds.center, 0.0f, 0.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(meshlet.bounds.radius, Sqrt(2.0f)));

    Vector<f32> noVertexExpansion;
    MeshletBounds recomputedBounds;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::ComputeMeshletDeformationBounds(
            positions,
            vertexIndices,
            meshlet,
            noVertexExpansion,
            0.0f,
            recomputedBounds
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(recomputedBounds.minimum, -1.0f, -1.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(recomputedBounds.maximum, 1.0f, 1.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(recomputedBounds.center, 0.0f, 0.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(recomputedBounds.radius, Sqrt(2.0f)));
}

static void TestComputesMeshletDeformationBounds(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(-1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, -1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(-1.0f, 1.0f, 0.0f));

    Vector<u32> vertexIndices;
    vertexIndices.push_back(0u);
    vertexIndices.push_back(1u);
    vertexIndices.push_back(2u);
    vertexIndices.push_back(3u);

    MeshletCluster meshlet;
    meshlet.firstVertex = 0u;
    meshlet.vertexCount = 4u;

    Vector<f32> noVertexExpansion;
    MeshletBounds bounds;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::ComputeMeshletDeformationBounds(
            positions,
            vertexIndices,
            meshlet,
            noVertexExpansion,
            0.25f,
            bounds
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(bounds.minimum, -1.25f, -1.25f, -0.25f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(bounds.maximum, 1.25f, 1.25f, 0.25f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(bounds.center, 0.0f, 0.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(bounds.radius, Sqrt(2.0f) + 0.25f));

    Vector<f32> vertexExpansion;
    vertexExpansion.resize(positions.size(), 0.0f);
    vertexExpansion[2u] = 0.5f;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::ComputeMeshletDeformationBounds(
            positions,
            vertexIndices,
            meshlet,
            vertexExpansion,
            0.0f,
            bounds
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(bounds.minimum, -1.0f, -1.0f, -0.5f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(bounds.maximum, 1.5f, 1.5f, 0.5f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(bounds.center, 0.25f, 0.25f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, bounds.radius >= Sqrt(2.0f));
}

static void TestMeshletBuilderSplitsByLimits(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(0.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(0.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(2.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(3.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(2.0f, 1.0f, 0.0f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(3u);
    indices.push_back(4u);
    indices.push_back(5u);

    Vector<MeshletCluster> meshlets;
    Vector<u32> vertexIndices;
    Vector<u32> localIndices;
    MeshletBuildConfig config;
    config.maxVertices = 3u;
    config.maxTriangles = 2u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildMeshlets(positions, indices, config, meshlets, vertexIndices, localIndices)
    );
    NWB_GEOMETRY_TEST_CHECK(context, meshlets.size() == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[0].firstVertex == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[0].vertexCount == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[0].firstIndex == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[0].indexCount == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[1].firstVertex == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[1].vertexCount == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[1].firstIndex == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[1].indexCount == 3u);

    config.maxVertices = 6u;
    config.maxTriangles = 1u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildMeshlets(positions, indices, config, meshlets, vertexIndices, localIndices)
    );
    NWB_GEOMETRY_TEST_CHECK(context, meshlets.size() == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[0].vertexCount == 3u);
    NWB_GEOMETRY_TEST_CHECK(context, meshlets[1].vertexCount == 3u);
}

static void TestMeshletBuilderRebuildsDeterministically(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(0.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(0.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 1.0f, 0.0f));
    positions.push_back(Float3U(2.0f, 1.0f, 0.0f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(1u);
    indices.push_back(3u);
    indices.push_back(2u);
    indices.push_back(1u);
    indices.push_back(4u);
    indices.push_back(3u);

    MeshletBuildConfig config;
    config.maxVertices = 4u;
    config.maxTriangles = 2u;

    Vector<MeshletCluster> firstMeshlets;
    Vector<u32> firstVertexIndices;
    Vector<u32> firstLocalIndices;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildMeshlets(
            positions,
            indices,
            config,
            firstMeshlets,
            firstVertexIndices,
            firstLocalIndices
        )
    );

    Vector<MeshletCluster> secondMeshlets;
    Vector<u32> secondVertexIndices;
    Vector<u32> secondLocalIndices;
    secondMeshlets.push_back(MeshletCluster{});
    secondVertexIndices.push_back(99u);
    secondLocalIndices.push_back(99u);
    NWB_GEOMETRY_TEST_CHECK(
        context,
        NWB::Core::Geometry::BuildMeshlets(
            positions,
            indices,
            config,
            secondMeshlets,
            secondVertexIndices,
            secondLocalIndices
        )
    );

    NWB_GEOMETRY_TEST_CHECK(context, secondMeshlets.size() == firstMeshlets.size());
    NWB_GEOMETRY_TEST_CHECK(context, secondVertexIndices.size() == firstVertexIndices.size());
    NWB_GEOMETRY_TEST_CHECK(context, secondLocalIndices.size() == firstLocalIndices.size());
    for(usize i = 0u; i < firstMeshlets.size(); ++i){
        NWB_GEOMETRY_TEST_CHECK(context, secondMeshlets[i].firstVertex == firstMeshlets[i].firstVertex);
        NWB_GEOMETRY_TEST_CHECK(context, secondMeshlets[i].vertexCount == firstMeshlets[i].vertexCount);
        NWB_GEOMETRY_TEST_CHECK(context, secondMeshlets[i].firstIndex == firstMeshlets[i].firstIndex);
        NWB_GEOMETRY_TEST_CHECK(context, secondMeshlets[i].indexCount == firstMeshlets[i].indexCount);
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqualBounds(secondMeshlets[i].bounds, firstMeshlets[i].bounds));
    }
    for(usize i = 0u; i < firstVertexIndices.size(); ++i)
        NWB_GEOMETRY_TEST_CHECK(context, secondVertexIndices[i] == firstVertexIndices[i]);
    for(usize i = 0u; i < firstLocalIndices.size(); ++i)
        NWB_GEOMETRY_TEST_CHECK(context, secondLocalIndices[i] == firstLocalIndices[i]);

    positions[4u].x = Limit<f32>::s_QuietNaN;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildMeshlets(
            positions,
            indices,
            config,
            secondMeshlets,
            secondVertexIndices,
            secondLocalIndices
        )
    );
    NWB_GEOMETRY_TEST_CHECK(context, secondMeshlets.empty());
    NWB_GEOMETRY_TEST_CHECK(context, secondVertexIndices.empty());
    NWB_GEOMETRY_TEST_CHECK(context, secondLocalIndices.empty());
}

static void TestMeshletBuilderRejectsInvalidInput(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(0.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(0.0f, 1.0f, 0.0f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(3u);

    Vector<MeshletCluster> meshlets;
    Vector<u32> vertexIndices;
    Vector<u32> localIndices;
    MeshletBuildConfig config;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildMeshlets(positions, indices, config, meshlets, vertexIndices, localIndices)
    );
    NWB_GEOMETRY_TEST_CHECK(context, meshlets.empty());
    NWB_GEOMETRY_TEST_CHECK(context, vertexIndices.empty());
    NWB_GEOMETRY_TEST_CHECK(context, localIndices.empty());

    indices[2] = 1u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildMeshlets(positions, indices, config, meshlets, vertexIndices, localIndices)
    );

    indices[1] = 1u;
    indices[2] = 2u;
    positions[2].x = Limit<f32>::s_QuietNaN;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildMeshlets(positions, indices, config, meshlets, vertexIndices, localIndices)
    );

    positions[2] = Float3U(0.0f, 1.0f, 0.0f);
    config.maxVertices = 2u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::BuildMeshlets(positions, indices, config, meshlets, vertexIndices, localIndices)
    );
}

static void TestRejectsInvalidMeshletDeformationBounds(TestContext& context){
    Vector<Float3U> positions;
    positions.push_back(Float3U(0.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(1.0f, 0.0f, 0.0f));
    positions.push_back(Float3U(0.0f, 1.0f, 0.0f));

    Vector<u32> vertexIndices;
    vertexIndices.push_back(0u);
    vertexIndices.push_back(1u);
    vertexIndices.push_back(2u);

    MeshletCluster meshlet;
    meshlet.firstVertex = 0u;
    meshlet.vertexCount = 3u;

    Vector<f32> expansion;
    MeshletBounds bounds;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::ComputeMeshletDeformationBounds(
            positions,
            vertexIndices,
            meshlet,
            expansion,
            -0.01f,
            bounds
        )
    );

    expansion.resize(positions.size() - 1u, 0.0f);
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::ComputeMeshletDeformationBounds(
            positions,
            vertexIndices,
            meshlet,
            expansion,
            0.0f,
            bounds
        )
    );

    expansion.resize(positions.size(), 0.0f);
    expansion[1u] = Limit<f32>::s_QuietNaN;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::ComputeMeshletDeformationBounds(
            positions,
            vertexIndices,
            meshlet,
            expansion,
            0.0f,
            bounds
        )
    );

    expansion[1u] = 0.0f;
    vertexIndices[2u] = 3u;
    NWB_GEOMETRY_TEST_CHECK(
        context,
        !NWB::Core::Geometry::ComputeMeshletDeformationBounds(
            positions,
            vertexIndices,
            meshlet,
            expansion,
            0.0f,
            bounds
        )
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("geometry", [](NWB::Tests::TestContext& context){
        __hidden_geometry_tests::TestResolvesCoreFrameMath(context);
        __hidden_geometry_tests::TestBlendsSkinInfluenceWeights(context);
        __hidden_geometry_tests::TestBlendSkinInfluenceRejectsInvalidInput(context);
        __hidden_geometry_tests::TestBlendsFloat4Weights(context);
        __hidden_geometry_tests::TestBlendFloat4RejectsInvalidInput(context);
        __hidden_geometry_tests::TestBlendsMorphDeltaWeights(context);
        __hidden_geometry_tests::TestBlendMorphDeltaReportsInactiveOutput(context);
        __hidden_geometry_tests::TestBlendMorphDeltaRejectsInvalidInput(context);
        __hidden_geometry_tests::TestRebuildsFlatQuadFrame(context);
        __hidden_geometry_tests::TestDegenerateUvsUseStableTangentFallback(context);
        __hidden_geometry_tests::TestRejectsDegenerateTriangle(context);
        __hidden_geometry_tests::TestOrdersBoundaryLoopCounterClockwise(context);
        __hidden_geometry_tests::TestRejectsBranchedBoundaryLoop(context);
        __hidden_geometry_tests::TestBuildsBoundaryEdgesFromRemovedTriangles(context);
        __hidden_geometry_tests::TestRejectsMalformedRemovedTriangleBoundaries(context);
        __hidden_geometry_tests::TestAppendsWallTrianglePairs(context);
        __hidden_geometry_tests::TestRejectsMalformedWallTrianglePairs(context);
        __hidden_geometry_tests::TestBuildsBoundaryLoopVertexFrame(context);
        __hidden_geometry_tests::TestBuildsSurfacePatchLoopDistances(context);
        __hidden_geometry_tests::TestRejectsMalformedSurfacePatchLoopDistances(context);
        __hidden_geometry_tests::TestBuildsSurfacePatchRingEdges(context);
        __hidden_geometry_tests::TestRejectsMalformedSurfacePatchRingEdges(context);
        __hidden_geometry_tests::TestBuildsSurfacePatchWallVertices(context);
        __hidden_geometry_tests::TestRejectsMalformedSurfacePatchWallVertices(context);
        __hidden_geometry_tests::TestBuildsSingleQuadMeshlet(context);
        __hidden_geometry_tests::TestComputesMeshletDeformationBounds(context);
        __hidden_geometry_tests::TestMeshletBuilderSplitsByLimits(context);
        __hidden_geometry_tests::TestMeshletBuilderRebuildsDeterministically(context);
        __hidden_geometry_tests::TestMeshletBuilderRejectsInvalidInput(context);
        __hidden_geometry_tests::TestRejectsInvalidMeshletDeformationBounds(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

