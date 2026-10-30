// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RendererTaskTimingFeedbackPolicyTransition{
    Core::GpuTaskTimingFeedbackPolicy previousPolicy;
    Core::GpuTaskTimingFeedbackPolicy requestedPolicy;
    RendererTaskTimingFeedbackCollectionAction::Enum action = RendererTaskTimingFeedbackCollectionAction::None;
};

[[nodiscard]] RendererTaskTimingFeedbackPolicyTransition PrepareRendererTaskTimingFeedbackPolicyTransition(Core::GpuTaskTimingFeedbackPolicy& currentPolicy, const Core::GpuTaskTimingFeedbackPolicy& requestedPolicy, b8 rendererActive)noexcept;
void ResolveRendererTaskTimingFeedbackPolicyTransition(Core::GpuTaskTimingFeedbackPolicy& currentPolicy, const RendererTaskTimingFeedbackPolicyTransition& transition, b8 collectionUpdated)noexcept;


// Drainable ledger for submission resolution and sample completion; callbacks stay allocation-free.
class RendererTaskTimingFeedbackState final : NoCopy{
private:
    struct PendingSample{
        Core::GpuTimingSampleAttribution attribution = Core::s_NoGpuTimingSampleAttribution;
        Name scopeName = s_NameNone;
        Core::GpuTaskTimingKey key;
        Core::GpuPhysicalQueueId expectedQueue;
        u64 sourceFrameIndex = 0u;
        f64 durationSeconds = 0.0;
        b8 recordsNonCommittingTimingSample = false;
        b8 submissionResolved = false;
        b8 accepted = false;
        b8 assignmentRecorded = false;
        b8 sampleResolved = false;
        b8 hasUsableSample = false;
    };


public:
    explicit RendererTaskTimingFeedbackState(Core::Alloc::GlobalArena& arena)noexcept
        : m_pendingSamples(arena)
    {}


public:
    [[nodiscard]] b8 trackSample(Core::GpuTimingSampleAttribution attribution, const Name& scopeName, const Core::GpuTaskTimingKey& key, const Core::GpuPhysicalQueueId& expectedQueue, u64 sourceFrameIndex, b8 recordsNonCommittingTimingSample);
    void acceptSubmission(Core::GpuTimingSampleAttribution attribution, const Core::QueueSubmissionToken& token, b8 feedbackActive)noexcept;
    void discardRecording(Core::GpuTimingSampleAttribution attribution)noexcept;
    void completeSample(const Core::GpuTimingSample& sample, b8 feedbackActive)noexcept;
    [[nodiscard]] RendererTaskTimingFeedbackDrainResult drain(Core::GpuTaskTimingHistoryStore& history, u16 deviceGeneration);
    void reset()noexcept{ m_pendingSamples.clear(); }


private:
    [[nodiscard]] usize findPendingSample(Core::GpuTimingSampleAttribution attribution)const noexcept;
    void retirePendingSample(usize pendingIndex)noexcept;


private:
    Vector<PendingSample, Core::Alloc::GlobalArena> m_pendingSamples;
};


// Renderer owns timing lifetime; compiler consumes only an immutable history snapshot.
class RendererTaskTimingFeedback final : NoCopy{
private:
    static void OnGpuTimingSampleCallback(void* context, const Core::GpuTimingSample& sample)noexcept;


public:
    RendererTaskTimingFeedback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, NotNull<const Name*> feedbackCollectionScopes, usize feedbackCollectionScopeCount);
    ~RendererTaskTimingFeedback()noexcept;


public:
    void activate();
    void deactivate()noexcept;
    [[nodiscard]] Expected<Core::GpuTimingSampleAttribution> beginSample(const Name& scopeName, const Core::GpuTaskTimingKey& key, const Core::GpuPhysicalQueueId& expectedQueue, b8 recordsNonCommittingTimingSample);
    void acceptSubmission(Core::GpuTimingSampleAttribution attribution, const Core::QueueSubmissionToken& token)noexcept;
    void discardRecording(Core::GpuTimingSampleAttribution attribution)noexcept;
    void configureCompileOptions(Core::GpuTaskGraphCompileOptions& options, u64 frameIndex);
    void reset()noexcept;


private:
    [[nodiscard]] b8 collectsScope(const Name& scopeName)const noexcept;
    void onGpuTimingSample(const Core::GpuTimingSample& sample)noexcept;


private:
    Core::GraphicsRuntime& m_graphics;
    RendererTaskTimingFeedbackState m_state;
    Core::GpuTaskTimingHistoryStore m_history;
    Core::GpuTaskTimingHistorySnapshot m_snapshot;
    Vector<Name, Core::Alloc::GlobalArena> m_feedbackCollectionScopes;
    Futex m_lifecycleMutex;
    Futex m_mutex;
    Core::GpuTaskTimingFeedbackPolicy m_policy;
    Core::GpuTimingSampleSubscription m_subscription;
    b8 m_active = false;
};

namespace ECSRenderDetail{

// Callers own the recording scope and clear attribution when dispatch records nothing.
[[nodiscard]] Expected<Core::GpuTimingSampleAttribution> BeginTaskTimingSample(RendererTaskTimingFeedback* timingFeedback, const Core::GpuTimingScopeDefinition* timingScope, const Core::GpuTaskRecordContext& context);

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
