// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_statistics_reporter.h"

#include <impl/ecs_render/module.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ReflectionStatisticsReporter::poll(Impl::RendererSystem& renderer){
    const auto statisticsResult = renderer.tryGetLatestReflectionStatistics();
    if(!statisticsResult)
        return false;
    const NWB::Impl::ReflectionStatistics& statistics = *statisticsResult;
    if(statistics.sequence == m_statisticsSequence && statistics.generation == m_statisticsGeneration)
        return false;
    m_statisticsSequence = statistics.sequence;
    m_statisticsGeneration = statistics.generation;
    m_latestStatistics = statistics;
    m_feedbackObservedBypass = m_feedbackObservedBypass || statistics.feedbackBypassedPixels > 0u;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeStatistics: sequence={} generation={} frame={} mode={} width={} height={}")
        NWB_TEXT(" requested_budget={} effective_budget={} queue_capacity={} hardware_requested={} hardware_available={} hardware_ready={}")
        NWB_TEXT(" token_queue={} token_value={} physical_queue={} device_generation={}")
        NWB_TEXT(" candidates={} hardware_rays={} hardware_hits={} opaque_pixels={} glass_pixels={} fallback_pixels={}")
        NWB_TEXT(" screen_attempts={} screen_hits={}")
        , statistics.sequence
        , statistics.generation
        , statistics.frameIndex
        , static_cast<u32>(statistics.traceMode)
        , statistics.width
        , statistics.height
        , statistics.requestedHardwareBudget
        , statistics.effectiveHardwareBudget
        , statistics.queueCapacity
        , statistics.hardwareRequested ? 1u : 0u
        , statistics.hardwareAvailable ? 1u : 0u
        , statistics.hardwareReady ? 1u : 0u
        , static_cast<u32>(statistics.acceptedToken.queue)
        , statistics.acceptedToken.value
        , statistics.acceptedToken.physicalQueueIndex
        , statistics.acceptedToken.deviceGeneration
        , statistics.candidates
        , statistics.hardwareRays
        , statistics.hardwareHits
        , statistics.opaquePixels
        , statistics.glassPixels
        , statistics.fallbackPixels
        , statistics.screenAttempts
        , statistics.screenHits
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeHistory: sequence={} generation={} graphics_frame={} epoch={}") NWB_TEXT(" start_graphics_frame={} count={} sample_index={} seed={} eligible={} reused={} reset={} reason={}")
        , statistics.sequence
        , statistics.generation
        , statistics.graphicsFrameIndex
        , statistics.historyEpoch
        , statistics.historyStartGraphicsFrame
        , statistics.historySampleCount
        , statistics.sampleIndex
        , statistics.samplingSeed
        , statistics.historyEligible ? 1u : 0u
        , statistics.historyReused ? 1u : 0u
        , statistics.historyReset ? 1u : 0u
        , static_cast<u32>(statistics.historyResetReason)
    );
    {
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeOptics: sequence={} generation={} max_queries={} query_budget_units={} physical_queries={}")
            NWB_TEXT(" bootstrap_events={} transparent_paths={} unsupported_paths={} limited_paths={}")
            NWB_TEXT(" ambiguous_paths={} tir_events={} medium_overflow_paths={} transport_enabled={}")
            , statistics.sequence
            , statistics.generation
            , statistics.maxOpticalQueries
            , statistics.queryBudgetUnits
            , statistics.physicalQueries
            , statistics.bootstrapEvents
            , statistics.transparentPaths
            , statistics.unsupportedPaths
            , statistics.limitedPaths
            , statistics.ambiguousPaths
            , statistics.tirEvents
            , statistics.mediumOverflowPaths
            , statistics.opticalTransportEnabled ? 1u : 0u
        );
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeFeedback: sequence={} generation={} graphics_frame={}")
        NWB_TEXT(" feedback_sequence={} epoch={} start_graphics_frame={} probe_index={}")
        NWB_TEXT(" requested={} enabled={} reused={} reset={} reason={} scheduling_valid={}")
        NWB_TEXT(" potential_receivers={} screen_returns={} bypassed_pixels={} probe_tiles={}")
        NWB_TEXT(" screen_iterations={} screen_limit_misses={}")
        , statistics.sequence
        , statistics.generation
        , statistics.graphicsFrameIndex
        , statistics.feedbackSequence
        , statistics.feedbackEpoch
        , statistics.feedbackStartGraphicsFrame
        , statistics.feedbackProbeIndex
        , statistics.feedbackRequested ? 1u : 0u
        , statistics.feedbackEnabled ? 1u : 0u
        , statistics.feedbackReused ? 1u : 0u
        , statistics.feedbackReset ? 1u : 0u
        , static_cast<u32>(statistics.feedbackResetReason)
        , statistics.schedulingCounterValid ? 1u : 0u
        , statistics.potentialReceivers
        , statistics.screenReturns
        , statistics.feedbackBypassedPixels
        , statistics.feedbackProbeTiles
        , statistics.screenIterations
        , statistics.screenLimitMisses
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

