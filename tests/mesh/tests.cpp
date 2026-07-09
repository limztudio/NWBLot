// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/core/mesh/frame_math.h>
#include <global/core/mesh/classification.h>
#include <global/core/mesh/tangent_frame_rebuild.h>

#include <tests/test_context.h>
#include <gtest/gtest.h>

#include <global/core/common/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_tests{


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


TEST(Mesh, MeshClassMetadata){
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
        EXPECT_TRUE(ParseMeshClassText(testCase.text, parsedClass));
        EXPECT_EQ(parsedClass, testCase.meshClass);
        EXPECT_TRUE(ValidMeshClass(testCase.meshClass));
        EXPECT_EQ(MeshClassText(testCase.meshClass), testCase.text);
        EXPECT_EQ(MeshClassUsesSkinning(testCase.meshClass), testCase.usesSkinning);
    }

    u32 parsedClass = MeshClass::Static;
    EXPECT_FALSE(ParseMeshClassText("STATIC", parsedClass));
    EXPECT_EQ(parsedClass, MeshClass::Invalid);
    EXPECT_EQ(MeshClassText(MeshClass::Invalid), AStringView("invalid"));
    EXPECT_EQ(MeshClassText(999u), AStringView("unknown"));
}

TEST(Mesh, ResolvesCoreFrameMath){
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

    EXPECT_TRUE(NearlyEqual4(normalValue, 0.0f, 0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3LengthSq(tangent)), 1.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3LengthSq(bitangent)), 1.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3Dot(normal, tangent)), 0.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3Dot(normal, bitangent)), 0.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3Dot(tangent, bitangent)), 0.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetW(tangent), -1.0f));
    EXPECT_TRUE(NearlyEqual(bitangentValue.w, 0.0f));
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

TEST(Mesh, RebuildsFlatQuadFrame){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();

    NWB::Core::Mesh::TangentFrameRebuildResult result;
    EXPECT_TRUE(NWB::Core::Mesh::RebuildTangentFrames(vertices, indices, &result));
    EXPECT_EQ(result.rebuiltVertexCount, vertices.size());
    EXPECT_EQ(result.degenerateUvTriangleCount, 0u);
    EXPECT_EQ(result.fallbackTangentVertexCount, 0u);

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE(NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        EXPECT_TRUE(NearlyEqual3(vertex.tangent, 1.0f, 0.0f, 0.0f));
        EXPECT_TRUE(NearlyEqual(vertex.tangent.w, 1.0f));
    }
}

TEST(Mesh, DegenerateUvsUseStableTangentFallback){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.uv0 = Float2U(0.0f, 0.0f);

    NWB::Core::Mesh::TangentFrameRebuildResult result;
    EXPECT_TRUE(NWB::Core::Mesh::RebuildTangentFrames(vertices, indices, &result));
    EXPECT_EQ(result.rebuiltVertexCount, vertices.size());
    EXPECT_EQ(result.degenerateUvTriangleCount, 2u);
    EXPECT_EQ(result.fallbackTangentVertexCount, vertices.size());

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE(NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3LengthSq(LoadFloat(vertex.tangent))), 1.0f));
        EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3Dot(LoadFloat(vertex.normal), LoadFloat(vertex.tangent))), 0.0f));
    }
}

TEST(Mesh, RejectsDegenerateTriangle){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
    const Vector<u32> indices = MakeTriangleIndices();

    EXPECT_FALSE(NWB::Core::Mesh::RebuildTangentFrames(vertices, indices));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

