// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu_timing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


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

void GpuTimingRecorder::markPendingAttributionsForRetirementLocked(
    const u64 subscriptionIdentityLimit
)noexcept{
    for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
        it.value()->markAttributionsForRetirement(subscriptionIdentityLimit);
    m_pendingAttributionRetirements = true;
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

