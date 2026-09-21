// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/alloc/general.h>
#include <impl/assets_skeleton/joint_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkeletonSkinningMode{
    static constexpr auto kSkeletonSkinningModeLinearBlendBase = 0u;
    enum Enum : u32{
        LinearBlend = kSkeletonSkinningModeLinearBlendBase,
        DualQuaternion,
    };
};

[[nodiscard]] NWB_INLINE bool ValidSkeletonSkinningMode(const u32 mode){
    return mode == SkeletonSkinningMode::LinearBlend || mode == SkeletonSkinningMode::DualQuaternion;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkeletonJointPaletteComponent{
    using JointVector = Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena>;

    JointVector joints;
    u32 skinningMode = SkeletonSkinningMode::LinearBlend;

    explicit SkeletonJointPaletteComponent(Core::Alloc::GlobalArena& arena)
        : joints(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SkeletonRootParent = Limit<u32>::s_Max;

struct SkeletonPoseComponent{
    using ParentJointVector = Vector<u32, Core::Alloc::GlobalArena>;
    using JointVector = Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena>;

    ParentJointVector parentJoints;
    JointVector localJoints;
    u32 skinningMode = SkeletonSkinningMode::LinearBlend;

    explicit SkeletonPoseComponent(Core::Alloc::GlobalArena& arena)
        : parentJoints(arena)
        , localJoints(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

