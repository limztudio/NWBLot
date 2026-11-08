// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"

#include <core/telemetry/stream_header.adhd.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


public:
    [[nodiscard]] b8 ok()const noexcept{ return status == DecodeStatus::Ok; }
};

struct DecodedEvent{
    EventRecord event;
    usize bytesRead = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 EncodeEvent(const EventRecord& event, TelemetryBytes& outBytes);
[[nodiscard]] b8 EncodeEvent(const EventHeader& header, const void* payload, usize payloadBytes, TelemetryBytes& outBytes);
[[nodiscard]] b8 EncodeEventStream(const EventView& events, TelemetryBytes& outBytes);
[[nodiscard]] Expected<DecodedEvent, DecodeResult> DecodeEvent(TelemetryArena& arena, const void* bytes, usize byteCount);
// The destination must not be changed concurrently. Input may borrow one of its current event payloads.
[[nodiscard]] Expected<usize, DecodeResult> DecodeEventStream(const void* bytes, usize byteCount, TelemetryArena& arena, Recorder& inOutRecorder);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

