// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/reflection/settings.adhd.h>
#include <impl/ecs_render/shared/statistics_readback_state.h>

#include <core/alloc/global.h>
#include <core/graphics/rhi/command.h>

#include <global/plan_reservation.h>
#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Accepted writers publish feedback; writer-less classification only invalidates control.
// One shared Graphics queue; declared hazards order reuse without CPU waits.
// An unprovable token quarantines the generation until reset after join.
class ReflectionFeedbackState : NoCopy{
public:
    explicit ReflectionFeedbackState(const u16 deviceGeneration)noexcept
        : m_deviceGeneration(deviceGeneration)
    {}


public:
    void reset(const u16 deviceGeneration)noexcept{
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
    [[nodiscard]] ReflectionFeedbackPlan plan(const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, const b8 enabled, const u64 graphicsFrameIndex)noexcept{
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
    [[nodiscard]] b8 reserve(const ReflectionFeedbackPlan& plan)noexcept{
        NothrowScopedLock lock(m_mutex);

        if(!CanReserveReflectionFeedbackPlan(m_quarantined, m_deviceGeneration, m_reservedSequence, m_generation, m_acceptedSequence, plan))
            return false;
        m_reservedSequence = plan.sequence;
        return true;
    }
    void discard(const ReflectionFeedbackPlan& plan)noexcept{
        NothrowScopedLock lock(m_mutex);

        if(plan.generation == m_generation && plan.sequence == m_reservedSequence)
            m_reservedSequence = 0u;
    }
    [[nodiscard]] b8 accept(const ReflectionFeedbackPlan& plan, const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
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
        m_acceptedToken = token;
        ApplyAcceptedReflectionFeedbackPlan(
            plan,
            hardwareReady,
            m_stamp,
            m_settings,
            m_acceptedSequence,
            m_epoch,
            m_startGraphicsFrame,
            m_nextProbeIndex,
            m_acceptedBank,
            m_enabled,
            m_eligible,
            m_hardwareReady,
            m_resourcesChanged
        );
        return true;
    }


private:
    ReflectionSceneContentStamp m_stamp;
    ReflectionSettings m_settings;
    u64 m_generation = 1u;
    u64 m_nextSequence = 1u;
    u64 m_acceptedSequence = 0u;
    u64 m_reservedSequence = 0u;
    u64 m_epoch = 0u;
    u64 m_startGraphicsFrame = 0u;
    Core::QueueSubmissionToken m_acceptedToken;
    u32 m_nextProbeIndex = 0u;
    u32 m_acceptedBank = 0u;
    mutable Futex m_mutex;
    u16 m_deviceGeneration = 0u;
    b8 m_enabled = false;
    b8 m_eligible = false;
    b8 m_hardwareReady = false;
    b8 m_resourcesChanged = false;
    b8 m_quarantined = false;
};

using ReflectionFeedbackControl = RefCounter<ReflectionFeedbackState>;
using ReflectionFeedbackControlHandle = RefCountPtr< ReflectionFeedbackControl, ArenaRefDeleter<ReflectionFeedbackControl, Core::Alloc::GlobalArena> >;


class ReflectionFeedbackReservation final : public PlanReservationBase<ReflectionFeedbackControlHandle, ReflectionFeedbackPlan>{
public:
    using PlanReservationBase<ReflectionFeedbackControlHandle, ReflectionFeedbackPlan>::PlanReservationBase;


public:
    void accept(const Core::QueueSubmissionToken& token, const b8 hardwareReady)noexcept{
        if(m_reserved && !m_control->accept(m_plan, token, hardwareReady))
            m_control->discard(m_plan);
        m_reserved = false;
        m_control = nullptr;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

