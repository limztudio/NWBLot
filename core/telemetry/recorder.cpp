
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
    ScopedLock lock(m_mutex);
    m_capture = options;
    if(!m_capture.enabled())
        m_events.clear();
}

void Recorder::clear(){
    ScopedLock lock(m_mutex);
    m_events.clear();
}

CaptureOptions Recorder::captureOptions()const{
    ScopedLock lock(m_mutex);
    return m_capture;
}

bool Recorder::enabled()const{
    ScopedLock lock(m_mutex);
    return enabledUnlocked();
}

bool Recorder::enabled(const EventKind::Enum kind)const{
    ScopedLock lock(m_mutex);
    return enabledUnlocked(kind);
}

usize Recorder::eventCount()const{
    ScopedLock lock(m_mutex);
    return m_events.size();
}

bool Recorder::recordBinary(
    const EventKind::Enum kind,
    const u64 frameIndex,
    const void* payload,
    const usize payloadBytes,
    const u32 streamId
){
    ScopedLock lock(m_mutex);
    if(!enabledUnlocked(kind))
        return false;

    EventHeader header;
    header.kind = kind;
    header.streamId = streamId;
    header.frameIndex = frameIndex;
    header.timestampNanoseconds = __hidden_telemetry_recorder::TimestampNanoseconds();
    header.payloadBytes = payloadBytes;

    return appendUnlocked(header, payload, payloadBytes);
}

bool Recorder::append(const EventHeader& header, const void* payload, const usize payloadBytes){
    ScopedLock lock(m_mutex);
    return appendUnlocked(header, payload, payloadBytes);
}

bool Recorder::appendUnlocked(const EventHeader& header, const void* payload, const usize payloadBytes){
    if(!header.valid())
        return false;
    if(header.payloadBytes != payloadBytes)
        return false;
    if(payloadBytes != 0u && !payload)
        return false;

    auto record = MakeGlobalUnique<EventRecord>(m_arena, m_arena);
    if(!record)
        return false;

    record->header = header;

    if(payloadBytes != 0u){
        record->payload.resize(payloadBytes);
        NWB_MEMCPY(record->payload.data(), record->payload.size(), payload, payloadBytes);
    }

    m_events.push_back(Move(record));
    return true;
}

const EventRecord* Recorder::eventAt(const usize index)const{
    ScopedLock lock(m_mutex);
    if(index >= m_events.size())
        return nullptr;

    return m_events[index].get();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

