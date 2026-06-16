// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "codec.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_codec{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ByteView{
    using value_type = u8;

    const u8* bytes = nullptr;
    usize byteCount = 0u;

    [[nodiscard]] usize size()const{ return byteCount; }
    [[nodiscard]] const u8* data()const{ return bytes; }
    [[nodiscard]] u8 operator[](const usize index)const{ return bytes[index]; }
};

[[nodiscard]] static bool ValidatePayloadPointer(const void* const payload, const usize payloadBytes)noexcept{
    return payloadBytes == 0u || payload != nullptr;
}

[[nodiscard]] static bool ValidateHeaderPayload(const EventHeader& header)noexcept{
    return header.valid()
        && header.version == s_CodecVersion
        && (header.payloadFormat != PayloadFormat::None || header.payloadBytes == 0u)
    ;
}

[[nodiscard]] static bool ValidateEventPayload(const EventHeader& header, const void* const payload, const usize payloadBytes)noexcept{
    return ValidatePayloadPointer(payload, payloadBytes)
        && header.payloadBytes == payloadBytes
        && ValidateHeaderPayload(header)
    ;
}

[[nodiscard]] static bool ValidateStreamHeader(const EncodedStreamHeader& header)noexcept{
    return header.magic == s_StreamMagic
        && header.version == s_CodecVersion
        && header.reserved == 0u
    ;
}

[[nodiscard]] static EncodedEventHeader EncodeHeader(const EventHeader& header)noexcept{
    EncodedEventHeader encoded;
    encoded.magic = header.magic;
    encoded.version = header.version;
    encoded.kind = static_cast<u16>(header.kind);
    encoded.payloadFormat = static_cast<u8>(header.payloadFormat);
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
    header.payloadFormat = static_cast<PayloadFormat::Enum>(encoded.payloadFormat);
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

[[nodiscard]] static DecodeResult ReadHeader(const ByteView& encoded, EventHeader& outHeader){
    DecodeResult result;
    result.status = DecodeStatus::TruncatedHeader;

    usize cursor = 0u;
    EncodedEventHeader encodedHeader;
    if(!ReadPOD(encoded, cursor, encodedHeader)){
        result.bytesRead = cursor;
        return result;
    }

    outHeader = DecodeHeader(encodedHeader);
    result.status = DecodeStatus::Ok;
    result.bytesRead = cursor;
    return result;
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

DecodeResult DecodeEvent(TelemetryArena& arena, const void* const bytes, const usize byteCount, EventRecord& outEvent){
    DecodeResult result;
    outEvent = EventRecord(arena);

    if(byteCount < sizeof(EncodedEventHeader) || !bytes){
        result.status = DecodeStatus::TruncatedHeader;
        return result;
    }

    const __hidden_telemetry_codec::ByteView encoded{ static_cast<const u8*>(bytes), byteCount };
    result = __hidden_telemetry_codec::ReadHeader(encoded, outEvent.header);
    if(!result.ok())
        return result;

    if(!__hidden_telemetry_codec::ValidateHeaderPayload(outEvent.header)){
        result.status = DecodeStatus::InvalidHeader;
        return result;
    }

    if(outEvent.header.payloadBytes > static_cast<u64>(Limit<usize>::s_Max)){
        result.status = DecodeStatus::PayloadSizeOverflow;
        return result;
    }

    const usize payloadBytes = static_cast<usize>(outEvent.header.payloadBytes);
    if(byteCount - result.bytesRead < payloadBytes){
        result.status = DecodeStatus::TruncatedPayload;
        return result;
    }

    if(payloadBytes != 0u){
        outEvent.payload.resize(payloadBytes);
        NWB_MEMCPY(outEvent.payload.data(), outEvent.payload.size(), encoded.data() + result.bytesRead, payloadBytes);
    }
    result.bytesRead += payloadBytes;
    return result;
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

DecodeResult DecodeEventStream(TelemetryArena& arena, const void* const bytes, const usize byteCount, Recorder& outRecorder){
    DecodeResult result;
    outRecorder.clear();

    if(byteCount < sizeof(EncodedStreamHeader) || !bytes){
        result.status = DecodeStatus::TruncatedHeader;
        return result;
    }

    const __hidden_telemetry_codec::ByteView encoded{ static_cast<const u8*>(bytes), byteCount };
    usize cursor = 0u;

    EncodedStreamHeader streamHeader;
    if(!ReadPOD(encoded, cursor, streamHeader)){
        result.status = DecodeStatus::TruncatedHeader;
        result.bytesRead = cursor;
        return result;
    }

    if(!__hidden_telemetry_codec::ValidateStreamHeader(streamHeader)){
        result.status = DecodeStatus::InvalidHeader;
        result.bytesRead = cursor;
        return result;
    }

    if(streamHeader.payloadBytes > static_cast<u64>(Limit<usize>::s_Max)){
        result.status = DecodeStatus::PayloadSizeOverflow;
        result.bytesRead = cursor;
        return result;
    }

    const usize streamPayloadBytes = static_cast<usize>(streamHeader.payloadBytes);
    if(byteCount - cursor < streamPayloadBytes){
        result.status = DecodeStatus::TruncatedPayload;
        result.bytesRead = cursor;
        return result;
    }

    const usize streamEnd = cursor + streamPayloadBytes;
    for(u64 i = 0u; i < streamHeader.eventCount; ++i){
        EventRecord event(arena);
        const DecodeResult eventResult = DecodeEvent(arena, encoded.data() + cursor, streamEnd - cursor, event);
        if(!eventResult.ok()){
            result.status = eventResult.status;
            result.bytesRead = cursor + eventResult.bytesRead;
            return result;
        }
        cursor += eventResult.bytesRead;

        if(!outRecorder.append(event.header, event.payload.data(), event.payload.size())){
            result.status = DecodeStatus::InvalidHeader;
            result.bytesRead = cursor;
            return result;
        }
    }

    if(cursor != streamEnd){
        result.status = DecodeStatus::InvalidHeader;
        result.bytesRead = cursor;
        return result;
    }

    result.status = DecodeStatus::Ok;
    result.bytesRead = cursor;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
