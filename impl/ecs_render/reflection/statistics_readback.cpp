// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "statistics_readback.h"

#include <impl/assets/graphics/reflection/frame_constants.h>

#include <core/alloc/general.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionStatisticsState::ReflectionStatisticsState(const u16 deviceGeneration)
    : m_deviceGeneration(deviceGeneration)
{}

void ReflectionStatisticsState::reset(const u16 deviceGeneration)noexcept{
    NothrowScopedLock lock(m_mutex);

    m_generation = m_generation == Limit<u64>::s_Max ? 0u : m_generation + 1u;
    m_deviceGeneration = deviceGeneration;
    m_latest = {};
    for(Slot& slot : m_slots)
        slot = {};
}

ReflectionStatisticsReservationKey ReflectionStatisticsState::reserve(const ReflectionStatistics& metadata)noexcept{
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
        return ReflectionStatisticsReservationKey{slot.metadata.sequence, m_generation, index};
    }
    return {};
}

void ReflectionStatisticsState::discard(const ReflectionStatisticsReservationKey& key)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = matchingSlot(key);
    if(slot && slot->reserved && !slot->inFlight)
        *slot = {};
}

void ReflectionStatisticsState::accept(
    const ReflectionStatisticsReservationKey& key,
    const Core::QueueSubmissionToken& token,
    const bool hardwareReady,
    const ReflectionHistoryOutcome* history,
    const ReflectionFeedbackOutcome* feedback)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = matchingSlot(key);
    if(!slot || !slot->reserved || slot->inFlight)
        return;
    slot->reserved = false;
    slot->inFlight = true;
    slot->metadata.acceptedToken = token;
    slot->metadata.hardwareReady = hardwareReady;
    slot->metadata.opticalTransportEnabled = slot->metadata.opticalTransportEnabled && hardwareReady;
    if(history){
        slot->metadata.historyEpoch = history->epoch;
        slot->metadata.historyStartGraphicsFrame = history->historyStartGraphicsFrame;
        slot->metadata.historySampleCount = history->sampleCount;
        slot->metadata.sampleIndex = history->sampleIndex;
        slot->metadata.historyEligible = history->eligible;
        slot->metadata.historyReused = history->reused;
        slot->metadata.historyReset = history->reset;
        slot->metadata.historyResetReason = history->resetReason;
    }
    if(feedback){
        slot->metadata.feedbackEpoch = feedback->epoch;
        slot->metadata.feedbackStartGraphicsFrame = feedback->startGraphicsFrame;
        slot->metadata.feedbackProbeIndex = feedback->probeIndex;
        slot->metadata.feedbackEnabled = feedback->eligible;
        slot->metadata.feedbackReused = feedback->reused;
        slot->metadata.feedbackReset = feedback->reset;
        slot->metadata.feedbackResetReason = feedback->resetReason;
    }
    // Never recycle memory whose GPU completion is unproved.
}

bool ReflectionStatisticsState::pending(
    const u32 index,
    ReflectionStatisticsReservationKey& outKey,
    Core::QueueSubmissionToken& outToken)const noexcept{
    NothrowScopedLock lock(m_mutex);

    outKey = {};
    outToken = {};
    if(index >= s_SlotCount)
        return false;
    const Slot& slot = m_slots[index];
    const Core::QueueSubmissionToken& token = slot.metadata.acceptedToken;
    if(!slot.inFlight || !token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration)
        return false;
    outKey = ReflectionStatisticsReservationKey{slot.metadata.sequence, slot.metadata.generation, index};
    outToken = token;
    return true;
}

void ReflectionStatisticsState::complete(
    const ReflectionStatisticsReservationKey& key,
    const Core::QueueSubmissionToken& token,
    const u32* counters)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = matchingSlot(key);
    if(!slot || !slot->inFlight)
        return;
    const Core::QueueSubmissionToken& accepted = slot->metadata.acceptedToken;
    if(
        !token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration
        || accepted.queue != token.queue || accepted.value != token.value
        || !accepted.matchesPhysicalQueue(token.physicalQueueIndex, token.deviceGeneration)
    )
        return;
    if(counters && slot->metadata.sequence > m_latest.sequence){
        m_latest = slot->metadata;
        m_latest.candidates = counters[NWB_REFLECTION_COUNTER_CANDIDATES / sizeof(u32)];
        m_latest.hardwareRays = counters[NWB_REFLECTION_COUNTER_HARDWARE_RAYS / sizeof(u32)];
        m_latest.hardwareHits = counters[NWB_REFLECTION_COUNTER_HARDWARE_HITS / sizeof(u32)];
        m_latest.opaquePixels = counters[NWB_REFLECTION_COUNTER_OPAQUE_PIXELS / sizeof(u32)];
        m_latest.glassPixels = counters[NWB_REFLECTION_COUNTER_GLASS_PIXELS / sizeof(u32)];
        m_latest.fallbackPixels = counters[NWB_REFLECTION_COUNTER_FALLBACK_PIXELS / sizeof(u32)];
        m_latest.screenAttempts = counters[NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS / sizeof(u32)];
        m_latest.screenHits = counters[NWB_REFLECTION_COUNTER_SCREEN_HITS / sizeof(u32)];
        m_latest.hardwareQueries = counters[NWB_REFLECTION_COUNTER_HARDWARE_QUERIES / sizeof(u32)];
        m_latest.bootstrapEvents = counters[NWB_REFLECTION_COUNTER_BOOTSTRAP_EVENTS / sizeof(u32)];
        m_latest.transparentPaths = counters[NWB_REFLECTION_COUNTER_TRANSPARENT_PATHS / sizeof(u32)];
        m_latest.unsupportedPaths = counters[NWB_REFLECTION_COUNTER_UNSUPPORTED_PATHS / sizeof(u32)];
        m_latest.limitedPaths = counters[NWB_REFLECTION_COUNTER_LIMITED_PATHS / sizeof(u32)];
        m_latest.ambiguousPaths = counters[NWB_REFLECTION_COUNTER_AMBIGUOUS_PATHS / sizeof(u32)];
        m_latest.tirEvents = counters[NWB_REFLECTION_COUNTER_TIR_EVENTS / sizeof(u32)];
        m_latest.mediumOverflowPaths = counters[NWB_REFLECTION_COUNTER_MEDIUM_OVERFLOW_PATHS / sizeof(u32)];
        m_latest.potentialReceivers = counters[NWB_REFLECTION_COUNTER_POTENTIAL_RECEIVERS / sizeof(u32)];
        m_latest.screenReturns = counters[NWB_REFLECTION_COUNTER_SCREEN_RETURNS / sizeof(u32)];
        m_latest.feedbackBypassedPixels = counters[NWB_REFLECTION_COUNTER_FEEDBACK_BYPASSED_PIXELS / sizeof(u32)];
        m_latest.feedbackProbeTiles = counters[NWB_REFLECTION_COUNTER_FEEDBACK_PROBE_TILES / sizeof(u32)];
        m_latest.screenIterations = static_cast<u64>(counters[NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_LOW / sizeof(u32)])
            | (static_cast<u64>(counters[NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_HIGH / sizeof(u32)]) << 32u)
        ;
        m_latest.screenLimitMisses = counters[NWB_REFLECTION_COUNTER_SCREEN_LIMIT_MISSES / sizeof(u32)];
    }
    *slot = {};
}

bool ReflectionStatisticsState::tryGetLatestStatistics(ReflectionStatistics& outStatistics)const noexcept{
    NothrowScopedLock lock(m_mutex);

    if(m_latest.sequence == 0u)
        return false;
    outStatistics = m_latest;
    return true;
}

ReflectionStatisticsState::Slot* ReflectionStatisticsState::matchingSlot(const ReflectionStatisticsReservationKey& key)noexcept{
    if(!key.valid() || key.generation != m_generation || key.slot >= s_SlotCount)
        return nullptr;
    Slot& slot = m_slots[key.slot];
    return slot.metadata.sequence == key.sequence && slot.metadata.generation == key.generation ? &slot : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionStatisticsControlHandle CreateReflectionStatisticsControl(Core::Alloc::GlobalArena& arena, const u16 deviceGeneration){
    return ReflectionStatisticsControlHandle(
        NewArenaObject<ReflectionStatisticsControl>(arena, deviceGeneration),
        ArenaRefDeleter<ReflectionStatisticsControl, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionStatisticsReservation::ReflectionStatisticsReservation(
    ReflectionStatisticsControlHandle control,
    const ReflectionStatistics& metadata)
    : m_control(Move(control))
{
    if(m_control)
        m_key = m_control->reserve(metadata);
}

ReflectionStatisticsReservation::ReflectionStatisticsReservation(ReflectionStatisticsReservation&& other)noexcept
    : m_control(Move(other.m_control))
    , m_key(other.m_key)
{
    other.m_key = {};
}

ReflectionStatisticsReservation::~ReflectionStatisticsReservation()noexcept{
    discard();
}

ReflectionStatisticsReservation& ReflectionStatisticsReservation::operator=(ReflectionStatisticsReservation&& other)noexcept{
    if(this != &other){
        discard();
        m_control = Move(other.m_control);
        m_key = other.m_key;
        other.m_key = {};
    }
    return *this;
}

void ReflectionStatisticsReservation::accept(
    const Core::QueueSubmissionToken& token,
    const bool hardwareReady,
    const ReflectionHistoryOutcome* history,
    const ReflectionFeedbackOutcome* feedback)noexcept{
    if(m_control && m_key.valid())
        m_control->accept(m_key, token, hardwareReady, history, feedback);
    m_key = {};
    m_control = nullptr;
}

void ReflectionStatisticsReservation::discard()noexcept{
    if(m_control && m_key.valid())
        m_control->discard(m_key);
    m_key = {};
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionStatisticsReadback::ReflectionStatisticsReadback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)
    : m_arena(arena)
    , m_graphics(graphics)
{}

void ReflectionStatisticsReadback::invalidateResources(){
    if(m_control)
        m_control->reset(0u);
    for(Core::BufferHandle& buffer : m_buffers)
        buffer = nullptr;
}

bool ReflectionStatisticsReadback::prepareResources(){
    if(m_buffers[0])
        return true;
    const u16 deviceGeneration = m_graphics.getDevice().getDeviceGeneration();
    if(!m_control)
        m_control = CreateReflectionStatisticsControl(m_arena, deviceGeneration);
    if(!m_control){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection statistics: failed to allocate readback control"));
        return false;
    }
    static constexpr Name s_Names[] = {
        Name("engine/reflection/statistics_readback_0"),
        Name("engine/reflection/statistics_readback_1"),
        Name("engine/reflection/statistics_readback_2"),
    };
    Core::BufferHandle buffers[ReflectionStatisticsState::s_SlotCount];
    for(u32 index = 0u; index < ReflectionStatisticsState::s_SlotCount; ++index){
        Core::BufferDesc desc;
        desc
            .setByteSize(NWB_REFLECTION_COUNTER_SIZE)
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setDebugName(s_Names[index])
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        buffers[index] = m_graphics.createBuffer(desc);
        if(!buffers[index])
            return false;
    }
    m_control->reset(deviceGeneration);
    for(u32 index = 0u; index < ReflectionStatisticsState::s_SlotCount; ++index)
        m_buffers[index] = Move(buffers[index]);
    return true;
}

void ReflectionStatisticsReadback::pollCompleted(){
    if(!m_control)
        return;
    auto& device = m_graphics.getDevice();
    for(u32 index = 0u; index < ReflectionStatisticsState::s_SlotCount; ++index){
        ReflectionStatisticsReservationKey key;
        Core::QueueSubmissionToken token;
        if(!m_buffers[index] || !m_control->pending(index, key, token) || token.deviceGeneration != device.getDeviceGeneration())
            continue;
        const Core::GpuPhysicalQueueId physicalQueue{token.physicalQueueIndex, token.deviceGeneration};
        if(device.queueGetCompletedInstance(physicalQueue) < token.value)
            continue;
        const auto* const counters = static_cast<const u32*>(device.mapBuffer(*m_buffers[index], Core::CpuAccessMode::Read));
        if(!counters){
            NWB_LOGGER_ERROR(NWB_TEXT("Reflection statistics: failed to map a completed readback"));
            m_control->complete(key, token, nullptr);
            continue;
        }
        u32 completedCounters[NWB_REFLECTION_COUNTER_SIZE / sizeof(u32)];
        NWB_MEMCPY(completedCounters, sizeof(completedCounters), counters, sizeof(completedCounters));
        device.unmapBuffer(*m_buffers[index]);
        m_control->complete(key, token, completedCounters);
    }
}

bool ReflectionStatisticsReadback::tryGetLatestStatistics(ReflectionStatistics& outStatistics)const{
    return m_control && m_control->tryGetLatestStatistics(outStatistics);
}

ReflectionStatisticsReadbackSnapshot ReflectionStatisticsReadback::snapshot(const ReflectionStatistics& metadata)const{
    ReflectionStatisticsReadbackSnapshot result;
    if(!m_control || !m_buffers[0])
        return result;
    result.control = m_control;
    result.metadata = metadata;
    for(u32 index = 0u; index < ReflectionStatisticsState::s_SlotCount; ++index)
        result.buffers[index] = m_buffers[index];
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

