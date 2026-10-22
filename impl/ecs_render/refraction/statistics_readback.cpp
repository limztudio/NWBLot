// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "statistics_readback.h"

#include <impl/assets/graphics/refraction/diagnostics_layout.h>

#include <core/alloc/general.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RefractionStatisticsState::RefractionStatisticsState(const u16 deviceGeneration)noexcept
    : m_deviceGeneration(deviceGeneration)
{}

void RefractionStatisticsState::reset(const u16 deviceGeneration)noexcept{
    NothrowScopedLock lock(m_mutex);

    m_generation = m_generation == Limit<u64>::s_Max ? 0u : m_generation + 1u;
    m_deviceGeneration = deviceGeneration;
    m_latest = {};
    for(Slot& slot : m_slots)
        slot = {};
}

RefractionStatisticsReservationKey RefractionStatisticsState::reserve(const RefractionStatistics& metadata)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(m_deviceGeneration == 0u || m_generation == 0u || m_nextSequence == Limit<u64>::s_Max)
        return {};
    for(u32 index = 0u; index < s_SlotCount; ++index){
        Slot& slot = m_slots[index];
        if(slot.reserved || slot.inFlight)
            continue;
        slot.metadata = metadata;
        slot.metadata.sequence = m_nextSequence++;
        slot.metadata.generation = m_generation;
        slot.metadata.acceptedToken = {};
        slot.metadata.hardwareReady = false;
        slot.reserved = true;
        return RefractionStatisticsReservationKey{slot.metadata.sequence, m_generation, index};
    }
    return {};
}

void RefractionStatisticsState::discard(const RefractionStatisticsReservationKey& key)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = matchingSlot(key);
    if(slot && slot->reserved && !slot->inFlight)
        *slot = {};
}

void RefractionStatisticsState::accept(const RefractionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const bool hardwareReady)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = matchingSlot(key);
    if(!slot || !slot->reserved || slot->inFlight)
        return;
    slot->reserved = false;
    slot->inFlight = true;
    slot->metadata.acceptedToken = token;
    slot->metadata.hardwareReady = hardwareReady;
    slot->metadata.csgHardwareEnabled = slot->metadata.csgHardwareEnabled && hardwareReady;
    // Never recycle memory whose GPU completion is unproved.
}

Expected<RefractionStatisticsPendingReadback> RefractionStatisticsState::pending(const u32 index)const noexcept{
    NothrowScopedLock lock(m_mutex);

    if(index >= s_SlotCount)
        return MakeUnexpected(Failure{});
    const Slot& slot = m_slots[index];
    const Core::QueueSubmissionToken& token = slot.metadata.acceptedToken;
    if(!slot.inFlight || !token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration)
        return MakeUnexpected(Failure{});
    return RefractionStatisticsPendingReadback{ .key = {slot.metadata.sequence, slot.metadata.generation, index}, .token = token, };
}

void RefractionStatisticsState::complete(const RefractionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const u32* counters)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = matchingSlot(key);
    if(!slot || !slot->inFlight)
        return;
    const Core::QueueSubmissionToken& accepted = slot->metadata.acceptedToken;
    if(!token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration || accepted.queue != token.queue || accepted.value != token.value || !accepted.matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration))
        return;
    if(counters && slot->metadata.sequence > m_latest.sequence){
        m_latest = slot->metadata;
#define NWB_REFRACTION_READ_COUNTER(name, offset) m_latest.name = counters[offset / sizeof(u32)];
        NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_REFRACTION_READ_COUNTER)
#undef NWB_REFRACTION_READ_COUNTER
    }
    *slot = {};
}

Expected<RefractionStatistics> RefractionStatisticsState::tryGetLatestStatistics()const noexcept{
    NothrowScopedLock lock(m_mutex);

    if(m_latest.sequence == 0u)
        return MakeUnexpected(Failure{});
    return m_latest;
}

RefractionStatisticsState::Slot* RefractionStatisticsState::matchingSlot(const RefractionStatisticsReservationKey& key)noexcept{
    if(!key.valid() || key.generation != m_generation || key.slot >= s_SlotCount)
        return nullptr;
    Slot& slot = m_slots[key.slot];
    return slot.metadata.sequence == key.sequence && slot.metadata.generation == key.generation ? &slot : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RefractionStatisticsControlHandle CreateRefractionStatisticsControl(Core::Alloc::GlobalArena& arena, const u16 deviceGeneration)noexcept{
    return RefractionStatisticsControlHandle(NewArenaObject<RefractionStatisticsControl>(arena, deviceGeneration), ArenaRefDeleter<RefractionStatisticsControl, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RefractionStatisticsReservation::RefractionStatisticsReservation(RefractionStatisticsControlHandle control, const RefractionStatistics& metadata)noexcept
    : m_control(Move(control))
{
    if(m_control)
        m_key = m_control->reserve(metadata);
}

RefractionStatisticsReservation::RefractionStatisticsReservation(RefractionStatisticsReservation&& other)noexcept
    : m_control(Move(other.m_control))
    , m_key(other.m_key)
{
    other.m_key = {};
}

RefractionStatisticsReservation::~RefractionStatisticsReservation()noexcept{
    discard();
}

RefractionStatisticsReservation& RefractionStatisticsReservation::operator=(RefractionStatisticsReservation&& other)noexcept{
    if(this != &other){
        discard();
        m_control = Move(other.m_control);
        m_key = other.m_key;
        other.m_key = {};
    }
    return *this;
}

void RefractionStatisticsReservation::accept(const Core::QueueSubmissionToken& token, const bool hardwareReady)noexcept{
    if(m_control && m_key.valid())
        m_control->accept(m_key, token, hardwareReady);
    m_key = {};
    m_control = nullptr;
}

void RefractionStatisticsReservation::discard()noexcept{
    if(m_control && m_key.valid())
        m_control->discard(m_key);
    m_key = {};
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RefractionStatisticsReadback::RefractionStatisticsReadback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept
    : m_arena(arena)
    , m_graphics(graphics)
{}

void RefractionStatisticsReadback::invalidateResources()noexcept{
    // Renderer invalidation follows the runtime's device join or terminal loss and graph-payload reset.
    // Retire the descriptor only at that boundary; disabling diagnostics keeps pending resources alive.
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(m_counterDescriptor.valid() && heap.isInitialized())
        heap.free(m_counterDescriptor);
    m_counterDescriptor = Core::GpuDescriptorHandle::Invalid();
    m_counters = nullptr;
    if(m_control)
        m_control->reset(0u);
    for(Core::BufferHandle& buffer : m_buffers)
        buffer = nullptr;
}

bool RefractionStatisticsReadback::prepareResources(){
    if(m_counters && m_counterDescriptor.valid() && m_buffers[0])
        return true;
    const u16 deviceGeneration = m_graphics.getDevice().getDeviceGeneration();
    if(!m_control)
        m_control = CreateRefractionStatisticsControl(m_arena, deviceGeneration);
    if(!m_control){
        NWB_LOGGER_ERROR(NWB_TEXT("Refraction statistics: failed to allocate readback control"));
        return false;
    }
    static constexpr Name s_Names[] = { Name("engine/refraction/statistics_readback_0"), Name("engine/refraction/statistics_readback_1"), Name("engine/refraction/statistics_readback_2"), };
    Core::BufferDesc counterDesc;
    counterDesc.setByteSize(NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE).setCanHaveUAVs(true).setCanHaveRawViews(true)
        .setDebugName(Name("engine/refraction/diagnostic_counters"))
        .enableAutomaticStateTracking(Core::ResourceStates::UnorderedAccess);
    const auto counterResult = m_graphics.createBuffer(counterDesc);
    if(!counterResult)
        return false;
    Core::BufferHandle counters = *counterResult;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    const auto descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!descriptor)
        return false;
    if(!heap.write(*descriptor, Core::DescriptorWriteItem::RawBufferUav(0u, counters.get()))){
        heap.free(*descriptor);
        return false;
    }
    Core::BufferHandle buffers[RefractionStatisticsState::s_SlotCount];
    for(u32 index = 0u; index < RefractionStatisticsState::s_SlotCount; ++index){
        Core::BufferDesc desc;
        desc.setByteSize(NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE).setCpuAccess(Core::CpuAccessMode::Read).setDebugName(s_Names[index]).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).enableAutomaticStateTracking(Core::ResourceStates::CopyDest);
        auto buffersResult = m_graphics.createBuffer(desc);
        if(buffersResult)
            buffers[index] = Move(*buffersResult);
        else
            buffers[index].reset();
        if(!buffersResult){
            heap.free(*descriptor);
            return false;
        }
    }
    m_control->reset(deviceGeneration);
    m_counters = Move(counters);
    m_counterDescriptor = *descriptor;
    for(u32 index = 0u; index < RefractionStatisticsState::s_SlotCount; ++index)
        m_buffers[index] = Move(buffers[index]);
    return true;
}

void RefractionStatisticsReadback::pollCompleted(){
    if(!m_control)
        return;
    auto& device = m_graphics.getDevice();
    for(u32 index = 0u; index < RefractionStatisticsState::s_SlotCount; ++index){
        if(!m_buffers[index])
            continue;
        const auto pending = m_control->pending(index);
        if(!pending || pending->token.deviceGeneration != device.getDeviceGeneration())
            continue;
        const auto& key = pending->key;
        const auto& token = pending->token;
        const Core::GpuPhysicalQueueId physicalQueue{.index = token.physicalQueueIndex, .deviceGeneration = token.deviceGeneration};
        const Expected<u64> completedValue = device.queueGetCompletedInstance(physicalQueue);
        if(!completedValue || *completedValue < token.value)
            continue;
        const Expected<void*> counters = device.mapBuffer(*m_buffers[index], Core::CpuAccessMode::Read);
        if(!counters){
            NWB_LOGGER_ERROR(NWB_TEXT("Refraction statistics: failed to map a completed readback"));
            m_control->complete(key, token, nullptr);
            continue;
        }
        u32 completedCounters[NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE / sizeof(u32)];
        NWB_MEMCPY(completedCounters, sizeof(completedCounters), *counters, sizeof(completedCounters));
        device.unmapBuffer(*m_buffers[index]);
        m_control->complete(key, token, completedCounters);
    }
}

Expected<RefractionStatistics> RefractionStatisticsReadback::tryGetLatestStatistics()const noexcept{
    if(!m_control)
        return MakeUnexpected(Failure{});
    return m_control->tryGetLatestStatistics();
}

RefractionStatisticsReadbackSnapshot RefractionStatisticsReadback::snapshot(const RefractionStatistics& metadata)const noexcept{
    RefractionStatisticsReadbackSnapshot result;
    if(!m_control || !m_counters || !m_counterDescriptor.valid() || !m_buffers[0])
        return result;
    result.control = m_control;
    result.counters = m_counters;
    result.counterDescriptor = m_counterDescriptor;
    result.metadata = metadata;
    for(u32 index = 0u; index < RefractionStatisticsState::s_SlotCount; ++index)
        result.buffers[index] = m_buffers[index];
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

