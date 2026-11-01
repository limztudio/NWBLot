// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "feedback.h"

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionFeedbackState::reset(const u16 deviceGeneration)noexcept{
    NothrowScopedLock lock(m_mutex);

    ++m_generation;
    m_deviceGeneration = deviceGeneration;
    m_acceptedSequence = 0u;
    m_reservedSequence = 0u;
    m_acceptedToken = {};
    m_nextProbeIndex = 0u;
    m_startGraphicsFrame = 0u;
    m_acceptedBank = 0u;
    m_enabled = false;
    m_eligible = false;
    m_hardwareReady = false;
    m_resourcesChanged = true;
    m_quarantined = false;
}

ReflectionFeedbackPlan ReflectionFeedbackState::plan(const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, const b8 enabled, const u64 graphicsFrameIndex)noexcept{
    NothrowScopedLock lock(m_mutex);

    const u64 sequence = m_nextSequence++;
    return ResolveReflectionFeedbackPlan(
        stamp,
        settings,
        enabled,
        graphicsFrameIndex,
        m_stamp,
        m_settings,
        m_generation,
        sequence,
        m_acceptedSequence,
        m_epoch,
        m_startGraphicsFrame,
        m_nextProbeIndex,
        m_acceptedBank,
        m_enabled,
        m_eligible,
        m_hardwareReady,
        m_resourcesChanged,
        m_quarantined
    );
}

b8 ReflectionFeedbackState::reserve(const ReflectionFeedbackPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(m_quarantined || m_deviceGeneration == 0u || m_reservedSequence != 0u || plan.generation != m_generation || plan.sequence <= m_acceptedSequence || plan.acceptedSequence != m_acceptedSequence)
        return false;
    m_reservedSequence = plan.sequence;
    return true;
}

void ReflectionFeedbackState::discard(const ReflectionFeedbackPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(plan.generation == m_generation && plan.sequence == m_reservedSequence)
        m_reservedSequence = 0u;
}

b8 ReflectionFeedbackState::accept(const ReflectionFeedbackPlan& plan, const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(plan.generation != m_generation || plan.sequence != m_reservedSequence)
        return false;
    m_reservedSequence = 0u;
    if(
        !token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration
        || token.queue != Core::CommandQueue::Graphics
        || (m_acceptedToken.valid() && (!token.matchesPhysicalQueue(m_acceptedToken.physicalQueueIndex, m_acceptedToken.deviceGeneration) || token.value <= m_acceptedToken.value))
    ){
        // Accepted work blocks discard-based overwrite proof.
        m_quarantined = true;
        return false;
    }
    const ReflectionFeedbackOutcome outcome = ResolveReflectionFeedbackOutcome(plan, hardwareReady);
    m_stamp = plan.stamp;
    m_settings = plan.settings;
    m_acceptedToken = token;
    m_acceptedSequence = plan.sequence;
    m_epoch = outcome.epoch;
    m_startGraphicsFrame = outcome.startGraphicsFrame;
    m_nextProbeIndex = outcome.eligible ? outcome.probeIndex + 1u : 0u;
    if(outcome.eligible)
        m_acceptedBank = plan.currentBank;
    m_enabled = plan.enabled;
    m_eligible = outcome.eligible;
    m_hardwareReady = hardwareReady;
    m_resourcesChanged = false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionFeedbackReservation::accept(const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
    if(m_reserved && !m_control->accept(m_plan, token, hardwareReady))
        m_control->discard(m_plan);
    m_reserved = false;
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

