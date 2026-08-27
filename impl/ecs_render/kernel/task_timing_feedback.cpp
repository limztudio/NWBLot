// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/task_timing_feedback.h>

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererTaskTimingFeedback::RendererTaskTimingFeedback(Core::Alloc::GlobalArena& arena, Core::Graphics& graphics)
    : m_graphics(graphics)
    , m_history(arena)
    , m_snapshot(arena)
    , m_pendingSamples(arena)
{}
RendererTaskTimingFeedback::~RendererTaskTimingFeedback(){
    deactivate();
}


void RendererTaskTimingFeedback::activate(){
    ScopedLock lifecycleLock(m_lifecycleMutex);

    bool feedbackCollectionEnabled = false;
    {
        ScopedLock lock(m_mutex);
        if(m_active)
            return;
        feedbackCollectionEnabled = m_policy.enabled;
    }

    Core::GpuTimingRecorder& timing = m_graphics.gpuTiming();
    const Core::GpuTimingSampleSubscription subscription = timing.subscribeSampleListener(Core::GpuTimingSampleListener{
        .context = this,
        .invoke = &OnGpuTimingSample,
    });
    if(!subscription.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("Renderer task timing feedback failed to subscribe to GPU timing samples."));
        return;
    }
    if(feedbackCollectionEnabled && !timing.setFeedbackCollectionEnabled(subscription, true)){
        timing.unsubscribeSampleListener(subscription);
        NWB_LOGGER_WARNING(NWB_TEXT("Renderer task timing feedback failed to enable GPU sample collection."));
        return;
    }

    ScopedLock lock(m_mutex);
    m_subscription = subscription;
    m_active = true;
}

void RendererTaskTimingFeedback::deactivate(){
    ScopedLock lifecycleLock(m_lifecycleMutex);

    Core::GpuTimingSampleSubscription subscription;
    {
        ScopedLock lock(m_mutex);
        if(!m_active)
            return;
        m_active = false;
        subscription = m_subscription;
        m_subscription = {};
    }

    // Unsubscription removes only this bridge's collection demand and waits for any callback retaining this context.
    m_graphics.gpuTiming().unsubscribeSampleListener(subscription);
    reset();
}

bool RendererTaskTimingFeedback::setPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy){
    if(!policy.valid())
        return false;

    ScopedLock lifecycleLock(m_lifecycleMutex);
    Core::GpuTaskTimingFeedbackPolicy previousPolicy;
    Core::GpuTimingSampleSubscription subscription;
    {
        ScopedLock lock(m_mutex);
        previousPolicy = m_policy;
        m_policy = policy;
        if(m_active)
            subscription = m_subscription;
    }
    if(subscription.valid() && !m_graphics.gpuTiming().setFeedbackCollectionEnabled(subscription, policy.enabled)){
        ScopedLock lock(m_mutex);
        m_policy = previousPolicy;
        return false;
    }
    return true;
}

Core::GpuTimingSampleAttribution RendererTaskTimingFeedback::beginSample(
    const Name& scopeName,
    const Core::GpuTaskTimingKey& key,
    const Core::GpuPhysicalQueueId& expectedQueue
){
    if(!scopeName || !key.valid() || !expectedQueue.valid())
        return Core::s_NoGpuTimingSampleAttribution;

    ScopedLock lock(m_mutex);
    if(!m_active || !m_policy.enabled || !m_subscription.valid())
        return Core::s_NoGpuTimingSampleAttribution;

    const Core::GpuTimingSampleAttribution attribution = m_graphics.gpuTiming().allocateSampleAttribution();
    if(!attribution.valid())
        return Core::s_NoGpuTimingSampleAttribution;

    m_pendingSamples.push_back(PendingSample{
        .attribution = attribution,
        .scopeName = scopeName,
        .key = key,
        .expectedQueue = expectedQueue,
        .sourceFrameIndex = m_graphics.getFrameIndex(),
    });
    return attribution;
}

void RendererTaskTimingFeedback::acceptSubmission(
    const Core::GpuTimingSampleAttribution attribution,
    const Core::QueueSubmissionToken& token
){
    if(attribution == Core::s_NoGpuTimingSampleAttribution)
        return;

    ScopedLock lock(m_mutex);
    const usize pendingIndex = findPendingSample(attribution);
    if(pendingIndex == m_pendingSamples.size())
        return;

    PendingSample& pending = m_pendingSamples[pendingIndex];
    const Core::GpuPhysicalQueueId acceptedQueue{
        token.physicalQueueIndex,
        token.deviceGeneration,
    };
    if(
        !m_active
        || !token.valid()
        || !token.hasPhysicalQueueIdentity()
        || token.queue != pending.key.queue
        || acceptedQueue != pending.expectedQueue
    ){
        m_pendingSamples.erase(m_pendingSamples.begin() + static_cast<isize>(pendingIndex));
        return;
    }

    m_history.resetForDeviceGeneration(acceptedQueue.deviceGeneration);
    if(!m_history.noteAcceptedAssignment(pending.key, acceptedQueue, pending.sourceFrameIndex)){
        m_pendingSamples.erase(m_pendingSamples.begin() + static_cast<isize>(pendingIndex));
        return;
    }

    pending.accepted = true;
    tryRecordSample(pendingIndex);
}

void RendererTaskTimingFeedback::discardRecording(const Core::GpuTimingSampleAttribution attribution){
    if(attribution == Core::s_NoGpuTimingSampleAttribution)
        return;

    ScopedLock lock(m_mutex);
    const usize pendingIndex = findPendingSample(attribution);
    if(pendingIndex != m_pendingSamples.size())
        m_pendingSamples.erase(m_pendingSamples.begin() + static_cast<isize>(pendingIndex));
}

void RendererTaskTimingFeedback::configureCompileOptions(Core::GpuTaskGraphCompileOptions& options, const u64 frameIndex){
    ScopedLock lock(m_mutex);
    const u16 deviceGeneration = m_graphics.getDevice().getDeviceGeneration();
    m_history.resetForDeviceGeneration(deviceGeneration);
    if(!m_policy.enabled)
        return;

    m_history.snapshot(m_snapshot);
    if(!m_snapshot.valid())
        return;

    options.queueAssignmentOptions.timingHistory = &m_snapshot;
    options.queueAssignmentOptions.timingFeedbackPolicy = &m_policy;
    options.queueAssignmentOptions.timingFrameIndex = frameIndex;
}

void RendererTaskTimingFeedback::reset(){
    ScopedLock lock(m_mutex);
    m_pendingSamples.clear();
    m_history.reset();
    m_history.snapshot(m_snapshot);
}


void RendererTaskTimingFeedback::OnGpuTimingSample(void* const context, const Core::GpuTimingSample& sample){
    RendererTaskTimingFeedback* const feedback = static_cast<RendererTaskTimingFeedback*>(context);
    if(feedback)
        feedback->onGpuTimingSample(sample);
}


void RendererTaskTimingFeedback::onGpuTimingSample(const Core::GpuTimingSample& sample){
    if(sample.attribution == Core::s_NoGpuTimingSampleAttribution)
        return;

    ScopedLock lock(m_mutex);
    const usize pendingIndex = findPendingSample(sample.attribution);
    if(pendingIndex == m_pendingSamples.size())
        return;

    PendingSample& pending = m_pendingSamples[pendingIndex];
    if(!sample.published){
        m_pendingSamples.erase(m_pendingSamples.begin() + static_cast<isize>(pendingIndex));
        return;
    }
    if(
        !m_active
        || sample.scopeName != pending.scopeName
        || sample.sourceFrameIndex != pending.sourceFrameIndex
        || sample.physicalQueue != pending.expectedQueue
    ){
        m_pendingSamples.erase(m_pendingSamples.begin() + static_cast<isize>(pendingIndex));
        return;
    }

    pending.durationSeconds = sample.durationSeconds;
    pending.hasSample = true;
    tryRecordSample(pendingIndex);
}

usize RendererTaskTimingFeedback::findPendingSample(const Core::GpuTimingSampleAttribution attribution)const noexcept{
    for(usize pendingIndex = 0u; pendingIndex < m_pendingSamples.size(); ++pendingIndex){
        if(m_pendingSamples[pendingIndex].attribution == attribution)
            return pendingIndex;
    }
    return m_pendingSamples.size();
}

void RendererTaskTimingFeedback::tryRecordSample(const usize pendingIndex){
    NWB_ASSERT(pendingIndex < m_pendingSamples.size());
    if(pendingIndex >= m_pendingSamples.size())
        return;

    const PendingSample& pending = m_pendingSamples[pendingIndex];
    if(!pending.accepted || !pending.hasSample)
        return;

    if(!m_history.recordSample(
        pending.key,
        pending.expectedQueue,
        pending.durationSeconds,
        pending.sourceFrameIndex
    ))
        NWB_LOGGER_WARNING(NWB_TEXT("Renderer task timing feedback rejected an accepted GPU timing sample."));
    m_pendingSamples.erase(m_pendingSamples.begin() + static_cast<isize>(pendingIndex));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

