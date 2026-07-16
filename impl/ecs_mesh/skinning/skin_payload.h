// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/assets_mesh/skin_validation.h>
#include <impl/ecs_skeleton/runtime_helpers.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) MeshSkinningInfluenceGpu{
    u32 joint[4] = {};
    Float4 weight = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(MeshSkinningInfluenceGpu) == sizeof(f32) * NWB_SKINNED_MESH_SKIN_INFLUENCE_FLOAT_COUNT,
    "MeshSkinning influence GPU layout drifted"
);
static_assert(
    alignof(MeshSkinningInfluenceGpu) >= alignof(Float4),
    "MeshSkinning influence GPU layout must stay SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshSkinningPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace JointPayloadBuildResult{
enum Enum : u8{
    Ready,
    InvalidJointMatrix,
    InvalidDualQuaternion
};
};

[[nodiscard]] inline JointPayloadBuildResult::Enum BuildStoredJointPayload(
    const SkeletonJointMatrix& poseJoint,
    const SkeletonJointMatrix* inverseBind,
    const bool useDualQuaternionPayload,
    SkeletonJointMatrix& outJointPayload
){
    const bool hasInverseBind = inverseBind != nullptr;
    const SIMDMatrix inverseBindMatrix = hasInverseBind ? LoadFloat(*inverseBind) : SIMDMatrix{};
    NWB_ASSERT(!hasInverseBind || SkinValidation::ValidAffineJointMatrix(inverseBindMatrix));

    SIMDMatrix jointMatrix{};
    if(!SkeletonRuntime::ResolveSkinningJointMatrix(
        LoadFloat(poseJoint),
        hasInverseBind,
        inverseBindMatrix,
        jointMatrix
    ))
        return JointPayloadBuildResult::InvalidJointMatrix;

    outJointPayload = SkeletonJointMatrix{};
    if(!useDualQuaternionPayload){
        StoreFloat(jointMatrix, &outJointPayload);
        return JointPayloadBuildResult::Ready;
    }

    SIMDVector real = QuaternionIdentity();
    SIMDVector dual = VectorZero();
    if(!SkeletonRuntime::TryBuildJointDualQuaternion(jointMatrix, real, dual))
        return JointPayloadBuildResult::InvalidDualQuaternion;

    StoreFloat(real, &outJointPayload.rows[0]);
    StoreFloat(dual, &outJointPayload.rows[1]);
    return JointPayloadBuildResult::Ready;
}

template<typename SourceJointVector, typename SkinInfluenceVector, typename JointPaletteVector>
[[nodiscard]] bool BuildSkinPayloadFromJointMatrices(
    const MeshSkinningRuntimeInstance& instance,
    const SourceJointVector& sourceJoints,
    const u32 skinningMode,
    SkinInfluenceVector& outSkinInfluences,
    JointPaletteVector& outJointPalette){
    outSkinInfluences.clear();
    outJointPalette.clear();

    if(instance.skin.empty() || sourceJoints.empty())
        return true;
    if(!ValidSkeletonSkinningMode(skinningMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skinning mode {} is invalid")
            , instance.handle.value
            , skinningMode
        );
        return false;
    }
    NWB_ASSERT(instance.skeletonJointCount != 0u);
    NWB_ASSERT(instance.skeletonJointCount <= static_cast<u32>(Limit<u16>::s_Max) + 1u);
    NWB_ASSERT(SkinValidation::ValidInverseBindMatrixCount(instance.inverseBindMatrices.size(), instance.skeletonJointCount));
    NWB_ASSERT(instance.skin.size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(sourceJoints.size() <= static_cast<usize>(Limit<u32>::s_Max));
    if(sourceJoints.size() < static_cast<usize>(instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette count {} is smaller than skeleton joint count {}")
            , instance.handle.value
            , sourceJoints.size()
            , instance.skeletonJointCount
        );
        return false;
    }

    const usize skinCount = instance.skin.size();
    const usize jointCount = sourceJoints.size();
    const bool useDualQuaternionPayload = skinningMode == SkeletonSkinningMode::DualQuaternion;
    const bool hasInverseBindMatrices = !instance.inverseBindMatrices.empty();
    outJointPalette.reserve(jointCount);

    for(usize jointIndex = 0; jointIndex < jointCount; ++jointIndex){
        NWB_ASSERT(!hasInverseBindMatrices || jointIndex < instance.inverseBindMatrices.size());

        const SkeletonJointMatrix* inverseBind = hasInverseBindMatrices
            ? &instance.inverseBindMatrices[jointIndex]
            : nullptr
        ;
        SkeletonJointMatrix storedJointMatrix{};
        const JointPayloadBuildResult::Enum buildResult = BuildStoredJointPayload(
            sourceJoints[jointIndex],
            inverseBind,
            useDualQuaternionPayload,
            storedJointMatrix
        );
        if(buildResult == JointPayloadBuildResult::InvalidJointMatrix){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette entry {} is not a finite invertible affine matrix")
                , instance.handle.value
                , jointIndex
            );
            return false;
        }
        if(buildResult == JointPayloadBuildResult::InvalidDualQuaternion){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette entry {} failed dual-quaternion payload build")
                , instance.handle.value
                , jointIndex
            );
            return false;
        }
        NWB_ASSERT(buildResult == JointPayloadBuildResult::Ready);
        outJointPalette.push_back(storedJointMatrix);
    }

    outSkinInfluences.reserve(skinCount);
    for(usize vertexIndex = 0; vertexIndex < skinCount; ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        NWB_ASSERT(SkinValidation::ValidSkinInfluenceWeights(VectorSet(
            sourceSkin.weight[0u],
            sourceSkin.weight[1u],
            sourceSkin.weight[2u],
            sourceSkin.weight[3u]
        )));
        u32 failedSkeletonJoint = 0u;
        NWB_ASSERT(SkinValidation::SkinInfluenceFitsSkeleton(
            sourceSkin,
            instance.skeletonJointCount,
            failedSkeletonJoint
        ));
        static_cast<void>(failedSkeletonJoint);

        MeshSkinningInfluenceGpu gpuSkin;
        for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
            const u32 joint = static_cast<u32>(sourceSkin.joint[influenceIndex]);
            gpuSkin.joint[influenceIndex] = joint;
        }
        gpuSkin.weight = Float4(
            sourceSkin.weight[0],
            sourceSkin.weight[1],
            sourceSkin.weight[2],
            sourceSkin.weight[3]
        );
        outSkinInfluences.push_back(gpuSkin);
    }

    return true;
}

template<typename SkinInfluenceVector, typename JointPaletteVector>
[[nodiscard]] bool BuildSkinPayload(
    const MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    SkinInfluenceVector& outSkinInfluences,
    JointPaletteVector& outJointPalette){
    outSkinInfluences.clear();
    outJointPalette.clear();

    if(!jointPalette)
        return true;

    return BuildSkinPayloadFromJointMatrices(
        instance,
        jointPalette->joints,
        jointPalette->skinningMode,
        outSkinInfluences,
        outJointPalette
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

