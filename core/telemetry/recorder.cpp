// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] static u64 TimestampNanoseconds()noexcept{
    return DurationInNS<u64>(TimerNow());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize EventView::eventCount()const{
    return m_recorder ? m_recorder->eventCount() : 0u;
}

const EventRecord* EventView::eventAt(const usize index)const{
    return m_recorder ? m_recorder->eventAt(index) : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Recorder::setCaptureOptions(const CaptureOptions& options){
    m_capture = options;
    if(!m_capture.enabled())
        clear();
}

void Recorder::clear(){
    m_events.clear();
}

bool Recorder::record(
    const EventKind::Enum kind,
    const PayloadFormat::Enum payloadFormat,
    const u64 frameIndex,
    const void* payload,
    const usize payloadBytes,
    const u32 streamId
){
    if(!enabled(kind))
        return false;
    if(!IsValidPayloadFormat(payloadFormat) || (payloadFormat == PayloadFormat::None && payloadBytes != 0u))
        return false;
    if(payloadBytes != 0u && !payload)
        return false;

    auto record = MakeGlobalUnique<EventRecord>(m_arena, m_arena);
    if(!record)
        return false;

    record->header.kind = kind;
    record->header.payloadFormat = payloadFormat;
    record->header.streamId = streamId;
    record->header.frameIndex = frameIndex;
    record->header.timestampNanoseconds = __hidden_telemetry_recorder::TimestampNanoseconds();
    record->header.payloadBytes = payloadBytes;

    if(payloadBytes != 0u){
        record->payload.resize(payloadBytes);
        NWB_MEMCPY(record->payload.data(), record->payload.size(), payload, payloadBytes);
    }

    m_events.push_back(Move(record));
    return true;
}

bool Recorder::recordBinary(
    const EventKind::Enum kind,
    const u64 frameIndex,
    const void* payload,
    const usize payloadBytes,
    const u32 streamId
){
    return record(kind, PayloadFormat::Binary, frameIndex, payload, payloadBytes, streamId);
}

const EventRecord* Recorder::eventAt(const usize index)const{
    if(index >= m_events.size())
        return nullptr;

    return m_events[index].get();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
