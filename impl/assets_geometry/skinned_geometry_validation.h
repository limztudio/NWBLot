// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometryValidation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;
static constexpr f32 s_RestFrameLengthSquaredEpsilon = 0.000001f;
static constexpr f32 s_RestFrameUnitLengthSquaredEpsilon = 0.01f;
static constexpr f32 s_RestFrameOrthogonalityEpsilon = 0.01f;
static constexpr f32 s_TangentHandednessEpsilon = 0.000001f;
static constexpr f32 s_TangentHandednessUnitEpsilon = 0.001f;
static constexpr f32 s_TriangleAreaLengthSquaredEpsilon = 1.0e-20f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
};

struct RuntimePayloadArrays{
    const Vector<SkinnedGeometryVertex>& restVertices;
    const Vector<u32>& indices;
    u32 skeletonJointCount;
    const Vector<SkinInfluence4>& skin;
    const Vector<SkinnedGeometryJointMatrix>& inverseBindMatrices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline RuntimePayloadFailureInfo MakeRuntimePayloadFailure(
    const RuntimePayloadFailure::Enum reason,
    const usize vertexIndex = 0,
    const usize indexBase = 0,
    const u32 vertexId = 0,
    const usize count = 0,
    const usize expectedCount = 0,
    const u32 failedJoint = 0,
    const RestVertexPayloadFailure::Enum restVertexFailure = RestVertexPayloadFailure::None
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
    return info;
}

[[nodiscard]] inline bool FiniteVector(SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] inline RestVertexPayloadFailure::Enum FindRestVertexPayloadFailure(const SkinnedGeometryVertex& vertex){
    const SIMDVector position = LoadSkinnedGeometryVertexPosition(vertex);
    const SIMDVector normal = LoadSkinnedGeometryVertexNormal(vertex);
    const SIMDVector tangent = LoadSkinnedGeometryVertexTangent(vertex);
    const SIMDVector uv0 = LoadSkinnedGeometryVertexUv0(vertex);
    const SIMDVector color0 = LoadSkinnedGeometryVertexColor0(vertex);
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

[[nodiscard]] inline bool ValidAffineJointMatrix(const SkinnedGeometryJointMatrix& matrix){
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
    const Vector<SkinnedGeometryJointMatrix>& inverseBindMatrices,
    const u32 skeletonJointCount){
    if(inverseBindMatrices.empty())
        return true;
    if(skeletonJointCount == 0u || inverseBindMatrices.size() != skeletonJointCount)
        return false;

    for(const SkinnedGeometryJointMatrix& matrix : inverseBindMatrices){
        if(!ValidAffineJointMatrix(matrix))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool ValidTriangleArea(const Vector<SkinnedGeometryVertex>& restVertices, const u32 a, const u32 b, const u32 c){
    const SIMDVector aPosition = LoadSkinnedGeometryVertexPosition(restVertices[a]);
    const SIMDVector ab = VectorSubtract(LoadSkinnedGeometryVertexPosition(restVertices[b]), aPosition);
    const SIMDVector ac = VectorSubtract(LoadSkinnedGeometryVertexPosition(restVertices[c]), aPosition);
    const f32 areaLengthSquared = VectorGetX(Vector3LengthSq(Vector3Cross(ab, ac)));
    return areaLengthSquared > s_TriangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] inline bool ValidTriangle(const Vector<SkinnedGeometryVertex>& restVertices, const u32 a, const u32 b, const u32 c){
    if(a >= restVertices.size() || b >= restVertices.size() || c >= restVertices.size())
        return false;
    if(a == b || a == c || b == c)
        return false;

    return ValidTriangleArea(restVertices, a, b, c);
}

[[nodiscard]] inline RuntimePayloadFailureInfo FindRuntimePayloadFailure(const RuntimePayloadArrays& payload){
    const Vector<SkinnedGeometryVertex>& restVertices = payload.restVertices;
    const Vector<u32>& indices = payload.indices;
    const u32 skeletonJointCount = payload.skeletonJointCount;
    const Vector<SkinInfluence4>& skin = payload.skin;
    const Vector<SkinnedGeometryJointMatrix>& inverseBindMatrices = payload.inverseBindMatrices;

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

    return {};
}

[[nodiscard]] inline bool ValidRuntimePayloadArrays(const RuntimePayloadArrays& payload){
    return FindRuntimePayloadFailure(payload).reason == RuntimePayloadFailure::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

