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
using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;
using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;
using TangentFrameRebuildVertex = NWB::Core::Mesh::TangentFrameRebuildVertex;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestMeshClassMetadata(){
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
        EXPECT_TRUE((ParseMeshClassText(testCase.text, parsedClass)));
        EXPECT_TRUE((parsedClass == testCase.meshClass));
        EXPECT_TRUE((ValidMeshClass(testCase.meshClass)));
        EXPECT_TRUE((MeshClassText(testCase.meshClass) == testCase.text));
        EXPECT_TRUE((MeshClassUsesSkinning(testCase.meshClass) == testCase.usesSkinning));
    }

    u32 parsedClass = MeshClass::Static;
    EXPECT_TRUE((!ParseMeshClassText("STATIC", parsedClass)));
    EXPECT_TRUE((parsedClass == MeshClass::Invalid));
    EXPECT_TRUE((MeshClassText(MeshClass::Invalid) == AStringView("invalid")));
    EXPECT_TRUE((MeshClassText(999u) == AStringView("unknown")));
}

static void TestResolvesCoreFrameMath(){
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

    EXPECT_TRUE((NearlyEqual4(normalValue, 0.0f, 0.0f, 1.0f, 0.0f)));
    EXPECT_TRUE((NearlyEqual(VectorGetX(Vector3LengthSq(tangent)), 1.0f)));
    EXPECT_TRUE((NearlyEqual(VectorGetX(Vector3LengthSq(bitangent)), 1.0f)));
    EXPECT_TRUE((NearlyEqual(VectorGetX(Vector3Dot(normal, tangent)), 0.0f)));
    EXPECT_TRUE((NearlyEqual(VectorGetX(Vector3Dot(normal, bitangent)), 0.0f)));
    EXPECT_TRUE((NearlyEqual(VectorGetX(Vector3Dot(tangent, bitangent)), 0.0f)));
    EXPECT_TRUE((NearlyEqual(VectorGetW(tangent), -1.0f)));
    EXPECT_TRUE((NearlyEqual(bitangentValue.w, 0.0f)));
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

static void TestRebuildsFlatQuadFrame(){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();

    NWB::Core::Mesh::TangentFrameRebuildResult result;
    EXPECT_TRUE((NWB::Core::Mesh::RebuildTangentFrames(vertices, indices, &result)));
    EXPECT_TRUE((result.rebuiltVertexCount == vertices.size()));
    EXPECT_TRUE((result.degenerateUvTriangleCount == 0u));
    EXPECT_TRUE((result.fallbackTangentVertexCount == 0u));

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE((NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f)));
        EXPECT_TRUE((NearlyEqual3(vertex.tangent, 1.0f, 0.0f, 0.0f)));
        EXPECT_TRUE((NearlyEqual(vertex.tangent.w, 1.0f)));
    }
}

static void TestDegenerateUvsUseStableTangentFallback(){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.uv0 = Float2U(0.0f, 0.0f);

    NWB::Core::Mesh::TangentFrameRebuildResult result;
    EXPECT_TRUE((NWB::Core::Mesh::RebuildTangentFrames(vertices, indices, &result)));
    EXPECT_TRUE((result.rebuiltVertexCount == vertices.size()));
    EXPECT_TRUE((result.degenerateUvTriangleCount == 2u));
    EXPECT_TRUE((result.fallbackTangentVertexCount == vertices.size()));

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE((NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f)));
        EXPECT_TRUE((NearlyEqual(VectorGetX(Vector3LengthSq(LoadFloat(vertex.tangent))), 1.0f)));
        EXPECT_TRUE((NearlyEqual(VectorGetX(Vector3Dot(LoadFloat(vertex.normal), LoadFloat(vertex.tangent))), 0.0f)));
    }
}

static void TestRejectsDegenerateTriangle(){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
    const Vector<u32> indices = MakeTriangleIndices();

    EXPECT_TRUE((!NWB::Core::Mesh::RebuildTangentFrames(vertices, indices)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Mesh, MeshClassMetadata){
    __hidden_tests::TestMeshClassMetadata();
}

TEST(Mesh, ResolvesCoreFrameMath){
    __hidden_tests::TestResolvesCoreFrameMath();
}

TEST(Mesh, RebuildsFlatQuadFrame){
    __hidden_tests::TestRebuildsFlatQuadFrame();
}

TEST(Mesh, DegenerateUvsUseStableTangentFallback){
    __hidden_tests::TestDegenerateUvsUseStableTangentFallback();
}

TEST(Mesh, RejectsDegenerateTriangle){
    __hidden_tests::TestRejectsDegenerateTriangle();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

