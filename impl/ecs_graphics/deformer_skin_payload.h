// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_helpers.h"
#include "deformer_system.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformerSkinPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SkinInfluenceVector, typename JointPaletteVector>
[[nodiscard]] bool BuildSkinPayload(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableJointPaletteComponent* jointPalette,
    SkinInfluenceVector& outSkinInfluences,
    JointPaletteVector& outJointPalette)
{
    outSkinInfluences.clear();
    outJointPalette.clear();

    if(instance.skin.empty() || !jointPalette || jointPalette->joints.empty())
        return true;
    if(instance.skin.size() != instance.restVertices.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' skin count does not match vertex count"),
            instance.handle.value
        );
        return false;
    }
    if(instance.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' has skin but no skeleton joint count"),
            instance.handle.value
        );
        return false;
    }
    if(instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' skeleton joint count {} exceeds skin stream limits"),
            instance.handle.value,
            instance.skeletonJointCount
        );
        return false;
    }
    if(!DeformableValidation::ValidInverseBindMatrices(instance.inverseBindMatrices, instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' inverse bind matrices are invalid"),
            instance.handle.value
        );
        return false;
    }
    if(instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max)
        || jointPalette->joints.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' skin payload exceeds u32 limits"),
            instance.handle.value
        );
        return false;
    }

    const usize skinCount = instance.skin.size();
    const usize jointCount = jointPalette->joints.size();
    outSkinInfluences.reserve(skinCount);
    outJointPalette.reserve(jointCount);

    for(usize jointIndex = 0; jointIndex < jointCount; ++jointIndex){
        SIMDMatrix jointMatrix;
        if(!DeformableRuntime::ResolveSkinningJointMatrix(
                instance,
                static_cast<u32>(jointIndex),
                jointPalette->joints[jointIndex],
                jointMatrix
            )
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' joint palette entry {} is not a finite invertible affine matrix"),
                instance.handle.value,
                jointIndex
            );
            return false;
        }
        outJointPalette.push_back(DeformableRuntime::StoreJointMatrix(jointMatrix));
    }

    for(usize vertexIndex = 0; vertexIndex < skinCount; ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        if(!DeformableValidation::ValidSkinInfluence(sourceSkin)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} has invalid skin weights"),
                instance.handle.value,
                vertexIndex
            );
            return false;
        }

        u32 failedSkeletonJoint = 0u;
        if(!DeformableValidation::SkinInfluenceFitsSkeleton(
                sourceSkin,
                instance.skeletonJointCount,
                failedSkeletonJoint
            )
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} references joint {} outside skeleton joint count {}"),
                instance.handle.value,
                vertexIndex,
                failedSkeletonJoint,
                instance.skeletonJointCount
            );
            return false;
        }

        DeformerSystem::DeformerSkinInfluenceGpu gpuSkin;
        for(u32 influenceIndex = 0; influenceIndex < 4u; ++influenceIndex){
            const u32 joint = static_cast<u32>(sourceSkin.joint[influenceIndex]);
            const f32 weight = sourceSkin.weight[influenceIndex];
            if(DeformableValidation::ActiveWeight(weight) && joint >= jointCount){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex {} references joint {} outside palette size {}"),
                    instance.handle.value,
                    vertexIndex,
                    joint,
                    jointCount
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
