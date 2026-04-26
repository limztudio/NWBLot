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


namespace MorphPayloadFailure{
    enum Enum : u8{
        None,
        MorphCountLimit,
        EmptyMorph,
        DuplicateMorphName,
        MorphDeltaCountLimit,
        InvalidMorphDelta,
        DuplicateMorphDeltaVertex,
    };
};

struct MorphPayloadFailureInfo{
    MorphPayloadFailure::Enum reason = MorphPayloadFailure::None;
    usize morphIndex = 0;
    usize deltaIndex = 0;
    u32 vertexId = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline MorphPayloadFailureInfo MakeMorphPayloadFailure(
    const MorphPayloadFailure::Enum reason,
    const usize morphIndex = 0,
    const usize deltaIndex = 0,
    const u32 vertexId = 0)
{
    MorphPayloadFailureInfo info;
    info.reason = reason;
    info.morphIndex = morphIndex;
    info.deltaIndex = deltaIndex;
    info.vertexId = vertexId;
    return info;
}

[[nodiscard]] inline bool ActiveWeight(const f32 value){
    return value > s_Epsilon || value < -s_Epsilon;
}

[[nodiscard]] inline bool FiniteVector(SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] inline bool ValidRestVertex(const DeformableVertexRest& vertex){
    const SIMDVector position = LoadFloat(vertex.position);
    const SIMDVector normal = LoadFloat(vertex.normal);
    const SIMDVector tangent = LoadFloat(vertex.tangent);
    const SIMDVector uv0 = LoadFloat(vertex.uv0);
    const SIMDVector color0 = LoadFloat(vertex.color0);
    return FiniteVector(position, 0x7u)
        && FiniteVector(normal, 0x7u)
        && FiniteVector(tangent, 0xFu)
        && FiniteVector(uv0, 0x3u)
        && FiniteVector(color0, 0xFu)
    ;
}

[[nodiscard]] inline bool ValidRestVertexFrameImpl(const DeformableVertexRest& vertex, const bool requireUnitFrame){
    const SIMDVector position = LoadFloat(vertex.position);
    const SIMDVector normal = LoadFloat(vertex.normal);
    const SIMDVector tangent = LoadFloat(vertex.tangent);
    const SIMDVector uv0 = LoadFloat(vertex.uv0);
    const SIMDVector color0 = LoadFloat(vertex.color0);
    if(!FiniteVector(position, 0x7u)
        || !FiniteVector(normal, 0x7u)
        || !FiniteVector(tangent, 0xFu)
        || !FiniteVector(uv0, 0x3u)
        || !FiniteVector(color0, 0xFu)
    )
        return false;

    const f32 normalLengthSquared = VectorGetX(Vector3LengthSq(normal));
    const f32 tangentLengthSquared = VectorGetX(Vector3LengthSq(tangent));
    const f32 tangentHandedness = Abs(VectorGetW(tangent));
    const f32 frameCrossLengthSquared = VectorGetX(Vector3LengthSq(Vector3Cross(normal, tangent)));
    if(normalLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentHandedness <= s_TangentHandednessEpsilon
        || Abs(tangentHandedness - 1.0f) > s_TangentHandednessUnitEpsilon
        || frameCrossLengthSquared <= s_RestFrameLengthSquaredEpsilon
    )
        return false;

    if(!requireUnitFrame)
        return true;

    const f32 frameDot = VectorGetX(Vector3Dot(normal, tangent));
    return Abs(normalLengthSquared - 1.0f) <= s_RestFrameUnitLengthSquaredEpsilon
        && Abs(tangentLengthSquared - 1.0f) <= s_RestFrameUnitLengthSquaredEpsilon
        && Abs(frameDot) <= s_RestFrameOrthogonalityEpsilon
    ;
}

[[nodiscard]] inline bool ValidRestVertexFrameBasis(const DeformableVertexRest& vertex){
    return ValidRestVertexFrameImpl(vertex, false);
}

[[nodiscard]] inline bool ValidRestVertexFrame(const DeformableVertexRest& vertex){
    return ValidRestVertexFrameImpl(vertex, true);
}

[[nodiscard]] inline bool ValidBarycentric(SIMDVector baryVector, const f32 minimumBarycentric){
    const f32 barySum = VectorGetX(Vector3Dot(baryVector, s_SIMDOne));
    return FiniteVector(baryVector, 0x7u)
        && Vector3GreaterOrEqual(baryVector, VectorReplicate(minimumBarycentric))
        && Abs(barySum - 1.0f) <= s_BarycentricSumEpsilon
    ;
}

[[nodiscard]] inline bool ValidBarycentric(const f32 (&bary)[3], const f32 minimumBarycentric){
    return ValidBarycentric(VectorSet(bary[0], bary[1], bary[2], 0.0f), minimumBarycentric);
}

[[nodiscard]] inline bool ValidBarycentric(const Float4& bary, const f32 minimumBarycentric){
    return ValidBarycentric(LoadFloat(bary), minimumBarycentric);
}

[[nodiscard]] inline bool ValidSourceBarycentric(const f32 (&bary)[3]){
    return ValidBarycentric(bary, 0.0f);
}

[[nodiscard]] inline bool ValidSourceBarycentric(const Float4& bary){
    return ValidBarycentric(bary, 0.0f);
}

[[nodiscard]] inline bool ValidLooseBarycentric(const f32 (&bary)[3]){
    return ValidBarycentric(bary, -s_Epsilon);
}

[[nodiscard]] inline bool ValidLooseBarycentric(const Float4& bary){
    return ValidBarycentric(bary, -s_Epsilon);
}

[[nodiscard]] inline bool NormalizeSourceBarycentricVector(const SIMDVector baryVector, f32 (&outBary)[3]){
    if(!ValidBarycentric(baryVector, -s_Epsilon))
        return false;

    const SIMDVector clampedBary = VectorClamp(baryVector, VectorZero(), s_SIMDOne);
    const f32 barySum = VectorGetX(Vector3Dot(clampedBary, s_SIMDOne));
    if(!IsFinite(barySum) || barySum <= s_Epsilon)
        return false;

    const SIMDVector normalizedBary = VectorScale(clampedBary, 1.0f / barySum);
    if(!ValidBarycentric(normalizedBary, 0.0f))
        return false;

    outBary[0] = VectorGetX(normalizedBary);
    outBary[1] = VectorGetY(normalizedBary);
    outBary[2] = VectorGetZ(normalizedBary);
    return true;
}

[[nodiscard]] inline bool NormalizeSourceBarycentric(const f32 (&bary)[3], f32 (&outBary)[3]){
    return NormalizeSourceBarycentricVector(VectorSet(bary[0], bary[1], bary[2], 0.0f), outBary);
}

[[nodiscard]] inline bool NormalizeSourceBarycentric(const Float4& bary, f32 (&outBary)[3]){
    return NormalizeSourceBarycentricVector(LoadFloat(bary), outBary);
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
    if(!FiniteVector(weights, 0xFu) || !Vector4GreaterOrEqual(weights, VectorZero()))
        return false;

    return Abs(weightSum - 1.0f) <= s_SkinWeightSumEpsilon;
}

[[nodiscard]] inline bool ValidMorphDelta(const DeformableMorphDelta& delta, const usize vertexCount){
    const SIMDVector deltaPosition = LoadFloat(delta.deltaPosition);
    const SIMDVector deltaNormal = LoadFloat(delta.deltaNormal);
    const SIMDVector deltaTangent = LoadFloat(delta.deltaTangent);
    return delta.vertexId < vertexCount
        && FiniteVector(deltaPosition, 0x7u)
        && FiniteVector(deltaNormal, 0x7u)
        && FiniteVector(deltaTangent, 0xFu)
    ;
}

[[nodiscard]] inline MorphPayloadFailureInfo FindMorphPayloadFailure(
    const Vector<DeformableMorph>& morphs,
    const usize vertexCount)
{
    if(morphs.size() > static_cast<usize>(Limit<u32>::s_Max))
        return MakeMorphPayloadFailure(MorphPayloadFailure::MorphCountLimit);
    if(morphs.empty())
        return {};

    Core::Alloc::ScratchArena<> scratchArena;
    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, Core::Alloc::ScratchAllocator<NameHash>> seenMorphNames(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        Core::Alloc::ScratchAllocator<NameHash>(scratchArena)
    );
    seenMorphNames.reserve(morphs.size());

    for(usize morphIndex = 0; morphIndex < morphs.size(); ++morphIndex){
        const DeformableMorph& morph = morphs[morphIndex];
        if(!morph.name || morph.deltas.empty())
            return MakeMorphPayloadFailure(MorphPayloadFailure::EmptyMorph, morphIndex);
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max))
            return MakeMorphPayloadFailure(MorphPayloadFailure::MorphDeltaCountLimit, morphIndex);
        const auto morphNameInsert = seenMorphNames.insert(morph.name.hash());
        if(!morphNameInsert.second)
            return MakeMorphPayloadFailure(MorphPayloadFailure::DuplicateMorphName, morphIndex);

        HashSet<u32, Hasher<u32>, EqualTo<u32>, Core::Alloc::ScratchAllocator<u32>> seenDeltaVertices(
            0,
            Hasher<u32>(),
            EqualTo<u32>(),
            Core::Alloc::ScratchAllocator<u32>(scratchArena)
        );
        seenDeltaVertices.reserve(morph.deltas.size());

        for(usize deltaIndex = 0; deltaIndex < morph.deltas.size(); ++deltaIndex){
            const DeformableMorphDelta& delta = morph.deltas[deltaIndex];
            if(!ValidMorphDelta(delta, vertexCount))
                return MakeMorphPayloadFailure(
                    MorphPayloadFailure::InvalidMorphDelta,
                    morphIndex,
                    deltaIndex,
                    delta.vertexId
                );
            const auto deltaVertexInsert = seenDeltaVertices.insert(delta.vertexId);
            if(!deltaVertexInsert.second)
                return MakeMorphPayloadFailure(
                    MorphPayloadFailure::DuplicateMorphDeltaVertex,
                    morphIndex,
                    deltaIndex,
                    delta.vertexId
                );
        }
    }
    return {};
}

[[nodiscard]] inline bool ValidMorphPayload(const Vector<DeformableMorph>& morphs, const usize vertexCount){
    return FindMorphPayloadFailure(morphs, vertexCount).reason == MorphPayloadFailure::None;
}

[[nodiscard]] inline bool ValidEditMaskPayload(const Vector<DeformableEditMaskFlags>& editMaskPerTriangle, const usize triangleCount){
    if(editMaskPerTriangle.empty())
        return true;
    if(editMaskPerTriangle.size() != triangleCount)
        return false;

    for(const DeformableEditMaskFlags flags : editMaskPerTriangle){
        if(!ValidDeformableEditMaskFlags(flags))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool ValidTriangle(const Vector<DeformableVertexRest>& restVertices, const u32 a, const u32 b, const u32 c){
    if(a >= restVertices.size() || b >= restVertices.size() || c >= restVertices.size())
        return false;
    if(a == b || a == c || b == c)
        return false;

    const SIMDVector aPosition = LoadFloat(restVertices[a].position);
    const SIMDVector ab = VectorSubtract(LoadFloat(restVertices[b].position), aPosition);
    const SIMDVector ac = VectorSubtract(LoadFloat(restVertices[c].position), aPosition);
    const f32 areaLengthSquared = VectorGetX(Vector3LengthSq(Vector3Cross(ab, ac)));
    return areaLengthSquared > s_TriangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] inline bool ValidRuntimePayloadArrays(
    const Vector<DeformableVertexRest>& restVertices,
    const Vector<u32>& indices,
    const u32 sourceTriangleCount,
    const Vector<SkinInfluence4>& skin,
    const Vector<SourceSample>& sourceSamples,
    const Vector<DeformableEditMaskFlags>& editMaskPerTriangle,
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
    if(!ValidEditMaskPayload(editMaskPerTriangle, indices.size() / 3u))
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

