// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/geometry/tangent_frame_rebuild.h>

#include <tests/test_context.h>

#include <core/common/common.h>

#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
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

