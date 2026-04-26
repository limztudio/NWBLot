// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/geometry/mesh_topology.h>
#include <core/geometry/tangent_frame_rebuild.h>

#include <tests/test_context.h>

#include <core/common/common.h>

#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using MeshTopologyEdge = NWB::Core::Geometry::MeshTopologyEdge;
using TangentFrameRebuildVertex = NWB::Core::Geometry::TangentFrameRebuildVertex;


#define NWB_GEOMETRY_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    return Abs(lhs - rhs) <= epsilon;
}

static bool NearlyEqual3(const Float3U& value, const f32 x, const f32 y, const f32 z){
    return NearlyEqual(value.x, x)
        && NearlyEqual(value.y, y)
        && NearlyEqual(value.z, z)
    ;
}

static bool NearlyEqual4(const Float4U& value, const f32 x, const f32 y, const f32 z, const f32 w){
    return NearlyEqual(value.x, x)
        && NearlyEqual(value.y, y)
        && NearlyEqual(value.z, z)
        && NearlyEqual(value.w, w)
    ;
}

static TangentFrameRebuildVertex MakeVertex(
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 u,
    const f32 v)
{
    TangentFrameRebuildVertex vertex;
    vertex.position = Float3U(x, y, z);
    vertex.uv0 = Float2U(u, v);
    vertex.normal = Float3U(0.0f, 0.0f, 0.0f);
    vertex.tangent = Float4U(0.0f, 0.0f, 0.0f, 0.0f);
    return vertex;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "geometry tests failed: common initialization failed\n";
        return -1;
    }

    __hidden_geometry_tests::TestContext context;
    __hidden_geometry_tests::TestRebuildsFlatQuadFrame(context);
    __hidden_geometry_tests::TestDegenerateUvsUseStableTangentFallback(context);
    __hidden_geometry_tests::TestRejectsDegenerateTriangle(context);
    __hidden_geometry_tests::TestOrdersBoundaryLoopCounterClockwise(context);
    __hidden_geometry_tests::TestRejectsBranchedBoundaryLoop(context);
    __hidden_geometry_tests::TestAppendsWallTrianglePairs(context);
    __hidden_geometry_tests::TestRejectsMalformedWallTrianglePairs(context);
    if(context.failed != 0u){
        NWB_CERR << "geometry tests failed: " << context.failed << " failed, " << context.passed << " passed\n";
        return -1;
    }

    NWB_COUT << "geometry tests passed: " << context.passed << '\n';
    return 0;
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

