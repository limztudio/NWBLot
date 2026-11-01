// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "statistics.h"

#include <impl/ecs_render/shared/plan_reservation_base.h>
#include <impl/ecs_render/shared/statistics_readback_state.h>

#include <core/alloc/global.h>

#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU acceptance chooses banks; GPU ordering protects reuse; owner never waits.
class ReflectionHistoryState : NoCopy{
public:
    explicit ReflectionHistoryState(const u16 deviceGeneration)noexcept
        : m_deviceGeneration(deviceGeneration)
    {}


public:
    void reset(u16 deviceGeneration)noexcept;
    [[nodiscard]] ReflectionHistoryPlan plan(const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, u64 graphicsFrameIndex)noexcept;
    [[nodiscard]] b8 reserve(const ReflectionHistoryPlan& plan)noexcept;
    void discard(const ReflectionHistoryPlan& plan)noexcept;
    void accept(const ReflectionHistoryPlan& plan, const Core::QueueSubmissionToken& token, b8 hardwareReady)noexcept;


private:
    ReflectionSceneContentStamp m_stamp;
    ReflectionSettings m_settings;
    u64 m_generation = 1u;
    u64 m_nextSequence = 1u;
    u64 m_acceptedSequence = 0u;
    u64 m_reservedSequence = 0u;
    u64 m_epoch = 0u;
    u64 m_historyStartGraphicsFrame = 0u;
    u32 m_nextSampleIndex = 0u;
    u32 m_sampleCount = 0u;
    u32 m_acceptedBank = 0u;
    mutable Futex m_mutex;
    u16 m_deviceGeneration = 0u;
    b8 m_eligible = false;
    b8 m_hardwareReady = false;
    b8 m_resourcesChanged = false;
};

using ReflectionHistoryControl = RefCounter<ReflectionHistoryState>;
using ReflectionHistoryControlHandle = RefCountPtr< ReflectionHistoryControl, ArenaRefDeleter<ReflectionHistoryControl, Core::Alloc::GlobalArena> >;


class ReflectionHistoryReservation final : public PlanReservationBase<ReflectionHistoryControlHandle, ReflectionHistoryPlan>{
public:
    using PlanReservationBase<ReflectionHistoryControlHandle, ReflectionHistoryPlan>::PlanReservationBase;


public:
    void accept(const Core::QueueSubmissionToken& token, b8 hardwareReady)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

