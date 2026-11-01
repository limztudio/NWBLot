// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_skeleton/components.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkeletonRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<SIMDMatrix> ResolveSkinningJointMatrix(const SIMDMatrix& poseJoint, b8 hasInverseBind, const SIMDMatrix& inverseBind, f32 inverseBindDeterminantEpsilon)noexcept;
[[nodiscard]] Expected<SIMDMatrix> ResolveSkeletonPoseJointMatrix(const SIMDMatrix& localJoint, const SIMDMatrix* parentJoint)noexcept;

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
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const u32 parentJoint = pose.parentJoints[jointIndex];
        SIMDMatrix parentJointMatrix{};
        const SIMDMatrix* parentJointMatrixPtr = nullptr;
        if(parentJoint != s_SkeletonRootParent){
            parentJointMatrix = LoadFloat(outJointPalette[parentJoint]);
            parentJointMatrixPtr = &parentJointMatrix;
        }

        const auto resolvedJointMatrix = ResolveSkeletonPoseJointMatrix(LoadFloat(pose.localJoints[jointIndex]), parentJointMatrixPtr);
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

