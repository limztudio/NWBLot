// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "recorder.h"

#include <global/allocation_size.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_recorder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 TimestampNanoseconds()noexcept{
    return DurationInNS<u64>(TimerNow());
}

[[nodiscard]] static EventHeader MakeEventHeader(const EventKind::Enum kind, const u64 frameIndex, const usize payloadBytes, const u32 streamId)noexcept{
    EventHeader header;
    header.kind = kind;
    header.streamId = streamId;
    header.frameIndex = frameIndex;
    header.timestampNanoseconds = TimestampNanoseconds();
    header.payloadBytes = payloadBytes;
    return header;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize EventView::eventCount()const noexcept{
    return m_recorder ? m_recorder->eventCount() : 0u;
}

const EventRecord* EventView::eventAt(const usize index)const noexcept{
    return m_recorder ? m_recorder->eventAt(index) : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Recorder::EventSlotLease::EventSlotLease(Recorder& recorder)noexcept
    : m_recorder(recorder)
    , m_lock(recorder.m_mutex)
{}

Recorder::EventSlotLease::~EventSlotLease()noexcept{
    if(!m_slot)
        return;
    if(!m_lock.owns_lock())
        m_lock.lock();
    m_recorder.recycleSlotUnlocked(Move(m_slot));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Recorder::~Recorder()noexcept{
    releaseAvailableSlotsUnlocked();
}

void Recorder::setCaptureOptions(const CaptureOptions& options){
    NothrowScopedLock lock(m_mutex);
    if(options.diagnosticEnabled())
        prepareDiagnosticSlotsUnlocked();
    m_capture = options;
    m_diagnosticCaptureEnabled.store(options.diagnosticEnabled(), MemoryOrder::release);
    if(!m_capture.enabled()){
        m_events.clear();
        releaseAvailableSlotsUnlocked();
    }
}

void Recorder::clear()noexcept{
    NothrowScopedLock lock(m_mutex);
    for(auto& slot : m_events)
        recycleSlotUnlocked(Move(slot));
    m_events.clear();
}

CaptureOptions Recorder::captureOptions()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_capture;
}

bool Recorder::enabled()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return enabledUnlocked();
}

bool Recorder::enabled(const EventKind::Enum kind)const noexcept{
    NothrowScopedLock lock(m_mutex);
    return enabledUnlocked(kind);
}

usize Recorder::eventCount()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_events.size();
}

bool Recorder::recordBinary(const EventKind::Enum kind, const u64 frameIndex, const void* payload, const usize payloadBytes, const u32 streamId){
    EventSlotLease lease(*this);
    if(!enabledUnlocked(kind))
        return false;

    return appendUnlocked(lease, __hidden_telemetry_recorder::MakeEventHeader(kind, frameIndex, payloadBytes, streamId), payload, payloadBytes);
}

bool Recorder::recordPayload(const EventKind::Enum kind, const u64 frameIndex, TelemetryBytes&& payload, const u32 streamId){
    EventSlotLease lease(*this);
    if(!enabledUnlocked(kind))
        return false;

    return appendPayloadUnlocked(lease, __hidden_telemetry_recorder::MakeEventHeader(kind, frameIndex, payload.size(), streamId), Move(payload));
}

bool Recorder::append(const EventHeader& header, const void* payload, const usize payloadBytes){
    EventSlotLease lease(*this);
    return appendUnlocked(lease, header, payload, payloadBytes);
}

bool Recorder::append(const EventHeader& header, TelemetryBytes&& payload){
    EventSlotLease lease(*this);
    return appendPayloadUnlocked(lease, header, Move(payload));
}

bool Recorder::appendUnlocked(EventSlotLease& lease, const EventHeader& header, const void* payload, const usize payloadBytes){
    if(!header.valid())
        return false;
    if(header.payloadBytes != payloadBytes)
        return false;
    if(payloadBytes != 0u && !payload)
        return false;

    lease.m_slot = acquireSlotUnlocked();
    if(!lease.m_slot)
        return false;

    if(payloadBytes != 0u){
        auto& destination = lease.m_slot->record.payload;
        destination.resize(payloadBytes);
        NWB_MEMCPY(destination.data(), destination.size(), payload, payloadBytes);
    }

    publishSlotUnlocked(lease, header);
    return true;
}

bool Recorder::appendPayloadUnlocked(EventSlotLease& lease, const EventHeader& header, TelemetryBytes&& payload){
    if(!header.valid())
        return false;
    if(header.payloadBytes != payload.size())
        return false;

    // Prebuilt payloads from the recorder arena can transfer their backing allocation.
    // Preserve the raw-byte append behavior for callers whose payload belongs to a different arena.
    if(payload.get_allocator().arenaPtr() != &m_arena)
        return appendUnlocked(lease, header, payload.data(), payload.size());

    lease.m_slot = acquireSlotUnlocked();
    if(!lease.m_slot)
        return false;

    lease.m_slot->record.payload = Move(payload);
    publishSlotUnlocked(lease, header);
    return true;
}

Recorder::EventSlotPtr Recorder::acquireSlotUnlocked(){
    if(!m_availableSlots)
        return MakeGlobalUnique<EventSlot>(m_arena, m_arena);

    EventSlot* const slot = m_availableSlots;
    m_availableSlots = slot->nextAvailable;
    slot->nextAvailable = nullptr;
    return EventSlotPtr(slot, EventSlotPtr::deleter_type(m_arena));
}

void Recorder::prepareDiagnosticSlotsUnlocked(){
    if(m_diagnosticPrepared)
        return;

    m_events.reserve(AddSize(m_events.size(), s_DiagnosticCaptureEventCapacity));
    // Preparation can throw; retain completed slots so a later attempt finishes the same bounded pool.
    while(m_diagnosticSlotCount < s_DiagnosticCaptureEventCapacity){
        auto slot = MakeGlobalUnique<EventSlot>(m_arena, m_arena);
        if(!slot)
            throw AllocationException{};
        slot->record.payload.reserve(s_DiagnosticCapturePayloadCapacity);
        slot->diagnosticReserved = true;
        slot->nextAvailable = m_diagnosticAvailableSlots;
        m_diagnosticAvailableSlots = slot.release();
        ++m_diagnosticSlotCount;
    }
    m_diagnosticPrepared = true;
}

void Recorder::recordCapturedDiagnostic(const u64 frameIndex, const NotNull<const u8*> payload, const usize payloadBytes, const u32 streamId)noexcept{
    if(!m_diagnosticCaptureEnabled.load(MemoryOrder::acquire))
        return;
    if(!m_mutex.try_lock()){
        dropDiagnosticCapture();
        return;
    }
    ScopeExit unlock([this]()noexcept{ m_mutex.unlock(); });
    if(!m_capture.diagnosticEnabled())
        return;
    if(payloadBytes > s_DiagnosticCapturePayloadCapacity || !m_diagnosticAvailableSlots || m_events.size() == m_events.capacity()){
        dropDiagnosticCapture();
        return;
    }

    EventSlot* const available = m_diagnosticAvailableSlots;
    auto& destination = available->record.payload;
    if(payloadBytes > destination.capacity()){
        dropDiagnosticCapture();
        return;
    }
    // Both capacities were admitted before publication. Byte initialization and owning-pointer movement cannot throw.
    destination.resize(payloadBytes);
    NWB_MEMCPY(destination.data(), destination.size(), payload.get(), payloadBytes);
    available->record.header = __hidden_telemetry_recorder::MakeEventHeader(EventKind::Diagnostic, frameIndex, payloadBytes, streamId);
    m_diagnosticAvailableSlots = available->nextAvailable;
    available->nextAvailable = nullptr;
    m_events.push_back(EventSlotPtr(available, EventSlotPtr::deleter_type(m_arena)));
}

void Recorder::dropDiagnosticCapture()noexcept{
    m_droppedDiagnosticCount.fetch_add(1u, MemoryOrder::relaxed);
}

void Recorder::recycleSlotUnlocked(EventSlotPtr&& slot)noexcept{
    if(!enabledUnlocked() || slot->record.payload.get_allocator().arenaPtr() != &m_arena){
        slot.reset();
        return;
    }
    slot->record.payload.clear();
    EventSlot*& available = slot->diagnosticReserved ? m_diagnosticAvailableSlots : m_availableSlots;
    slot->nextAvailable = available;
    available = slot.release();
}

void Recorder::releaseAvailableSlotsUnlocked()noexcept{
    while(m_availableSlots){
        EventSlot* const slot = m_availableSlots;
        m_availableSlots = slot->nextAvailable;
        EventSlotPtr::deleter_type{ m_arena }(slot);
    }
    while(m_diagnosticAvailableSlots){
        EventSlot* const slot = m_diagnosticAvailableSlots;
        m_diagnosticAvailableSlots = slot->nextAvailable;
        EventSlotPtr::deleter_type{ m_arena }(slot);
    }
    m_diagnosticSlotCount = 0u;
    m_diagnosticPrepared = false;
}

void Recorder::publishSlotUnlocked(EventSlotLease& lease, const EventHeader& header){
    lease.m_slot->record.header = header;
    m_events.push_back(Move(lease.m_slot));
}

Recorder::BuiltPayloadResult::Enum Recorder::publishBuiltSlot(EventSlotLease& lease, const EventKind::Enum kind, const u64 frameIndex, const u32 streamId){
    lease.m_lock.lock();
    if(!enabledUnlocked(kind))
        return BuiltPayloadResult::Filtered;

    const EventHeader header = __hidden_telemetry_recorder::MakeEventHeader(kind, frameIndex, lease.m_slot->record.payload.size(), streamId);
    if(!header.valid())
        return BuiltPayloadResult::Failed;

    if(lease.m_slot->record.payload.get_allocator().arenaPtr() != &m_arena){
        EventSlotPtr foreignSlot = Move(lease.m_slot);
        return appendUnlocked(lease, header, foreignSlot->record.payload.data(), foreignSlot->record.payload.size()) ? BuiltPayloadResult::Recorded : BuiltPayloadResult::Failed;
    }

    publishSlotUnlocked(lease, header);
    return BuiltPayloadResult::Recorded;
}

const EventRecord* Recorder::eventAt(const usize index)const noexcept{
    NothrowScopedLock lock(m_mutex);
    if(index >= m_events.size())
        return nullptr;

    return &m_events[index]->record;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

