
#include "text_log.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_text_log{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidatePayloadHeader(const EncodedTextLogPayloadHeader& header)noexcept{
    return header.magic == s_TextLogPayloadMagic
        && header.reserved == 0u
        && IsValidTextLogType(static_cast<Common::LogType::Enum>(header.type))
    ;
}

template<typename Out>
static void AppendUtf8Text(Out& outText, const TStringView message){
    auto inserter = std::back_inserter(outText);
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
    const TStringView message,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(!IsValidTextLogType(type))
        return false;

    AString<TelemetryArena> messageUtf8(arena);
    messageUtf8.reserve(message.size());
    __hidden_telemetry_text_log::AppendUtf8Text(messageUtf8, message);

    usize payloadBytes = sizeof(EncodedTextLogPayloadHeader);
    if(!AddBinaryReserveBytes(payloadBytes, messageUtf8.size()))
        return false;

    EncodedTextLogPayloadHeader header;
    header.type = static_cast<u8>(type);
    header.messageBytes = static_cast<u64>(messageUtf8.size());

    outPayload.reserve(payloadBytes);
    AppendPOD(outPayload, header);
    if(!messageUtf8.empty())
        BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, messageUtf8.data(), messageUtf8.size());

    return outPayload.size() == payloadBytes;
}

bool ParseTextLogPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes,
    TextLogPayload& outPayload
){
    outPayload = TextLogPayload(arena);

    if(payloadBytes < sizeof(EncodedTextLogPayloadHeader) || !payload)
        return false;

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    EncodedTextLogPayloadHeader header;
    if(!ReadPOD(encoded, cursor, header))
        return false;
    if(!__hidden_telemetry_text_log::ValidatePayloadHeader(header))
        return false;
    if(header.messageBytes > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    const usize messageBytes = static_cast<usize>(header.messageBytes);
    if(payloadBytes - cursor != messageBytes)
        return false;

    outPayload.type = static_cast<Common::LogType::Enum>(header.type);
    outPayload.messageUtf8.assign(reinterpret_cast<const char*>(encoded.data() + cursor), messageBytes);
    return true;
}

bool RecordTextLog(
    Recorder& recorder,
    const Common::LogType::Enum type,
    const TStringView message,
    const u64 frameIndex,
    const u32 streamId
){
    return Detail::RecordBuiltPayload(
        recorder,
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

