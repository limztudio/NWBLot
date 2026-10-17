// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "event.h"

#include <core/alloc/module.h>

#include <global/not_null.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TelemetryArena = Alloc::GlobalArena;
using TelemetryBytes = Vector<u8, TelemetryArena>;

class Recorder;
class EventView;
class DiagnosticCaptureGuard;
class TextLogCaptureLogger;

inline constexpr usize s_DiagnosticCaptureEventCapacity = 64u;
inline constexpr usize s_DiagnosticCapturePayloadCapacity = 8192u;

struct EventRecord{
    EventHeader header;
    TelemetryBytes payload;


public:
    explicit EventRecord(TelemetryArena& arena)noexcept
        : payload(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EventView final{
public:
    EventView()noexcept = default;
    explicit EventView(const Recorder& recorder)noexcept
        : m_recorder(&recorder)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{ return m_recorder != nullptr; }
    // Views are intended for quiescent export/readback points.
    // Individual reads are serialized, but callers should not clear the recorder while iterating a view.
    [[nodiscard]] usize eventCount()const noexcept;
    [[nodiscard]] const EventRecord* eventAt(usize index)const noexcept;


private:
    const Recorder* m_recorder = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Recorder final : NoCopy{
    friend EventView;
    friend DiagnosticCaptureGuard;
    friend TextLogCaptureLogger;


private:
    struct EventSlot{
        EventRecord record;
        EventSlot* nextAvailable = nullptr;
        bool diagnosticReserved = false;


    public:
        explicit EventSlot(TelemetryArena& arena)noexcept
            : record(arena)
        {}
    };


private:
    using EventSlotPtr = GlobalUniquePtr<EventSlot>;
    using EventVector = Vector<EventSlotPtr, TelemetryArena>;


private:
    struct BuiltPayloadResult{
        enum Enum : u8{
            Recorded,
            Filtered,
            Failed,
        };
    };


private:
    class EventSlotLease final : NoCopy{
        friend Recorder;


    public:
        explicit EventSlotLease(Recorder& recorder)noexcept;
        ~EventSlotLease()noexcept;


    private:
        Recorder& m_recorder;
        UniqueLock<Futex> m_lock;
        EventSlotPtr m_slot;
    };


public:
    explicit Recorder(TelemetryArena& arena)noexcept
        : m_arena(arena)
        , m_events(arena)
    {}
    ~Recorder()noexcept;


public:
    void setCaptureOptions(const CaptureOptions& options);
    // Enabled capture retains slots and payload capacity for the next frame. Disabling capture releases them.
    void clear()noexcept;
    [[nodiscard]] TelemetryArena& arena()noexcept{ return m_arena; }
    [[nodiscard]] const TelemetryArena& arena()const noexcept{ return m_arena; }
    [[nodiscard]] CaptureOptions captureOptions()const noexcept;
    [[nodiscard]] bool enabled()const noexcept;
    [[nodiscard]] bool enabled(EventKind::Enum kind)const noexcept;
    [[nodiscard]] usize eventCount()const noexcept;
    // Cumulative observer drops include contention, exhausted storage, and oversized payloads. Filtered events are excluded.
    [[nodiscard]] u64 droppedDiagnosticCount()const noexcept{ return m_droppedDiagnosticCount.load(MemoryOrder::relaxed); }
    [[nodiscard]] EventView view()const noexcept{ return EventView(*this); }

    [[nodiscard]] bool recordBinary(EventKind::Enum kind, u64 frameIndex, const void* payload, usize payloadBytes, u32 streamId = 0u);
    [[nodiscard]] bool recordPayload(EventKind::Enum kind, u64 frameIndex, TelemetryBytes&& payload, u32 streamId = 0u);
    template<typename BuildPayloadT>
    [[nodiscard]] bool recordBuiltPayload(const EventKind::Enum kind, const u64 frameIndex, const u32 streamId, BuildPayloadT buildPayload){
        return recordBuiltPayloadResult(kind, frameIndex, streamId, buildPayload) == BuiltPayloadResult::Recorded;
    }
    [[nodiscard]] bool append(const EventHeader& header, const void* payload, usize payloadBytes);
    [[nodiscard]] bool append(const EventHeader& header, TelemetryBytes&& payload);


private:
    template<typename BuildPayloadT>
    [[nodiscard]] BuiltPayloadResult::Enum recordBuiltPayloadResult(const EventKind::Enum kind, const u64 frameIndex, const u32 streamId, BuildPayloadT& buildPayload){
        EventSlotLease lease(*this);
        if(!enabledUnlocked(kind))
            return BuiltPayloadResult::Filtered;
        lease.m_slot = acquireSlotUnlocked();
        if(!lease.m_slot)
            return BuiltPayloadResult::Failed;

        // The callback can reenter or change options while this destination remains exclusively leased.
        lease.m_lock.unlock();
        if(!buildPayload(m_arena, lease.m_slot->record.payload))
            return BuiltPayloadResult::Failed;
        return publishBuiltSlot(lease, kind, frameIndex, streamId);
    }
    [[nodiscard]] bool enabledUnlocked()const noexcept{ return m_capture.enabled(); }
    [[nodiscard]] bool enabledUnlocked(EventKind::Enum kind)const noexcept{ return CaptureAllowsEventKind(m_capture, kind); }
    [[nodiscard]] bool appendUnlocked(EventSlotLease& lease, const EventHeader& header, const void* payload, usize payloadBytes);
    [[nodiscard]] bool appendPayloadUnlocked(EventSlotLease& lease, const EventHeader& header, TelemetryBytes&& payload);
    [[nodiscard]] EventSlotPtr acquireSlotUnlocked();
    void prepareDiagnosticSlotsUnlocked();
    void recordCapturedDiagnostic(u64 frameIndex, NotNull<const u8*> payload, usize payloadBytes, u32 streamId)noexcept;
    void dropDiagnosticCapture()noexcept;
    void recycleSlotUnlocked(EventSlotPtr&& slot)noexcept;
    void releaseAvailableSlotsUnlocked()noexcept;
    void publishSlotUnlocked(EventSlotLease& lease, const EventHeader& header);
    [[nodiscard]] BuiltPayloadResult::Enum publishBuiltSlot(EventSlotLease& lease, EventKind::Enum kind, u64 frameIndex, u32 streamId);
    [[nodiscard]] const EventRecord* eventAt(usize index)const noexcept;


private:
    TelemetryArena& m_arena;
    EventVector m_events;
    EventSlot* m_availableSlots = nullptr;
    EventSlot* m_diagnosticAvailableSlots = nullptr;
    usize m_diagnosticSlotCount = 0u;
    bool m_diagnosticPrepared = false;
    Atomic<bool> m_diagnosticCaptureEnabled{ false };
    Atomic<u64> m_droppedDiagnosticCount{ 0u };
    CaptureOptions m_capture;
    mutable Futex m_mutex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

