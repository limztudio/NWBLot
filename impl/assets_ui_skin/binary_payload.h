// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiSkinBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_UiSkinMagic = 0x55495331u; // UIS1
inline constexpr u32 s_UiSkinVersion = 1u;

#pragma pack(push, 1)
struct HeaderBinary{
    u32 magic = s_UiSkinMagic;
    u32 version = s_UiSkinVersion;
    NameHash textureNameHash = {};
    u32 atlasWidth = 0u;
    u32 atlasHeight = 0u;
    f32 referenceDensity = 1.0f;
    u32 regionCount = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
};

struct RegionBinary{
    NameHash nameHash = {};
    u32 x = 0u;
    u32 y = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 sliceLeft = 0u;
    u32 sliceTop = 0u;
    u32 sliceRight = 0u;
    u32 sliceBottom = 0u;
    f32 paddingLeft = 0.0f;
    f32 paddingTop = 0.0f;
    f32 paddingRight = 0.0f;
    f32 paddingBottom = 0.0f;
    f32 minimumWidth = 0.0f;
    f32 minimumHeight = 0.0f;
    u32 drawMode = 0u;
    u32 reserved = 0u;
};
#pragma pack(pop)

static_assert(sizeof(HeaderBinary) == sizeof(NameHash) + 32u, "UI skin header layout drifted");
static_assert(sizeof(RegionBinary) == sizeof(NameHash) + 64u, "UI skin region layout drifted");
static_assert(alignof(HeaderBinary) == 1u && alignof(RegionBinary) == 1u, "UI skin payload must stay packed");
static_assert(IsStandardLayout_V<HeaderBinary> && IsTriviallyCopyable_V<HeaderBinary>, "UI skin header must stay binary-serializable");
static_assert(IsStandardLayout_V<RegionBinary> && IsTriviallyCopyable_V<RegionBinary>, "UI skin region must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

