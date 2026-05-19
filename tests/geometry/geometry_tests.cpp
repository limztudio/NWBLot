// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/geometry/frame_math.h>
#include <core/geometry/geometry_class.h>
#include <core/geometry/tangent_frame_rebuild.h>

#include <tests/test_context.h>

#include <core/common/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;
using TangentFrameRebuildVertex = NWB::Core::Geometry::TangentFrameRebuildVertex;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


#define NWB_GEOMETRY_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    return Abs(lhs - rhs) <= epsilon;
}

static bool NearlyEqual4(const Float4U& value, const f32 x, const f32 y, const f32 z, const f32 w){
    return
        NearlyEqual(value.x, x)
        && NearlyEqual(value.y, y)
        && NearlyEqual(value.z, z)
        && NearlyEqual(value.w, w)
    ;
}

static bool NearlyEqual3(const Float4& value, const f32 x, const f32 y, const f32 z){
    return
        NearlyEqual(value.x, x)
        && NearlyEqual(value.y, y)
        && NearlyEqual(value.z, z)
    ;
}

static void TestGeometryClassMetadata(TestContext& context){
    using namespace NWB::Core::Geometry;

    struct Case{
        AStringView text;
        u32 geometryClass;
        bool usesSkinning;
        bool usesSkinnedGeometryRuntime;
    };

    const Case cases[] = {
        { "static", GeometryClass::Static, false, false },
        { "skinned", GeometryClass::Skinned, true, true },
    };

    for(const Case& testCase : cases){
        u32 parsedClass = GeometryClass::Invalid;
        NWB_GEOMETRY_TEST_CHECK(context, ParseGeometryClassText(testCase.text, parsedClass));
        NWB_GEOMETRY_TEST_CHECK(context, parsedClass == testCase.geometryClass);
        NWB_GEOMETRY_TEST_CHECK(context, ValidGeometryClass(testCase.geometryClass));
        NWB_GEOMETRY_TEST_CHECK(context, GeometryClassText(testCase.geometryClass) == testCase.text);
        NWB_GEOMETRY_TEST_CHECK(context, GeometryClassUsesSkinning(testCase.geometryClass) == testCase.usesSkinning);
        NWB_GEOMETRY_TEST_CHECK(context, GeometryClassUsesSkinnedGeometryRuntime(testCase.geometryClass) == testCase.usesSkinnedGeometryRuntime);
    }

    u32 parsedClass = GeometryClass::Static;
    NWB_GEOMETRY_TEST_CHECK(context, !ParseGeometryClassText("STATIC", parsedClass));
    NWB_GEOMETRY_TEST_CHECK(context, parsedClass == GeometryClass::Invalid);
    NWB_GEOMETRY_TEST_CHECK(context, GeometryClassText(GeometryClass::Invalid) == AStringView("invalid"));
    NWB_GEOMETRY_TEST_CHECK(context, GeometryClassText(999u) == AStringView("unknown"));
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
    Float4U bitangentValue;
    StoreFloat(normal, &normalValue);
    StoreFloat(bitangent, &bitangentValue);

    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual4(normalValue, 0.0f, 0.0f, 1.0f, 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(tangent)), 1.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(bitangent)), 1.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(normal, tangent)), 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(normal, bitangent)), 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(tangent, bitangent)), 0.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetW(tangent), -1.0f));
    NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(bitangentValue.w, 0.0f));
}

static TangentFrameRebuildVertex MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u, const f32 v){
    TangentFrameRebuildVertex vertex;
    vertex.position = Float4(x, y, z, 0.0f);
    vertex.normal = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    vertex.tangent = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    vertex.uv0 = Float2U(u, v);
    return vertex;
}

static Vector<TangentFrameRebuildVertex> MakeFlatQuadVertices(){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, 1.0f, 0.0f, 1.0f, 1.0f));
    vertices.push_back(MakeVertex(-1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
    return vertices;
}

static void TestRebuildsFlatQuadFrame(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();

    NWB::Core::Geometry::TangentFrameRebuildResult result;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::RebuildTangentFrames(vertices, indices, &result));
    NWB_GEOMETRY_TEST_CHECK(context, result.rebuiltVertexCount == vertices.size());
    NWB_GEOMETRY_TEST_CHECK(context, result.degenerateUvTriangleCount == 0u);
    NWB_GEOMETRY_TEST_CHECK(context, result.fallbackTangentVertexCount == 0u);

    for(const TangentFrameRebuildVertex& vertex : vertices){
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(vertex.tangent, 1.0f, 0.0f, 0.0f));
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(vertex.tangent.w, 1.0f));
    }
}

static void TestDegenerateUvsUseStableTangentFallback(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.uv0 = Float2U(0.0f, 0.0f);

    NWB::Core::Geometry::TangentFrameRebuildResult result;
    NWB_GEOMETRY_TEST_CHECK(context, NWB::Core::Geometry::RebuildTangentFrames(vertices, indices, &result));
    NWB_GEOMETRY_TEST_CHECK(context, result.rebuiltVertexCount == vertices.size());
    NWB_GEOMETRY_TEST_CHECK(context, result.degenerateUvTriangleCount == 2u);
    NWB_GEOMETRY_TEST_CHECK(context, result.fallbackTangentVertexCount == vertices.size());

    for(const TangentFrameRebuildVertex& vertex : vertices){
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(LoadFloat(vertex.tangent))), 1.0f));
        NWB_GEOMETRY_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(LoadFloat(vertex.normal), LoadFloat(vertex.tangent))), 0.0f));
    }
}

static void TestRejectsDegenerateTriangle(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
    const Vector<u32> indices = MakeTriangleIndices();

    NWB_GEOMETRY_TEST_CHECK(context, !NWB::Core::Geometry::RebuildTangentFrames(vertices, indices));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("geometry", [](NWB::Tests::TestContext& context){
    __hidden_geometry_tests::TestGeometryClassMetadata(context);
    __hidden_geometry_tests::TestResolvesCoreFrameMath(context);
    __hidden_geometry_tests::TestRebuildsFlatQuadFrame(context);
    __hidden_geometry_tests::TestDegenerateUvsUseStableTangentFallback(context);
    __hidden_geometry_tests::TestRejectsDegenerateTriangle(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

