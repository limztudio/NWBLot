// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_log.h"

#include <global/binary.h>
#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_text_log{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidatePayloadHeader(const EncodedTextLogPayloadHeader& header)noexcept{
    return header.magic == s_TextLogPayloadMagic
        && header.version == s_TextLogPayloadVersion
        && header.reserved == 0u
        && IsValidTextLogType(static_cast<Common::LogType::Enum>(header.type))
    ;
}

template<typename Out>
static void AppendUtf8Text(Out& outText, const TStringView message){
    auto inserter = BackInserter(outText);
    BasicStringDetail::WriteConvertedText<char>(inserter, message);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsValidTextLogType(const Common::LogType::Enum type)noexcept{
    switch(type){
    case Common::LogType::Info:
    case Common::LogType::EssentialInfo:
    case Common::LogType::Warning:
    case Common::LogType::CriticalWarning:
    case Common::LogType::Error:
    case Common::LogType::Fatal:
    case Common::LogType::Assert:
        return true;
    }
    return false;
}

bool BuildTextLogPayload(
    TelemetryArena& arena,
    const Common::LogType::Enum type,
    TStringView message,
    TelemetryBytes& outPayload
){
    if(!IsValidTextLogType(type)){
        outPayload.clear();
        return false;
    }

    // Preserve views into the previous payload before a header write or vector growth can invalidate them.
    // Ordinary logger input is external, so it converts directly into the reusable destination allocation.
    Optional<TString<TelemetryArena>> aliasedMessage;
    if(!message.empty() && outPayload.data()){
        const usize sourceAddress = reinterpret_cast<usize>(message.data());
        const usize payloadAddress = reinterpret_cast<usize>(outPayload.data());
        if(sourceAddress >= payloadAddress && sourceAddress - payloadAddress < outPayload.capacity()){
            TString<TelemetryArena>& alias = aliasedMessage.emplace(arena);
            alias.assign(message.data(), message.size());
            message = TStringView(alias.data(), alias.size());
        }
    }

    outPayload.clear();
    usize minimumPayloadBytes = sizeof(EncodedTextLogPayloadHeader);
    if(!AddBinaryReserveBytes(minimumPayloadBytes, message.size()))
        return false;

    EncodedTextLogPayloadHeader header;
    header.type = static_cast<u8>(type);
    outPayload.reserve(minimumPayloadBytes);
    AppendPOD(outPayload, header);
    __hidden_telemetry_text_log::AppendUtf8Text(outPayload, message);

    header.messageBytes = static_cast<u64>(outPayload.size() - sizeof(header));
    NWB_MEMCPY(outPayload.data(), outPayload.size(), &header, sizeof(header));
    return true;
}

Expected<TextLogPayload> ParseTextLogPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes
){
    TextLogPayload parsedPayload(arena);

    if(payloadBytes < sizeof(EncodedTextLogPayloadHeader) || !payload)
        return MakeUnexpected(Failure{});

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    const auto decodedHeader = ReadPOD<EncodedTextLogPayloadHeader>(encoded, cursor);
    if(!decodedHeader)
        return MakeUnexpected(Failure{});
    const EncodedTextLogPayloadHeader& header = *decodedHeader;
    if(!__hidden_telemetry_text_log::ValidatePayloadHeader(header))
        return MakeUnexpected(Failure{});
    if(header.messageBytes > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(Failure{});

    const usize messageBytes = static_cast<usize>(header.messageBytes);
    if(payloadBytes - cursor != messageBytes)
        return MakeUnexpected(Failure{});

    parsedPayload.type = static_cast<Common::LogType::Enum>(header.type);
    parsedPayload.messageUtf8.assign(reinterpret_cast<const char*>(encoded.data() + cursor), messageBytes);
    return parsedPayload;
}

bool RecordTextLog(
    Recorder& recorder,
    const Common::LogType::Enum type,
    const TStringView message,
    const u64 frameIndex,
    const u32 streamId
){
    return recorder.recordBuiltPayload(
        EventKind::TextLog,
        frameIndex,
        streamId,
        [type, message](TelemetryArena& arena, TelemetryBytes& payload){
            return BuildTextLogPayload(arena, type, message, payload);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLogCaptureLogger::TextLogCaptureLogger(Recorder& recorder, Common::ILogger* const forwardLogger)
    : m_recorder(recorder)
    , m_forwardLogger(forwardLogger)
{}

Common::LogArena& TextLogCaptureLogger::arena(){
    return m_forwardLogger ? m_forwardLogger->arena() : m_recorder.arena();
}

void TextLogCaptureLogger::enqueue(Common::LogString&& str, const Common::LogType::Enum type){
    capture(TStringView(str.data(), str.size()), type);
    if(m_forwardLogger)
        m_forwardLogger->enqueue(Move(str), type);
}

void TextLogCaptureLogger::enqueue(const Common::LogString& str, const Common::LogType::Enum type){
    capture(TStringView(str.data(), str.size()), type);
    if(m_forwardLogger)
        m_forwardLogger->enqueue(str, type);
}

void TextLogCaptureLogger::capture(const TStringView message, const Common::LogType::Enum type){
    if(!RecordTextLog(m_recorder, type, message, m_frameIndex, m_streamId))
        return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

