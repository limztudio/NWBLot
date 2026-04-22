// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_geometry_asset.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformableValidation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_BarycentricSumEpsilon = 0.001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;
static constexpr f32 s_RestFrameLengthSquaredEpsilon = 0.000001f;
static constexpr f32 s_RestFrameUnitLengthSquaredEpsilon = 0.01f;
static constexpr f32 s_RestFrameOrthogonalityEpsilon = 0.01f;
static constexpr f32 s_TangentHandednessEpsilon = 0.000001f;
static constexpr f32 s_TangentHandednessUnitEpsilon = 0.001f;
static constexpr f32 s_TriangleAreaLengthSquaredEpsilon = 0.000000000001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline f32 AbsF32(const f32 value){
    return value < 0.0f ? -value : value;
}

[[nodiscard]] inline bool ActiveWeight(const f32 value){
    return value > s_Epsilon || value < -s_Epsilon;
}

[[nodiscard]] inline f32 Clamp01(const f32 value){
    if(value < 0.0f)
        return 0.0f;
    if(value > 1.0f)
        return 1.0f;
    return value;
}

[[nodiscard]] inline bool IsFiniteFloat2(const Float2Data& value){
    const SIMDVector valueVector = LoadFloat(value);
    return !Vector2IsNaN(valueVector) && !Vector2IsInfinite(valueVector);
}

[[nodiscard]] inline bool IsFiniteFloat3(const Float3Data& value){
    const SIMDVector valueVector = LoadFloat(value);
    return !Vector3IsNaN(valueVector) && !Vector3IsInfinite(valueVector);
}

[[nodiscard]] inline bool IsFiniteFloat3(const AlignedFloat4Data& value){
    const SIMDVector valueVector = LoadFloat(value);
    return !Vector3IsNaN(valueVector) && !Vector3IsInfinite(valueVector);
}

[[nodiscard]] inline bool IsFiniteFloat4(const Float4Data& value){
    const SIMDVector valueVector = LoadFloat(value);
    return !Vector4IsNaN(valueVector) && !Vector4IsInfinite(valueVector);
}

[[nodiscard]] inline f32 LengthSquared3(const f32 x, const f32 y, const f32 z){
    return VectorGetX(Vector3LengthSq(VectorSet(x, y, z, 0.0f)));
}

[[nodiscard]] inline f32 Dot3(const Float3Data& lhs, const Float3Data& rhs){
    return VectorGetX(Vector3Dot(LoadFloat(lhs), LoadFloat(rhs)));
}

[[nodiscard]] inline Float3Data Subtract3(const Float3Data& lhs, const Float3Data& rhs){
    Float3Data result;
    StoreFloat(VectorSubtract(LoadFloat(lhs), LoadFloat(rhs)), &result);
    return result;
}

[[nodiscard]] inline Float3Data Cross3(const Float3Data& lhs, const Float3Data& rhs){
    Float3Data result;
    StoreFloat(Vector3Cross(LoadFloat(rhs), LoadFloat(lhs)), &result);
    return result;
}

[[nodiscard]] inline bool NearlyOne(const f32 value, const f32 epsilon = s_BarycentricSumEpsilon){
    return AbsF32(value - 1.0f) <= epsilon;
}

[[nodiscard]] inline bool NearlySignedOne(const f32 value){
    return AbsF32(AbsF32(value) - 1.0f) <= s_TangentHandednessUnitEpsilon;
}

[[nodiscard]] inline bool NearlyUnitLengthSquared(const f32 value){
    return AbsF32(value - 1.0f) <= s_RestFrameUnitLengthSquaredEpsilon;
}

[[nodiscard]] inline bool ValidRestVertex(const DeformableVertexRest& vertex){
    return IsFiniteFloat3(vertex.position)
        && IsFiniteFloat3(vertex.normal)
        && IsFiniteFloat4(vertex.tangent)
        && IsFiniteFloat2(vertex.uv0)
        && IsFiniteFloat4(vertex.color0)
    ;
}

[[nodiscard]] inline bool ValidRestVertexFrameBasis(const DeformableVertexRest& vertex){
    if(!ValidRestVertex(vertex))
        return false;

    const f32 normalLengthSquared = LengthSquared3(vertex.normal.x, vertex.normal.y, vertex.normal.z);
    const f32 tangentLengthSquared = LengthSquared3(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
    const Float3Data tangentVector(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
    const f32 tangentHandedness = AbsF32(vertex.tangent.w);
    const Float3Data frameCross = Cross3(vertex.normal, tangentVector);
    const f32 frameCrossLengthSquared = LengthSquared3(frameCross.x, frameCross.y, frameCross.z);
    if(normalLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentHandedness <= s_TangentHandednessEpsilon
        || !NearlySignedOne(vertex.tangent.w)
        || frameCrossLengthSquared <= s_RestFrameLengthSquaredEpsilon
    )
        return false;

    return true;
}

[[nodiscard]] inline bool ValidRestVertexFrame(const DeformableVertexRest& vertex){
    if(!ValidRestVertexFrameBasis(vertex))
        return false;

    const f32 normalLengthSquared = LengthSquared3(vertex.normal.x, vertex.normal.y, vertex.normal.z);
    const f32 tangentLengthSquared = LengthSquared3(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
    const Float3Data tangentVector(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
    const f32 frameDot = Dot3(vertex.normal, tangentVector);
    return NearlyUnitLengthSquared(normalLengthSquared)
        && NearlyUnitLengthSquared(tangentLengthSquared)
        && AbsF32(frameDot) <= s_RestFrameOrthogonalityEpsilon
    ;
}

[[nodiscard]] inline bool ValidBarycentric(const f32 (&bary)[3], const f32 minimumBarycentric){
    const SIMDVector baryVector = VectorSet(bary[0], bary[1], bary[2], 0.0f);
    const f32 barySum = VectorGetX(Vector3Dot(baryVector, s_SIMDOne));
    return !Vector3IsNaN(baryVector)
        && !Vector3IsInfinite(baryVector)
        && Vector3GreaterOrEqual(baryVector, VectorReplicate(minimumBarycentric))
        && NearlyOne(barySum)
    ;
}

[[nodiscard]] inline bool ValidBarycentric(const AlignedFloat4Data& bary, const f32 minimumBarycentric){
    const SIMDVector baryVector = LoadFloat(bary);
    const f32 barySum = VectorGetX(Vector3Dot(baryVector, s_SIMDOne));
    return !Vector3IsNaN(baryVector)
        && !Vector3IsInfinite(baryVector)
        && Vector3GreaterOrEqual(baryVector, VectorReplicate(minimumBarycentric))
        && NearlyOne(barySum)
    ;
}

[[nodiscard]] inline bool ValidSourceBarycentric(const f32 (&bary)[3]){
    return ValidBarycentric(bary, 0.0f);
}

[[nodiscard]] inline bool ValidSourceBarycentric(const AlignedFloat4Data& bary){
    return ValidBarycentric(bary, 0.0f);
}

[[nodiscard]] inline bool ValidLooseBarycentric(const f32 (&bary)[3]){
    return ValidBarycentric(bary, -s_Epsilon);
}

[[nodiscard]] inline bool ValidLooseBarycentric(const AlignedFloat4Data& bary){
    return ValidBarycentric(bary, -s_Epsilon);
}

[[nodiscard]] inline bool NormalizeSourceBarycentric(const f32 (&bary)[3], f32 (&outBary)[3]){
    if(!ValidLooseBarycentric(bary))
        return false;

    const SIMDVector clampedBary = VectorClamp(VectorSet(bary[0], bary[1], bary[2], 0.0f), VectorZero(), s_SIMDOne);
    const f32 barySum = VectorGetX(Vector3Dot(clampedBary, s_SIMDOne));
    if(!IsFinite(barySum) || barySum <= s_Epsilon)
        return false;

    const SIMDVector normalizedBary = VectorScale(clampedBary, 1.0f / barySum);
    outBary[0] = VectorGetX(normalizedBary);
    outBary[1] = VectorGetY(normalizedBary);
    outBary[2] = VectorGetZ(normalizedBary);
    return ValidSourceBarycentric(outBary);
}

[[nodiscard]] inline bool NormalizeSourceBarycentric(const AlignedFloat4Data& bary, f32 (&outBary)[3]){
    if(!ValidLooseBarycentric(bary))
        return false;

    const SIMDVector clampedBary = VectorClamp(LoadFloat(bary), VectorZero(), s_SIMDOne);
    const f32 barySum = VectorGetX(Vector3Dot(clampedBary, s_SIMDOne));
    if(!IsFinite(barySum) || barySum <= s_Epsilon)
        return false;

    const SIMDVector normalizedBary = VectorScale(clampedBary, 1.0f / barySum);
    outBary[0] = VectorGetX(normalizedBary);
    outBary[1] = VectorGetY(normalizedBary);
    outBary[2] = VectorGetZ(normalizedBary);
    return ValidSourceBarycentric(outBary);
}

[[nodiscard]] inline bool ValidSourceSample(const SourceSample& sample, const u32 sourceTriangleCount){
    return sourceTriangleCount != 0u
        && sample.sourceTri < sourceTriangleCount
        && ValidSourceBarycentric(sample.bary)
    ;
}

[[nodiscard]] inline bool ValidSkinInfluence(const SkinInfluence4& skin){
    const SIMDVector weights = VectorSet(skin.weight[0], skin.weight[1], skin.weight[2], skin.weight[3]);
    const f32 weightSum = VectorGetX(Vector4Dot(weights, s_SIMDOne));
    if(Vector4IsNaN(weights) || Vector4IsInfinite(weights) || !Vector4GreaterOrEqual(weights, VectorZero()))
        return false;

    return NearlyOne(weightSum, s_SkinWeightSumEpsilon);
}

[[nodiscard]] inline bool ValidMorphDelta(const DeformableMorphDelta& delta, const usize vertexCount){
    return delta.vertexId < vertexCount
        && IsFiniteFloat3(delta.deltaPosition)
        && IsFiniteFloat3(delta.deltaNormal)
        && IsFiniteFloat4(delta.deltaTangent)
    ;
}

[[nodiscard]] inline bool ValidMorphPayload(const Vector<DeformableMorph>& morphs, const usize vertexCount){
    if(morphs.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    Core::Alloc::ScratchArena<> scratchArena;
    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, Core::Alloc::ScratchAllocator<NameHash>> seenMorphNames(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        Core::Alloc::ScratchAllocator<NameHash>(scratchArena)
    );
    seenMorphNames.reserve(morphs.size());

    for(const DeformableMorph& morph : morphs){
        if(!morph.name || morph.deltas.empty())
            return false;
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max))
            return false;
        if(!seenMorphNames.insert(morph.name.hash()).second)
            return false;

        HashSet<u32, Hasher<u32>, EqualTo<u32>, Core::Alloc::ScratchAllocator<u32>> seenDeltaVertices(
            0,
            Hasher<u32>(),
            EqualTo<u32>(),
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        );
        seenDeltaVertices.reserve(morph.deltas.size());

        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!ValidMorphDelta(delta, vertexCount))
                return false;
            if(!seenDeltaVertices.insert(delta.vertexId).second)
                return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool ValidTriangle(
    const Vector<DeformableVertexRest>& restVertices,
    const u32 a,
    const u32 b,
    const u32 c)
{
    if(a >= restVertices.size() || b >= restVertices.size() || c >= restVertices.size())
        return false;
    if(a == b || a == c || b == c)
        return false;

    const Float3Data ab = Subtract3(restVertices[b].position, restVertices[a].position);
    const Float3Data ac = Subtract3(restVertices[c].position, restVertices[a].position);
    const Float3Data areaCross = Cross3(ab, ac);
    const f32 areaLengthSquared = LengthSquared3(areaCross.x, areaCross.y, areaCross.z);
    return areaLengthSquared > s_TriangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] inline bool ValidRuntimePayloadArrays(
    const Vector<DeformableVertexRest>& restVertices,
    const Vector<u32>& indices,
    const u32 sourceTriangleCount,
    const Vector<SkinInfluence4>& skin,
    const Vector<SourceSample>& sourceSamples,
    const Vector<DeformableMorph>& morphs)
{
    if(restVertices.empty() || indices.empty())
        return false;
    if(restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || indices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || (indices.size() % 3u) != 0u
    )
        return false;
    if(!skin.empty() && skin.size() != restVertices.size())
        return false;
    if(!sourceSamples.empty() && sourceSamples.size() != restVertices.size())
        return false;
    if(!sourceSamples.empty() && sourceTriangleCount == 0u)
        return false;

    for(const DeformableVertexRest& vertex : restVertices){
        if(!ValidRestVertexFrame(vertex))
            return false;
    }
    for(usize indexBase = 0; indexBase < indices.size(); indexBase += 3u){
        if(!ValidTriangle(
                restVertices,
                indices[indexBase + 0u],
                indices[indexBase + 1u],
                indices[indexBase + 2u]
            )
        )
            return false;
    }
    for(const SkinInfluence4& influence : skin){
        if(!ValidSkinInfluence(influence))
            return false;
    }
    for(const SourceSample& sample : sourceSamples){
        if(!ValidSourceSample(sample, sourceTriangleCount))
            return false;
    }
    return ValidMorphPayload(morphs, restVertices.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

