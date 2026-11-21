// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "codec.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_codec{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ValidatePayloadPointer(const void* const payload, const usize payloadBytes)noexcept{
    return payloadBytes == 0u || payload != nullptr;
}

[[nodiscard]] static b8 ValidateEventPayload(const EventHeader& header, const void* const payload, const usize payloadBytes)noexcept{
    return ValidatePayloadPointer(payload, payloadBytes) && header.payloadBytes == payloadBytes && header.valid();
}

[[nodiscard]] static b8 ValidateStreamHeader(const EncodedStreamHeader& header)noexcept{
    return header.magic == s_StreamMagic && header.version == s_TelemetryFormatVersion && header.reserved == 0u;
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

static void AppendEncodedEvent(const EventHeader& header, const void* const payload, const usize payloadBytes, TelemetryBytes& outBytes){
    FOUNDATION_ASSERT(payloadBytes <= Limit<usize>::s_Max - sizeof(EncodedEventHeader));

    const EncodedEventHeader encodedHeader = EncodeHeader(header);
    AppendPOD(outBytes, encodedHeader);
    if(payloadBytes != 0u)
        AppendBytesNoReserveUnchecked(outBytes, payload, payloadBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TelemetryCodecAccess{
    [[nodiscard]] static const Recorder* RecorderPtr(const EventView& view)noexcept{
        return view.m_recorder;
    }

    [[nodiscard]] static b8 EncodeStream(const Recorder& recorder, TelemetryBytes& outBytes){
        NothrowScopedLock lock(recorder.m_mutex);
        const usize eventCount = recorder.m_events.size();
        usize payloadBytes = 0u;
        for(usize i = 0u; i < eventCount; ++i){
            const EventRecord& event = recorder.m_events[i]->record;
            FOUNDATION_ASSERT(__hidden_telemetry_codec::ValidateEventPayload(event.header, event.payload.data(), event.payload.size()));
            if(!AddBinaryReserveBytes(payloadBytes, sizeof(EncodedEventHeader)))
                return false;
            if(!AddBinaryReserveBytes(payloadBytes, event.payload.size()))
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
            const EventRecord& event = recorder.m_events[i]->record;
            __hidden_telemetry_codec::AppendEncodedEvent(event.header, event.payload.data(), event.payload.size(), outBytes);
        }

        FOUNDATION_ASSERT(outBytes.size() == encodedBytes);
        return true;
    }

    [[nodiscard]] static Expected<usize, DecodeResult> DecodeStream(const void* bytes, const usize byteCount, TelemetryArena& arena, Recorder& inOutRecorder){
        TelemetryBytes preserved(arena);
        NothrowScopedLock lock(inOutRecorder.m_mutex);
        if(bytes && byteCount != 0u){
            const Span<const u8> sourceSpan(static_cast<const u8*>(bytes), byteCount);
            for(const auto& slot : inOutRecorder.m_events){
                const auto alias = ContainerDetail::SourceAliasesDestination(slot->record.payload, sourceSpan);
                if(!alias)
                    continue;
                if(byteCount > slot->record.payload.size() - *alias){
                    inOutRecorder.clearUnlocked();
                    return MakeUnexpected(DecodeResult{ .status = DecodeStatus::TruncatedPayload });
                }
                preserved.resize(byteCount);
                FOUNDATION_MEMCPY(preserved.data(), preserved.size(), bytes, byteCount);
                bytes = preserved.data();
                break;
            }
        }
        inOutRecorder.clearUnlocked();
        if(!bytes)
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
        const usize maxPossibleEvents = streamPayloadBytes / sizeof(EncodedEventHeader);
        const usize reserveCount = static_cast<usize>(Min<u64>(streamHeader.eventCount, static_cast<u64>(maxPossibleEvents)));
        inOutRecorder.m_events.reserve(reserveCount);
        for(u64 i = 0u; i < streamHeader.eventCount; ++i){
            auto decodedEvent = DecodeEvent(arena, encoded.data() + cursor, streamEnd - cursor);
            if(!decodedEvent){
                const DecodeResult& failure = decodedEvent.error();
                return MakeUnexpected(DecodeResult{ .bytesRead = cursor + failure.bytesRead, .status = failure.status });
            }
            cursor += decodedEvent->bytesRead;
            if(!inOutRecorder.appendOwnedPayloadUnlocked(decodedEvent->event.header, Move(decodedEvent->event.payload)))
                return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::InvalidHeader });
        }
        if(cursor != streamEnd)
            return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::InvalidHeader });
        return cursor;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EncodeEvent(const EventRecord& event, TelemetryBytes& outBytes){
    return EncodeEvent(event.header, event.payload.data(), event.payload.size(), outBytes);
}

b8 EncodeEvent(const EventHeader& header, const void* payload, const usize payloadBytes, TelemetryBytes& outBytes){
    if(!__hidden_telemetry_codec::ValidateEventPayload(header, payload, payloadBytes))
        return false;
    usize encodedBytes = sizeof(EncodedEventHeader);
    if(!AddBinaryReserveBytes(encodedBytes, payloadBytes))
        return false;

    const EncodedEventHeader encodedHeader = __hidden_telemetry_codec::EncodeHeader(header);
    const auto aliasOffset = ContainerDetail::SourceAliasesDestination(outBytes, Span<const u8>(static_cast<const u8*>(payload), payloadBytes));
    if(aliasOffset){
        if(payloadBytes > outBytes.size() - *aliasOffset)
            return false;
        AssignTriviallyCopyableVector(outBytes, BinaryByteView{ outBytes.data() + *aliasOffset, payloadBytes });
        outBytes.reserve(encodedBytes);
        const auto* const headerBytes = reinterpret_cast<const u8*>(&encodedHeader);
        outBytes.insert(outBytes.begin(), headerBytes, headerBytes + sizeof(encodedHeader));
        FOUNDATION_ASSERT(outBytes.size() == encodedBytes);
        return true;
    }

    outBytes.clear();
    outBytes.reserve(encodedBytes);
    AppendPOD(outBytes, encodedHeader);
    if(payloadBytes != 0u)
        AppendBytesNoReserveUnchecked(outBytes, payload, payloadBytes);
    FOUNDATION_ASSERT(outBytes.size() == encodedBytes);
    return true;
}

b8 EncodeEventStream(const EventView& events, TelemetryBytes& outBytes){
    const Recorder* const recorder = TelemetryCodecAccess::RecorderPtr(events);
    if(!recorder)
        return false;
    return TelemetryCodecAccess::EncodeStream(*recorder, outBytes);
}

Expected<DecodedEvent, DecodeResult> DecodeEvent(TelemetryArena& arena, const void* const bytes, const usize byteCount){
    if(!bytes)
        return MakeUnexpected(DecodeResult{ .status = DecodeStatus::TruncatedHeader });

    const BinaryByteView encoded{ static_cast<const u8*>(bytes), byteCount };
    usize cursor = 0u;
    const auto encodedHeader = ReadPOD<EncodedEventHeader>(encoded, cursor);
    if(!encodedHeader)
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::TruncatedHeader });

    EventRecord event(arena);
    event.header = __hidden_telemetry_codec::DecodeHeader(*encodedHeader);
    if(!event.header.valid())
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::InvalidHeader });
    if(event.header.payloadBytes > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::PayloadSizeOverflow });

    const usize payloadBytes = static_cast<usize>(event.header.payloadBytes);
    if(byteCount - cursor < payloadBytes)
        return MakeUnexpected(DecodeResult{ .bytesRead = cursor, .status = DecodeStatus::TruncatedPayload });
    if(payloadBytes != 0u){
        event.payload.resize(payloadBytes);
        FOUNDATION_MEMCPY(event.payload.data(), event.payload.size(), encoded.data() + cursor, payloadBytes);
    }
    return DecodedEvent{ .event = Move(event), .bytesRead = cursor + payloadBytes };
}

Expected<usize, DecodeResult> DecodeEventStream(const void* const bytes, const usize byteCount, TelemetryArena& arena, Recorder& inOutRecorder){
    return TelemetryCodecAccess::DecodeStream(bytes, byteCount, arena, inOutRecorder);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

