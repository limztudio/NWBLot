// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "statistics_readback.h"

#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/shared/statistics_readback_detail.h>

#include <core/alloc/general.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionStatisticsState::accept(const ReflectionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const bool hardwareReady, const ReflectionHistoryOutcome* history, const ReflectionFeedbackOutcome* feedback)noexcept{
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

void ReflectionStatisticsState::complete(const ReflectionStatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const u32* counters)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = beginCompleteWithinLock(key, token);
    if(!slot)
        return;
    if(counters && slot->metadata.sequence > m_latest.sequence){
        m_latest = slot->metadata;
        m_latest.candidates = counters[NWB_REFLECTION_COUNTER_CANDIDATES / sizeof(u32)];
        m_latest.hardwareRays = counters[NWB_REFLECTION_COUNTER_HARDWARE_RAYS / sizeof(u32)];
        m_latest.exteriorEligibleRays = counters[NWB_REFLECTION_COUNTER_EXTERIOR_ELIGIBLE_RAYS / sizeof(u32)];
        m_latest.hardwareHits = counters[NWB_REFLECTION_COUNTER_HARDWARE_HITS / sizeof(u32)];
        m_latest.opaquePixels = counters[NWB_REFLECTION_COUNTER_OPAQUE_PIXELS / sizeof(u32)];
        m_latest.glassPixels = counters[NWB_REFLECTION_COUNTER_GLASS_PIXELS / sizeof(u32)];
        m_latest.fallbackPixels = counters[NWB_REFLECTION_COUNTER_FALLBACK_PIXELS / sizeof(u32)];
        m_latest.screenAttempts = counters[NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS / sizeof(u32)];
        m_latest.screenHits = counters[NWB_REFLECTION_COUNTER_SCREEN_HITS / sizeof(u32)];
        m_latest.queryBudgetUnits = counters[NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS / sizeof(u32)];
        m_latest.physicalQueries = slot->metadata.csgHardwareEnabled ? counters[NWB_REFLECTION_COUNTER_PHYSICAL_QUERIES / sizeof(u32)] : m_latest.queryBudgetUnits;
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
        constexpr u32 s_CounterHighWordShiftBits = 32u;
        m_latest.screenIterations = static_cast<u64>(counters[NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_LOW / sizeof(u32)]) | (static_cast<u64>(counters[NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_HIGH / sizeof(u32)]) << s_CounterHighWordShiftBits);
        m_latest.screenLimitMisses = counters[NWB_REFLECTION_COUNTER_SCREEN_LIMIT_MISSES / sizeof(u32)];
    }
    *slot = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ReflectionStatisticsControlHandle> CreateReflectionStatisticsControl(Core::Alloc::GlobalArena& arena, const u16 deviceGeneration)noexcept{
    return CreateStatisticsControl<ReflectionStatisticsControl>(arena, deviceGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionStatisticsReservation::accept(const Core::QueueSubmissionToken& token, const bool hardwareReady, const ReflectionHistoryOutcome* history, const ReflectionFeedbackOutcome* feedback)noexcept{
    if(m_control && m_key.valid())
        m_control->accept(m_key, token, hardwareReady, history, feedback);
    m_key = {};
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionStatisticsReadback::ReflectionStatisticsReadback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept
    : m_arena(arena)
    , m_graphics(graphics)
{}

void ReflectionStatisticsReadback::invalidateResources()noexcept{
    if(m_control)
        m_control->reset(0u);
    for(Core::BufferHandle& buffer : m_buffers)
        buffer = nullptr;
}

bool ReflectionStatisticsReadback::prepareResources(){
    if(m_buffers[0])
        return true;
    const u16 deviceGeneration = m_graphics.getDevice().getDeviceGeneration();
    if(!m_control){
        auto controlResult = CreateReflectionStatisticsControl(m_arena, deviceGeneration);
        if(!controlResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Reflection statistics: failed to allocate readback control"));
            return false;
        }
        m_control = Move(*controlResult);
    }
    static constexpr Name s_Names[] = { Name("engine/reflection/statistics_readback_0"), Name("engine/reflection/statistics_readback_1"), Name("engine/reflection/statistics_readback_2"), };
    Core::BufferHandle buffers[ReflectionStatisticsState::s_SlotCount];
    if(!ECSRenderDetail::CreateStatisticsReadbackBuffers(m_graphics, NWB_REFLECTION_COUNTER_SIZE, s_Names, buffers))
        return false;
    m_control->reset(deviceGeneration);
    for(u32 index = 0u; index < ReflectionStatisticsState::s_SlotCount; ++index)
        m_buffers[index] = Move(buffers[index]);
    return true;
}

void ReflectionStatisticsReadback::pollCompleted(){
    ECSRenderDetail::PollCompletedStatisticsReadback<NWB_REFLECTION_COUNTER_SIZE>(
        m_control,
        m_graphics,
        m_buffers,
        NWB_TEXT("Reflection statistics: failed to map a completed readback")
    );
}

Expected<ReflectionStatistics> ReflectionStatisticsReadback::tryGetLatestStatistics()const noexcept{
    if(!m_control)
        return MakeUnexpected(Failure{});
    return m_control->tryGetLatestStatistics();
}

ReflectionStatisticsReadbackSnapshot ReflectionStatisticsReadback::snapshot(const ReflectionStatistics& metadata)const noexcept{
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

