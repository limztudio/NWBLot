// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "feedback.h"

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_feedback{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SameScreenSettings(const ReflectionSettings& a, const ReflectionSettings& b)noexcept{
    // Sampling/filter/diagnostics leave smooth rays unchanged.
    return
        a.traceMode == b.traceMode && a.samplingSeed == b.samplingSeed
        && a.maxHardwareRaysPerFrame == b.maxHardwareRaysPerFrame && a.maxOpticalQueries == b.maxOpticalQueries
        && a.maxRayDistance == b.maxRayDistance && a.distanceFadeStart == b.distanceFadeStart
        && a.roughnessCutoff == b.roughnessCutoff && a.screenMaxSteps == b.screenMaxSteps
        && a.screenThickness == b.screenThickness && a.screenConfidenceThreshold == b.screenConfidenceThreshold
        && a.screenEdgeFade == b.screenEdgeFade
        && a.environmentTop.x == b.environmentTop.x && a.environmentTop.y == b.environmentTop.y
        && a.environmentTop.z == b.environmentTop.z && a.environmentBottom.x == b.environmentBottom.x
        && a.environmentBottom.y == b.environmentBottom.y && a.environmentBottom.z == b.environmentBottom.z
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionFeedbackOutcome ResolveReflectionFeedbackOutcome(const ReflectionFeedbackPlan& plan, const bool hardwareReady)noexcept{
    const bool hardwareChanged = !plan.reset && plan.acceptedSequence != 0u && plan.previousHardwareReady != hardwareReady;
    ReflectionFeedbackOutcome result;
    result.epoch = plan.epoch + (hardwareChanged ? 1u : 0u);
    result.startGraphicsFrame = hardwareChanged ? plan.graphicsFrameIndex : plan.startGraphicsFrame;
    result.probeIndex = hardwareChanged ? 0u : plan.probeIndex;
    result.eligible = plan.eligible && hardwareReady;
    result.reused = result.eligible && plan.reused && !hardwareChanged;
    result.reset = plan.reset || hardwareChanged;
    result.resetReason = hardwareChanged ? ReflectionFeedbackResetReason::HardwareChanged : plan.resetReason;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionFeedbackState::ReflectionFeedbackState(const u16 deviceGeneration)
    : m_deviceGeneration(deviceGeneration)
{}

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

ReflectionFeedbackPlan ReflectionFeedbackState::plan(
    const ReflectionSceneContentStamp& stamp,
    const ReflectionSettings& settings,
    const bool enabled,
    const u64 graphicsFrameIndex)noexcept{
    NothrowScopedLock lock(m_mutex);

    ReflectionFeedbackPlan result;
    result.stamp = stamp;
    result.settings = settings;
    result.generation = m_generation;
    result.sequence = m_nextSequence++;
    result.acceptedSequence = m_acceptedSequence;
    result.graphicsFrameIndex = graphicsFrameIndex;
    result.enabled = enabled;
    result.previousHardwareReady = m_hardwareReady;
    result.quarantined = m_quarantined;
    if(m_quarantined)
        result.resetReason = ReflectionFeedbackResetReason::InvalidAcceptance;
    else if(m_acceptedSequence == 0u)
        result.resetReason = m_resourcesChanged ? ReflectionFeedbackResetReason::ResourcesChanged : ReflectionFeedbackResetReason::FirstObservation;
    else if(enabled != m_enabled || !__hidden_reflection_feedback::SameScreenSettings(settings, m_settings))
        result.resetReason = ReflectionFeedbackResetReason::SettingsChanged;
    else if(
        stamp.geometry != m_stamp.geometry || stamp.material != m_stamp.material
        || stamp.lighting != m_stamp.lighting || stamp.trusted != m_stamp.trusted
    )
        result.resetReason = ReflectionFeedbackResetReason::SceneChanged;
    else if(stamp.view != m_stamp.view)
        result.resetReason = ReflectionFeedbackResetReason::ViewChanged;
    result.reset = result.resetReason != ReflectionFeedbackResetReason::None;
    result.epoch = m_epoch + (result.reset ? 1u : 0u);
    result.startGraphicsFrame = result.reset ? graphicsFrameIndex : m_startGraphicsFrame;
    result.probeIndex = result.reset ? 0u : m_nextProbeIndex;
    result.eligible =
        !m_quarantined && enabled && stamp.trusted && settings.traceMode == ReflectionTraceMode::Hybrid
        && settings.maxHardwareRaysPerFrame > 0u
    ;
    result.reused = result.eligible && m_eligible && !result.reset;
    result.previousBank = m_acceptedBank;
    // Never overwrite the accepted bank.
    result.currentBank = m_acceptedSequence != 0u ? 1u - m_acceptedBank : 0u;
    if(!m_quarantined && result.reset && (!enabled || settings.traceMode != ReflectionTraceMode::Hybrid || settings.maxHardwareRaysPerFrame == 0u))
        result.resetReason = ReflectionFeedbackResetReason::Disabled;
    else if(!m_quarantined && result.reset && !stamp.trusted)
        result.resetReason = ReflectionFeedbackResetReason::UntrustedScene;
    return result;
}

bool ReflectionFeedbackState::reserve(const ReflectionFeedbackPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(
        m_quarantined || m_deviceGeneration == 0u || m_reservedSequence != 0u || plan.generation != m_generation
        || plan.sequence == 0u || plan.sequence <= m_acceptedSequence || plan.acceptedSequence != m_acceptedSequence
    )
        return false;
    m_reservedSequence = plan.sequence;
    return true;
}

void ReflectionFeedbackState::discard(const ReflectionFeedbackPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(plan.generation == m_generation && plan.sequence == m_reservedSequence)
        m_reservedSequence = 0u;
}

bool ReflectionFeedbackState::accept(
    const ReflectionFeedbackPlan& plan,
    const Core::QueueSubmissionToken& token,
    const bool hardwareReady)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(plan.generation != m_generation || plan.sequence != m_reservedSequence)
        return false;
    m_reservedSequence = 0u;
    if(
        !token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration
        || token.queue != Core::CommandQueue::Graphics
        || (m_acceptedToken.valid() && (
            !token.matchesPhysicalQueue(m_acceptedToken.physicalQueueIndex, m_acceptedToken.deviceGeneration)
            || token.value <= m_acceptedToken.value
        ))
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


ReflectionFeedbackControlHandle CreateReflectionFeedbackControl(Core::Alloc::GlobalArena& arena, const u16 deviceGeneration){
    return ReflectionFeedbackControlHandle(
        NewArenaObject<ReflectionFeedbackControl>(arena, deviceGeneration),
        ArenaRefDeleter<ReflectionFeedbackControl, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionFeedbackReservation::ReflectionFeedbackReservation(ReflectionFeedbackControlHandle control, const ReflectionFeedbackPlan& plan)
    : m_control(Move(control))
    , m_plan(plan)
{
    m_reserved = m_control && m_control->reserve(m_plan);
}

ReflectionFeedbackReservation::ReflectionFeedbackReservation(ReflectionFeedbackReservation&& other)noexcept
    : m_control(Move(other.m_control))
    , m_plan(other.m_plan)
    , m_reserved(other.m_reserved)
{
    other.m_reserved = false;
}

ReflectionFeedbackReservation::~ReflectionFeedbackReservation()noexcept{
    discard();
}

ReflectionFeedbackReservation& ReflectionFeedbackReservation::operator=(ReflectionFeedbackReservation&& other)noexcept{
    if(this != &other){
        discard();
        m_control = Move(other.m_control);
        m_plan = other.m_plan;
        m_reserved = other.m_reserved;
        other.m_reserved = false;
    }
    return *this;
}

void ReflectionFeedbackReservation::accept(const Core::QueueSubmissionToken& token, const bool hardwareReady)noexcept{
    if(m_reserved && !m_control->accept(m_plan, token, hardwareReady))
        m_control->discard(m_plan);
    m_reserved = false;
    m_control = nullptr;
}

void ReflectionFeedbackReservation::discard()noexcept{
    if(m_reserved)
        m_control->discard(m_plan);
    m_reserved = false;
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

