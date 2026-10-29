// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scene_content_stamp.h"
#include "statistics.h"

#include <core/alloc/global.h>

#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ReflectionHistoryPlan{
    ReflectionSceneContentStamp stamp;
    ReflectionSettings settings;
    u64 generation = 0u;
    u64 sequence = 0u;
    u64 acceptedSequence = 0u;
    u64 epoch = 0u;
    u64 graphicsFrameIndex = 0u;
    u64 historyStartGraphicsFrame = 0u;
    u32 sampleIndex = 0u;
    u32 previousSampleCount = 0u;
    u32 currentBank = 0u;
    u32 previousBank = 0u;
    b8 eligible = false;
    b8 reused = false;
    b8 reset = false;
    b8 previousHardwareReady = false;
    ReflectionHistoryResetReason::Enum resetReason = ReflectionHistoryResetReason::None;
};

struct ReflectionHistoryOutcome{
    u64 epoch = 0u;
    u64 historyStartGraphicsFrame = 0u;
    u32 sampleIndex = 0u;
    u32 sampleCount = 0u;
    b8 eligible = false;
    b8 reused = false;
    b8 reset = false;
    ReflectionHistoryResetReason::Enum resetReason = ReflectionHistoryResetReason::None;
};

[[nodiscard]] ReflectionHistoryOutcome ResolveReflectionHistoryOutcome(const ReflectionHistoryPlan& plan, b8 hardwareReady)noexcept;

// CPU acceptance chooses banks; GPU ordering protects reuse; owner never waits.
class ReflectionHistoryState : NoCopy{
public:
    explicit ReflectionHistoryState(u16 deviceGeneration)noexcept;


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

[[nodiscard]] Expected<ReflectionHistoryControlHandle> CreateReflectionHistoryControl(Core::Alloc::GlobalArena& arena, u16 deviceGeneration)noexcept;

class ReflectionHistoryReservation final : NoCopy{
public:
    ReflectionHistoryReservation(ReflectionHistoryControlHandle control, const ReflectionHistoryPlan& plan)noexcept;
    ReflectionHistoryReservation(ReflectionHistoryReservation&& other)noexcept;
    ~ReflectionHistoryReservation()noexcept;


public:
    ReflectionHistoryReservation& operator=(ReflectionHistoryReservation&& other)noexcept;


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_reserved; }
    void accept(const Core::QueueSubmissionToken& token, b8 hardwareReady)noexcept;
    void discard()noexcept;


private:
    ReflectionHistoryControlHandle m_control;
    ReflectionHistoryPlan m_plan;
    b8 m_reserved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

