// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_payload.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshSkinningPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<MeshSkinningInfluenceGpu, Core::Alloc::ScratchArena>> BuildSkinInfluences(const MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& arena){
    Vector<MeshSkinningInfluenceGpu, Core::Alloc::ScratchArena> influences(arena);
    if(instance.skin.empty())
        return influences;
    if(instance.skeletonJointCount == 0u || instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u || instance.skin.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skin influence counts are invalid"), instance.handle.value);
        return MakeUnexpected(Failure{});
    }

    influences.reserve(instance.skin.size());
    for(usize vertexIndex = 0u; vertexIndex < instance.skin.size(); ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        if(!ValidSkinInfluenceWeights(sourceSkin.weight) || !SkinInfluenceFitsSkeleton(sourceSkin, instance.skeletonJointCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skin influence {} is invalid"), instance.handle.value, vertexIndex);
            return MakeUnexpected(Failure{});
        }

        MeshSkinningInfluenceGpu gpuSkin;
        gpuSkin.packedJoint = PackSkinInfluenceJoints(sourceSkin);
        gpuSkin.weight = sourceSkin.weight;
        influences.push_back(gpuSkin);
    }
    return influences;
}

Expected<Span<const SkeletonJointMatrix>> ResolveGpuJointPalette(const MeshSkinningRuntimeInstance& instance, const RuntimeSkinPayloadScratch& payload)noexcept{
    const usize jointCount = instance.skeletonJointCount;
    if(!payload.hasActiveSkin() || payload.skinInfluenceCount != instance.skin.size() || jointCount == 0u || jointCount > static_cast<usize>(Limit<u16>::s_Max) + 1u || payload.jointMatrices.size() < jointCount)
        return MakeUnexpected(Failure{});
    return Span<const SkeletonJointMatrix>(payload.jointMatrices.data(), jointCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildRuntimeSkinPayload(const MeshSkinningRuntimeInstance& instance, const SkeletonJointPaletteComponent* jointPalette, const SkeletonPoseComponent* skeletonPose, RuntimeSkinPayloadScratch& payload){
    payload.skinInfluenceCount = 0u;
    payload.jointMatrices.clear();
    payload.poseJoints.clear();
    payload.resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkeletonSkinningMode::LinearBlend;
    if(skeletonPose && HasSkeletonPoseData(*skeletonPose)){
        const auto skinningMode = SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*skeletonPose, payload.poseJoints);
        if(!skinningMode){
            payload.poseJoints.clear();
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skeleton pose is invalid"), instance.handle.value);
            return false;
        }
        payload.resolvedSkinningMode = *skinningMode;
        if(!BuildSkinJointPalette(instance, payload.poseJoints, payload.resolvedSkinningMode, payload.jointMatrices)){
            payload.poseJoints.clear();
            return false;
        }
    }
    else if(jointPalette && !BuildSkinJointPalette(instance, jointPalette->joints, payload.resolvedSkinningMode, payload.jointMatrices))
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

