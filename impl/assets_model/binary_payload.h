// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include <impl/assets_skeleton/joint_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ModelBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ModelMagic = 0x4D444C31u; // MDL1

struct ModelHeaderBinary{
    u32 magic = s_ModelMagic;
    u32 padding0 = 0u;
    u64 skeletonObjectCount = 0u;
    u64 staticMeshObjectCount = 0u;
    u64 skinnedMeshObjectCount = 0u;
};
static_assert(IsStandardLayout_V<ModelHeaderBinary>, "ModelHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<ModelHeaderBinary>, "ModelHeaderBinary must stay binary-serializable");

struct ModelSkeletonObjectBinary{
    NameHash nameHash = {};
    NameHash skeletonNameHash = {};
    SkeletonJointMatrix transform = {};
};
static_assert(IsStandardLayout_V<ModelSkeletonObjectBinary>, "ModelSkeletonObjectBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<ModelSkeletonObjectBinary>, "ModelSkeletonObjectBinary must stay binary-serializable");

struct ModelStaticMeshObjectBinary{
    NameHash nameHash = {};
    NameHash meshNameHash = {};
    NameHash materialNameHash = {};
    NameHash parentObjectNameHash = {};
    NameHash parentJointNameHash = {};
    SkeletonJointMatrix transform = {};
};
static_assert(IsStandardLayout_V<ModelStaticMeshObjectBinary>, "ModelStaticMeshObjectBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<ModelStaticMeshObjectBinary>, "ModelStaticMeshObjectBinary must stay binary-serializable");

struct ModelSkinnedMeshObjectBinary{
    NameHash nameHash = {};
    NameHash meshNameHash = {};
    NameHash skinNameHash = {};
    NameHash materialNameHash = {};
    NameHash skeletonObjectNameHash = {};
    SkeletonJointMatrix transform = {};
};
static_assert(IsStandardLayout_V<ModelSkinnedMeshObjectBinary>, "ModelSkinnedMeshObjectBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<ModelSkinnedMeshObjectBinary>, "ModelSkinnedMeshObjectBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
