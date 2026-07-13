
#pragma once


#include "event.h"

#include <core/alloc/module.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TelemetryArena = Alloc::GlobalArena;
using TelemetryBytes = Vector<u8, TelemetryArena>;

class Recorder;
class EventView;

struct EventRecord{
    EventHeader header;
    TelemetryBytes payload;

    explicit EventRecord(TelemetryArena& arena)
        : payload(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EventView final{
public:
    EventView() = default;
    explicit EventView(const Recorder& recorder)
        : m_recorder(&recorder)
    {}


public:
    [[nodiscard]] bool valid()const{ return m_recorder != nullptr; }
    // Views are intended for quiescent export/readback points. Individual reads are serialized,
    // but callers should not clear the recorder while iterating a view.
    [[nodiscard]] usize eventCount()const;
    [[nodiscard]] const EventRecord* eventAt(usize index)const;


private:
    const Recorder* m_recorder = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Recorder final : NoCopy{
    friend EventView;

private:
    using EventRecordPtr = GlobalUniquePtr<EventRecord>;
    using EventVector = Vector<EventRecordPtr, TelemetryArena>;


public:
    explicit Recorder(TelemetryArena& arena)
        : m_arena(arena)
        , m_events(arena)
    {}


public:
    void setCaptureOptions(const CaptureOptions& options);
    void clear();
    [[nodiscard]] TelemetryArena& arena(){ return m_arena; }
    [[nodiscard]] const TelemetryArena& arena()const{ return m_arena; }
    [[nodiscard]] CaptureOptions captureOptions()const;
    [[nodiscard]] bool enabled()const;
    [[nodiscard]] bool enabled(EventKind::Enum kind)const;
    [[nodiscard]] usize eventCount()const;
    [[nodiscard]] EventView view()const{ return EventView(*this); }

    [[nodiscard]] bool recordBinary(
        EventKind::Enum kind,
        u64 frameIndex,
        const void* payload,
        usize payloadBytes,
        u32 streamId = 0u
    );
    [[nodiscard]] bool append(const EventHeader& header, const void* payload, usize payloadBytes);


private:
    [[nodiscard]] bool enabledUnlocked()const{ return m_capture.enabled(); }
    [[nodiscard]] bool enabledUnlocked(EventKind::Enum kind)const{ return CaptureAllowsEventKind(m_capture, kind); }
    [[nodiscard]] bool appendUnlocked(const EventHeader& header, const void* payload, usize payloadBytes);
    [[nodiscard]] const EventRecord* eventAt(usize index)const;


private:
    TelemetryArena& m_arena;
    EventVector m_events;
    CaptureOptions m_capture;
    mutable Futex m_mutex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename BuildPayloadT>
[[nodiscard]] bool RecordBuiltPayload(
    Recorder& recorder,
    const EventKind::Enum kind,
    const u64 frameIndex,
    const u32 streamId,
    BuildPayloadT buildPayload
){
    if(!recorder.enabled(kind))
        return false;

    TelemetryBytes payload(recorder.arena());
    if(!buildPayload(recorder.arena(), payload))
        return false;

    return recorder.recordBinary(kind, frameIndex, payload.data(), payload.size(), streamId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

