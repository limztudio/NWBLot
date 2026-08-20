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
    {
        ScopedLock lock(m_mutex);
        if(m_active)
            return;
    }

    m_graphics.gpuTiming().setSampleListener(Core::GpuTimingSampleListener{
        .context = this,
        .invoke = &OnGpuTimingSample,
    });

    ScopedLock lock(m_mutex);
    m_active = true;
}

void RendererTaskTimingFeedback::deactivate(){
    {
        ScopedLock lock(m_mutex);
        if(!m_active)
            return;
        m_active = false;
    }

    // GpuTimingRecorder serializes listener replacement with an active callback, so no callback can retain this
    // bridge after the clear returns.
    m_graphics.gpuTiming().setFeedbackCollectionEnabled(false);
    m_graphics.gpuTiming().setSampleListener({});
    reset();
}

bool RendererTaskTimingFeedback::setPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy){
    if(!policy.valid())
        return false;

    {
        ScopedLock lock(m_mutex);
        m_policy = policy;
    }
    m_graphics.gpuTiming().setFeedbackCollectionEnabled(policy.enabled);
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
    if(!m_active)
        return Core::s_NoGpuTimingSampleAttribution;

    ++m_nextAttribution;
    if(m_nextAttribution == Core::s_NoGpuTimingSampleAttribution)
        ++m_nextAttribution;

    m_pendingSamples.push_back(PendingSample{
        .attribution = m_nextAttribution,
        .scopeName = scopeName,
        .key = key,
        .expectedQueue = expectedQueue,
        .sourceFrameIndex = m_graphics.getFrameIndex(),
    });
    return m_nextAttribution;
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

