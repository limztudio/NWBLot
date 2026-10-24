// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProjectBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ProjectMagic = 0x50524A31u; // PRJ1
inline constexpr u32 s_ProjectVersion = 1u;
inline constexpr TStringView s_ProjectAssetKindLabel = NWB_TEXT("project");
inline constexpr TStringView s_ProjectLoadBinaryContext = NWB_TEXT("ProjectAsset::loadBinary");
inline constexpr TStringView s_ProjectSerializeContext = NWB_TEXT("ProjectAssetCodec::serialize");

struct ProjectHeaderBinary{
    u32 magic = s_ProjectMagic;
    u32 version = s_ProjectVersion;
    NameHash startSceneHash = {};
};
static_assert(IsStandardLayout_V<ProjectHeaderBinary>, "ProjectHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<ProjectHeaderBinary>, "ProjectHeaderBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SceneBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SceneMagic = 0x53434E31u; // SCN1
inline constexpr u32 s_SceneVersion = 1u;
inline constexpr TStringView s_SceneAssetKindLabel = NWB_TEXT("scene");
inline constexpr TStringView s_SceneLoadBinaryContext = NWB_TEXT("SceneAsset::loadBinary");
inline constexpr TStringView s_SceneSerializeContext = NWB_TEXT("SceneAssetCodec::serialize");
inline constexpr TStringView s_SubsystemsPayloadLabel = NWB_TEXT("scene subsystems");
inline constexpr TStringView s_EnabledFeaturesPayloadLabel = NWB_TEXT("scene enabled features");
inline constexpr TStringView s_AssetsPayloadLabel = NWB_TEXT("scene referenced assets");

struct SceneHeaderBinary{
    u32 magic = s_SceneMagic;
    u32 version = s_SceneVersion;
    u64 subsystemCount = 0u;
    u64 enabledFeatureCount = 0u;
    u64 assetCount = 0u;
};
static_assert(IsStandardLayout_V<SceneHeaderBinary>, "SceneHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SceneHeaderBinary>, "SceneHeaderBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

