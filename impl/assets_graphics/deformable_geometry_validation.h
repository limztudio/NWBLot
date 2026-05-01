// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_geometry_types.h"

#include <core/alloc/scratch.h>
#include <core/geometry/tangent_frame_rebuild.h>


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

namespace RestVertexPayloadFailure{
    enum Enum : u8{
        None,
        NonFiniteData,
        DegenerateFrame,
        InvalidFrame,
    };
};

namespace RuntimePayloadFailure{
    enum Enum : u8{
        None,
        IncompleteRestIndexPayload,
        VertexIndexCountLimit,
        IndexCountNotTriangleList,
        InvalidRestVertex,
        IndexOutOfRange,
        DegenerateTriangle,
        ZeroAreaTriangle,
        SkinCountMismatch,
        SkinMissingSkeleton,
        SkeletonJointCountLimit,
        InvalidInverseBindMatrices,
        InvalidSkinInfluence,
        SkinJointOutOfRange,
        SourceSampleCountMismatch,
        InvalidSourceSample,
        EditMaskCountMismatch,
        InvalidEditMask,
        MorphPayload,
    };
};

struct RuntimePayloadFailureInfo{
    RuntimePayloadFailure::Enum reason = RuntimePayloadFailure::None;
    usize vertexIndex = 0;
    usize indexBase = 0;
    u32 vertexId = 0;
    usize count = 0;
    usize expectedCount = 0;
    u32 failedJoint = 0;
    RestVertexPayloadFailure::Enum restVertexFailure = RestVertexPayloadFailure::None;
    MorphPayloadFailureInfo morphFailure;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline MorphPayloadFailureInfo MakeMorphPayloadFailure(
    const MorphPayloadFailure::Enum reason,
    const usize morphIndex = 0,
    const usize deltaIndex = 0,
    const u32 vertexId = 0){
    MorphPayloadFailureInfo info;
    info.reason = reason;
    info.morphIndex = morphIndex;
    info.deltaIndex = deltaIndex;
    info.vertexId = vertexId;
    return info;
}

[[nodiscard]] inline RuntimePayloadFailureInfo MakeRuntimePayloadFailure(
    const RuntimePayloadFailure::Enum reason,
    const usize vertexIndex = 0,
    const usize indexBase = 0,
    const u32 vertexId = 0,
    const usize count = 0,
    const usize expectedCount = 0,
    const u32 failedJoint = 0,
    const RestVertexPayloadFailure::Enum restVertexFailure = RestVertexPayloadFailure::None,
    const MorphPayloadFailureInfo& morphFailure = {}
){
    RuntimePayloadFailureInfo info;
    info.reason = reason;
    info.vertexIndex = vertexIndex;
    info.indexBase = indexBase;
    info.vertexId = vertexId;
    info.count = count;
    info.expectedCount = expectedCount;
    info.failedJoint = failedJoint;
    info.restVertexFailure = restVertexFailure;
    info.morphFailure = morphFailure;
    return info;
}

[[nodiscard]] inline TString MorphPayloadFailureMorphNameText(
    const Vector<DeformableMorph>& morphs,
    const MorphPayloadFailureInfo& failure){
    const DeformableMorph* morph = failure.morphIndex < morphs.size()
        ? &morphs[failure.morphIndex]
        : nullptr
    ;
    return
        (morph && morph->name)
            ? StringConvert(morph->name.c_str())
            : TString(NWB_TEXT("<unnamed>"))
    ;
}

[[nodiscard]] inline bool ActiveWeight(const f32 value){
    return value > s_Epsilon || value < -s_Epsilon;
}

[[nodiscard]] inline bool FiniteVector(SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] inline RestVertexPayloadFailure::Enum FindRestVertexPayloadFailure(const DeformableVertexRest& vertex){
    const SIMDVector position = LoadRestVertexPosition(vertex);
    const SIMDVector normal = LoadRestVertexNormal(vertex);
    const SIMDVector tangent = LoadRestVertexTangent(vertex);
    const SIMDVector uv0 = LoadRestVertexUv0(vertex);
    const SIMDVector color0 = LoadRestVertexColor0(vertex);
    if(
        !FiniteVector(position, 0x7u)
        || !FiniteVector(normal, 0x7u)
        || !FiniteVector(tangent, 0xFu)
        || !FiniteVector(uv0, 0x3u)
        || !FiniteVector(color0, 0xFu)
    )
        return RestVertexPayloadFailure::NonFiniteData;

    const f32 normalLengthSquared = VectorGetX(Vector3LengthSq(normal));
    const f32 tangentLengthSquared = VectorGetX(Vector3LengthSq(tangent));
    const f32 tangentHandedness = Abs(VectorGetW(tangent));
    const SIMDVector frameCross = Vector3Cross(normal, tangent);
    const f32 frameCrossLengthSquared = VectorGetX(Vector3LengthSq(frameCross));
    if(
        normalLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentLengthSquared <= s_RestFrameLengthSquaredEpsilon
        || tangentHandedness <= s_TangentHandednessEpsilon
        || Abs(tangentHandedness - 1.0f) > s_TangentHandednessUnitEpsilon
        || frameCrossLengthSquared <= s_RestFrameLengthSquaredEpsilon
    )
        return RestVertexPayloadFailure::DegenerateFrame;

    const f32 frameDot = VectorGetX(Vector3Dot(normal, tangent));
    if(
        Abs(normalLengthSquared - 1.0f) > s_RestFrameUnitLengthSquaredEpsilon
        || Abs(tangentLengthSquared - 1.0f) > s_RestFrameUnitLengthSquaredEpsilon
        || Abs(frameDot) > s_RestFrameOrthogonalityEpsilon
    )
        return RestVertexPayloadFailure::InvalidFrame;

    return RestVertexPayloadFailure::None;
}

[[nodiscard]] inline bool ValidRestVertexFrame(const DeformableVertexRest& vertex){
    return FindRestVertexPayloadFailure(vertex) == RestVertexPayloadFailure::None;
}

template<typename RebuildAllocator>
inline void BuildRestVertexTangentFrameRebuildInput(
    const Vector<DeformableVertexRest>& vertices,
    Vector<Core::Geometry::TangentFrameRebuildVertex, RebuildAllocator>& outRebuildVertices){
    outRebuildVertices.clear();
    outRebuildVertices.reserve(vertices.size());
    for(usize vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex){
        const DeformableVertexRest& vertex = vertices[vertexIndex];
        outRebuildVertices.push_back(Core::Geometry::TangentFrameRebuildVertex{
            vertex.position,
            vertex.uv0,
            vertex.normal,
            vertex.tangent,
        });
    }
}

template<typename RebuildAllocator>
[[nodiscard]] inline bool ValidRestVertexTangentFrameRebuild(
    const Vector<DeformableVertexRest>& vertices,
    const Vector<Core::Geometry::TangentFrameRebuildVertex, RebuildAllocator>& rebuildVertices){
    if(rebuildVertices.size() != vertices.size())
        return false;

    for(usize vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex){
        DeformableVertexRest rebuiltVertex = vertices[vertexIndex];
        rebuiltVertex.normal = rebuildVertices[vertexIndex].normal;
        rebuiltVertex.tangent = rebuildVertices[vertexIndex].tangent;
        if(!ValidRestVertexFrame(rebuiltVertex))
            return false;
    }
    return true;
}

template<typename RebuildAllocator>
inline void ApplyRestVertexTangentFrameRebuild(
    Vector<DeformableVertexRest>& vertices,
    const Vector<Core::Geometry::TangentFrameRebuildVertex, RebuildAllocator>& rebuildVertices){
    for(usize vertexIndex = 0u; vertexIndex < vertices.size(); ++vertexIndex){
        DeformableVertexRest& vertex = vertices[vertexIndex];
        vertex.normal = rebuildVertices[vertexIndex].normal;
        vertex.tangent = rebuildVertices[vertexIndex].tangent;
    }
}

[[nodiscard]] inline bool RebuildRestVertexTangentFrames(
    Vector<DeformableVertexRest>& vertices,
    const Vector<u32>& indices,
    Core::Geometry::TangentFrameRebuildResult* outResult = nullptr){
    Core::Alloc::ScratchArena<> scratchArena;
    using RebuildVertex = Core::Geometry::TangentFrameRebuildVertex;
    using RebuildAllocator = Core::Alloc::ScratchAllocator<RebuildVertex>;
    Vector<RebuildVertex, RebuildAllocator> rebuildVertices{ RebuildAllocator(scratchArena) };
    BuildRestVertexTangentFrameRebuildInput(vertices, rebuildVertices);

    if(!Core::Geometry::RebuildTangentFrames(rebuildVertices, indices, outResult))
        return false;

    if(!ValidRestVertexTangentFrameRebuild(vertices, rebuildVertices))
        return false;

    ApplyRestVertexTangentFrameRebuild(vertices, rebuildVertices);
    return true;
}

inline void ApplyCleanRestVertexTangentFrameRebuildIfPossible(
    Vector<DeformableVertexRest>& vertices,
    const Vector<u32>& indices){
    Core::Alloc::ScratchArena<> scratchArena;
    using RebuildVertex = Core::Geometry::TangentFrameRebuildVertex;
    using RebuildAllocator = Core::Alloc::ScratchAllocator<RebuildVertex>;
    Vector<RebuildVertex, RebuildAllocator> rebuildVertices{ RebuildAllocator(scratchArena) };
    BuildRestVertexTangentFrameRebuildInput(vertices, rebuildVertices);

    Core::Geometry::TangentFrameRebuildResult rebuildResult;
    if(!Core::Geometry::RebuildTangentFrames(rebuildVertices, indices, &rebuildResult))
        return;
    if(rebuildResult.degenerateUvTriangleCount != 0u || rebuildResult.fallbackTangentVertexCount != 0u)
        return;
    if(!ValidRestVertexTangentFrameRebuild(vertices, rebuildVertices))
        return;

    ApplyRestVertexTangentFrameRebuild(vertices, rebuildVertices);
}

[[nodiscard]] inline bool ValidBarycentric(SIMDVector baryVector, const f32 minimumBarycentric){
    const f32 barySum = VectorGetX(Vector3Dot(baryVector, s_SIMDOne));
    return
        FiniteVector(baryVector, 0x7u)
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
    return
        sourceTriangleCount != 0u
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

[[nodiscard]] inline bool SkinInfluenceFitsSkeleton(const SkinInfluence4& skin, const u32 skeletonJointCount, u32& outJoint){
    outJoint = 0u;
    if(skeletonJointCount == 0u)
        return true;

    for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
        const u32 joint = static_cast<u32>(skin.joint[influenceIndex]);
        if(joint < skeletonJointCount)
            continue;

        outJoint = joint;
        return false;
    }
    return true;
}

[[nodiscard]] inline bool ValidAffineJointMatrix(const DeformableJointMatrix& matrix){
    const SIMDMatrix simdMatrix = LoadFloat(matrix);
    const SIMDVector column0 = simdMatrix.v[0];
    const SIMDVector column1 = simdMatrix.v[1];
    const SIMDVector column2 = simdMatrix.v[2];
    const SIMDVector column3 = simdMatrix.v[3];
    const SIMDVector affineW = VectorSet(
        VectorGetW(column0),
        VectorGetW(column1),
        VectorGetW(column2),
        VectorGetW(column3)
    );
    if(
        !FiniteVector(column0, 0xFu)
        || !FiniteVector(column1, 0xFu)
        || !FiniteVector(column2, 0xFu)
        || !FiniteVector(column3, 0xFu)
        || !Vector4NearEqual(affineW, s_SIMDIdentityR3, VectorReplicate(s_Epsilon))
    )
        return false;

    const f32 determinant = VectorGetX(Vector3Dot(
        VectorSetW(column0, 0.0f),
        Vector3Cross(VectorSetW(column1, 0.0f), VectorSetW(column2, 0.0f))
    ));
    return IsFinite(determinant) && Abs(determinant) > s_Epsilon;
}

[[nodiscard]] inline bool ValidInverseBindMatrices(
    const Vector<DeformableJointMatrix>& inverseBindMatrices,
    const u32 skeletonJointCount){
    if(inverseBindMatrices.empty())
        return true;
    if(skeletonJointCount == 0u || inverseBindMatrices.size() != skeletonJointCount)
        return false;

    for(const DeformableJointMatrix& matrix : inverseBindMatrices){
        if(!ValidAffineJointMatrix(matrix))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool ValidMorphDelta(const DeformableMorphDelta& delta, const usize vertexCount){
    const SIMDVector deltaPosition = LoadFloat(delta.deltaPosition);
    const SIMDVector deltaNormal = LoadFloat(delta.deltaNormal);
    const SIMDVector deltaTangent = LoadFloat(delta.deltaTangent);
    return
        delta.vertexId < vertexCount
        && FiniteVector(deltaPosition, 0x7u)
        && FiniteVector(deltaNormal, 0x7u)
        && FiniteVector(deltaTangent, 0xFu)
    ;
}

[[nodiscard]] inline MorphPayloadFailureInfo FindMorphPayloadFailure(
    const Vector<DeformableMorph>& morphs,
    const usize vertexCount){
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

[[nodiscard]] inline bool ValidTriangleArea(const Vector<DeformableVertexRest>& restVertices, const u32 a, const u32 b, const u32 c){
    const SIMDVector aPosition = LoadRestVertexPosition(restVertices[a]);
    const SIMDVector ab = VectorSubtract(LoadRestVertexPosition(restVertices[b]), aPosition);
    const SIMDVector ac = VectorSubtract(LoadRestVertexPosition(restVertices[c]), aPosition);
    const f32 areaLengthSquared = VectorGetX(Vector3LengthSq(Vector3Cross(ab, ac)));
    return areaLengthSquared > s_TriangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] inline bool ValidTriangle(const Vector<DeformableVertexRest>& restVertices, const u32 a, const u32 b, const u32 c){
    if(a >= restVertices.size() || b >= restVertices.size() || c >= restVertices.size())
        return false;
    if(a == b || a == c || b == c)
        return false;

    return ValidTriangleArea(restVertices, a, b, c);
}

[[nodiscard]] inline bool ValidRuntimePayloadArrays(
    const Vector<DeformableVertexRest>& restVertices,
    const Vector<u32>& indices,
    const u32 sourceTriangleCount,
    const u32 skeletonJointCount,
    const Vector<SkinInfluence4>& skin,
    const Vector<DeformableJointMatrix>& inverseBindMatrices,
    const Vector<SourceSample>& sourceSamples,
    const Vector<DeformableEditMaskFlags>& editMaskPerTriangle,
    const Vector<DeformableMorph>& morphs);

[[nodiscard]] inline RuntimePayloadFailureInfo FindRuntimePayloadFailure(
    const Vector<DeformableVertexRest>& restVertices,
    const Vector<u32>& indices,
    const u32 sourceTriangleCount,
    const u32 skeletonJointCount,
    const Vector<SkinInfluence4>& skin,
    const Vector<DeformableJointMatrix>& inverseBindMatrices,
    const Vector<SourceSample>& sourceSamples,
    const Vector<DeformableEditMaskFlags>& editMaskPerTriangle,
    const Vector<DeformableMorph>& morphs){
    if(restVertices.empty() || indices.empty())
        return MakeRuntimePayloadFailure(RuntimePayloadFailure::IncompleteRestIndexPayload);
    if(
        restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || indices.size() > static_cast<usize>(Limit<u32>::s_Max)
    )
        return MakeRuntimePayloadFailure(RuntimePayloadFailure::VertexIndexCountLimit);
    if((indices.size() % 3u) != 0u)
        return MakeRuntimePayloadFailure(RuntimePayloadFailure::IndexCountNotTriangleList, 0, 0, 0, indices.size());
    if(!skin.empty() && skin.size() != restVertices.size())
        return MakeRuntimePayloadFailure(
            RuntimePayloadFailure::SkinCountMismatch,
            0,
            0,
            0,
            skin.size(),
            restVertices.size()
        );
    if(!skin.empty() && skeletonJointCount == 0u)
        return MakeRuntimePayloadFailure(RuntimePayloadFailure::SkinMissingSkeleton);
    if(skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u)
        return MakeRuntimePayloadFailure(
            RuntimePayloadFailure::SkeletonJointCountLimit,
            0,
            0,
            0,
            skeletonJointCount
        );
    if(!ValidInverseBindMatrices(inverseBindMatrices, skeletonJointCount))
        return MakeRuntimePayloadFailure(RuntimePayloadFailure::InvalidInverseBindMatrices);
    if(!sourceSamples.empty() && sourceSamples.size() != restVertices.size())
        return MakeRuntimePayloadFailure(
            RuntimePayloadFailure::SourceSampleCountMismatch,
            0,
            0,
            0,
            sourceSamples.size(),
            restVertices.size()
        );
    if(!sourceSamples.empty() && sourceTriangleCount == 0u)
        return MakeRuntimePayloadFailure(RuntimePayloadFailure::InvalidSourceSample);
    const usize triangleCount = indices.size() / 3u;
    if(!editMaskPerTriangle.empty() && editMaskPerTriangle.size() != triangleCount){
        return MakeRuntimePayloadFailure(
            RuntimePayloadFailure::EditMaskCountMismatch,
            0,
            0,
            0,
            editMaskPerTriangle.size(),
            triangleCount
        );
    }

    for(usize vertexIndex = 0; vertexIndex < restVertices.size(); ++vertexIndex){
        const RestVertexPayloadFailure::Enum restVertexFailure = FindRestVertexPayloadFailure(restVertices[vertexIndex]);
        if(restVertexFailure != RestVertexPayloadFailure::None){
            return MakeRuntimePayloadFailure(
                RuntimePayloadFailure::InvalidRestVertex,
                vertexIndex,
                0,
                0,
                0,
                0,
                0,
                restVertexFailure
            );
        }
    }
    for(usize indexBase = 0; indexBase < indices.size(); indexBase += 3u){
        const u32 a = indices[indexBase + 0u];
        const u32 b = indices[indexBase + 1u];
        const u32 c = indices[indexBase + 2u];
        if(a >= restVertices.size())
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::IndexOutOfRange, 0, indexBase, a, restVertices.size());
        if(b >= restVertices.size())
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::IndexOutOfRange, 0, indexBase, b, restVertices.size());
        if(c >= restVertices.size())
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::IndexOutOfRange, 0, indexBase, c, restVertices.size());
        if(a == b || a == c || b == c)
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::DegenerateTriangle, 0, indexBase);
        if(!ValidTriangleArea(restVertices, a, b, c))
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::ZeroAreaTriangle, 0, indexBase);
    }
    for(usize vertexIndex = 0; vertexIndex < skin.size(); ++vertexIndex){
        const SkinInfluence4& influence = skin[vertexIndex];
        if(!ValidSkinInfluence(influence))
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::InvalidSkinInfluence, vertexIndex);
        u32 failedJoint = 0u;
        if(!SkinInfluenceFitsSkeleton(influence, skeletonJointCount, failedJoint))
            return MakeRuntimePayloadFailure(
                RuntimePayloadFailure::SkinJointOutOfRange,
                vertexIndex,
                0,
                0,
                skeletonJointCount,
                0,
                failedJoint
            );
    }
    for(usize vertexIndex = 0; vertexIndex < sourceSamples.size(); ++vertexIndex){
        if(!ValidSourceSample(sourceSamples[vertexIndex], sourceTriangleCount))
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::InvalidSourceSample, vertexIndex);
    }
    for(usize triangleIndex = 0; triangleIndex < editMaskPerTriangle.size(); ++triangleIndex){
        if(!ValidDeformableEditMaskFlags(editMaskPerTriangle[triangleIndex]))
            return MakeRuntimePayloadFailure(RuntimePayloadFailure::InvalidEditMask, 0, triangleIndex * 3u);
    }

    const MorphPayloadFailureInfo morphFailure = FindMorphPayloadFailure(morphs, restVertices.size());
    if(morphFailure.reason != MorphPayloadFailure::None){
        return MakeRuntimePayloadFailure(
            RuntimePayloadFailure::MorphPayload,
            0,
            0,
            0,
            0,
            0,
            0,
            RestVertexPayloadFailure::None,
            morphFailure
        );
    }

    return {};
}

[[nodiscard]] inline bool ValidRuntimePayloadArrays(
    const Vector<DeformableVertexRest>& restVertices,
    const Vector<u32>& indices,
    const u32 sourceTriangleCount,
    const u32 skeletonJointCount,
    const Vector<SkinInfluence4>& skin,
    const Vector<DeformableJointMatrix>& inverseBindMatrices,
    const Vector<SourceSample>& sourceSamples,
    const Vector<DeformableEditMaskFlags>& editMaskPerTriangle,
    const Vector<DeformableMorph>& morphs){
    return
        FindRuntimePayloadFailure(
            restVertices,
            indices,
            sourceTriangleCount,
            skeletonJointCount,
            skin,
            inverseBindMatrices,
            sourceSamples,
            editMaskPerTriangle,
            morphs
        ).reason == RuntimePayloadFailure::None
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

