// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/expected.h>
#include <global/containers.h>
#include <global/simd/collision_triangle.h>
#include <global/simd/convert.h>
#include <global/simd/frame.h>
#include <global/simplemath.h>
#include <global/span.h>


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


inline constexpr f32 s_TangentOrthogonalityTolerance = 0.001f;
inline constexpr usize s_IndicesPerTriangle = 3u;
inline constexpr u32 s_UvComponentMask = 0x3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TangentFrameAccumulator{
    Double4 normal;
    Double4 tangent;
    Double4 bitangent;
};

struct alignas(SIMDVectorDouble) TangentFrameDirections64{
    SIMDVectorDouble tangent;
    SIMDVectorDouble bitangent;
};
static_assert(alignof(Expected<TangentFrameDirections64>) >= alignof(TangentFrameDirections64));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 ValidInputVertex(const SIMDVectorSingle position, const SIMDVectorSingle uv0)noexcept{
    return Vector3IsFinite(position) && VectorIsFinite(uv0, s_UvComponentMask);
}

[[nodiscard]] inline Expected<TangentFrameDirections64> TriangleTangentDirections64(
    const SIMDVectorDouble uv0,
    const SIMDVectorDouble uv1,
    const SIMDVectorDouble uv2,
    const SIMDVectorDouble edge01,
    const SIMDVectorDouble edge02
)noexcept{
    const SIMDVectorDouble uvDelta1 = VectorDoubleSubtract(uv1, uv0);
    const SIMDVectorDouble uvDelta2 = VectorDoubleSubtract(uv2, uv0);
    const SIMDVectorDouble determinantVector = VectorDouble2Cross(uvDelta1, uvDelta2);
    if(!VectorDoubleIsFinite(determinantVector, VectorDoubleComponentMask::s_DoubleXYZW) || VectorDouble4LessOrEqual(VectorDoubleAbs(determinantVector), VectorDoubleReplicate(static_cast<f64>(s_FrameHandednessEpsilon))))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble du1 = VectorDoubleSplatX(uvDelta1);
    const SIMDVectorDouble dv1 = VectorDoubleSplatY(uvDelta1);
    const SIMDVectorDouble du2 = VectorDoubleSplatX(uvDelta2);
    const SIMDVectorDouble dv2 = VectorDoubleSplatY(uvDelta2);
    const SIMDVectorDouble tangent = VectorDoubleDivide(VectorDoubleSubtract(VectorDoubleMultiply(edge01, dv2), VectorDoubleMultiply(edge02, dv1)), determinantVector);
    const SIMDVectorDouble bitangent = VectorDoubleDivide(VectorDoubleSubtract(VectorDoubleMultiply(edge02, du1), VectorDoubleMultiply(edge01, du2)), determinantVector);
    const SIMDVectorDouble tangentLengthSq = VectorDouble3LengthSq(tangent);
    const SIMDVectorDouble bitangentLengthSq = VectorDouble3LengthSq(bitangent);
    if(
        !VectorDoubleIsFinite(tangentLengthSq, VectorDoubleComponentMask::s_DoubleXYZW)
        || !VectorDoubleIsFinite(bitangentLengthSq, VectorDoubleComponentMask::s_DoubleXYZW)
        || !VectorDouble3Greater(tangentLengthSq, VectorDoubleZero())
        || !VectorDouble3Greater(bitangentLengthSq, VectorDoubleZero())
    )
        return MakeUnexpected(Failure{});
    return TangentFrameDirections64{ VectorDoubleAndInt(tangent, s_DoubleSIMDMask3), VectorDoubleAndInt(bitangent, s_DoubleSIMDMask3) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ScratchArenaT>
[[nodiscard]] inline Expected<TangentFrameRebuildResult> RebuildTangentFrames(
    ScratchArenaT& scratchArena,
    const Span<const u32> indices,
    const Span<TangentFrameRebuildVertex> vertices,
    const f64 minimumTriangleAreaLengthSquared = static_cast<f64>(s_FrameDirectionEpsilon)
){
    const usize vertexCount = vertices.size();
    const usize indexCount = indices.size();
    if(
        !vertices.data()
        || !indices.data()
        || vertexCount == 0u
        || indexCount == 0u
        || (indexCount % TangentFrameRebuildDetail::s_IndicesPerTriangle) != 0u
        || vertexCount > static_cast<usize>(Limit<u32>::s_Max)
        || !IsFinite(minimumTriangleAreaLengthSquared)
        || minimumTriangleAreaLengthSquared < 0.0
    )
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
        const SIMDVectorDouble p0d = VectorDoubleAndInt(ConvertVectorSingleToDouble(p0), s_DoubleSIMDMask3);
        const SIMDVectorDouble edge01 = VectorDoubleSubtract(VectorDoubleAndInt(ConvertVectorSingleToDouble(p1), s_DoubleSIMDMask3), p0d);
        const SIMDVectorDouble edge02 = VectorDoubleSubtract(VectorDoubleAndInt(ConvertVectorSingleToDouble(p2), s_DoubleSIMDMask3), p0d);
        const SIMDVectorDouble uv0 = ConvertVectorSingleToDouble(LoadFloat(vertex0.uv0));
        const SIMDVectorDouble uv1 = ConvertVectorSingleToDouble(LoadFloat(vertex1.uv0));
        const SIMDVectorDouble uv2 = ConvertVectorSingleToDouble(LoadFloat(vertex2.uv0));
        const SIMDVectorDouble faceNormal = BuildTriangleAreaNormal64(p0, p1, p2);
        const SIMDVectorDouble areaLengthSq = TriangleAreaNormalLengthSquared(faceNormal);
        if(!VectorDoubleIsFinite(areaLengthSq, VectorDoubleComponentMask::s_DoubleXYZW) || !(VectorDoubleGetX(areaLengthSq) > minimumTriangleAreaLengthSquared))
            return MakeUnexpected(Failure{});

        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator0 = accumulators[i0];
        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator1 = accumulators[i1];
        TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator2 = accumulators[i2];
        StoreDouble(VectorDoubleAdd(LoadDouble(accumulator0.normal), faceNormal), accumulator0.normal);
        StoreDouble(VectorDoubleAdd(LoadDouble(accumulator1.normal), faceNormal), accumulator1.normal);
        StoreDouble(VectorDoubleAdd(LoadDouble(accumulator2.normal), faceNormal), accumulator2.normal);

        const auto directions = TangentFrameRebuildDetail::TriangleTangentDirections64(uv0, uv1, uv2, edge01, edge02);
        if(directions){
            const SIMDVectorDouble tangent = directions->tangent;
            const SIMDVectorDouble bitangent = directions->bitangent;
            StoreDouble(VectorDoubleAdd(LoadDouble(accumulator0.tangent), tangent), accumulator0.tangent);
            StoreDouble(VectorDoubleAdd(LoadDouble(accumulator1.tangent), tangent), accumulator1.tangent);
            StoreDouble(VectorDoubleAdd(LoadDouble(accumulator2.tangent), tangent), accumulator2.tangent);
            StoreDouble(VectorDoubleAdd(LoadDouble(accumulator0.bitangent), bitangent), accumulator0.bitangent);
            StoreDouble(VectorDoubleAdd(LoadDouble(accumulator1.bitangent), bitangent), accumulator1.bitangent);
            StoreDouble(VectorDoubleAdd(LoadDouble(accumulator2.bitangent), bitangent), accumulator2.bitangent);
        }
        else{
            ++result.degenerateUvTriangleCount;
        }
    }

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        TangentFrameRebuildVertex& vertex = vertices[vertexIndex];
        const TangentFrameRebuildDetail::TangentFrameAccumulator& accumulator = accumulators[vertexIndex];
        const SIMDVectorSingle previousNormal = FrameNormalizeDirection(LoadFloat(vertex.normal), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const auto normalizedNormal = VectorDouble3TryNormalize(LoadDouble(accumulator.normal));
        const SIMDVectorSingle normal = normalizedNormal ? ConvertVectorDoubleToSingle(normalizedNormal->value) : previousNormal;
        if(!FrameValidDirection(normal))
            return MakeUnexpected(Failure{});

        const SIMDVectorSingle vertexTangent = LoadFloat(vertex.tangent);
        const SIMDVectorSingle previousTangent = VectorSetW(vertexTangent, 0.0f);
        const auto normalizedTangent = VectorDouble3TryNormalize(LoadDouble(accumulator.tangent));
        SIMDVectorSingle tangentSource = previousTangent;
        if(normalizedTangent){
            tangentSource = ConvertVectorDoubleToSingle(normalizedTangent->value);
        }
        else{
            ++result.fallbackTangentVertexCount;
        }

        const SIMDVectorSingle tangent = FrameResolveTangent(normal, tangentSource, previousTangent);
        if(!FrameValidDirection(tangent) || Vector4Greater(VectorAbs(Vector3Dot(normal, tangent)), VectorReplicate(TangentFrameRebuildDetail::s_TangentOrthogonalityTolerance)))
            return MakeUnexpected(Failure{});

        const auto normalizedBitangent = VectorDouble3TryNormalize(LoadDouble(accumulator.bitangent));
        const SIMDVectorSingle bitangent = normalizedBitangent ? ConvertVectorDoubleToSingle(normalizedBitangent->value) : VectorZero();
        StoreFloat(VectorSetW(normal, 0.0f), vertex.normal);
        StoreFloat(FrameResolveTangentHandedness(vertexTangent, normal, tangent, bitangent), vertex.tangent);
        ++result.rebuiltVertexCount;
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

