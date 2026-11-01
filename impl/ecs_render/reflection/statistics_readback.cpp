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


void ReflectionStatisticsState::accept(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const b8 hardwareReady, const ReflectionHistoryOutcome* history, const ReflectionFeedbackOutcome* feedback)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = beginAcceptWithinLock(key, token, hardwareReady);
    if(!slot)
        return;
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
}

void ReflectionStatisticsState::complete(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const Span<const u32> counters)noexcept{
    constexpr usize s_RequiredCounterCount = NWB_REFLECTION_COUNTER_SIZE / sizeof(u32);
    completeSlot<s_RequiredCounterCount>(
        key,
        token,
        counters,
        [](ReflectionStatistics& latest, const ReflectionStatistics& metadata, const Span<const u32> completedCounters)noexcept{
            latest.candidates = completedCounters[NWB_REFLECTION_COUNTER_CANDIDATES / sizeof(u32)];
            latest.hardwareRays = completedCounters[NWB_REFLECTION_COUNTER_HARDWARE_RAYS / sizeof(u32)];
            latest.exteriorEligibleRays = completedCounters[NWB_REFLECTION_COUNTER_EXTERIOR_ELIGIBLE_RAYS / sizeof(u32)];
            latest.hardwareHits = completedCounters[NWB_REFLECTION_COUNTER_HARDWARE_HITS / sizeof(u32)];
            latest.opaquePixels = completedCounters[NWB_REFLECTION_COUNTER_OPAQUE_PIXELS / sizeof(u32)];
            latest.glassPixels = completedCounters[NWB_REFLECTION_COUNTER_GLASS_PIXELS / sizeof(u32)];
            latest.fallbackPixels = completedCounters[NWB_REFLECTION_COUNTER_FALLBACK_PIXELS / sizeof(u32)];
            latest.screenAttempts = completedCounters[NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS / sizeof(u32)];
            latest.screenHits = completedCounters[NWB_REFLECTION_COUNTER_SCREEN_HITS / sizeof(u32)];
            latest.queryBudgetUnits = completedCounters[NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS / sizeof(u32)];
            latest.physicalQueries = metadata.csgHardwareEnabled ? completedCounters[NWB_REFLECTION_COUNTER_PHYSICAL_QUERIES / sizeof(u32)] : latest.queryBudgetUnits;
            latest.bootstrapEvents = completedCounters[NWB_REFLECTION_COUNTER_BOOTSTRAP_EVENTS / sizeof(u32)];
            latest.transparentPaths = completedCounters[NWB_REFLECTION_COUNTER_TRANSPARENT_PATHS / sizeof(u32)];
            latest.unsupportedPaths = completedCounters[NWB_REFLECTION_COUNTER_UNSUPPORTED_PATHS / sizeof(u32)];
            latest.limitedPaths = completedCounters[NWB_REFLECTION_COUNTER_LIMITED_PATHS / sizeof(u32)];
            latest.ambiguousPaths = completedCounters[NWB_REFLECTION_COUNTER_AMBIGUOUS_PATHS / sizeof(u32)];
            latest.tirEvents = completedCounters[NWB_REFLECTION_COUNTER_TIR_EVENTS / sizeof(u32)];
            latest.mediumOverflowPaths = completedCounters[NWB_REFLECTION_COUNTER_MEDIUM_OVERFLOW_PATHS / sizeof(u32)];
            latest.potentialReceivers = completedCounters[NWB_REFLECTION_COUNTER_POTENTIAL_RECEIVERS / sizeof(u32)];
            latest.screenReturns = completedCounters[NWB_REFLECTION_COUNTER_SCREEN_RETURNS / sizeof(u32)];
            latest.feedbackBypassedPixels = completedCounters[NWB_REFLECTION_COUNTER_FEEDBACK_BYPASSED_PIXELS / sizeof(u32)];
            latest.feedbackProbeTiles = completedCounters[NWB_REFLECTION_COUNTER_FEEDBACK_PROBE_TILES / sizeof(u32)];
            const u64 screenIterationsLow = static_cast<u64>(completedCounters[NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_LOW / sizeof(u32)]);
            const u64 screenIterationsHigh = static_cast<u64>(completedCounters[NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_HIGH / sizeof(u32)]);
            latest.screenIterations = screenIterationsLow | (screenIterationsHigh << s_ReflectionCounterHighWordShiftBits);
            latest.screenLimitMisses = completedCounters[NWB_REFLECTION_COUNTER_SCREEN_LIMIT_MISSES / sizeof(u32)];
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ReflectionStatisticsReadback::prepareResources(){
    if(m_buffers[0])
        return true;
    static constexpr Array<Name, s_StatisticsReadbackSlotCount> s_Names = { s_ReflectionStatisticsReadback0Name, s_ReflectionStatisticsReadback1Name, s_ReflectionStatisticsReadback2Name, };
    return ECSRenderDetail::PrepareStatisticsReadbackControlAndBuffers<NWB_REFLECTION_COUNTER_SIZE>(
        s_Names,
        NWB_TEXT("Reflection statistics: failed to allocate readback control"),
        m_arena,
        m_graphics,
        m_control,
        m_buffers
    );
}

void ReflectionStatisticsReadback::pollCompleted(){
    ECSRenderDetail::PollCompletedStatisticsReadback<NWB_REFLECTION_COUNTER_SIZE>(m_buffers, NWB_TEXT("Reflection statistics: failed to map a completed readback"), m_control, m_graphics);
}

ReflectionStatisticsReadbackSnapshot ReflectionStatisticsReadback::snapshot(const ReflectionStatistics& metadata)const noexcept{
    ReflectionStatisticsReadbackSnapshot result;
    if(!m_control || !m_buffers[0])
        return result;
    populateSnapshot(metadata, result);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

