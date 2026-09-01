// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/compiler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Summarizes one owner-serialized ledger drain at the compiler boundary.
struct RendererTaskTimingFeedbackDrainResult{
    usize acceptedAssignmentCount = 0u;
    usize recordedSampleCount = 0u;
    usize retiredSampleCount = 0u;
    usize rejectedAssignmentCount = 0u;
    usize rejectedSampleCount = 0u;
};


namespace RendererTaskTimingFeedbackCollectionAction{
    enum Enum : u8{
        None,
        Enable,
        Disable,

        kCount,
    };
};

struct RendererTaskTimingFeedbackPolicyTransition{
    Core::GpuTaskTimingFeedbackPolicy previousPolicy;
    Core::GpuTaskTimingFeedbackPolicy requestedPolicy;
    RendererTaskTimingFeedbackCollectionAction::Enum action = RendererTaskTimingFeedbackCollectionAction::None;
};

[[nodiscard]] RendererTaskTimingFeedbackPolicyTransition PrepareRendererTaskTimingFeedbackPolicyTransition(
    Core::GpuTaskTimingFeedbackPolicy& currentPolicy,
    const Core::GpuTaskTimingFeedbackPolicy& requestedPolicy,
    bool rendererActive
)noexcept;
void ResolveRendererTaskTimingFeedbackPolicyTransition(
    Core::GpuTaskTimingFeedbackPolicy& currentPolicy,
    const RendererTaskTimingFeedbackPolicyTransition& transition,
    bool collectionUpdated
)noexcept;


// Stores owner-serialized submission resolution and asynchronous sample completion in a drainable ledger. Recording
// may grow storage, but accepted/discarded/sample callbacks only mutate existing entries and are allocation-free.
class RendererTaskTimingFeedbackState final : NoCopy{
private:
    struct PendingSample{
        Core::GpuTimingSampleAttribution attribution = Core::s_NoGpuTimingSampleAttribution;
        Name scopeName = NAME_NONE;
        Core::GpuTaskTimingKey key;
        Core::GpuPhysicalQueueId expectedQueue;
        u64 sourceFrameIndex = 0u;
        f64 durationSeconds = 0.0;
        bool recordsNonCommittingTimingSample = false;
        bool submissionResolved = false;
        bool accepted = false;
        bool assignmentRecorded = false;
        bool sampleResolved = false;
        bool hasUsableSample = false;
    };


public:
    explicit RendererTaskTimingFeedbackState(Core::Alloc::GlobalArena& arena)
        : m_pendingSamples(arena)
    {}


public:
    [[nodiscard]] bool trackSample(
        Core::GpuTimingSampleAttribution attribution,
        const Name& scopeName,
        const Core::GpuTaskTimingKey& key,
        const Core::GpuPhysicalQueueId& expectedQueue,
        u64 sourceFrameIndex,
        bool recordsNonCommittingTimingSample
    );
    void acceptSubmission(
        Core::GpuTimingSampleAttribution attribution,
        const Core::QueueSubmissionToken& token,
        bool feedbackActive
    )noexcept;
    void discardRecording(Core::GpuTimingSampleAttribution attribution)noexcept;
    void completeSample(const Core::GpuTimingSample& sample, bool feedbackActive)noexcept;
    [[nodiscard]] RendererTaskTimingFeedbackDrainResult drain(
        Core::GpuTaskTimingHistoryStore& history,
        u16 deviceGeneration
    );
    void reset()noexcept{ m_pendingSamples.clear(); }


private:
    [[nodiscard]] usize findPendingSample(Core::GpuTimingSampleAttribution attribution)const noexcept;
    void retirePendingSample(usize pendingIndex);


private:
    Vector<PendingSample, Core::Alloc::GlobalArena> m_pendingSamples;
};


// Keeps renderer-selected task timing separate from the graph compiler. The renderer owns query-attribution lifetime,
// while the compiler consumes only an immutable history snapshot during the following frame's compile.
class RendererTaskTimingFeedback final : NoCopy{
private:
    static void onGpuTimingSampleCallback(void* context, const Core::GpuTimingSample& sample)noexcept;


public:
    RendererTaskTimingFeedback(
        Core::Alloc::GlobalArena& arena,
        Core::Graphics& graphics,
        NotNull<const Name*> feedbackCollectionScopes,
        usize feedbackCollectionScopeCount
    );
    ~RendererTaskTimingFeedback()noexcept;


public:
    void activate();
    void deactivate()noexcept;
    [[nodiscard]] bool setPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy);
    [[nodiscard]] Core::GpuTimingSampleAttribution beginSample(
        const Name& scopeName,
        const Core::GpuTaskTimingKey& key,
        const Core::GpuPhysicalQueueId& expectedQueue,
        bool recordsNonCommittingTimingSample
    );
    void acceptSubmission(
        Core::GpuTimingSampleAttribution attribution,
        const Core::QueueSubmissionToken& token
    )noexcept;
    void discardRecording(Core::GpuTimingSampleAttribution attribution)noexcept;
    void configureCompileOptions(Core::GpuTaskGraphCompileOptions& options, u64 frameIndex);
    void reset()noexcept;


private:
    [[nodiscard]] bool collectsScope(const Name& scopeName)const noexcept;
    void onGpuTimingSample(const Core::GpuTimingSample& sample)noexcept;


private:
    Core::Graphics& m_graphics;
    RendererTaskTimingFeedbackState m_state;
    Core::GpuTaskTimingHistoryStore m_history;
    Core::GpuTaskTimingHistorySnapshot m_snapshot;
    Vector<Name, Core::Alloc::GlobalArena> m_feedbackCollectionScopes;
    Futex m_lifecycleMutex;
    Futex m_mutex;
    Core::GpuTaskTimingFeedbackPolicy m_policy;
    Core::GpuTimingSampleSubscription m_subscription;
    bool m_active = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

