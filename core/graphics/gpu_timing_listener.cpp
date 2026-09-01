// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_listener{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextSampleSubscriptionIdentity{ 1u };
inline constexpr Name s_GpuTimingListenerScratchArena("graphics.gpu_timing.listener_scratch");


[[nodiscard]] static u64 AllocateSampleSubscriptionIdentity()noexcept{
    u64 identity = s_NextSampleSubscriptionIdentity.load(MemoryOrder::relaxed);
    while(identity != Limit<u64>::s_Max){
        if(s_NextSampleSubscriptionIdentity.compare_exchange_weak(
            identity,
            identity + 1u,
            MemoryOrder::relaxed,
            MemoryOrder::relaxed
        ))
            return identity;
    }
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingSampleSubscription GpuTimingRecorder::subscribeSampleListener(const GpuTimingSampleListener& listener){
    if(!listener.valid())
        return {};

    SampleListenerRecord* const listenerRecord = NewArenaObject<SampleListenerRecord>(m_arena, m_arena);
    if(!listenerRecord)
        return {};
    SampleListenerRecordPtr record(
        listenerRecord,
        SampleListenerRecordPtr::deleter_type(&m_arena),
        AdoptRef
    );

    ScopedLock lock(m_sampleListenerMutex);
    const u64 identity = __hidden_gpu_timing_listener::AllocateSampleSubscriptionIdentity();
    if(identity == 0u)
        return {};

    const GpuTimingSampleSubscription subscription(identity);
    record->subscription = subscription;
    record->listener = listener;
    m_sampleListeners.push_back(Move(record));
    publishSampleSubscriptionIdentityLimitLocked();
    return subscription;
}

void GpuTimingRecorder::unsubscribeSampleListener(const GpuTimingSampleSubscription& subscription)noexcept{
    SampleListenerRecordPtr record;
    u64 subscriptionIdentityLimit = 0u;
    bool retirePendingAttributions = false;
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        record = findSampleListenerLocked(subscription);
        if(!record)
            return;

        if(!record->removing){
            record->removing = true;
            publishSampleSubscriptionIdentityLimitLocked();
            subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();
            if(!record->feedbackScopes.empty()){
                ScopedLock recorderLock(m_mutex);

                removeFeedbackScopeDemandsLocked(record->feedbackScopes);
                record->feedbackScopes.clear();
                syncActiveState(subscriptionIdentityLimit);
                retirePendingAttributions = m_pendingAttributionRetirements;
                if(subscriptionIdentityLimit == 0u)
                    discardMarkedPendingAttributionsLocked();
            }
            else if(subscriptionIdentityLimit == 0u){
                ScopedLock recorderLock(m_mutex);

                discardMarkedPendingAttributionsLocked();
            }
        }
    }

    {
        ScopedLock callbackLock(m_sampleCallbackMutex);
        ScopedLock listenerLock(m_sampleListenerMutex);

        eraseSampleListenerLocked(*record);
    }

    if(!retirePendingAttributions || subscriptionIdentityLimit == 0u)
        return;

    while(true){
        SampleDispatch retiredSample;
        {
            ScopedLock recorderLock(m_mutex);
            if(!retireMarkedPendingAttributionLocked(retiredSample))
                return;
        }
        dispatchCompletedSample(retiredSample.sample, retiredSample.subscriptionIdentityLimit);
    }
}

bool GpuTimingRecorder::setFeedbackCollectionScopes(
    const GpuTimingSampleSubscription& subscription,
    const NotNull<const Name*> scopeNames,
    const usize scopeCount
){
    if(scopeCount == 0u)
        return false;
    return replaceFeedbackCollectionScopes(subscription, scopeNames.get(), scopeCount);
}

bool GpuTimingRecorder::clearFeedbackCollectionScopes(const GpuTimingSampleSubscription& subscription){
    return replaceFeedbackCollectionScopes(subscription, nullptr, 0u);
}

bool GpuTimingRecorder::replaceFeedbackCollectionScopes(
    const GpuTimingSampleSubscription& subscription,
    const Name* const scopeNames,
    const usize scopeCount
){
    if(!subscription.valid() || (scopeCount != 0u && !scopeNames))
        return false;

    Vector<Name, Alloc::GlobalArena> replacementScopes(m_arena);
    replacementScopes.reserve(scopeCount);
    for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
        const Name& scopeName = scopeNames[scopeIndex];
        if(!scopeName)
            return false;
        for(const Name& previousScope : replacementScopes){
            if(previousScope == scopeName)
                return false;
        }
        replacementScopes.push_back(scopeName);
    }

    Alloc::ScratchArena scratchArena(__hidden_gpu_timing_listener::s_GpuTimingListenerScratchArena);
    SampleDispatchVector retiredSamples{ scratchArena };
    {
        ScopedLock listenerLock(m_sampleListenerMutex);
        SampleListenerRecordPtr record = findSampleListenerLocked(subscription);
        if(!record || record->removing)
            return false;
        const u64 subscriptionIdentityLimit = sampleSubscriptionIdentityLimitLocked();

        ScopedLock recorderLock(m_mutex);
        for(const Name& scopeName : replacementScopes){
            if(m_metricCorrelator.hasOutputRole(scopeName))
                return false;
            const usize demandIndex = findFeedbackScopeDemandLocked(scopeName);
            if(demandIndex == m_feedbackScopeDemands.size())
                continue;

            bool alreadyOwned = false;
            for(const Name& ownedScope : record->feedbackScopes)
                alreadyOwned = alreadyOwned || ownedScope == scopeName;
            if(!alreadyOwned && m_feedbackScopeDemands[demandIndex].ownerCount == Limit<u64>::s_Max)
                return false;
        }
        m_feedbackScopeDemands.reserve(AddSize(m_feedbackScopeDemands.size(), replacementScopes.size()));
        reservePendingAttributionSamplesLocked(retiredSamples);

        removeFeedbackScopeDemandsLocked(record->feedbackScopes);
        addFeedbackScopeDemandsLocked(replacementScopes);
        record->feedbackScopes.swap(replacementScopes);
        syncActiveState(subscriptionIdentityLimit);
        if(m_pendingAttributionRetirements){
            if(subscriptionIdentityLimit != 0u)
                retireMarkedPendingAttributionsLocked(retiredSamples);
            else
                discardMarkedPendingAttributionsLocked();
        }
    }
    dispatchCompletedSamples(retiredSamples);
    return true;
}

usize GpuTimingRecorder::findFeedbackScopeDemandLocked(const Name& scopeName)const noexcept{
    for(usize demandIndex = 0u; demandIndex < m_feedbackScopeDemands.size(); ++demandIndex){
        if(m_feedbackScopeDemands[demandIndex].scopeName == scopeName)
            return demandIndex;
    }
    return m_feedbackScopeDemands.size();
}

bool GpuTimingRecorder::feedbackScopeDemandedLocked(const Name& scopeName)const noexcept{
    const usize demandIndex = findFeedbackScopeDemandLocked(scopeName);
    return demandIndex != m_feedbackScopeDemands.size() && m_feedbackScopeDemands[demandIndex].ownerCount != 0u;
}

void GpuTimingRecorder::addFeedbackScopeDemandsLocked(
    const Vector<Name, Alloc::GlobalArena>& scopeNames
)noexcept{
    for(const Name& scopeName : scopeNames){
        const usize demandIndex = findFeedbackScopeDemandLocked(scopeName);
        if(demandIndex != m_feedbackScopeDemands.size()){
            NWB_ASSERT(m_feedbackScopeDemands[demandIndex].ownerCount != Limit<u64>::s_Max);
            if(m_feedbackScopeDemands[demandIndex].ownerCount != Limit<u64>::s_Max)
                ++m_feedbackScopeDemands[demandIndex].ownerCount;
            continue;
        }
        m_feedbackScopeDemands.push_back(FeedbackScopeDemand{
            .scopeName = scopeName,
            .ownerCount = 1u,
        });
    }
}

void GpuTimingRecorder::removeFeedbackScopeDemandsLocked(
    const Vector<Name, Alloc::GlobalArena>& scopeNames
)noexcept{
    for(const Name& scopeName : scopeNames){
        const usize demandIndex = findFeedbackScopeDemandLocked(scopeName);
        NWB_ASSERT(demandIndex != m_feedbackScopeDemands.size());
        if(demandIndex == m_feedbackScopeDemands.size())
            continue;

        FeedbackScopeDemand& demand = m_feedbackScopeDemands[demandIndex];
        NWB_ASSERT(demand.ownerCount > 0u);
        if(demand.ownerCount > 1u){
            --demand.ownerCount;
            continue;
        }
        m_feedbackScopeDemands.erase(m_feedbackScopeDemands.begin() + static_cast<isize>(demandIndex));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingRecorder::SampleListenerRecordPtr GpuTimingRecorder::findSampleListenerLocked(
    const GpuTimingSampleSubscription& subscription
)noexcept{
    if(!subscription.valid())
        return {};

    for(const SampleListenerRecordPtr& record : m_sampleListeners){
        if(record && record->subscription == subscription && record->listener.valid())
            return record;
    }
    return {};
}

u64 GpuTimingRecorder::sampleSubscriptionIdentityLimitLocked()const noexcept{
    u64 result = 0u;
    for(const SampleListenerRecordPtr& record : m_sampleListeners){
        if(record && !record->removing && record->subscription.valid() && record->listener.valid())
            result = Max(result, record->subscription.m_identity);
    }
    return result;
}

void GpuTimingRecorder::publishSampleSubscriptionIdentityLimitLocked()noexcept{
    m_sampleSubscriptionIdentityLimit.store(sampleSubscriptionIdentityLimitLocked(), MemoryOrder::release);
}

void GpuTimingRecorder::eraseSampleListenerLocked(SampleListenerRecord& record)noexcept{
    if(!record.removing || record.activeCallbackCount != 0u)
        return;

    for(auto it = m_sampleListeners.begin(); it != m_sampleListeners.end(); ++it){
        if(it->get() == &record){
            m_sampleListeners.erase(it);
            return;
        }
    }
}

void GpuTimingRecorder::dispatchCompletedSample(
    const GpuTimingSample& sample,
    const u64 subscriptionIdentityLimit
)noexcept{
    u64 dispatchedIdentity = 0u;
    while(dispatchedIdentity < subscriptionIdentityLimit){
        SampleListenerRecordPtr nextRecord;
        {
            ScopedLock listenerLock(m_sampleListenerMutex);
            u64 nextIdentity = Limit<u64>::s_Max;
            for(const SampleListenerRecordPtr& record : m_sampleListeners){
                if(!record || record->removing)
                    continue;
                const u64 identity = record->subscription.m_identity;
                if(
                    identity > dispatchedIdentity
                    && identity <= subscriptionIdentityLimit
                    && identity < nextIdentity
                    && record->listener.valid()
                ){
                    nextRecord = record;
                    nextIdentity = identity;
                }
            }
            if(!nextRecord)
                return;
            dispatchedIdentity = nextIdentity;
        }

        ScopedLock callbackLock(m_sampleCallbackMutex);
        GpuTimingSampleListener listener;
        {
            ScopedLock listenerLock(m_sampleListenerMutex);
            if(nextRecord->removing || !nextRecord->listener.valid())
                continue;
            NWB_ASSERT(nextRecord->activeCallbackCount != Limit<u32>::s_Max);
            if(nextRecord->activeCallbackCount == Limit<u32>::s_Max)
                continue;
            ++nextRecord->activeCallbackCount;
            listener = nextRecord->listener;
        }

        bool callbackFailed = false;
        try{
            listener.invoke(listener.context, sample);
        }
        catch(...){
            callbackFailed = true;
        }

        {
            ScopedLock listenerLock(m_sampleListenerMutex);
            NWB_ASSERT(nextRecord->activeCallbackCount > 0u);
            if(nextRecord->activeCallbackCount > 0u)
                --nextRecord->activeCallbackCount;
            eraseSampleListenerLocked(*nextRecord);
        }

        if(callbackFailed){
            ScopedLock recorderLock(m_mutex);
            if(m_statistics.sampleListenerFailureCount != Limit<u64>::s_Max)
                ++m_statistics.sampleListenerFailureCount;
        }
    }
}

void GpuTimingRecorder::dispatchCompletedSamples(const SampleDispatchVector& samples)noexcept{
    for(const SampleDispatch& sample : samples)
        dispatchCompletedSample(sample.sample, sample.subscriptionIdentityLimit);
}

void GpuTimingRecorder::reservePendingAttributionSamplesLocked(SampleDispatchVector& outSamples)const{
    usize requiredCapacity = outSamples.size();
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        requiredCapacity = AddSize(requiredCapacity, it.value()->pendingAttributionCount());
    outSamples.reserve(requiredCapacity);
}

bool GpuTimingRecorder::retireMarkedPendingAttributionLocked(SampleDispatch& outDispatch)noexcept{
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it){
        if(it.value()->retireMarkedAttribution(outDispatch))
            return true;
    }
    m_pendingAttributionRetirements = false;
    return false;
}

void GpuTimingRecorder::retireMarkedPendingAttributionsLocked(SampleDispatchVector& outSamples){
    while(true){
        SampleDispatch dispatch;
        if(!retireMarkedPendingAttributionLocked(dispatch))
            return;
        outSamples.push_back(dispatch);
    }
}

void GpuTimingRecorder::discardMarkedPendingAttributionsLocked()noexcept{
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->discardMarkedAttributions();
    m_pendingAttributionRetirements = false;
}

void GpuTimingRecorder::retirePendingAttributionsLocked(
    SampleDispatchVector& outSamples,
    const u64 subscriptionIdentityLimit
){
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->retireAttributions(outSamples, subscriptionIdentityLimit);
    m_pendingAttributionRetirements = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

