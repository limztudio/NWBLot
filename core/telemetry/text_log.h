// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_TextLogPayloadVersion = 1u;
inline constexpr u32 s_TextLogPayloadMagic = 0x4E574C47u; // NWLG

#pragma pack(push, 1)
struct EncodedTextLogPayloadHeader{
    u32 magic = s_TextLogPayloadMagic;
    u16 version = s_TextLogPayloadVersion;
    u8 type = Common::LogType::Info;
    u8 reserved = 0u;
    u64 messageBytes = 0u;
};
#pragma pack(pop)
static constexpr usize s_EncodedTextLogPayloadHeaderByteSize = 16u;
static_assert(sizeof(EncodedTextLogPayloadHeader) == s_EncodedTextLogPayloadHeaderByteSize, "EncodedTextLogPayloadHeader wire layout drifted");
static constexpr usize s_TextLogPackedAlignBytes = 1u;
static_assert(alignof(EncodedTextLogPayloadHeader) == s_TextLogPackedAlignBytes, "EncodedTextLogPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedTextLogPayloadHeader>, "EncodedTextLogPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedTextLogPayloadHeader>, "EncodedTextLogPayloadHeader must stay binary-serializable");

// messageUtf8 borrows the parsed payload bytes; keep them alive and unchanged while reading.
struct TextLogPayload{
    AStringView messageUtf8;
    Common::LogType::Enum type = Common::LogType::Info;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 IsValidTextLogType(Common::LogType::Enum type)noexcept;
[[nodiscard]] b8 BuildTextLogPayload(Common::LogType::Enum type, TStringView message, Alloc::ScratchArena& scratchArena, TelemetryBytes& outPayload);
[[nodiscard]] Expected<TextLogPayload> ParseTextLogPayload(const void* payload, usize payloadBytes)noexcept;
[[nodiscard]] b8 RecordTextLog(Recorder& recorder, Common::LogType::Enum type, TStringView message, u64 frameIndex = 0u, u32 streamId = 0u);




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

