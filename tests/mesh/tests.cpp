// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/mesh/frame_math.h>
#include <core/mesh/classification.h>
#include <core/mesh/tangent_frame_rebuild.h>

#include <tests/test_context.h>
#include <gtest/gtest.h>

#include <core/common/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;
using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;
using TangentFrameRebuildVertex = NWB::Core::Mesh::TangentFrameRebuildVertex;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


#define NWB_MESH_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestMeshClassMetadata(TestContext& context){
    using namespace NWB::Core::Mesh;

    struct Case{
        AStringView text;
        u32 meshClass;
        bool usesSkinning;
    };

    const Case cases[] = {
        { "static", MeshClass::Static, false },
        { "skinned", MeshClass::Skinned, true },
    };

    for(const Case& testCase : cases){
        u32 parsedClass = MeshClass::Invalid;
        NWB_MESH_TEST_CHECK(context, ParseMeshClassText(testCase.text, parsedClass));
        NWB_MESH_TEST_CHECK(context, parsedClass == testCase.meshClass);
        NWB_MESH_TEST_CHECK(context, ValidMeshClass(testCase.meshClass));
        NWB_MESH_TEST_CHECK(context, MeshClassText(testCase.meshClass) == testCase.text);
        NWB_MESH_TEST_CHECK(context, MeshClassUsesSkinning(testCase.meshClass) == testCase.usesSkinning);
    }

    u32 parsedClass = MeshClass::Static;
    NWB_MESH_TEST_CHECK(context, !ParseMeshClassText("STATIC", parsedClass));
    NWB_MESH_TEST_CHECK(context, parsedClass == MeshClass::Invalid);
    NWB_MESH_TEST_CHECK(context, MeshClassText(MeshClass::Invalid) == AStringView("invalid"));
    NWB_MESH_TEST_CHECK(context, MeshClassText(999u) == AStringView("unknown"));
}

static void TestResolvesCoreFrameMath(TestContext& context){
    SIMDVector normal = VectorSet(0.0f, 0.0f, 5.0f, 0.0f);
    SIMDVector tangent = VectorSet(2.0f, 1.0f, 0.0f, -0.25f);
    NWB::Core::Mesh::FrameOrthonormalize(
        normal,
        tangent,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        VectorSet(1.0f, 0.0f, 0.0f, -1.0f)
    );

    const SIMDVector bitangent = NWB::Core::Mesh::FrameResolveBitangent(
        normal,
        tangent,
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );

    Float4U normalValue;
    Float4U bitangentValue;
    StoreFloat(normal, &normalValue);
    StoreFloat(bitangent, &bitangentValue);

    NWB_MESH_TEST_CHECK(context, NearlyEqual4(normalValue, 0.0f, 0.0f, 1.0f, 0.0f));
    NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(tangent)), 1.0f));
    NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(bitangent)), 1.0f));
    NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(normal, tangent)), 0.0f));
    NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(normal, bitangent)), 0.0f));
    NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(tangent, bitangent)), 0.0f));
    NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetW(tangent), -1.0f));
    NWB_MESH_TEST_CHECK(context, NearlyEqual(bitangentValue.w, 0.0f));
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

    NWB::Core::Mesh::TangentFrameRebuildResult result;
    NWB_MESH_TEST_CHECK(context, NWB::Core::Mesh::RebuildTangentFrames(vertices, indices, &result));
    NWB_MESH_TEST_CHECK(context, result.rebuiltVertexCount == vertices.size());
    NWB_MESH_TEST_CHECK(context, result.degenerateUvTriangleCount == 0u);
    NWB_MESH_TEST_CHECK(context, result.fallbackTangentVertexCount == 0u);

    for(const TangentFrameRebuildVertex& vertex : vertices){
        NWB_MESH_TEST_CHECK(context, NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        NWB_MESH_TEST_CHECK(context, NearlyEqual3(vertex.tangent, 1.0f, 0.0f, 0.0f));
        NWB_MESH_TEST_CHECK(context, NearlyEqual(vertex.tangent.w, 1.0f));
    }
}

static void TestDegenerateUvsUseStableTangentFallback(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.uv0 = Float2U(0.0f, 0.0f);

    NWB::Core::Mesh::TangentFrameRebuildResult result;
    NWB_MESH_TEST_CHECK(context, NWB::Core::Mesh::RebuildTangentFrames(vertices, indices, &result));
    NWB_MESH_TEST_CHECK(context, result.rebuiltVertexCount == vertices.size());
    NWB_MESH_TEST_CHECK(context, result.degenerateUvTriangleCount == 2u);
    NWB_MESH_TEST_CHECK(context, result.fallbackTangentVertexCount == vertices.size());

    for(const TangentFrameRebuildVertex& vertex : vertices){
        NWB_MESH_TEST_CHECK(context, NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3LengthSq(LoadFloat(vertex.tangent))), 1.0f));
        NWB_MESH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(LoadFloat(vertex.normal), LoadFloat(vertex.tangent))), 0.0f));
    }
}

static void TestRejectsDegenerateTriangle(TestContext& context){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
    const Vector<u32> indices = MakeTriangleIndices();

    NWB_MESH_TEST_CHECK(context, !NWB::Core::Mesh::RebuildTangentFrames(vertices, indices));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Mesh, MeshClassMetadata){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestMeshClassMetadata(nwbTestContext);
}

TEST(Mesh, ResolvesCoreFrameMath){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestResolvesCoreFrameMath(nwbTestContext);
}

TEST(Mesh, RebuildsFlatQuadFrame){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestRebuildsFlatQuadFrame(nwbTestContext);
}

TEST(Mesh, DegenerateUvsUseStableTangentFallback){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestDegenerateUvsUseStableTangentFallback(nwbTestContext);
}

TEST(Mesh, RejectsDegenerateTriangle){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestRejectsDegenerateTriangle(nwbTestContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

