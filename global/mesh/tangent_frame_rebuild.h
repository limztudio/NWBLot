// limztudio@gmail.com


#pragma once


#include "../containers.h"
#include "../math/frame.h"


struct alignas(Float4) TangentFrameRebuildVertex{
    Float4 position;
    Float4 normal;
    Float4 tangent;
    Float2U uv0;
};
static_assert(IsStandardLayout_V<TangentFrameRebuildVertex>, "TangentFrameRebuildVertex must stay layout-stable");
static_assert(IsTriviallyCopyable_V<TangentFrameRebuildVertex>, "TangentFrameRebuildVertex must stay cheap to copy");
static_assert(sizeof(TangentFrameRebuildVertex) == sizeof(f32) * 16u, "TangentFrameRebuildVertex layout drifted");
static_assert(alignof(TangentFrameRebuildVertex) >= alignof(Float4), "TangentFrameRebuildVertex must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, position) % alignof(Float4)) == 0, "TangentFrameRebuildVertex::position must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, normal) % alignof(Float4)) == 0, "TangentFrameRebuildVertex::normal must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, tangent) % alignof(Float4)) == 0, "TangentFrameRebuildVertex::tangent must stay SIMD-aligned");

struct TangentFrameRebuildResult{
    u32 rebuiltVertexCount = 0;
    u32 degenerateUvTriangleCount = 0;
    u32 fallbackTangentVertexCount = 0;
};
static_assert(IsStandardLayout_V<TangentFrameRebuildResult>, "TangentFrameRebuildResult must stay layout-stable");
static_assert(IsTriviallyCopyable_V<TangentFrameRebuildResult>, "TangentFrameRebuildResult must stay cheap to copy");


namespace TangentFrameRebuildDetail{


inline constexpr f32 s_Epsilon = 0.000001f;
inline constexpr f32 s_TangentOrthogonalityTolerance = 0.001f;
inline constexpr usize s_IndicesPerTriangle = 3u;
inline constexpr u32 s_UvComponentMask = 0x3u;


struct alignas(Float4) TangentFrameAccumulator{
    Float4 normal;
    Float4 tangent;
    Float4 bitangent;
};
static_assert(
    alignof(TangentFrameAccumulator) >= alignof(Float4),
    "TangentFrameAccumulator must keep vector storage lanes aligned"
);
static_assert(
    (sizeof(TangentFrameAccumulator) % alignof(TangentFrameAccumulator)) == 0,
    "TangentFrameAccumulator array stride must keep every element aligned"
);


[[nodiscard]] inline f32 ResolveTangentHandedness(const f32 handedness){
    if(IsFinite(handedness) && Abs(handedness) > s_Epsilon)
        return handedness < 0.0f ? -1.0f : 1.0f;
    return 1.0f;
}

[[nodiscard]] inline bool ValidInputVertex(const SIMDVector position, const SIMDVector uv0){
    return Vector3IsFinite(position) && VectorIsFinite(uv0, s_UvComponentMask);
}

[[nodiscard]] inline bool AccumulateTriangleTangentFrame(
    const SIMDVector uv0,
    const SIMDVector uv1,
    const SIMDVector uv2,
    SIMDVector edge01,
    SIMDVector edge02,
    SIMDVector& outTangent,
    SIMDVector& outBitangent
){
    const SIMDVector uvDelta1 = VectorSubtract(uv1, uv0);
    const SIMDVector uvDelta2 = VectorSubtract(uv2, uv0);
    const SIMDVector determinantVector = Vector2Cross(uvDelta1, uvDelta2);
    const f32 determinant = VectorGetX(determinantVector);
    if(!IsFinite(determinant) || Abs(determinant) <= s_Epsilon)
        return false;

    const SIMDVector du1 = VectorSplatX(uvDelta1);
    const SIMDVector dv1 = VectorSplatY(uvDelta1);
    const SIMDVector du2 = VectorSplatX(uvDelta2);
    const SIMDVector dv2 = VectorSplatY(uvDelta2);
    const SIMDVector inverseDeterminant = VectorReciprocal(determinantVector);
    outTangent = VectorMultiply(
        VectorSubtract(VectorMultiply(edge01, dv2), VectorMultiply(edge02, dv1)),
        inverseDeterminant
    );
    outBitangent = VectorMultiply(
        VectorSubtract(VectorMultiply(edge02, du1), VectorMultiply(edge01, du2)),
        inverseDeterminant
    );
    return FrameValidDirection(outTangent) && FrameValidDirection(outBitangent);
}


};


template<typename ScratchArenaT>
[[nodiscard]] inline bool RebuildTangentFrames(
    ScratchArenaT& scratchArena,
    TangentFrameRebuildVertex* vertices,
    const usize vertexCount,
    const u32* indices,
    const usize indexCount,
    TangentFrameRebuildResult* outResult = nullptr
){
    if(outResult)
        *outResult = TangentFrameRebuildResult{};
    if(
        !vertices
        || !indices
        || vertexCount == 0u
        || indexCount == 0u
        || (indexCount % TangentFrameRebuildDetail::s_IndicesPerTriangle) != 0u
        || vertexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!TangentFrameRebuildDetail::ValidInputVertex(LoadFloat(vertices[vertexIndex].position), LoadFloat(vertices[vertexIndex].uv0)))
            return false;
    }

    Vector<TangentFrameRebuildDetail::TangentFrameAccumulator, ScratchArenaT> accumulators(
        vertexCount,
        TangentFrameRebuildDetail::TangentFrameAccumulator{},
        scratchArena
    );

    TangentFrameRebuildResult result;
    const usize triangleCount = indexCount / TangentFrameRebuildDetail::s_IndicesPerTriangle;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * TangentFrameRebuildDetail::s_IndicesPerTriangle;
        const u32 i0 = indices[indexBase + 0u];
        const u32 i1 = indices[indexBase + 1u];
        const u32 i2 = indices[indexBase + 2u];
        if(i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            return false;
        if(i0 == i1 || i0 == i2 || i1 == i2)
            return false;

        const TangentFrameRebuildVertex& vertex0 = vertices[i0];
        const TangentFrameRebuildVertex& vertex1 = vertices[i1];
        const TangentFrameRebuildVertex& vertex2 = vertices[i2];
        const SIMDVector p0 = LoadFloat(vertex0.position);
        const SIMDVector p1 = LoadFloat(vertex1.position);
        const SIMDVector p2 = LoadFloat(vertex2.position);
        const SIMDVector edge01 = VectorSubtract(p1, p0);
        const SIMDVector edge02 = VectorSubtract(p2, p0);
        const SIMDVector uv0 = LoadFloat(vertex0.uv0);
        const SIMDVector uv1 = LoadFloat(vertex1.uv0);
        const SIMDVector uv2 = LoadFloat(vertex2.uv0);
        const SIMDVector faceNormal = TriangleTests::AreaNormal(p0, p1, p2);
        if(!FrameValidDirection(faceNormal))
            return false;

        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator0 = accumulators[i0];
        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator1 = accumulators[i1];
        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator2 = accumulators[i2];
        StoreFloat(VectorAdd(LoadFloat(accumulator0.normal), faceNormal), &accumulator0.normal);
        StoreFloat(VectorAdd(LoadFloat(accumulator1.normal), faceNormal), &accumulator1.normal);
        StoreFloat(VectorAdd(LoadFloat(accumulator2.normal), faceNormal), &accumulator2.normal);

        SIMDVector tangent = VectorZero();
        SIMDVector bitangent = VectorZero();
        if(TangentFrameRebuildDetail::AccumulateTriangleTangentFrame(uv0, uv1, uv2, edge01, edge02, tangent, bitangent)){
            StoreFloat(VectorAdd(LoadFloat(accumulator0.tangent), tangent), &accumulator0.tangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator1.tangent), tangent), &accumulator1.tangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator2.tangent), tangent), &accumulator2.tangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator0.bitangent), bitangent), &accumulator0.bitangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator1.bitangent), bitangent), &accumulator1.bitangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator2.bitangent), bitangent), &accumulator2.bitangent);
        }
        else{
            ++result.degenerateUvTriangleCount;
        }
    }

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        TangentFrameRebuildVertex& vertex = vertices[vertexIndex];
        const TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator = accumulators[vertexIndex];
        const SIMDVector previousNormal = FrameNormalizeDirection(LoadFloat(vertex.normal), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const SIMDVector normal = FrameNormalizeDirection(LoadFloat(accumulator.normal), previousNormal);
        if(!FrameValidDirection(normal))
            return false;

        const SIMDVector previousTangent = VectorSetW(LoadFloat(vertex.tangent), 0.0f);
        SIMDVector tangentSource = LoadFloat(accumulator.tangent);
        if(!FrameValidDirection(tangentSource)){
            tangentSource = previousTangent;
            ++result.fallbackTangentVertexCount;
        }

        const SIMDVector tangent = FrameResolveTangent(normal, tangentSource, previousTangent);
        if(!FrameValidDirection(tangent) || Abs(VectorGetX(Vector3Dot(normal, tangent))) > TangentFrameRebuildDetail::s_TangentOrthogonalityTolerance)
            return false;

        f32 handedness = TangentFrameRebuildDetail::ResolveTangentHandedness(vertex.tangent.w);
        const SIMDVector bitangent = LoadFloat(accumulator.bitangent);
        if(FrameValidDirection(bitangent)){
            const f32 bitangentSign = VectorGetX(Vector3Dot(Vector3Cross(normal, tangent), bitangent));
            if(IsFinite(bitangentSign) && Abs(bitangentSign) > TangentFrameRebuildDetail::s_Epsilon)
                handedness = bitangentSign < 0.0f ? -1.0f : 1.0f;
        }

        StreamFloat(VectorSetW(normal, 0.0f), &vertex.normal);
        StreamFloat(VectorSetW(tangent, handedness), &vertex.tangent);
        ++result.rebuiltVertexCount;
    }
    StreamFloatFence();

    if(outResult)
        *outResult = result;
    return true;
}

template<typename ScratchArenaT, typename VertexVectorT, typename IndexVectorT>
[[nodiscard]] inline bool RebuildTangentFrames(
    ScratchArenaT& scratchArena,
    VertexVectorT& vertices,
    const IndexVectorT& indices,
    TangentFrameRebuildResult* outResult = nullptr
){
    return RebuildTangentFrames(scratchArena, vertices.data(), vertices.size(), indices.data(), indices.size(), outResult);
}


