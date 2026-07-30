// limztudio@gmail.com


using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;
using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;


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

static TangentFrameRebuildVertex MakeTangentFrameVertex(const f32 x, const f32 y, const f32 z, const f32 u, const f32 v){
    TangentFrameRebuildVertex vertex;
    vertex.position = Float4(x, y, z, 0.0f);
    vertex.normal = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    vertex.tangent = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    vertex.uv0 = Float2U(u, v);
    return vertex;
}

static Vector<TangentFrameRebuildVertex> MakeFlatTangentFrameQuadVertices(){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeTangentFrameVertex(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeTangentFrameVertex(1.0f, -1.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeTangentFrameVertex(1.0f, 1.0f, 0.0f, 1.0f, 1.0f));
    vertices.push_back(MakeTangentFrameVertex(-1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
    return vertices;
}

TEST(Global, RebuildsFlatQuadTangentFrame){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatTangentFrameQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);

    TangentFrameRebuildResult result;
    EXPECT_TRUE(::RebuildTangentFrames(scratchArena, vertices, indices, &result));
    EXPECT_EQ(result.rebuiltVertexCount, vertices.size());
    EXPECT_EQ(result.degenerateUvTriangleCount, 0u);
    EXPECT_EQ(result.fallbackTangentVertexCount, 0u);

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE(NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        EXPECT_TRUE(NearlyEqual3(vertex.tangent, 1.0f, 0.0f, 0.0f));
        EXPECT_TRUE(NearlyEqual(vertex.tangent.w, 1.0f));
    }
}

TEST(Global, DegenerateUvsUseStableTangentFallback){
    Vector<TangentFrameRebuildVertex> vertices = MakeFlatTangentFrameQuadVertices();
    const Vector<u32> indices = MakeQuadTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    for(TangentFrameRebuildVertex& vertex : vertices)
        vertex.uv0 = Float2U(0.0f, 0.0f);

    TangentFrameRebuildResult result;
    EXPECT_TRUE(::RebuildTangentFrames(scratchArena, vertices, indices, &result));
    EXPECT_EQ(result.rebuiltVertexCount, vertices.size());
    EXPECT_EQ(result.degenerateUvTriangleCount, 2u);
    EXPECT_EQ(result.fallbackTangentVertexCount, vertices.size());

    for(const TangentFrameRebuildVertex& vertex : vertices){
        EXPECT_TRUE(NearlyEqual3(vertex.normal, 0.0f, 0.0f, 1.0f));
        EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3LengthSq(LoadFloat(vertex.tangent))), 1.0f));
        EXPECT_TRUE(NearlyEqual(VectorGetX(Vector3Dot(LoadFloat(vertex.normal), LoadFloat(vertex.tangent))), 0.0f));
    }
}

TEST(Global, RejectsDegenerateTangentFrameTriangle){
    Vector<TangentFrameRebuildVertex> vertices;
    vertices.push_back(MakeTangentFrameVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeTangentFrameVertex(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
    vertices.push_back(MakeTangentFrameVertex(0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
    const Vector<u32> indices = MakeTriangleIndices();
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);

    EXPECT_FALSE(::RebuildTangentFrames(scratchArena, vertices, indices));
}


