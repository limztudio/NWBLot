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


// Calculation scratch for one rebuild; never retained in mesh or asset storage.
struct alignas(Float4) TangentFrameAccumulator{
    SIMDVectorSingle normal = {};
    SIMDVectorSingle tangent = {};
    SIMDVectorSingle bitangent = {};
};
static_assert(alignof(TangentFrameAccumulator) >= alignof(Float4), "TangentFrameAccumulator must keep SIMD calculation lanes aligned");
static_assert((sizeof(TangentFrameAccumulator) % alignof(TangentFrameAccumulator)) == s_AlignRemainderZero, "TangentFrameAccumulator array stride must keep every element aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline f32 ResolveTangentHandedness(const f32 handedness)noexcept{
    if(IsFinite(handedness) && Abs(handedness) > s_Epsilon)
        return handedness < 0.0f ? -1.0f : 1.0f;
    return 1.0f;
}

[[nodiscard]] inline bool ValidInputVertex(const SIMDVectorSingle position, const SIMDVectorSingle uv0)noexcept{
    return Vector3IsFinite(position) && VectorIsFinite(uv0, s_UvComponentMask);
}

// Transient SIMD calculation result consumed by the owning rebuild boundary.
struct TangentFrameDirections{
    SIMDVectorSingle tangent;
    SIMDVectorSingle bitangent;
};

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
[[nodiscard]] inline Expected<TangentFrameRebuildResult> RebuildTangentFrames(ScratchArenaT& scratchArena, TangentFrameRebuildVertex* vertices, const usize vertexCount, const u32* indices, const usize indexCount){
    if(!vertices || !indices || vertexCount == 0u || indexCount == 0u || (indexCount % TangentFrameRebuildDetail::s_IndicesPerTriangle) != 0u || vertexCount > static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!TangentFrameRebuildDetail::ValidInputVertex(LoadFloat(vertices[vertexIndex].position), LoadFloat(vertices[vertexIndex].uv0)))
            return MakeUnexpected(Failure{});
    }

    Vector<TangentFrameRebuildDetail::TangentFrameAccumulator, ScratchArenaT> accumulators(vertexCount, TangentFrameRebuildDetail::TangentFrameAccumulator{}, scratchArena);

    TangentFrameRebuildResult result;
    const usize triangleCount = indexCount / TangentFrameRebuildDetail::s_IndicesPerTriangle;
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
        accumulator0.normal = VectorAdd(accumulator0.normal, faceNormal);
        accumulator1.normal = VectorAdd(accumulator1.normal, faceNormal);
        accumulator2.normal = VectorAdd(accumulator2.normal, faceNormal);

        const auto directions = TangentFrameRebuildDetail::AccumulateTriangleTangentFrame(uv0, uv1, uv2, edge01, edge02);
        if(directions){
            const SIMDVectorSingle tangent = directions->tangent;
            const SIMDVectorSingle bitangent = directions->bitangent;
            accumulator0.tangent = VectorAdd(accumulator0.tangent, tangent);
            accumulator1.tangent = VectorAdd(accumulator1.tangent, tangent);
            accumulator2.tangent = VectorAdd(accumulator2.tangent, tangent);
            accumulator0.bitangent = VectorAdd(accumulator0.bitangent, bitangent);
            accumulator1.bitangent = VectorAdd(accumulator1.bitangent, bitangent);
            accumulator2.bitangent = VectorAdd(accumulator2.bitangent, bitangent);
        }
        else{
            ++result.degenerateUvTriangleCount;
        }
    }

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        TangentFrameRebuildVertex& vertex = vertices[vertexIndex];
        const TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator = accumulators[vertexIndex];
        const SIMDVectorSingle previousNormal = FrameNormalizeDirection(LoadFloat(vertex.normal), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const SIMDVectorSingle normal = FrameNormalizeDirection(accumulator.normal, previousNormal);
        if(!FrameValidDirection(normal))
            return MakeUnexpected(Failure{});

        const SIMDVectorSingle previousTangent = VectorSetW(LoadFloat(vertex.tangent), 0.0f);
        SIMDVectorSingle tangentSource = accumulator.tangent;
        if(!FrameValidDirection(tangentSource)){
            tangentSource = previousTangent;
            ++result.fallbackTangentVertexCount;
        }

        const SIMDVectorSingle tangent = FrameResolveTangent(normal, tangentSource, previousTangent);
        if(!FrameValidDirection(tangent) || Vector4Greater(VectorAbs(Vector3Dot(normal, tangent)), VectorReplicate(TangentFrameRebuildDetail::s_TangentOrthogonalityTolerance)))
            return MakeUnexpected(Failure{});

        f32 handedness = TangentFrameRebuildDetail::ResolveTangentHandedness(vertex.tangent.w);
        const SIMDVectorSingle bitangent = accumulator.bitangent;
        if(FrameValidDirection(bitangent)){
            const SIMDVectorSingle bitangentSign = Vector3Dot(Vector3Cross(normal, tangent), bitangent);
            if(VectorIsFinite(bitangentSign, VectorComponentMask::s_XYZW) && Vector4Greater(VectorAbs(bitangentSign), VectorReplicate(TangentFrameRebuildDetail::s_Epsilon)))
                handedness = Vector4Less(bitangentSign, VectorZero()) ? -1.0f : 1.0f;
        }

        StoreFloat(VectorSetW(normal, 0.0f), vertex.normal);
        StoreFloat(VectorSetW(tangent, handedness), vertex.tangent);
        ++result.rebuiltVertexCount;
    }

    return result;
}

template<typename ScratchArenaT, typename VertexVectorT, typename IndexVectorT>
[[nodiscard]] inline Expected<TangentFrameRebuildResult> RebuildTangentFrames(ScratchArenaT& scratchArena, VertexVectorT& vertices, const IndexVectorT& indices){
    return RebuildTangentFrames(scratchArena, vertices.data(), vertices.size(), indices.data(), indices.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

