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
    bool eligible = false;
    bool reused = false;
    bool reset = false;
    bool previousHardwareReady = false;
    ReflectionHistoryResetReason::Enum resetReason = ReflectionHistoryResetReason::None;
};

struct ReflectionHistoryOutcome{
    u64 epoch = 0u;
    u64 historyStartGraphicsFrame = 0u;
    u32 sampleIndex = 0u;
    u32 sampleCount = 0u;
    bool eligible = false;
    bool reused = false;
    bool reset = false;
    ReflectionHistoryResetReason::Enum resetReason = ReflectionHistoryResetReason::None;
};

[[nodiscard]] ReflectionHistoryOutcome ResolveReflectionHistoryOutcome(const ReflectionHistoryPlan& plan, bool hardwareReady)noexcept;

// CPU acceptance chooses banks; GPU queue ordering protects image reuse. This owner never waits for GPU completion.
class ReflectionHistoryState : NoCopy{
public:
    explicit ReflectionHistoryState(u16 deviceGeneration);
    void reset(u16 deviceGeneration)noexcept;
    [[nodiscard]] ReflectionHistoryPlan plan(
        const ReflectionSceneContentStamp& stamp,
        const ReflectionSettings& settings,
        u64 graphicsFrameIndex
    )noexcept;
    [[nodiscard]] bool reserve(const ReflectionHistoryPlan& plan)noexcept;
    void discard(const ReflectionHistoryPlan& plan)noexcept;
    void accept(const ReflectionHistoryPlan& plan, const Core::QueueSubmissionToken& token, bool hardwareReady)noexcept;

private:
    // Field order is alignment-descending: the 8-byte stamp/generation lanes lead and the 4-byte mutex plus the
    // 2-byte device generation pack with the 1-byte flags (200 -> 192 bytes).
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
    bool m_eligible = false;
    bool m_hardwareReady = false;
    bool m_resourcesChanged = false;
};

using ReflectionHistoryControl = RefCounter<ReflectionHistoryState>;
using ReflectionHistoryControlHandle = RefCountPtr<
    ReflectionHistoryControl, ArenaRefDeleter<ReflectionHistoryControl, Core::Alloc::GlobalArena>
>;

[[nodiscard]] ReflectionHistoryControlHandle CreateReflectionHistoryControl(Core::Alloc::GlobalArena& arena, u16 deviceGeneration);

class ReflectionHistoryReservation final : NoCopy{
public:
    ReflectionHistoryReservation(ReflectionHistoryControlHandle control, const ReflectionHistoryPlan& plan);
    ReflectionHistoryReservation(ReflectionHistoryReservation&& other)noexcept;
    ~ReflectionHistoryReservation()noexcept;
    ReflectionHistoryReservation& operator=(ReflectionHistoryReservation&& other)noexcept;

    [[nodiscard]] bool valid()const noexcept{ return m_reserved; }
    void accept(const Core::QueueSubmissionToken& token, bool hardwareReady)noexcept;
    void discard()noexcept;

private:
    ReflectionHistoryControlHandle m_control;
    ReflectionHistoryPlan m_plan;
    bool m_reserved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

