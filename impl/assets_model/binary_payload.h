// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_model/types.adhd.h>
#include <impl/assets_skeleton/joint_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ModelBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ModelMagic = s_ModelBinaryMagic; // MDL2
inline constexpr TStringView s_ModelAssetKindLabel = NWB_TEXT("model");
inline constexpr TStringView s_ModelLoadBinaryContext = NWB_TEXT("Model::loadBinary");
inline constexpr TStringView s_ModelSerializeContext = NWB_TEXT("ModelAssetCodec::serialize");
inline constexpr TStringView s_SkeletonObjectsPayloadLabel = NWB_TEXT("skeleton objects");
inline constexpr TStringView s_StaticMeshObjectsPayloadLabel = NWB_TEXT("static mesh objects");
inline constexpr TStringView s_SkinnedMeshObjectsPayloadLabel = NWB_TEXT("skinned mesh objects");

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

