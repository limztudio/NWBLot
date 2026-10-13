// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/simd/type.h>
#include <global/simd/vector.h>
#include <global/simdmath.h>

#include <core/alloc/scratch.h>
#include <core/mesh/tangent_frame_rebuild.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tangent_frame_tests{

using namespace NWB::Core::Mesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using Vector = NWB::Tests::TestVector<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TangentFrameFixture{
public:
    static TangentFrameRebuildVertex MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u, const f32 v)noexcept;
    static Vector<TangentFrameRebuildVertex> MakeFlatQuadVertices();


public:
    TangentFrameFixture() = delete;
    TangentFrameFixture(const TangentFrameFixture&) = delete;
    TangentFrameFixture& operator=(const TangentFrameFixture&) = delete;
    ~TangentFrameFixture() = delete;
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
    vertices.reserve(4u);
    vertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeVertex(1.0f, 1.0f, 0.0f, 1.0f, 1.0f));
    vertices.push_back(MakeVertex(-1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
    return vertices;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Mesh, DegenerateUvsUseStableTangentFallback){
    Vector<TangentFrameRebuildVertex> vertices = TangentFrameFixture::MakeFlatQuadVertices();
    const Vector<u32> indices = NWB::Tests::MakeQuadTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.uv0 = Float2U(0.0f, 0.0f);

    const auto result = RebuildTangentFrames(scratchArena, vertices, indices);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->rebuiltVertexCount, vertices.size());
    EXPECT_EQ(result->degenerateUvTriangleCount, s_ExpectedDualCount);
    EXPECT_EQ(result->fallbackTangentVertexCount, vertices.size());

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE(NWB::Tests::NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3LengthSq(LoadFloat(vertex.tangent))), 1.0f));
        EXPECT_TRUE(NWB::Tests::NearlyEqual(VectorGetX(Vector3Dot(LoadFloat(vertex.normal), LoadFloat(vertex.tangent))), 0.0f));
    }
}

TEST(Mesh, RejectsDegenerateTangentFrameTriangle){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.reserve(3u);
    vertices.push_back(TangentFrameFixture::MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(TangentFrameFixture::MakeVertex(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(TangentFrameFixture::MakeVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
    const Vector<u32> indices = NWB::Tests::MakeTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);

    EXPECT_FALSE(RebuildTangentFrames(scratchArena, vertices, indices));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

