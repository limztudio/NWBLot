// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/math/frame.h>
#include <global/math/type.h>
#include <global/math/vector.h>
#include <global/mesh/tangent_frame_rebuild.h>
#include <global/simdmath.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tangent_frame_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TangentFrameFixture{
public:
    TangentFrameFixture() = delete;
    TangentFrameFixture(const TangentFrameFixture&) = delete;
    TangentFrameFixture& operator=(const TangentFrameFixture&) = delete;
    ~TangentFrameFixture() = delete;


public:
    static TangentFrameRebuildVertex MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u, const f32 v)noexcept;
    static Vector<TangentFrameRebuildVertex> MakeFlatQuadVertices();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TangentFrameRebuildVertex TangentFrameFixture::MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u, const f32 v)noexcept{
    TangentFrameRebuildVertex vertex;
    vertex.position = Float4(x, y, z, 0.0f);
    vertex.normal = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    vertex.tangent = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    vertex.uv0 = Float2U(u, v);
    return vertex;
}

Vector<TangentFrameRebuildVertex> TangentFrameFixture::MakeFlatQuadVertices(){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, 1.0f, 0.0f, 1.0f, 1.0f));
    vertices.push_back(MakeVertex(-1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
    return vertices;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, ResolvesFrameMath){
    SIMDVector normal = VectorSet(0.0f, 0.0f, 5.0f, 0.0f);
    SIMDVector tangent = VectorSet(2.0f, 1.0f, 0.0f, -0.25f);
    ::FrameOrthonormalize(
        normal,
        tangent,
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        VectorSet(1.0f, 0.0f, 0.0f, -1.0f)
    );

    const SIMDVector bitangent = ::FrameResolveBitangent(
        normal,
        tangent,
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );

    Float4U normalValue;
    Float4U bitangentValue;
    StoreFloat(normal, normalValue);
    StoreFloat(bitangent, bitangentValue);

    EXPECT_TRUE(NWB::Tests::NearlyEqual4(normalValue, 0.0f, 0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3LengthSq(tangent)), 1.0f));
    EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3LengthSq(bitangent)), 1.0f));
    EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3Dot(normal, tangent)), 0.0f));
    EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3Dot(normal, bitangent)), 0.0f));
    EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3Dot(tangent, bitangent)), 0.0f));
    EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetW(tangent), -1.0f));
    EXPECT_TRUE(NWB::Tests::NearlyEqual(bitangentValue.w, 0.0f));
}

TEST(Global, RebuildsFlatQuadTangentFrame){
    Vector<TangentFrameRebuildVertex> vertices = TangentFrameFixture::MakeFlatQuadVertices();
    const Vector<u32> indices = NWB::Tests::MakeQuadTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);

    TangentFrameRebuildResult result;
    EXPECT_TRUE(::RebuildTangentFrames(scratchArena, vertices, indices, &result));
    EXPECT_EQ(result.rebuiltVertexCount, vertices.size());
    EXPECT_EQ(result.degenerateUvTriangleCount, 0u);
    EXPECT_EQ(result.fallbackTangentVertexCount, 0u);

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE(NWB::Tests::NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        EXPECT_TRUE(NWB::Tests::NearlyEqual3(vertex.tangent, 1.0f, 0.0f, 0.0f));
        EXPECT_TRUE(NWB::Tests::NearlyEqual(vertex.tangent.w, 1.0f));
    }
}

TEST(Global, DegenerateUvsUseStableTangentFallback){
    Vector<TangentFrameRebuildVertex> vertices = TangentFrameFixture::MakeFlatQuadVertices();
    const Vector<u32> indices = NWB::Tests::MakeQuadTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.uv0 = Float2U(0.0f, 0.0f);

    TangentFrameRebuildResult result;
    EXPECT_TRUE(::RebuildTangentFrames(scratchArena, vertices, indices, &result));
    EXPECT_EQ(result.rebuiltVertexCount, vertices.size());
    EXPECT_EQ(result.degenerateUvTriangleCount, s_ExpectedDualCount);
    EXPECT_EQ(result.fallbackTangentVertexCount, vertices.size());

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE(NWB::Tests::NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3LengthSq(LoadFloat(vertex.tangent))), 1.0f));
        EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3Dot(LoadFloat(vertex.normal), LoadFloat(vertex.tangent))), 0.0f));
    }
}

TEST(Global, RejectsDegenerateTangentFrameTriangle){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(TangentFrameFixture::MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(TangentFrameFixture::MakeVertex(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(TangentFrameFixture::MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
    const Vector<u32> indices = NWB::Tests::MakeTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);

    EXPECT_FALSE(::RebuildTangentFrames(scratchArena, vertices, indices));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
