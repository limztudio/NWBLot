// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset_ref.h>
#include <core/ecs/entity_id.h>
#include <impl/assets_geometry/skinned_geometry_types.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedGeometry;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RuntimeMeshDirtyFlag{
    enum Enum : u8{
        None = 0,
        TopologyDirty = 1u << 0u,
        AttributesDirty = 1u << 1u,
        SkinnedGeometryInputDirty = 1u << 2u,
        GpuUploadDirty = 1u << 3u,
        All = TopologyDirty | AttributesDirty | SkinnedGeometryInputDirty | GpuUploadDirty,
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


struct SkinnedGeometryMorphWeight{
    Name morph = NAME_NONE;
    f32 weight = 0.0f;
};

struct SkinnedGeometryMorphWeightsComponent{
    Vector<SkinnedGeometryMorphWeight> weights;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometrySkinningMode{
    enum Enum : u32{
        LinearBlend = 0,
        DualQuaternion = 1,
    };
};

[[nodiscard]] inline bool ValidSkinnedGeometrySkinningMode(const u32 mode){
    return
        mode == SkinnedGeometrySkinningMode::LinearBlend
        || mode == SkinnedGeometrySkinningMode::DualQuaternion
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedGeometryJointPaletteComponent{
    Vector<SkinnedGeometryJointMatrix> joints;
    u32 skinningMode = SkinnedGeometrySkinningMode::LinearBlend;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SkinnedGeometrySkeletonRootParent = Limit<u32>::s_Max;

struct SkinnedGeometrySkeletonPoseComponent{
    Vector<u32> parentJoints;
    Vector<SkinnedGeometryJointMatrix> localJoints;
    u32 skinningMode = SkinnedGeometrySkinningMode::LinearBlend;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedGeometryDisplacementComponent{
    f32 amplitudeScale = 1.0f;
    bool enabled = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedGeometryComponent{
    Core::Assets::AssetRef<SkinnedGeometry> skinnedGeometry;
    RuntimeMeshHandle runtimeMesh;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

