// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/alloc/general.h>
#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>
#include <impl/skinned_mesh/joint_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedMesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RuntimeMeshDirtyFlag{
    enum Enum : u8{
        None = 0,
        TopologyDirty = 1u << 0u,
        AttributesDirty = 1u << 1u,
        SkinnedMeshInputDirty = 1u << 2u,
        GpuUploadDirty = 1u << 3u,
        MeshletBoundsDirty = 1u << 4u,
        All = TopologyDirty | AttributesDirty | SkinnedMeshInputDirty | GpuUploadDirty | MeshletBoundsDirty,
    };
};
using RuntimeMeshDirtyFlags = u8;

struct RuntimeMeshHandle{
    u64 value = 0;

    [[nodiscard]] bool valid()const{ return value != 0u; }
    [[nodiscard]] explicit operator bool()const{ return valid(); }
    void reset(){ value = 0; }
};

[[nodiscard]] inline bool operator==(const RuntimeMeshHandle& lhs, const RuntimeMeshHandle& rhs){
    return lhs.value == rhs.value;
}
[[nodiscard]] inline bool operator!=(const RuntimeMeshHandle& lhs, const RuntimeMeshHandle& rhs){
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedMeshSkinningMode{
    enum Enum : u32{
        LinearBlend = 0,
        DualQuaternion = 1,
    };
};

[[nodiscard]] inline bool ValidSkinnedMeshSkinningMode(const u32 mode){
    return mode == SkinnedMeshSkinningMode::LinearBlend || mode == SkinnedMeshSkinningMode::DualQuaternion;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedMeshJointPaletteComponent{
    using JointVector = Vector<SkinnedMeshJointMatrix, Core::Alloc::GlobalArena>;

    JointVector joints;
    u32 skinningMode = SkinnedMeshSkinningMode::LinearBlend;

    explicit SkinnedMeshJointPaletteComponent(Core::Alloc::GlobalArena& arena)
        : joints(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SkinnedMeshSkeletonRootParent = Limit<u32>::s_Max;

struct SkinnedMeshSkeletonPoseComponent{
    using ParentJointVector = Vector<u32, Core::Alloc::GlobalArena>;
    using JointVector = Vector<SkinnedMeshJointMatrix, Core::Alloc::GlobalArena>;

    ParentJointVector parentJoints;
    JointVector localJoints;
    u32 skinningMode = SkinnedMeshSkinningMode::LinearBlend;

    explicit SkinnedMeshSkeletonPoseComponent(Core::Alloc::GlobalArena& arena)
        : parentJoints(arena)
        , localJoints(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedMeshComponent{
    Core::Assets::AssetRef<SkinnedMesh> skinnedMesh;
    RuntimeMeshHandle runtimeMesh;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

