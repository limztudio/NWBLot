// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "history.h"

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_history{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SameEstimatorSettings(const ReflectionSettings& a, const ReflectionSettings& b)noexcept{
    // Presentation diagnostics and the spatial filter do not affect the unfiltered temporal estimator.
    return
        a.traceMode == b.traceMode && a.temporalEnabled == b.temporalEnabled
        && a.temporalMaxSamples == b.temporalMaxSamples && a.samplingSeed == b.samplingSeed
        && a.maxHardwareRaysPerFrame == b.maxHardwareRaysPerFrame && a.maxOpticalQueries == b.maxOpticalQueries
        && a.maxRayDistance == b.maxRayDistance
        && a.distanceFadeStart == b.distanceFadeStart && a.roughnessCutoff == b.roughnessCutoff
        && a.screenMaxSteps == b.screenMaxSteps && a.screenThickness == b.screenThickness
        && a.screenConfidenceThreshold == b.screenConfidenceThreshold && a.screenEdgeFade == b.screenEdgeFade
        && a.environmentTop.x == b.environmentTop.x && a.environmentTop.y == b.environmentTop.y
        && a.environmentTop.z == b.environmentTop.z && a.environmentBottom.x == b.environmentBottom.x
        && a.environmentBottom.y == b.environmentBottom.y && a.environmentBottom.z == b.environmentBottom.z
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionHistoryOutcome ResolveReflectionHistoryOutcome(const ReflectionHistoryPlan& plan, const bool hardwareReady)noexcept{
    const bool hardwareChanged = !plan.reset && plan.acceptedSequence != 0u && plan.previousHardwareReady != hardwareReady;
    ReflectionHistoryOutcome result;
    result.epoch = plan.epoch + (hardwareChanged ? 1u : 0u);
    result.sampleIndex = hardwareChanged ? 0u : plan.sampleIndex;
    result.eligible = plan.eligible;
    result.reused = plan.reused && !hardwareChanged;
    result.historyStartGraphicsFrame = result.eligible ? (result.reused ? plan.historyStartGraphicsFrame : plan.graphicsFrameIndex) : 0u;
    result.reset = plan.reset || hardwareChanged;
    result.resetReason = hardwareChanged ? ReflectionHistoryResetReason::HardwareChanged : plan.resetReason;
    result.sampleCount = result.eligible ? Min(result.reused ? plan.previousSampleCount + 1u : 1u, plan.settings.temporalMaxSamples) : 0u;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionHistoryState::ReflectionHistoryState(const u16 deviceGeneration)
    : m_deviceGeneration(deviceGeneration)
{}

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

ReflectionHistoryPlan ReflectionHistoryState::plan(
    const ReflectionSceneContentStamp& stamp,
    const ReflectionSettings& settings,
    const u64 graphicsFrameIndex)noexcept{
    NothrowScopedLock lock(m_mutex);

    ReflectionHistoryPlan result;
    result.stamp = stamp;
    result.graphicsFrameIndex = graphicsFrameIndex;
    result.historyStartGraphicsFrame = m_historyStartGraphicsFrame;
    result.settings = settings;
    result.generation = m_generation;
    result.sequence = m_nextSequence++;
    result.acceptedSequence = m_acceptedSequence;
    result.previousHardwareReady = m_hardwareReady;
    if(m_acceptedSequence == 0u)
        result.resetReason = m_resourcesChanged ? ReflectionHistoryResetReason::ResourcesChanged : ReflectionHistoryResetReason::FirstSample;
    else if(!__hidden_reflection_history::SameEstimatorSettings(settings, m_settings))
        result.resetReason = ReflectionHistoryResetReason::SettingsChanged;
    else if(
        stamp.geometry != m_stamp.geometry || stamp.material != m_stamp.material
        || stamp.lighting != m_stamp.lighting || stamp.trusted != m_stamp.trusted
    )
        result.resetReason = ReflectionHistoryResetReason::SceneChanged;
    else if(stamp.view != m_stamp.view)
        result.resetReason = ReflectionHistoryResetReason::ViewChanged;
    result.reset = result.resetReason != ReflectionHistoryResetReason::None;
    result.epoch = m_epoch + (result.reset ? 1u : 0u);
    result.sampleIndex = result.reset ? 0u : m_nextSampleIndex;
    result.eligible = settings.temporalEnabled && stamp.trusted && settings.traceMode != ReflectionTraceMode::Disabled;
    result.reused = result.eligible && m_eligible && !result.reset && m_sampleCount > 0u;
    result.previousSampleCount = result.reused ? m_sampleCount : 0u;
    result.previousBank = m_acceptedBank;
    // A rejected transition must preserve the last accepted history even if the next frame disables accumulation.
    result.currentBank = (result.eligible || m_eligible) && m_acceptedSequence != 0u ? 1u - m_acceptedBank : 0u;
    if(result.reset && (!settings.temporalEnabled || settings.traceMode == ReflectionTraceMode::Disabled))
        result.resetReason = ReflectionHistoryResetReason::Disabled;
    else if(result.reset && !stamp.trusted)
        result.resetReason = ReflectionHistoryResetReason::UntrustedScene;
    return result;
}

bool ReflectionHistoryState::reserve(const ReflectionHistoryPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(
        m_deviceGeneration == 0u || m_reservedSequence != 0u || plan.generation != m_generation
        || plan.sequence == 0u || plan.acceptedSequence != m_acceptedSequence
    )
        return false;
    m_reservedSequence = plan.sequence;
    return true;
}

void ReflectionHistoryState::discard(const ReflectionHistoryPlan& plan)noexcept{
    NothrowScopedLock lock(m_mutex);

    if(plan.generation == m_generation && plan.sequence == m_reservedSequence)
        m_reservedSequence = 0u;
}

void ReflectionHistoryState::accept(
    const ReflectionHistoryPlan& plan,
    const Core::QueueSubmissionToken& token,
    const bool hardwareReady)noexcept{
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


ReflectionHistoryControlHandle CreateReflectionHistoryControl(Core::Alloc::GlobalArena& arena, const u16 deviceGeneration){
    return ReflectionHistoryControlHandle(
        NewArenaObject<ReflectionHistoryControl>(arena, deviceGeneration),
        ArenaRefDeleter<ReflectionHistoryControl, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionHistoryReservation::ReflectionHistoryReservation(ReflectionHistoryControlHandle control, const ReflectionHistoryPlan& plan)
    : m_control(Move(control))
    , m_plan(plan)
{
    m_reserved = m_control && m_control->reserve(m_plan);
}

ReflectionHistoryReservation::ReflectionHistoryReservation(ReflectionHistoryReservation&& other)noexcept
    : m_control(Move(other.m_control))
    , m_plan(other.m_plan)
    , m_reserved(other.m_reserved)
{
    other.m_reserved = false;
}

ReflectionHistoryReservation::~ReflectionHistoryReservation()noexcept{
    discard();
}

ReflectionHistoryReservation& ReflectionHistoryReservation::operator=(ReflectionHistoryReservation&& other)noexcept{
    if(this != &other){
        discard();
        m_control = Move(other.m_control);
        m_plan = other.m_plan;
        m_reserved = other.m_reserved;
        other.m_reserved = false;
    }
    return *this;
}

void ReflectionHistoryReservation::accept(const Core::QueueSubmissionToken& token, const bool hardwareReady)noexcept{
    if(m_reserved)
        m_control->accept(m_plan, token, hardwareReady);
    m_reserved = false;
    m_control = nullptr;
}

void ReflectionHistoryReservation::discard()noexcept{
    if(m_reserved)
        m_control->discard(m_plan);
    m_reserved = false;
    m_control = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

