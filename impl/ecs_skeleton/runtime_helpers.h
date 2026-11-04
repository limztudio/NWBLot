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
    && noexcept(DeclVal<SIMDMatrix&>() = LoadFloat(outJointPalette[DeclVal<const u32&>()]))
    && noexcept(outJointPalette.push_back(DeclVal<SkeletonJointMatrix&>()))
){
    outJointPalette.clear();

    if(!HasSkeletonPoseData(pose))
        return static_cast<u32>(SkeletonSkinningMode::LinearBlend);
    if(!ValidSkeletonPoseTopology(pose))
        return MakeUnexpected(Failure{});

    const usize jointCount = pose.localJoints.size();

    outJointPalette.reserve(jointCount);
    const SIMDMatrix identityJoint = MatrixIdentity();
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const u32 parentJoint = pose.parentJoints[jointIndex];
        const b8 hasParent = parentJoint != s_SkeletonInvalidJointIndex;
        const SIMDMatrix parentJointMatrix = hasParent ? LoadFloat(outJointPalette[parentJoint]) : identityJoint;

        const auto resolvedJointMatrix = TryResolveSkeletonPoseJointMatrix(LoadFloat(pose.localJoints[jointIndex]), hasParent, parentJointMatrix);
        if(!resolvedJointMatrix)
            return MakeUnexpected(Failure{});

        SkeletonJointMatrix storedJointMatrix{};
        StoreFloat(*resolvedJointMatrix, storedJointMatrix);
        outJointPalette.push_back(storedJointMatrix);
    }

    return pose.skinningMode;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

