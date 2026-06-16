// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "event.h"

#include <core/alloc/module.h>


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
    [[nodiscard]] CaptureOptions captureOptions()const{ return m_capture; }
    [[nodiscard]] bool enabled()const{ return m_capture.enabled(); }
    [[nodiscard]] bool enabled(EventKind::Enum kind)const{ return CaptureAllowsEventKind(m_capture, kind); }
    [[nodiscard]] usize eventCount()const{ return m_events.size(); }
    [[nodiscard]] EventView view()const{ return EventView(*this); }

    [[nodiscard]] bool record(
        const EventKind::Enum kind,
        const PayloadFormat::Enum payloadFormat,
        u64 frameIndex,
        const void* payload,
        usize payloadBytes,
        u32 streamId = 0u
    );
    [[nodiscard]] bool recordBinary(
        EventKind::Enum kind,
        u64 frameIndex,
        const void* payload,
        usize payloadBytes,
        u32 streamId = 0u
    );
    [[nodiscard]] bool append(const EventHeader& header, const void* payload, usize payloadBytes);


private:
    [[nodiscard]] const EventRecord* eventAt(usize index)const;


private:
    TelemetryArena& m_arena;
    EventVector m_events;
    CaptureOptions m_capture;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
