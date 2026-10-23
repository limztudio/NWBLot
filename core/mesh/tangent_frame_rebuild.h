// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/expected.h>
#include <global/containers.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
static constexpr usize s_AlignRemainderZero = 0;
static_assert((offsetof(TangentFrameRebuildVertex, position) % alignof(Float4)) == s_AlignRemainderZero, "TangentFrameRebuildVertex::position must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, normal) % alignof(Float4)) == s_AlignRemainderZero, "TangentFrameRebuildVertex::normal must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, tangent) % alignof(Float4)) == s_AlignRemainderZero, "TangentFrameRebuildVertex::tangent must stay SIMD-aligned");

struct TangentFrameRebuildResult{
    u32 rebuiltVertexCount = 0;
    u32 degenerateUvTriangleCount = 0;
    u32 fallbackTangentVertexCount = 0;
};
static_assert(IsStandardLayout_V<TangentFrameRebuildResult>, "TangentFrameRebuildResult must stay layout-stable");
static_assert(IsTriviallyCopyable_V<TangentFrameRebuildResult>, "TangentFrameRebuildResult must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TangentFrameRebuildDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_Epsilon = 0.000001f;
inline constexpr f32 s_TangentOrthogonalityTolerance = 0.001f;
inline constexpr usize s_IndicesPerTriangle = 3u;
inline constexpr u32 s_UvComponentMask = 0x3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-vertex sums for one rebuild. They stay in memory between triangles, so they use Float storage.
struct TangentFrameAccumulator{
    Float4 normal;
    Float4 tangent;
    Float4 bitangent;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle ResolveTangentHandedness(
    const SIMDVectorSingle tangentWithHandedness,
    const SIMDVectorSingle normal,
    const SIMDVectorSingle tangent,
    const SIMDVectorSingle bitangent
)noexcept{
    const SIMDVectorSingle value = VectorSplatW(tangentWithHandedness);
    SIMDVectorSingle handedness = s_SIMDOne;
    if(VectorIsFinite(value, VectorComponentMask::s_XYZW)){
        const SIMDVectorSingle sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(value, VectorZero()));
        handedness = VectorSelect(s_SIMDOne, sign, VectorGreater(VectorAbs(value), VectorReplicate(s_Epsilon)));
    }
    if(FrameValidDirection(bitangent)){
        const SIMDVectorSingle bitangentSign = Vector3Dot(Vector3Cross(normal, tangent), bitangent);
        if(VectorIsFinite(bitangentSign, VectorComponentMask::s_XYZW) && Vector4Greater(VectorAbs(bitangentSign), VectorReplicate(s_Epsilon)))
            handedness = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(bitangentSign, VectorZero()));
    }
    return VectorSelect(tangent, handedness, s_SIMDMaskW);
}

[[nodiscard]] inline bool ValidInputVertex(const SIMDVectorSingle position, const SIMDVectorSingle uv0)noexcept{
    return Vector3IsFinite(position) && VectorIsFinite(uv0, s_UvComponentMask);
}

// Transient SIMD calculation result consumed by the owning rebuild boundary.
struct alignas(SIMDVectorSingle) TangentFrameDirections{
    SIMDVectorSingle tangent;
    SIMDVectorSingle bitangent;
};
static_assert(alignof(Expected<TangentFrameDirections>) >= alignof(TangentFrameDirections));

[[nodiscard]] inline Expected<TangentFrameDirections> AccumulateTriangleTangentFrame(const SIMDVectorSingle uv0, const SIMDVectorSingle uv1, const SIMDVectorSingle uv2, SIMDVectorSingle edge01, SIMDVectorSingle edge02)noexcept{
    const SIMDVectorSingle uvDelta1 = VectorSubtract(uv1, uv0);
    const SIMDVectorSingle uvDelta2 = VectorSubtract(uv2, uv0);
    const SIMDVectorSingle determinantVector = Vector2Cross(uvDelta1, uvDelta2);
    if(!VectorIsFinite(determinantVector, VectorComponentMask::s_XYZW) || Vector4LessOrEqual(VectorAbs(determinantVector), VectorReplicate(s_Epsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle du1 = VectorSplatX(uvDelta1);
    const SIMDVectorSingle dv1 = VectorSplatY(uvDelta1);
    const SIMDVectorSingle du2 = VectorSplatX(uvDelta2);
    const SIMDVectorSingle dv2 = VectorSplatY(uvDelta2);
    const SIMDVectorSingle inverseDeterminant = VectorReciprocal(determinantVector);
    const SIMDVectorSingle tangent = VectorMultiply(VectorSubtract(VectorMultiply(edge01, dv2), VectorMultiply(edge02, dv1)), inverseDeterminant);
    const SIMDVectorSingle bitangent = VectorMultiply(VectorSubtract(VectorMultiply(edge02, du1), VectorMultiply(edge01, du2)), inverseDeterminant);
    if(!FrameValidDirection(tangent) || !FrameValidDirection(bitangent))
        return MakeUnexpected(Failure{});
    return TangentFrameDirections{ tangent, bitangent };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ScratchArenaT>
[[nodiscard]] inline Expected<TangentFrameRebuildResult> RebuildTangentFrames(ScratchArenaT& scratchArena, const usize vertexCount, const u32* indices, const usize indexCount, TangentFrameRebuildVertex* vertices){
    if(!vertices || !indices || vertexCount == 0u || indexCount == 0u || (indexCount % TangentFrameRebuildDetail::s_IndicesPerTriangle) != 0u || vertexCount > static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    const usize triangleCount = indexCount / TangentFrameRebuildDetail::s_IndicesPerTriangle;
    if(triangleCount > static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!TangentFrameRebuildDetail::ValidInputVertex(LoadFloat(vertices[vertexIndex].position), LoadFloat(vertices[vertexIndex].uv0)))
            return MakeUnexpected(Failure{});
    }

    Vector<TangentFrameRebuildDetail::TangentFrameAccumulator, ScratchArenaT> accumulators(vertexCount, TangentFrameRebuildDetail::TangentFrameAccumulator{}, scratchArena);

    TangentFrameRebuildResult result;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * TangentFrameRebuildDetail::s_IndicesPerTriangle;
        const u32 i0 = indices[indexBase + 0u];
        const u32 i1 = indices[indexBase + 1u];
        const u32 i2 = indices[indexBase + 2u];
        if(i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            return MakeUnexpected(Failure{});
        if(i0 == i1 || i0 == i2 || i1 == i2)
            return MakeUnexpected(Failure{});

        const TangentFrameRebuildVertex& vertex0 = vertices[i0];
        const TangentFrameRebuildVertex& vertex1 = vertices[i1];
        const TangentFrameRebuildVertex& vertex2 = vertices[i2];
        const SIMDVectorSingle p0 = LoadFloat(vertex0.position);
        const SIMDVectorSingle p1 = LoadFloat(vertex1.position);
        const SIMDVectorSingle p2 = LoadFloat(vertex2.position);
        const SIMDVectorSingle edge01 = VectorSubtract(p1, p0);
        const SIMDVectorSingle edge02 = VectorSubtract(p2, p0);
        const SIMDVectorSingle uv0 = LoadFloat(vertex0.uv0);
        const SIMDVectorSingle uv1 = LoadFloat(vertex1.uv0);
        const SIMDVectorSingle uv2 = LoadFloat(vertex2.uv0);
        const SIMDVectorSingle faceNormal = TriangleTests::AreaNormal(p0, p1, p2);
        if(!FrameValidDirection(faceNormal))
            return MakeUnexpected(Failure{});

        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator0 = accumulators[i0];
        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator1 = accumulators[i1];
        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator2 = accumulators[i2];
        StoreFloat(VectorAdd(LoadFloat(accumulator0.normal), faceNormal), accumulator0.normal);
        StoreFloat(VectorAdd(LoadFloat(accumulator1.normal), faceNormal), accumulator1.normal);
        StoreFloat(VectorAdd(LoadFloat(accumulator2.normal), faceNormal), accumulator2.normal);

        const auto directions = TangentFrameRebuildDetail::AccumulateTriangleTangentFrame(uv0, uv1, uv2, edge01, edge02);
        if(directions){
            const SIMDVectorSingle tangent = directions->tangent;
            const SIMDVectorSingle bitangent = directions->bitangent;
            StoreFloat(VectorAdd(LoadFloat(accumulator0.tangent), tangent), accumulator0.tangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator1.tangent), tangent), accumulator1.tangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator2.tangent), tangent), accumulator2.tangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator0.bitangent), bitangent), accumulator0.bitangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator1.bitangent), bitangent), accumulator1.bitangent);
            StoreFloat(VectorAdd(LoadFloat(accumulator2.bitangent), bitangent), accumulator2.bitangent);
        }
        else{
            ++result.degenerateUvTriangleCount;
        }
    }

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        TangentFrameRebuildVertex& vertex = vertices[vertexIndex];
        const TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator = accumulators[vertexIndex];
        const SIMDVectorSingle previousNormal = FrameNormalizeDirection(LoadFloat(vertex.normal), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const SIMDVectorSingle normal = FrameNormalizeDirection(LoadFloat(accumulator.normal), previousNormal);
        if(!FrameValidDirection(normal))
            return MakeUnexpected(Failure{});

        const SIMDVectorSingle vertexTangent = LoadFloat(vertex.tangent);
        const SIMDVectorSingle previousTangent = VectorSetW(vertexTangent, 0.0f);
        SIMDVectorSingle tangentSource = LoadFloat(accumulator.tangent);
        if(!FrameValidDirection(tangentSource)){
            tangentSource = previousTangent;
            ++result.fallbackTangentVertexCount;
        }

        const SIMDVectorSingle tangent = FrameResolveTangent(normal, tangentSource, previousTangent);
        if(!FrameValidDirection(tangent) || Vector4Greater(VectorAbs(Vector3Dot(normal, tangent)), VectorReplicate(TangentFrameRebuildDetail::s_TangentOrthogonalityTolerance)))
            return MakeUnexpected(Failure{});

        StoreFloat(VectorSetW(normal, 0.0f), vertex.normal);
        StoreFloat(TangentFrameRebuildDetail::ResolveTangentHandedness(vertexTangent, normal, tangent, LoadFloat(accumulator.bitangent)), vertex.tangent);
        ++result.rebuiltVertexCount;
    }

    return result;
}

template<typename ScratchArenaT, typename IndexVectorT, typename VertexVectorT>
[[nodiscard]] inline Expected<TangentFrameRebuildResult> RebuildTangentFrames(ScratchArenaT& scratchArena, const IndexVectorT& indices, VertexVectorT& vertices){
    return RebuildTangentFrames(scratchArena, vertices.size(), indices.data(), indices.size(), vertices.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

