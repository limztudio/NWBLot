// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/assets_mesh/skin_validation.h>
#include <impl/ecs_skeleton/runtime_helpers.h>
#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) MeshSkinningInfluenceGpu{
    u32 joint[s_SkinInfluenceJointCount] = {};
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

// Resource preparation and graph upload declaration resolve the current pose independently. Static influences
// stay in the persistent skin buffer; the graph copies only jointMatrices before releasing this scratch storage.
struct RuntimeSkinPayloadScratch final{
    Vector<SkeletonJointMatrix, Core::Alloc::ScratchArena> poseJoints;
    Vector<SkeletonJointMatrix, Core::Alloc::ScratchArena> jointMatrices;
    usize skinInfluenceCount = 0u;
    u32 resolvedSkinningMode = SkeletonSkinningMode::LinearBlend;

    explicit RuntimeSkinPayloadScratch(Core::Alloc::ScratchArena& scratchArena)
        : poseJoints(scratchArena)
        , jointMatrices(scratchArena)
    {}

    [[nodiscard]] bool hasActiveSkin()const{
        return skinInfluenceCount != 0u && !jointMatrices.empty();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshSkinningPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Encode and validate mesh-owned data only when creating its GPU buffer, including after an edit revision changes.
template<typename SkinInfluenceVector>
[[nodiscard]] bool BuildSkinInfluences(const MeshSkinningRuntimeInstance& instance, SkinInfluenceVector& outSkinInfluences){
    outSkinInfluences.clear();
    if(instance.skin.empty())
        return true;
    if(
        instance.skeletonJointCount == 0u
        || instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u
        || instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skin influence counts are invalid"), instance.handle.value);
        return false;
    }

    outSkinInfluences.reserve(instance.skin.size());
    for(usize vertexIndex = 0u; vertexIndex < instance.skin.size(); ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        const SIMDVector weights = LoadFloat(sourceSkin.weight);
        u32 failedSkeletonJoint = 0u;
        if(
            !SkinValidation::ValidSkinInfluenceWeights(weights)
            || !SkinValidation::SkinInfluenceFitsSkeleton(sourceSkin, instance.skeletonJointCount, failedSkeletonJoint)
        ){
            outSkinInfluences.clear();
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skin influence {} is invalid")
                , instance.handle.value
                , vertexIndex
            );
            return false;
        }

        MeshSkinningInfluenceGpu gpuSkin;
        for(u32 influenceIndex = 0u; influenceIndex < s_SkinInfluenceJointCount; ++influenceIndex)
            gpuSkin.joint[influenceIndex] = static_cast<u32>(sourceSkin.joint[influenceIndex]);
        StoreFloat(weights, gpuSkin.weight);
        outSkinInfluences.push_back(gpuSkin);
    }
    return true;
}

template<typename SourceJointVector, typename JointPaletteVector>
[[nodiscard]] bool BuildSkinJointPalette(
    const MeshSkinningRuntimeInstance& instance,
    const SourceJointVector& sourceJoints,
    const u32 skinningMode,
    JointPaletteVector& outJointPalette){
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
    if(
        instance.skeletonJointCount == 0u
        || instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u
        || instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max)
        || sourceJoints.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint payload counts are invalid"), instance.handle.value);
        return false;
    }
    if(!SkinValidation::ValidInverseBindMatrixCount(instance.inverseBindMatrices.size(), instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' inverse bind matrix count is invalid"), instance.handle.value);
        return false;
    }
    if(sourceJoints.size() < static_cast<usize>(instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette count {} is smaller than skeleton joint count {}")
            , instance.handle.value
            , sourceJoints.size()
            , instance.skeletonJointCount
        );
        return false;
    }

    const usize jointCount = sourceJoints.size();
    const bool useDualQuaternionPayload = skinningMode == SkeletonSkinningMode::DualQuaternion;
    const bool hasInverseBindMatrices = !instance.inverseBindMatrices.empty();
    if(hasInverseBindMatrices && jointCount != instance.inverseBindMatrices.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette count {} differs from inverse bind matrix count {}")
            , instance.handle.value
            , jointCount
            , instance.inverseBindMatrices.size()
        );
        return false;
    }
    outJointPalette.reserve(jointCount);

    for(usize jointIndex = 0; jointIndex < jointCount; ++jointIndex){
        const SIMDMatrix inverseBindMatrix = hasInverseBindMatrices
            ? LoadFloat(instance.inverseBindMatrices[jointIndex])
            : SIMDMatrix{}
        ;

        SIMDMatrix jointMatrix{};
        if(!SkeletonRuntime::ResolveSkinningJointMatrix(
            LoadFloat(sourceJoints[jointIndex]),
            hasInverseBindMatrices,
            inverseBindMatrix,
            SkinValidation::s_Epsilon,
            jointMatrix
        )){
            outJointPalette.clear();
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette entry {} is not a finite invertible affine matrix")
                , instance.handle.value
                , jointIndex
            );
            return false;
        }

        SkeletonJointMatrix storedJointMatrix{};
        if(!useDualQuaternionPayload){
            StoreFloat(jointMatrix, storedJointMatrix);
        }
        else{
            SIMDVector real = QuaternionIdentity();
            SIMDVector dual = VectorZero();
            if(MatrixTryBuildRigidDualQuaternion(
                jointMatrix,
                SkeletonRuntime::s_AffineEpsilon,
                SkeletonRuntime::s_RigidJointEpsilon,
                real,
                dual
            )){
                StoreFloat(real, storedJointMatrix.rows[0]);
                StoreFloat(dual, storedJointMatrix.rows[1]);
            }
            else{
                outJointPalette.clear();
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette entry {} failed dual-quaternion payload build")
                    , instance.handle.value
                    , jointIndex
                );
                return false;
            }
        }
        outJointPalette.push_back(storedJointMatrix);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BuildRuntimeSkinPayload(
    const MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose,
    RuntimeSkinPayloadScratch& payload){
    payload.skinInfluenceCount = 0u;
    payload.jointMatrices.clear();
    payload.poseJoints.clear();
    payload.resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkeletonSkinningMode::LinearBlend;
    if(SkeletonRuntime::HasSkeletonPose(skeletonPose)){
        if(!SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*skeletonPose, payload.poseJoints, payload.resolvedSkinningMode)){
            payload.poseJoints.clear();
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skeleton pose is invalid"), instance.handle.value);
            return false;
        }
        if(!BuildSkinJointPalette(
            instance,
            payload.poseJoints,
            payload.resolvedSkinningMode,
            payload.jointMatrices
        )){
            payload.poseJoints.clear();
            return false;
        }
    }
    else if(jointPalette && !BuildSkinJointPalette(
        instance,
        jointPalette->joints,
        payload.resolvedSkinningMode,
        payload.jointMatrices
    ))
        return false;

    if(!payload.jointMatrices.empty())
        payload.skinInfluenceCount = instance.skin.size();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

