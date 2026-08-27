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


// Keeps renderer-selected task timing separate from the graph compiler.  The renderer owns query-attribution
// lifetime, while the compiler consumes only an immutable history snapshot during the following frame's compile.
class RendererTaskTimingFeedback final : NoCopy{
private:
    struct PendingSample{
        Core::GpuTimingSampleAttribution attribution = Core::s_NoGpuTimingSampleAttribution;
        Name scopeName = NAME_NONE;
        Core::GpuTaskTimingKey key;
        Core::GpuPhysicalQueueId expectedQueue;
        u64 sourceFrameIndex = 0u;
        f64 durationSeconds = 0.0;
        bool accepted = false;
        bool hasSample = false;
    };


private:
    static void OnGpuTimingSample(void* context, const Core::GpuTimingSample& sample);


public:
    RendererTaskTimingFeedback(Core::Alloc::GlobalArena& arena, Core::Graphics& graphics);
    ~RendererTaskTimingFeedback();


public:
    void activate();
    void deactivate();
    [[nodiscard]] bool setPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy);
    [[nodiscard]] Core::GpuTimingSampleAttribution beginSample(
        const Name& scopeName,
        const Core::GpuTaskTimingKey& key,
        const Core::GpuPhysicalQueueId& expectedQueue
    );
    void acceptSubmission(Core::GpuTimingSampleAttribution attribution, const Core::QueueSubmissionToken& token);
    void discardRecording(Core::GpuTimingSampleAttribution attribution);
    void configureCompileOptions(Core::GpuTaskGraphCompileOptions& options, u64 frameIndex);
    void reset();


private:
    void onGpuTimingSample(const Core::GpuTimingSample& sample);
    [[nodiscard]] usize findPendingSample(Core::GpuTimingSampleAttribution attribution)const noexcept;
    void tryRecordSample(usize pendingIndex);


private:
    Core::Graphics& m_graphics;
    Core::GpuTaskTimingHistoryStore m_history;
    Core::GpuTaskTimingHistorySnapshot m_snapshot;
    Vector<PendingSample, Core::Alloc::GlobalArena> m_pendingSamples;
    Futex m_lifecycleMutex;
    Futex m_mutex;
    Core::GpuTaskTimingFeedbackPolicy m_policy;
    Core::GpuTimingSampleSubscription m_subscription;
    bool m_active = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

