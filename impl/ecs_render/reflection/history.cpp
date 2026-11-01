// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "history.h"

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionHistoryState::reset(const u16 deviceGeneration)noexcept{
    NothrowScopedLock lock(m_mutex);

    ++m_generation;
    m_deviceGeneration = deviceGeneration;
    m_acceptedSequence = 0u;
    m_reservedSequence = 0u;
    m_nextSampleIndex = 0u;
    m_sampleCount = 0u;
    m_acceptedBank = 0u;
    m_eligible = false;
    m_hardwareReady = false;
    m_resourcesChanged = true;
}

ReflectionHistoryPlan ReflectionHistoryState::plan(const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, const u64 graphicsFrameIndex)noexcept{
    NothrowScopedLock lock(m_mutex);

    const u64 sequence = m_nextSequence++;
    return ResolveReflectionHistoryPlan(
        stamp,
        settings,
        graphicsFrameIndex,
        m_stamp,
        m_settings,
        m_generation,
        sequence,
        m_acceptedSequence,
        m_epoch,
        m_historyStartGraphicsFrame,
        m_nextSampleIndex,
        m_sampleCount,
        m_acceptedBank,
        m_eligible,
        m_hardwareReady,
        m_resourcesChanged
    );
}

b8 ReflectionHistoryState::reserve(const ReflectionHistoryPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(m_deviceGeneration == 0u || m_reservedSequence != 0u || plan.generation != m_generation || plan.sequence == 0u || plan.acceptedSequence != m_acceptedSequence)
        return false;
    m_reservedSequence = plan.sequence;
    return true;
}

void ReflectionHistoryState::discard(const ReflectionHistoryPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(plan.generation == m_generation && plan.sequence == m_reservedSequence)
        m_reservedSequence = 0u;
}

void ReflectionHistoryState::accept(const ReflectionHistoryPlan& plan, const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(plan.generation != m_generation || plan.sequence != m_reservedSequence)
        return;
    if(!token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration)
        return;
    const ReflectionHistoryOutcome outcome = ResolveReflectionHistoryOutcome(plan, hardwareReady);
    m_stamp = plan.stamp;
    m_settings = plan.settings;
    m_acceptedSequence = plan.sequence;
    m_reservedSequence = 0u;
    m_epoch = outcome.epoch;
    m_historyStartGraphicsFrame = outcome.historyStartGraphicsFrame;
    m_nextSampleIndex = outcome.sampleIndex + 1u;
    m_sampleCount = outcome.sampleCount;
    m_acceptedBank = plan.currentBank;
    m_eligible = outcome.eligible;
    m_hardwareReady = hardwareReady;
    m_resourcesChanged = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionHistoryReservation::accept(const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
    if(m_reserved)
        m_control->accept(m_plan, token, hardwareReady);
    m_reserved = false;
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

