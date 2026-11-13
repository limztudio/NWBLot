// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_log_capture.h"
#include "text_log_payload_builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TextLogCaptureLogger::enqueue(Common::LogString&& str, const Common::LogType::Enum type){
    capture(TStringView(str), type);
    if(Common::ILogger* const forwardLogger = m_forwardLogger.load(MemoryOrder::acquire)){
        if(&str.get_allocator().arena() == &forwardLogger->arena())
            forwardLogger->enqueue(Move(str), type);
        else
            forwardLogger->enqueue(Common::LogString(TStringView(str), forwardLogger->arena()), type);
    }
}

void TextLogCaptureLogger::enqueue(const Common::LogString& str, const Common::LogType::Enum type){
    capture(TStringView(str), type);
    if(Common::ILogger* const forwardLogger = m_forwardLogger.load(MemoryOrder::acquire))
        forwardLogger->enqueue(str, type);
}

void TextLogCaptureLogger::capture(const TStringView message, const Common::LogType::Enum type){
    const auto buildPayload = [type, message](TelemetryArena&, TelemetryBytes& payload){
        return TextLogPayloadBuilder::BuildUnaliased(type, message, payload);
    };
    const u64 currentFrameIndex = m_frameIndex.load(MemoryOrder::acquire);
    const u32 currentStreamId = m_streamId.load(MemoryOrder::acquire);
    if(m_recorder.recordBuiltPayloadResult(EventKind::TextLog, currentFrameIndex, currentStreamId, buildPayload) == Recorder::BuiltPayloadResult::Failed)
        m_droppedTextLogCount.fetch_add(1u, MemoryOrder::relaxed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

