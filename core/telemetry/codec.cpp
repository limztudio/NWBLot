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

template<typename Container>
static void AppendHeader(Container& outBytes, const EventHeader& header){
    AppendPOD(outBytes, header.magic);
    AppendPOD(outBytes, header.version);
    AppendPOD(outBytes, header.kind);
    AppendPOD(outBytes, header.payloadFormat);
    AppendPOD(outBytes, header.reserved);
    AppendPOD(outBytes, header.streamId);
    AppendPOD(outBytes, header.frameIndex);
    AppendPOD(outBytes, header.timestampNanoseconds);
    AppendPOD(outBytes, header.payloadBytes);
}

[[nodiscard]] static DecodeResult ReadHeader(const ByteView& encoded, EventHeader& outHeader){
    DecodeResult result;
    result.status = DecodeStatus::TruncatedHeader;

    usize cursor = 0u;
    if(
        !ReadPOD(encoded, cursor, outHeader.magic)
        || !ReadPOD(encoded, cursor, outHeader.version)
        || !ReadPOD(encoded, cursor, outHeader.kind)
        || !ReadPOD(encoded, cursor, outHeader.payloadFormat)
        || !ReadPOD(encoded, cursor, outHeader.reserved)
        || !ReadPOD(encoded, cursor, outHeader.streamId)
        || !ReadPOD(encoded, cursor, outHeader.frameIndex)
        || !ReadPOD(encoded, cursor, outHeader.timestampNanoseconds)
        || !ReadPOD(encoded, cursor, outHeader.payloadBytes)
    ){
        result.bytesRead = cursor;
        return result;
    }

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
    if(payloadBytes > Limit<usize>::s_Max - s_EncodedEventHeaderBytes)
        return false;

    outBytes.clear();
    outBytes.reserve(s_EncodedEventHeaderBytes + payloadBytes);
    __hidden_telemetry_codec::AppendHeader(outBytes, header);
    if(payloadBytes != 0u)
        BinaryDetail::AppendBytesNoReserveUnchecked(outBytes, payload, payloadBytes);

    return outBytes.size() == s_EncodedEventHeaderBytes + payloadBytes;
}

DecodeResult DecodeEvent(TelemetryArena& arena, const void* const bytes, const usize byteCount, EventRecord& outEvent){
    DecodeResult result;
    outEvent = EventRecord(arena);

    if(byteCount < s_EncodedEventHeaderBytes || !bytes){
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

