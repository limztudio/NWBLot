// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "codec.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_codec{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidatePayloadPointer(const void* const payload, const usize payloadBytes)noexcept{
    return payloadBytes == 0u || payload != nullptr;
}

[[nodiscard]] static bool ValidateHeaderPayload(const EventHeader& header)noexcept{
    // EventHeader::valid() owns the wire-format invariant checks for encoded telemetry events.
    return header.valid();
}

[[nodiscard]] static bool ValidateEventPayload(const EventHeader& header, const void* const payload, const usize payloadBytes)noexcept{
    return ValidatePayloadPointer(payload, payloadBytes)
        && header.payloadBytes == payloadBytes
        && ValidateHeaderPayload(header)
    ;
}

[[nodiscard]] static bool ValidateStreamHeader(const EncodedStreamHeader& header)noexcept{
    return header.magic == s_StreamMagic
        && header.version == s_TelemetryFormatVersion
        && header.reserved == 0u
    ;
}

[[nodiscard]] static EncodedEventHeader EncodeHeader(const EventHeader& header)noexcept{
    EncodedEventHeader encoded;
    encoded.magic = header.magic;
    encoded.version = header.version;
    encoded.kind = static_cast<u16>(header.kind);
    encoded.reserved = header.reserved;
    encoded.streamId = header.streamId;
    encoded.frameIndex = header.frameIndex;
    encoded.timestampNanoseconds = header.timestampNanoseconds;
    encoded.payloadBytes = header.payloadBytes;
    return encoded;
}

[[nodiscard]] static EventHeader DecodeHeader(const EncodedEventHeader& encoded)noexcept{
    EventHeader header;
    header.magic = encoded.magic;
    header.version = encoded.version;
    header.kind = static_cast<EventKind::Enum>(encoded.kind);
    header.reserved = encoded.reserved;
    header.streamId = encoded.streamId;
    header.frameIndex = encoded.frameIndex;
    header.timestampNanoseconds = encoded.timestampNanoseconds;
    header.payloadBytes = encoded.payloadBytes;
    return header;
}

template<typename Container>
[[nodiscard]] static bool AppendEncodedEvent(
    Container& outBytes,
    const EventHeader& header,
    const void* const payload,
    const usize payloadBytes
){
    if(!ValidateEventPayload(header, payload, payloadBytes))
        return false;
    if(payloadBytes > Limit<usize>::s_Max - sizeof(EncodedEventHeader))
        return false;

    const EncodedEventHeader encodedHeader = EncodeHeader(header);
    AppendPOD(outBytes, encodedHeader);
    if(payloadBytes != 0u)
        BinaryDetail::AppendBytesNoReserveUnchecked(outBytes, payload, payloadBytes);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EncodeEvent(const EventRecord& event, TelemetryBytes& outBytes){
    return EncodeEvent(event.header, event.payload.data(), event.payload.size(), outBytes);
}

bool EncodeEvent(const EventHeader& header, const void* payload, const usize payloadBytes, TelemetryBytes& outBytes){
    if(!__hidden_telemetry_codec::ValidateEventPayload(header, payload, payloadBytes))
        return false;
    usize encodedBytes = sizeof(EncodedEventHeader);
    if(!AddBinaryReserveBytes(encodedBytes, payloadBytes))
        return false;

    outBytes.clear();
    outBytes.reserve(encodedBytes);
    if(!__hidden_telemetry_codec::AppendEncodedEvent(outBytes, header, payload, payloadBytes))
        return false;

    return outBytes.size() == encodedBytes;
}

Expected<DecodedEvent, DecodeResult> DecodeEvent(TelemetryArena& arena, const void* const bytes, const usize byteCount){
    if(byteCount < sizeof(EncodedEventHeader) || !bytes)
        return MakeUnexpected(DecodeResult{ .status = DecodeStatus::TruncatedHeader });

    const BinaryByteView encoded{ static_cast<const u8*>(bytes), byteCount };
    usize cursor = 0u;
    const auto encodedHeader = ReadPOD<EncodedEventHeader>(encoded, cursor);
    if(!encodedHeader)
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::TruncatedHeader });

    EventRecord event(arena);
    event.header = __hidden_telemetry_codec::DecodeHeader(*encodedHeader);
    if(!__hidden_telemetry_codec::ValidateHeaderPayload(event.header))
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::InvalidHeader });
    if(event.header.payloadBytes > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::PayloadSizeOverflow });

    const usize payloadBytes = static_cast<usize>(event.header.payloadBytes);
    if(byteCount - cursor < payloadBytes)
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::TruncatedPayload });
    if(payloadBytes != 0u){
        event.payload.resize(payloadBytes);
        NWB_MEMCPY(event.payload.data(), event.payload.size(), encoded.data() + cursor, payloadBytes);
    }
    return DecodedEvent{ .event = Move(event), .bytesRead = cursor + payloadBytes };
}

bool EncodeEventStream(const EventView& events, TelemetryBytes& outBytes){
    if(!events.valid())
        return false;

    const usize eventCount = events.eventCount();
    usize payloadBytes = 0u;
    for(usize i = 0u; i < eventCount; ++i){
        const EventRecord* event = events.eventAt(i);
        if(!event)
            return false;
        if(!__hidden_telemetry_codec::ValidateEventPayload(event->header, event->payload.data(), event->payload.size()))
            return false;
        if(!AddBinaryReserveBytes(payloadBytes, sizeof(EncodedEventHeader)))
            return false;
        if(!AddBinaryReserveBytes(payloadBytes, event->payload.size()))
            return false;
    }

    usize encodedBytes = sizeof(EncodedStreamHeader);
    if(!AddBinaryReserveBytes(encodedBytes, payloadBytes))
        return false;

    EncodedStreamHeader streamHeader;
    streamHeader.eventCount = static_cast<u64>(eventCount);
    streamHeader.payloadBytes = static_cast<u64>(payloadBytes);

    outBytes.clear();
    outBytes.reserve(encodedBytes);
    AppendPOD(outBytes, streamHeader);

    for(usize i = 0u; i < eventCount; ++i){
        const EventRecord* event = events.eventAt(i);
        if(!__hidden_telemetry_codec::AppendEncodedEvent(outBytes, event->header, event->payload.data(), event->payload.size()))
            return false;
    }

    return outBytes.size() == encodedBytes;
}

Expected<usize, DecodeResult> DecodeEventStream(
    TelemetryArena& arena,
    const void* const bytes,
    const usize byteCount,
    Recorder& inOutRecorder
){
    inOutRecorder.clear();
    if(byteCount < sizeof(EncodedStreamHeader) || !bytes)
        return MakeUnexpected(DecodeResult{ .status = DecodeStatus::TruncatedHeader });

    const BinaryByteView encoded{ static_cast<const u8*>(bytes), byteCount };
    usize cursor = 0u;
    const auto decodedHeader = ReadPOD<EncodedStreamHeader>(encoded, cursor);
    if(!decodedHeader)
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::TruncatedHeader });
    const EncodedStreamHeader& streamHeader = *decodedHeader;
    if(!__hidden_telemetry_codec::ValidateStreamHeader(streamHeader))
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::InvalidHeader });
    if(streamHeader.payloadBytes > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::PayloadSizeOverflow });

    const usize streamPayloadBytes = static_cast<usize>(streamHeader.payloadBytes);
    if(byteCount - cursor < streamPayloadBytes)
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::TruncatedPayload });
    const usize streamEnd = cursor + streamPayloadBytes;
    for(u64 i = 0u; i < streamHeader.eventCount; ++i){
        auto decodedEvent = DecodeEvent(arena, encoded.data() + cursor, streamEnd - cursor);
        if(!decodedEvent){
            const DecodeResult& failure = decodedEvent.error();
            return MakeUnexpected(DecodeResult{ .bytesRead = cursor + failure.bytesRead, .status = failure.status });
        }
        cursor += decodedEvent->bytesRead;
        if(!inOutRecorder.append(decodedEvent->event.header, Move(decodedEvent->event.payload)))
            return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::InvalidHeader });
    }
    if(cursor != streamEnd)
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::InvalidHeader });
    return cursor;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

