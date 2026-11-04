// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_skeleton/components.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkeletonRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename JointMatrixVector>
[[nodiscard]] inline Expected<u32> BuildStoredJointPaletteFromSkeletonPose(const SkeletonPoseComponent& pose, JointMatrixVector& outJointPalette)noexcept(
    noexcept(outJointPalette.clear())
    && noexcept(outJointPalette.reserve(DeclVal<const usize&>()))
    && noexcept(outJointPalette.push_back(DeclVal<SkeletonJointMatrix&>()))
){
    outJointPalette.clear();

    if(!HasSkeletonPoseData(pose))
        return static_cast<u32>(SkeletonSkinningMode::LinearBlend);
    if(!ValidSkeletonPoseTopology(pose))
        return MakeUnexpected(Failure{});

    const usize jointCount = pose.localJoints.size();

    outJointPalette.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const Expected<SkeletonJointMatrix> resolvedJointMatrix = TryResolveSkeletonPoseJointMatrixAt(
            pose,
            jointIndex,
            Span<const SkeletonJointMatrix>(outJointPalette.data(), outJointPalette.size())
        );
        if(!resolvedJointMatrix)
            return MakeUnexpected(Failure{});
        outJointPalette.push_back(*resolvedJointMatrix);
    }

    return pose.skinningMode;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

