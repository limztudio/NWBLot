// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_StreamMagic = 0x4E574253u; // NWBS

#pragma pack(push, 1)
struct EncodedStreamHeader{
    u32 magic = s_StreamMagic;
    u16 version = s_TelemetryFormatVersion;
    u16 reserved = 0u;
    u64 eventCount = 0u;
    u64 payloadBytes = 0u;
};

struct EncodedEventHeader{
    u32 magic = s_EventMagic;
    u16 version = s_TelemetryFormatVersion;
    u16 kind = EventKind::Unknown;
    u8 reserved = 0u;
    u32 streamId = 0u;
    u64 frameIndex = 0u;
    u64 timestampNanoseconds = 0u;
    u64 payloadBytes = 0u;
};
#pragma pack(pop)
static constexpr usize s_EncodedStreamHeaderByteSize = 24u;
static_assert(sizeof(EncodedStreamHeader) == s_EncodedStreamHeaderByteSize, "EncodedStreamHeader wire layout drifted");
static constexpr usize s_CodecPackedAlignBytes = 1u;
static_assert(alignof(EncodedStreamHeader) == s_CodecPackedAlignBytes, "EncodedStreamHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedStreamHeader>, "EncodedStreamHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedStreamHeader>, "EncodedStreamHeader must stay binary-serializable");
static constexpr usize s_EncodedEventHeaderByteSize = 37u;
static_assert(sizeof(EncodedEventHeader) == s_EncodedEventHeaderByteSize, "EncodedEventHeader wire layout drifted");
static_assert(alignof(EncodedEventHeader) == s_CodecPackedAlignBytes, "EncodedEventHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedEventHeader>, "EncodedEventHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedEventHeader>, "EncodedEventHeader must stay binary-serializable");

namespace DecodeStatus{
    enum Enum : u8{
        Ok,
        TruncatedHeader,
        InvalidHeader,
        PayloadSizeOverflow,
        TruncatedPayload,
    };
};

struct DecodeResult{
    usize bytesRead = 0u;
    DecodeStatus::Enum status = DecodeStatus::Ok;

    [[nodiscard]] bool ok()const{ return status == DecodeStatus::Ok; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool EncodeEvent(const EventRecord& event, TelemetryBytes& outBytes);
[[nodiscard]] bool EncodeEvent(const EventHeader& header, const void* payload, usize payloadBytes, TelemetryBytes& outBytes);
[[nodiscard]] DecodeResult DecodeEvent(TelemetryArena& arena, const void* bytes, usize byteCount, EventRecord& outEvent);
[[nodiscard]] bool EncodeEventStream(const EventView& events, TelemetryBytes& outBytes);
[[nodiscard]] DecodeResult DecodeEventStream(TelemetryArena& arena, const void* bytes, usize byteCount, Recorder& outRecorder);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

