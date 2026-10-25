// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SubsystemBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SubsystemMagic = 0x53554231u; // SUB1
inline constexpr u32 s_SubsystemVersion = 1u;
inline constexpr TStringView s_SubsystemAssetKindLabel = NWB_TEXT("subsystem");
inline constexpr TStringView s_SubsystemLoadBinaryContext = NWB_TEXT("Subsystem::loadBinary");
inline constexpr TStringView s_SubsystemSerializeContext = NWB_TEXT("SubsystemAssetCodec::serialize");
inline constexpr TStringView s_SubsystemsPayloadLabel = NWB_TEXT("child subsystems");
inline constexpr TStringView s_AssetsPayloadLabel = NWB_TEXT("assets");

struct SubsystemHeaderBinary{
    u32 magic = s_SubsystemMagic;
    u32 version = s_SubsystemVersion;
    u64 subsystemCount = 0u;
    u64 assetCount = 0u;
};
static_assert(IsStandardLayout_V<SubsystemHeaderBinary>, "SubsystemHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SubsystemHeaderBinary>, "SubsystemHeaderBinary must stay binary-serializable");

struct SubsystemAssetEntryBinary{
    NameHash nicknameHash = {};
    NameHash assetHash = {};
};
static_assert(IsStandardLayout_V<SubsystemAssetEntryBinary>, "SubsystemAssetEntryBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SubsystemAssetEntryBinary>, "SubsystemAssetEntryBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

