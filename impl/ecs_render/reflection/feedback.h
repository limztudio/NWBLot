// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scene_content_stamp.h"
#include "settings.h"

#include <core/alloc/global.h>
#include <core/graphics/rhi/command.h>
#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ReflectionFeedbackResetReason{
    enum Enum : u8{
        None,
        FirstObservation,
        ResourcesChanged,
        SettingsChanged,
        SceneChanged,
        ViewChanged,
        Disabled,
        UntrustedScene,
        HardwareChanged,
        InvalidAcceptance,
    };
};

struct ReflectionFeedbackPlan{
    ReflectionSceneContentStamp stamp;
    ReflectionSettings settings;
    u64 generation = 0u;
    u64 sequence = 0u;
    u64 acceptedSequence = 0u;
    u64 epoch = 0u;
    u64 graphicsFrameIndex = 0u;
    u64 startGraphicsFrame = 0u;
    u32 probeIndex = 0u;
    u32 currentBank = 0u;
    u32 previousBank = 0u;
    bool enabled = false;
    bool eligible = false;
    bool reused = false;
    bool reset = false;
    bool previousHardwareReady = false;
    bool quarantined = false;
    ReflectionFeedbackResetReason::Enum resetReason = ReflectionFeedbackResetReason::None;
};

struct ReflectionFeedbackOutcome{
    u64 epoch = 0u;
    u64 startGraphicsFrame = 0u;
    u32 probeIndex = 0u;
    bool eligible = false;
    bool reused = false;
    bool reset = false;
    ReflectionFeedbackResetReason::Enum resetReason = ReflectionFeedbackResetReason::None;
};

[[nodiscard]] ReflectionFeedbackOutcome ResolveReflectionFeedbackOutcome(const ReflectionFeedbackPlan& plan, bool hardwareReady)noexcept;

// Accepted writers publish feedback; writer-less classification only invalidates control.
// One shared Graphics queue; declared hazards order reuse without CPU waits.
// An unprovable token quarantines the generation until reset after join.
class ReflectionFeedbackState : NoCopy{
public:
    explicit ReflectionFeedbackState(u16 deviceGeneration);


public:
    void reset(u16 deviceGeneration)noexcept;
    [[nodiscard]] ReflectionFeedbackPlan plan(
        const ReflectionSceneContentStamp& stamp,
        const ReflectionSettings& settings,
        bool enabled,
        u64 graphicsFrameIndex
    )noexcept;
    [[nodiscard]] bool reserve(const ReflectionFeedbackPlan& plan)noexcept;
    void discard(const ReflectionFeedbackPlan& plan)noexcept;
    [[nodiscard]] bool accept(const ReflectionFeedbackPlan& plan, const Core::QueueSubmissionToken& token, bool hardwareReady)noexcept;

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
    bool m_enabled = false;
    bool m_eligible = false;
    bool m_hardwareReady = false;
    bool m_resourcesChanged = false;
    bool m_quarantined = false;
};

using ReflectionFeedbackControl = RefCounter<ReflectionFeedbackState>;
using ReflectionFeedbackControlHandle = RefCountPtr<
    ReflectionFeedbackControl, ArenaRefDeleter<ReflectionFeedbackControl, Core::Alloc::GlobalArena>
>;

[[nodiscard]] ReflectionFeedbackControlHandle CreateReflectionFeedbackControl(Core::Alloc::GlobalArena& arena, u16 deviceGeneration);

class ReflectionFeedbackReservation final : NoCopy{
public:
    ReflectionFeedbackReservation(ReflectionFeedbackControlHandle control, const ReflectionFeedbackPlan& plan);
    ReflectionFeedbackReservation(ReflectionFeedbackReservation&& other)noexcept;
    ~ReflectionFeedbackReservation()noexcept;
    ReflectionFeedbackReservation& operator=(ReflectionFeedbackReservation&& other)noexcept;


public:
    [[nodiscard]] bool valid()const noexcept{ return m_reserved; }
    void accept(const Core::QueueSubmissionToken& token, bool hardwareReady)noexcept;
    void discard()noexcept;

private:
    ReflectionFeedbackControlHandle m_control;
    ReflectionFeedbackPlan m_plan;
    bool m_reserved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

