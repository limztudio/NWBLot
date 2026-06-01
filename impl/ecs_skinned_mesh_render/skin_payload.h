// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/assets_mesh/skinned_validation.h>
#include <impl/ecs_skinned_mesh/runtime_helpers.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) SkinnedMeshSkinInfluenceGpu{
    u32 joint[4] = {};
    Float4 weight = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(SkinnedMeshSkinInfluenceGpu) == sizeof(f32) * NWB_SKINNED_MESH_SKIN_INFLUENCE_FLOAT_COUNT,
    "SkinnedMesh skin influence GPU layout drifted"
);
static_assert(
    alignof(SkinnedMeshSkinInfluenceGpu) >= alignof(Float4),
    "SkinnedMesh skin influence GPU layout must stay SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedMeshSkinPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SourceJointVector, typename SkinInfluenceVector, typename JointPaletteVector>
[[nodiscard]] bool BuildSkinPayloadFromJointMatrices(
    const SkinnedMeshRuntimeMeshInstance& instance,
    const SourceJointVector& sourceJoints,
    const u32 skinningMode,
    SkinInfluenceVector& outSkinInfluences,
    JointPaletteVector& outJointPalette){
    outSkinInfluences.clear();
    outJointPalette.clear();

    if(instance.skin.empty() || sourceJoints.empty())
        return true;
    if(!ValidSkinnedMeshSkinningMode(skinningMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' skinning mode {} is invalid")
            , instance.handle.value
            , skinningMode
        );
        return false;
    }
#if defined(NWB_DEBUG)
    if(instance.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' has skin but no skeleton joint count")
            , instance.handle.value
        );
        return false;
    }
    if(instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' skeleton joint count {} exceeds skin stream limits")
            , instance.handle.value
            , instance.skeletonJointCount
        );
        return false;
    }
    if(!SkinnedMeshValidation::ValidInverseBindMatrices(instance.inverseBindMatrices, instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' inverse bind matrices are invalid")
            , instance.handle.value
        );
        return false;
    }
    if(
        instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max)
        || sourceJoints.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' skin payload exceeds u32 limits"), instance.handle.value);
        return false;
    }
#endif
    if(sourceJoints.size() < static_cast<usize>(instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' joint palette count {} is smaller than skeleton joint count {}")
            , instance.handle.value
            , sourceJoints.size()
            , instance.skeletonJointCount
        );
        return false;
    }

    const usize skinCount = instance.skin.size();
    const usize jointCount = sourceJoints.size();
    const bool useDualQuaternionPayload = skinningMode == SkinnedMeshSkinningMode::DualQuaternion;
    const bool hasInverseBindMatrices = !instance.inverseBindMatrices.empty();
    outJointPalette.reserve(jointCount);

    for(usize jointIndex = 0; jointIndex < jointCount; ++jointIndex){
#if defined(NWB_DEBUG)
        if(hasInverseBindMatrices && jointIndex >= instance.inverseBindMatrices.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' joint palette entry {} has no inverse bind matrix")
                , instance.handle.value
                , jointIndex
            );
            return false;
        }
#endif

        const SIMDMatrix poseJoint = LoadFloat(sourceJoints[jointIndex]);
        const SIMDMatrix inverseBind = hasInverseBindMatrices
            ? LoadFloat(instance.inverseBindMatrices[jointIndex])
            : SIMDMatrix{}
        ;
        SIMDMatrix jointMatrix;
        if(
            !SkinnedMeshRuntime::ResolveSkinningJointMatrix(
                poseJoint,
                hasInverseBindMatrices,
                inverseBind,
                jointMatrix
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' joint palette entry {} is not a finite invertible affine matrix")
                , instance.handle.value
                , jointIndex
            );
            return false;
        }
        if(useDualQuaternionPayload){
            SIMDVector real = QuaternionIdentity();
            SIMDVector dual = VectorZero();
            if(!SkinnedMeshRuntime::TryBuildJointDualQuaternion(jointMatrix, real, dual)){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' joint palette entry {} failed dual-quaternion payload build")
                    , instance.handle.value
                    , jointIndex
                );
                return false;
            }
            SkinnedMeshJointMatrix storedJointMatrix{};
            StoreFloat(real, &storedJointMatrix.rows[0]);
            StoreFloat(dual, &storedJointMatrix.rows[1]);
            outJointPalette.push_back(storedJointMatrix);
        }
        else{
            SkinnedMeshJointMatrix storedJointMatrix{};
            StoreFloat(jointMatrix, &storedJointMatrix);
            outJointPalette.push_back(storedJointMatrix);
        }
    }

    outSkinInfluences.reserve(skinCount);
    for(usize vertexIndex = 0; vertexIndex < skinCount; ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
#if defined(NWB_DEBUG)
        const SIMDVector weights = VectorSet(
            sourceSkin.weight[0u],
            sourceSkin.weight[1u],
            sourceSkin.weight[2u],
            sourceSkin.weight[3u]
        );
        if(!SkinnedMeshValidation::ValidSkinInfluenceWeights(weights)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' vertex {} has invalid skin weights")
                , instance.handle.value
                , vertexIndex
            );
            return false;
        }

        u32 failedSkeletonJoint = 0u;
        if(
            !SkinnedMeshValidation::SkinInfluenceFitsSkeleton(
                sourceSkin,
                instance.skeletonJointCount,
                failedSkeletonJoint
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' vertex {} references joint {} outside skeleton joint count {}")
                , instance.handle.value
                , vertexIndex
                , failedSkeletonJoint
                , instance.skeletonJointCount
            );
            return false;
        }
#endif

        SkinnedMeshSkinInfluenceGpu gpuSkin;
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
    const SkinnedMeshRuntimeMeshInstance& instance,
    const SkinnedMeshJointPaletteComponent* jointPalette,
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

