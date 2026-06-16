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
    if(!__hidden_telemetry_codec::ValidatePayloadPointer(payload, payloadBytes))
        return false;
    if(header.payloadBytes != payloadBytes)
        return false;
    if(!__hidden_telemetry_codec::ValidateHeaderPayload(header))
        return false;
    if(payloadBytes > Limit<usize>::s_Max - sizeof(EncodedEventHeader))
        return false;

    outBytes.clear();
    outBytes.reserve(sizeof(EncodedEventHeader) + payloadBytes);
    const EncodedEventHeader encodedHeader = __hidden_telemetry_codec::EncodeHeader(header);
    AppendPOD(outBytes, encodedHeader);
    if(payloadBytes != 0u)
        BinaryDetail::AppendBytesNoReserveUnchecked(outBytes, payload, payloadBytes);

    return outBytes.size() == sizeof(EncodedEventHeader) + payloadBytes;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
