// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "event.h"

#include <core/alloc/module.h>

#include <global/not_null.h>
#include <global/span.h>
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
    [[nodiscard]] b8 valid()const noexcept{ return m_recorder != nullptr; }
    [[nodiscard]] usize eventCount()const noexcept;
    [[nodiscard]] const EventRecord* eventAt(usize index)const noexcept;
    [[nodiscard]] Optional<NotNull<const EventRecord*>> borrowEventAt(const usize index)const noexcept{
        if(const EventRecord* const event = eventAt(index))
            return MakeNotNull(event);
        return {};
    }


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
        b8 diagnosticReserved = false;


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
        explicit EventSlotLease(Recorder& recorder)noexcept
            : m_recorder(recorder)
            , m_lock(recorder.m_mutex)
        {}
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
    ~Recorder()noexcept{
        releaseAvailableSlotsUnlocked();
    }


public:
    void setCaptureOptions(const CaptureOptions& options);
    void clear()noexcept;
    [[nodiscard]] TelemetryBytes createByteBuffer(){ return TelemetryBytes(m_arena); }

    [[nodiscard]] TelemetryArena& arena()noexcept{ return m_arena; }

    [[nodiscard]] const TelemetryArena& arena()const noexcept{ return m_arena; }
    [[nodiscard]] CaptureOptions captureOptions()const noexcept;
    [[nodiscard]] b8 enabled()const noexcept;
    [[nodiscard]] b8 enabled(EventKind::Enum kind)const noexcept;
    [[nodiscard]] usize eventCount()const noexcept;
    [[nodiscard]] u64 droppedDiagnosticCount()const noexcept{ return m_droppedDiagnosticCount.load(MemoryOrder::relaxed); }

    [[nodiscard]] EventView view()const noexcept{ return EventView(*this); }

    [[nodiscard]] b8 recordBinary(EventKind::Enum kind, u64 frameIndex, const void* payload, usize payloadBytes, u32 streamId = 0u);
    [[nodiscard]] b8 recordPayload(EventKind::Enum kind, u64 frameIndex, TelemetryBytes&& payload, u32 streamId = 0u);
    template<typename BuildPayloadT>
    [[nodiscard]] b8 recordBuiltPayload(const EventKind::Enum kind, const u64 frameIndex, const u32 streamId, BuildPayloadT buildPayload){
        return recordBuiltPayloadResult(kind, frameIndex, streamId, buildPayload) == BuiltPayloadResult::Recorded;
    }
    [[nodiscard]] b8 append(const EventHeader& header, const void* payload, usize payloadBytes);
    [[nodiscard]] b8 append(const EventHeader& header, TelemetryBytes&& payload);


private:
    template<typename BuildPayloadT>
    [[nodiscard]] BuiltPayloadResult::Enum recordBuiltPayloadResult(const EventKind::Enum kind, const u64 frameIndex, const u32 streamId, BuildPayloadT& buildPayload){
        EventSlotLease lease(*this);
        if(!enabledUnlocked(kind))
            return BuiltPayloadResult::Filtered;
        lease.m_slot = acquireSlotUnlocked();
        if(!lease.m_slot)
            return BuiltPayloadResult::Failed;

        lease.m_lock.unlock();
        if(!buildPayload(m_arena, lease.m_slot->record.payload))
            return BuiltPayloadResult::Failed;
        return publishBuiltSlot(lease, kind, frameIndex, streamId);
    }

    [[nodiscard]] b8 enabledUnlocked()const noexcept{ return m_capture.enabled(); }

    [[nodiscard]] b8 enabledUnlocked(EventKind::Enum kind)const noexcept{ return CaptureAllowsEventKind(m_capture, kind); }
    [[nodiscard]] b8 appendUnlocked(EventSlotLease& lease, const EventHeader& header, const void* payload, usize payloadBytes);
    [[nodiscard]] b8 appendPayloadUnlocked(EventSlotLease& lease, const EventHeader& header, TelemetryBytes&& payload);
    [[nodiscard]] EventSlotPtr acquireSlotUnlocked()noexcept;
    void prepareDiagnosticSlotsUnlocked();
    void recordCapturedDiagnostic(u64 frameIndex, Span<const u8> payload, u32 streamId)noexcept;
    void dropDiagnosticCapture()noexcept{
        m_droppedDiagnosticCount.fetch_add(1u, MemoryOrder::relaxed);
    }
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
    b8 m_diagnosticPrepared = false;
    Atomic<b8> m_diagnosticCaptureEnabled{ false };
    Atomic<u64> m_droppedDiagnosticCount{ 0u };
    CaptureOptions m_capture;
    mutable Futex m_mutex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

