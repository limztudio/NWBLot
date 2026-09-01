// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/task_timing_feedback.h>

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererTaskTimingFeedbackPolicyTransition PrepareRendererTaskTimingFeedbackPolicyTransition(
    Core::GpuTaskTimingFeedbackPolicy& currentPolicy,
    const Core::GpuTaskTimingFeedbackPolicy& requestedPolicy,
    const bool rendererActive
)noexcept{
    RendererTaskTimingFeedbackPolicyTransition transition{
        .previousPolicy = currentPolicy,
        .requestedPolicy = requestedPolicy,
    };
    if(!rendererActive || currentPolicy.enabled == requestedPolicy.enabled){
        currentPolicy = requestedPolicy;
        return transition;
    }

    transition.action = requestedPolicy.enabled
        ? RendererTaskTimingFeedbackCollectionAction::Enable
        : RendererTaskTimingFeedbackCollectionAction::Disable
    ;
    if(transition.action == RendererTaskTimingFeedbackCollectionAction::Disable)
        currentPolicy = requestedPolicy;
    return transition;
}

void ResolveRendererTaskTimingFeedbackPolicyTransition(
    Core::GpuTaskTimingFeedbackPolicy& currentPolicy,
    const RendererTaskTimingFeedbackPolicyTransition& transition,
    const bool collectionUpdated
)noexcept{
    if(transition.action == RendererTaskTimingFeedbackCollectionAction::None)
        return;

    NWB_ASSERT(transition.action < RendererTaskTimingFeedbackCollectionAction::kCount);
    if(transition.action >= RendererTaskTimingFeedbackCollectionAction::kCount)
        return;
    currentPolicy = collectionUpdated ? transition.requestedPolicy : transition.previousPolicy;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererTaskTimingFeedbackState::trackSample(
    const Core::GpuTimingSampleAttribution attribution,
    const Name& scopeName,
    const Core::GpuTaskTimingKey& key,
    const Core::GpuPhysicalQueueId& expectedQueue,
    const u64 sourceFrameIndex,
    const bool recordsNonCommittingTimingSample
){
    if(!attribution.valid() || !scopeName || !key.valid() || !expectedQueue.valid())
        return false;
    if(findPendingSample(attribution) != m_pendingSamples.size())
        return false;

    m_pendingSamples.push_back(PendingSample{
        .attribution = attribution,
        .scopeName = scopeName,
        .key = key,
        .expectedQueue = expectedQueue,
        .sourceFrameIndex = sourceFrameIndex,
        .recordsNonCommittingTimingSample = recordsNonCommittingTimingSample,
    });
    return true;
}

void RendererTaskTimingFeedbackState::acceptSubmission(
    const Core::GpuTimingSampleAttribution attribution,
    const Core::QueueSubmissionToken& token,
    const bool feedbackActive
)noexcept{
    const usize pendingIndex = findPendingSample(attribution);
    if(pendingIndex == m_pendingSamples.size())
        return;

    PendingSample& pending = m_pendingSamples[pendingIndex];
    if(pending.submissionResolved)
        return;

    const Core::GpuPhysicalQueueId acceptedQueue{
        token.physicalQueueIndex,
        token.deviceGeneration,
    };
    pending.submissionResolved = true;
    pending.accepted = feedbackActive
        && token.valid()
        && token.hasPhysicalQueueIdentity()
        && token.queue == pending.key.queue
        && acceptedQueue == pending.expectedQueue
    ;
}

void RendererTaskTimingFeedbackState::discardRecording(const Core::GpuTimingSampleAttribution attribution)noexcept{
    const usize pendingIndex = findPendingSample(attribution);
    if(pendingIndex == m_pendingSamples.size())
        return;

    PendingSample& pending = m_pendingSamples[pendingIndex];
    if(pending.submissionResolved)
        return;

    pending.submissionResolved = true;
    pending.accepted = false;
}

void RendererTaskTimingFeedbackState::completeSample(
    const Core::GpuTimingSample& sample,
    const bool feedbackActive
)noexcept{
    const usize pendingIndex = findPendingSample(sample.attribution);
    if(pendingIndex == m_pendingSamples.size())
        return;

    PendingSample& pending = m_pendingSamples[pendingIndex];
    if(pending.sampleResolved)
        return;

    // Every matching notification is terminal, including unpublished and malformed results. Acceptance still owns
    // route assignment independently, while only a complete positive duration is eligible for history.
    pending.sampleResolved = true;
    if(
        !feedbackActive
        || !sample.published
        || sample.scopeName != pending.scopeName
        || sample.sourceFrameIndex != pending.sourceFrameIndex
        || sample.physicalQueue != pending.expectedQueue
        || !IsFinite(sample.durationSeconds)
        || sample.durationSeconds <= 0.0
        || sample.durationSeconds >= Limit<f64>::s_Max
    )
        return;

    pending.durationSeconds = sample.durationSeconds;
    pending.hasUsableSample = true;
}

RendererTaskTimingFeedbackDrainResult RendererTaskTimingFeedbackState::drain(
    Core::GpuTaskTimingHistoryStore& history,
    const u16 deviceGeneration
){
    RendererTaskTimingFeedbackDrainResult result;
    usize pendingIndex = 0u;
    while(pendingIndex < m_pendingSamples.size()){
        PendingSample& pending = m_pendingSamples[pendingIndex];
        if(pending.expectedQueue.deviceGeneration != deviceGeneration){
            retirePendingSample(pendingIndex);
            ++result.retiredSampleCount;
            continue;
        }
        if(!pending.submissionResolved){
            ++pendingIndex;
            continue;
        }
        if(!pending.accepted){
            retirePendingSample(pendingIndex);
            ++result.retiredSampleCount;
            continue;
        }

        if(!pending.recordsNonCommittingTimingSample && !pending.assignmentRecorded){
            if(!history.noteAcceptedAssignment(pending.key, pending.expectedQueue, pending.sourceFrameIndex)){
                retirePendingSample(pendingIndex);
                ++result.retiredSampleCount;
                ++result.rejectedAssignmentCount;
                continue;
            }
            pending.assignmentRecorded = true;
            ++result.acceptedAssignmentCount;
        }

        if(!pending.sampleResolved){
            ++pendingIndex;
            continue;
        }
        if(pending.hasUsableSample){
            if(history.recordNonCommittingSample(pending.key, pending.expectedQueue, pending.durationSeconds))
                ++result.recordedSampleCount;
            else
                ++result.rejectedSampleCount;
        }
        retirePendingSample(pendingIndex);
        ++result.retiredSampleCount;
    }
    return result;
}

usize RendererTaskTimingFeedbackState::findPendingSample(
    const Core::GpuTimingSampleAttribution attribution
)const noexcept{
    for(usize pendingIndex = 0u; pendingIndex < m_pendingSamples.size(); ++pendingIndex){
        if(m_pendingSamples[pendingIndex].attribution == attribution)
            return pendingIndex;
    }
    return m_pendingSamples.size();
}

void RendererTaskTimingFeedbackState::retirePendingSample(const usize pendingIndex){
    NWB_ASSERT(pendingIndex < m_pendingSamples.size());
    m_pendingSamples.erase(m_pendingSamples.begin() + static_cast<isize>(pendingIndex));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererTaskTimingFeedback::onGpuTimingSampleCallback(
    void* const context,
    const Core::GpuTimingSample& sample
)noexcept{
    RendererTaskTimingFeedback* const feedback = static_cast<RendererTaskTimingFeedback*>(context);
    if(feedback)
        feedback->onGpuTimingSample(sample);
}


RendererTaskTimingFeedback::RendererTaskTimingFeedback(
    Core::Alloc::GlobalArena& arena,
    Core::Graphics& graphics,
    const NotNull<const Name*> feedbackCollectionScopes,
    const usize feedbackCollectionScopeCount
)
    : m_graphics(graphics)
    , m_state(arena)
    , m_history(arena)
    , m_snapshot(arena)
    , m_feedbackCollectionScopes(arena)
{
    NWB_ASSERT(feedbackCollectionScopeCount != 0u);
    const Name* const scopeNames = feedbackCollectionScopes.get();
    m_feedbackCollectionScopes.assign(scopeNames, scopeNames + feedbackCollectionScopeCount);
}
RendererTaskTimingFeedback::~RendererTaskTimingFeedback()noexcept{
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
        .invoke = &onGpuTimingSampleCallback,
    });
    if(!subscription.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("Renderer task timing feedback failed to subscribe to GPU timing samples."));
        return;
    }
    if(feedbackCollectionEnabled){
        try{
            if(!timing.setFeedbackCollectionScopes(
                subscription,
                NotNull<const Name*>(m_feedbackCollectionScopes.data()),
                m_feedbackCollectionScopes.size()
            )){
                timing.unsubscribeSampleListener(subscription);
                NWB_LOGGER_WARNING(NWB_TEXT("Renderer task timing feedback failed to enable GPU sample collection."));
                return;
            }
        }
        catch(...){
            timing.unsubscribeSampleListener(subscription);
            throw;
        }
    }

    ScopedLock lock(m_mutex);
    m_subscription = subscription;
    m_active = true;
}

void RendererTaskTimingFeedback::deactivate()noexcept{
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
    RendererTaskTimingFeedbackPolicyTransition transition;
    Core::GpuTimingSampleSubscription subscription;
    {
        ScopedLock lock(m_mutex);
        transition = PrepareRendererTaskTimingFeedbackPolicyTransition(m_policy, policy, m_active);
        if(transition.action == RendererTaskTimingFeedbackCollectionAction::None)
            return true;

        NWB_ASSERT(m_active && m_subscription.valid());
        if(!m_active || !m_subscription.valid()){
            ResolveRendererTaskTimingFeedbackPolicyTransition(m_policy, transition, false);
            return false;
        }
        subscription = m_subscription;
    }

    bool collectionUpdated = false;
    try{
        if(transition.action == RendererTaskTimingFeedbackCollectionAction::Enable){
            collectionUpdated = m_graphics.gpuTiming().setFeedbackCollectionScopes(
                subscription,
                NotNull<const Name*>(m_feedbackCollectionScopes.data()),
                m_feedbackCollectionScopes.size()
            );
        }else{
            collectionUpdated = m_graphics.gpuTiming().clearFeedbackCollectionScopes(subscription);
        }
    }
    catch(...){
        ScopedLock lock(m_mutex);
        ResolveRendererTaskTimingFeedbackPolicyTransition(m_policy, transition, false);
        throw;
    }
    {
        ScopedLock lock(m_mutex);
        ResolveRendererTaskTimingFeedbackPolicyTransition(m_policy, transition, collectionUpdated);
    }
    return collectionUpdated;
}

Core::GpuTimingSampleAttribution RendererTaskTimingFeedback::beginSample(
    const Name& scopeName,
    const Core::GpuTaskTimingKey& key,
    const Core::GpuPhysicalQueueId& expectedQueue,
    const bool recordsNonCommittingTimingSample
){
    if(!scopeName || !collectsScope(scopeName) || !key.valid() || !expectedQueue.valid())
        return Core::s_NoGpuTimingSampleAttribution;

    ScopedLock lock(m_mutex);
    if(!m_active || !m_policy.enabled || !m_subscription.valid())
        return Core::s_NoGpuTimingSampleAttribution;

    const Core::GpuTimingSampleAttribution attribution = m_graphics.gpuTiming().allocateSampleAttribution();
    if(!attribution.valid())
        return Core::s_NoGpuTimingSampleAttribution;

    if(!m_state.trackSample(
        attribution,
        scopeName,
        key,
        expectedQueue,
        m_graphics.getFrameIndex(),
        recordsNonCommittingTimingSample
    ))
        return Core::s_NoGpuTimingSampleAttribution;
    return attribution;
}

void RendererTaskTimingFeedback::acceptSubmission(
    const Core::GpuTimingSampleAttribution attribution,
    const Core::QueueSubmissionToken& token
)noexcept{
    if(attribution == Core::s_NoGpuTimingSampleAttribution)
        return;

    ScopedLock lock(m_mutex);
    m_state.acceptSubmission(attribution, token, m_active);
}

void RendererTaskTimingFeedback::discardRecording(const Core::GpuTimingSampleAttribution attribution)noexcept{
    if(attribution == Core::s_NoGpuTimingSampleAttribution)
        return;

    ScopedLock lock(m_mutex);
    m_state.discardRecording(attribution);
}

void RendererTaskTimingFeedback::configureCompileOptions(Core::GpuTaskGraphCompileOptions& options, const u64 frameIndex){
    options.queueAssignmentOptions.timingHistory = nullptr;
    options.queueAssignmentOptions.timingFeedbackPolicy = {};
    options.queueAssignmentOptions.timingFrameIndex = 0u;

    RendererTaskTimingFeedbackDrainResult drainResult;
    {
        ScopedLock lock(m_mutex);
        const u16 deviceGeneration = m_graphics.getDevice().getDeviceGeneration();
        m_history.resetForDeviceGeneration(deviceGeneration);
        drainResult = m_state.drain(m_history, deviceGeneration);

        if(m_active && m_subscription.valid() && m_policy.enabled){
            m_history.snapshot(m_snapshot);
            if(m_snapshot.valid()){
                options.queueAssignmentOptions.timingHistory = &m_snapshot;
                options.queueAssignmentOptions.timingFeedbackPolicy = m_policy;
                options.queueAssignmentOptions.timingFrameIndex = frameIndex;
            }
        }
    }
    if(drainResult.rejectedAssignmentCount != 0u || drainResult.rejectedSampleCount != 0u){
        NWB_LOGGER_WARNING(NWB_TEXT("Renderer task timing feedback rejected {} assignment(s) and {} terminal sample(s).")
            , drainResult.rejectedAssignmentCount
            , drainResult.rejectedSampleCount
        );
    }
}

void RendererTaskTimingFeedback::reset()noexcept{
    ScopedLock lock(m_mutex);
    m_state.reset();
    m_history.reset(m_snapshot);
}


bool RendererTaskTimingFeedback::collectsScope(const Name& scopeName)const noexcept{
    for(const Name& feedbackCollectionScope : m_feedbackCollectionScopes){
        if(feedbackCollectionScope == scopeName)
            return true;
    }
    return false;
}

void RendererTaskTimingFeedback::onGpuTimingSample(const Core::GpuTimingSample& sample)noexcept{
    if(sample.attribution == Core::s_NoGpuTimingSampleAttribution)
        return;

    ScopedLock lock(m_mutex);
    m_state.completeSample(sample, m_active);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

