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
            CopyReflectionDiagnosticCounters(completedCounters, metadata, latest);
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

