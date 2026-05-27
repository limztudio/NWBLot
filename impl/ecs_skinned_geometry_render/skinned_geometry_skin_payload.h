// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_runtime_mesh.h"

#include <impl/assets_geometry/skinned_geometry_validation.h>
#include <impl/ecs_skinned_geometry/skinned_geometry_runtime_helpers.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) SkinnedGeometrySkinInfluenceGpu{
    u32 joint[4] = {};
    Float4 weight = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(SkinnedGeometrySkinInfluenceGpu) == sizeof(f32) * 8u,
    "SkinnedGeometry skin influence GPU layout drifted"
);
static_assert(
    alignof(SkinnedGeometrySkinInfluenceGpu) >= alignof(Float4),
    "SkinnedGeometry skin influence GPU layout must stay SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometrySkinPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SourceJointVector, typename SkinInfluenceVector, typename JointPaletteVector>
[[nodiscard]] bool BuildSkinPayloadFromJointMatrices(
    const SkinnedGeometryRuntimeMeshInstance& instance,
    const SourceJointVector& sourceJoints,
    const u32 skinningMode,
    SkinInfluenceVector& outSkinInfluences,
    JointPaletteVector& outJointPalette){
    outSkinInfluences.clear();
    outJointPalette.clear();

    if(instance.skin.empty() || sourceJoints.empty())
        return true;
    if(!ValidSkinnedGeometrySkinningMode(skinningMode)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' skinning mode {} is invalid")
            , instance.handle.value
            , skinningMode
        );
        return false;
    }
    if(instance.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' has skin but no skeleton joint count")
            , instance.handle.value
        );
        return false;
    }
    if(instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' skeleton joint count {} exceeds skin stream limits")
            , instance.handle.value
            , instance.skeletonJointCount
        );
        return false;
    }
    if(!SkinnedGeometryValidation::ValidInverseBindMatrices(instance.inverseBindMatrices, instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' inverse bind matrices are invalid")
            , instance.handle.value
        );
        return false;
    }
    if(
        instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max)
        || sourceJoints.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' skin payload exceeds u32 limits"), instance.handle.value);
        return false;
    }

    const usize skinCount = instance.skin.size();
    const usize jointCount = sourceJoints.size();
    const bool useDualQuaternionPayload = skinningMode == SkinnedGeometrySkinningMode::DualQuaternion;
    outJointPalette.reserve(jointCount);

    for(usize jointIndex = 0; jointIndex < jointCount; ++jointIndex){
        SIMDMatrix jointMatrix;
        if(
            !SkinnedGeometryRuntime::ResolveSkinningJointMatrix(
                instance.inverseBindMatrices,
                static_cast<u32>(jointIndex),
                sourceJoints[jointIndex],
                jointMatrix
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' joint palette entry {} is not a finite invertible affine matrix")
                , instance.handle.value
                , jointIndex
            );
            return false;
        }
        if(useDualQuaternionPayload && !SkinnedGeometryRuntime::IsRigidJointMatrix(jointMatrix)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' joint palette entry {} is not rigid for dual-quaternion skinning")
                , instance.handle.value
                , jointIndex
            );
            return false;
        }
        if(useDualQuaternionPayload){
            SIMDVector real = QuaternionIdentity();
            SIMDVector dual = VectorZero();
            if(!SkinnedGeometryRuntime::TryBuildJointDualQuaternion(jointMatrix, real, dual)){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' joint palette entry {} failed dual-quaternion payload build")
                    , instance.handle.value
                    , jointIndex
                );
                return false;
            }
            outJointPalette.push_back(SkinnedGeometryRuntime::StoreJointDualQuaternionPayload(real, dual));
        }
        else{
            outJointPalette.push_back(SkinnedGeometryRuntime::StoreJointMatrix(jointMatrix));
        }
    }

    outSkinInfluences.reserve(skinCount);
    for(usize vertexIndex = 0; vertexIndex < skinCount; ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        if(!SkinnedGeometryValidation::ValidSkinInfluence(sourceSkin)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' vertex {} has invalid skin weights")
                , instance.handle.value
                , vertexIndex
            );
            return false;
        }

        u32 failedSkeletonJoint = 0u;
        if(
            !SkinnedGeometryValidation::SkinInfluenceFitsSkeleton(
                sourceSkin,
                instance.skeletonJointCount,
                failedSkeletonJoint
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' vertex {} references joint {} outside skeleton joint count {}")
                , instance.handle.value
                , vertexIndex
                , failedSkeletonJoint
                , instance.skeletonJointCount
            );
            return false;
        }

        SkinnedGeometrySkinInfluenceGpu gpuSkin;
        for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
            const u32 joint = static_cast<u32>(sourceSkin.joint[influenceIndex]);
            const f32 weight = sourceSkin.weight[influenceIndex];
            if(SkinnedGeometryRuntime::ActiveWeight(weight) && joint >= jointCount){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: runtime mesh '{}' vertex {} references joint {} outside palette size {}")
                    , instance.handle.value
                    , vertexIndex
                    , joint
                    , jointCount
                );
                return false;
            }
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
    const SkinnedGeometryRuntimeMeshInstance& instance,
    const SkinnedGeometryJointPaletteComponent* jointPalette,
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

