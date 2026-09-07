// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    struct EventSlot{
        EventRecord record;
        EventSlot* nextAvailable = nullptr;

        explicit EventSlot(TelemetryArena& arena)
            : record(arena)
        {}
    };

    using EventSlotPtr = GlobalUniquePtr<EventSlot>;
    using EventVector = Vector<EventSlotPtr, TelemetryArena>;


private:
    class EventSlotLease final : NoCopy{
        friend Recorder;

    public:
        explicit EventSlotLease(Recorder& recorder);
        ~EventSlotLease();

    private:
        Recorder& m_recorder;
        UniqueLock<Futex> m_lock;
        EventSlotPtr m_slot;
    };


public:
    explicit Recorder(TelemetryArena& arena)
        : m_arena(arena)
        , m_events(arena)
    {}
    ~Recorder();


public:
    void setCaptureOptions(const CaptureOptions& options);
    // Enabled capture retains slots and payload capacity for the next frame. Disabling capture releases them.
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
    [[nodiscard]] bool recordPayload(
        EventKind::Enum kind,
        u64 frameIndex,
        TelemetryBytes&& payload,
        u32 streamId = 0u
    );
    template<typename BuildPayloadT>
    [[nodiscard]] bool recordBuiltPayload(
        const EventKind::Enum kind,
        const u64 frameIndex,
        const u32 streamId,
        BuildPayloadT buildPayload
    ){
        EventSlotLease lease(*this);
        if(!enabledUnlocked(kind))
            return false;
        lease.m_slot = acquireSlotUnlocked();
        if(!lease.m_slot)
            return false;

        // Callbacks may reenter the recorder or change capture options. Their slot remains exclusively leased.
        lease.m_lock.unlock();
        if(!buildPayload(m_arena, lease.m_slot->record.payload))
            return false;
        return publishBuiltSlot(lease, kind, frameIndex, streamId);
    }
    [[nodiscard]] bool append(const EventHeader& header, const void* payload, usize payloadBytes);
    [[nodiscard]] bool append(const EventHeader& header, TelemetryBytes&& payload);


private:
    [[nodiscard]] bool enabledUnlocked()const{ return m_capture.enabled(); }
    [[nodiscard]] bool enabledUnlocked(EventKind::Enum kind)const{ return CaptureAllowsEventKind(m_capture, kind); }
    [[nodiscard]] bool appendUnlocked(EventSlotLease& lease, const EventHeader& header, const void* payload, usize payloadBytes);
    [[nodiscard]] bool appendPayloadUnlocked(EventSlotLease& lease, const EventHeader& header, TelemetryBytes&& payload);
    [[nodiscard]] EventSlotPtr acquireSlotUnlocked();
    void recycleSlotUnlocked(EventSlotPtr&& slot)noexcept;
    void releaseAvailableSlotsUnlocked()noexcept;
    void publishSlotUnlocked(EventSlotLease& lease, const EventHeader& header);
    [[nodiscard]] bool publishBuiltSlot(EventSlotLease& lease, EventKind::Enum kind, u64 frameIndex, u32 streamId);
    [[nodiscard]] const EventRecord* eventAt(usize index)const;


private:
    TelemetryArena& m_arena;
    EventVector m_events;
    EventSlot* m_availableSlots = nullptr;
    CaptureOptions m_capture;
    mutable Futex m_mutex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

