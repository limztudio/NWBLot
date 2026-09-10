// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 CpuTaskScheduler::workerWakeMaskLocked()const noexcept{
    const bool any = m_readyWorkerCosts[CpuTaskCost::Any] != 0u;
    const bool heavy = m_readyWorkerCosts[CpuTaskCost::Heavy] != 0u;
    const bool light = m_readyWorkerCosts[CpuTaskCost::Light] != 0u;
    u32 wakeMask = 0u;
    for(u32 affinity = 0u; affinity < 3u; ++affinity){
        if(m_sleepingWorkers[affinity] == 0u)
            continue;
        const bool heavyEligible = affinity != CpuAffinity::Efficiency || m_busyPerformance >= m_statistics.performanceWorkers;
        const bool lightEligible = affinity != CpuAffinity::Performance || m_busyEfficiency >= m_statistics.efficiencyWorkers;
        if(any || (heavy && heavyEligible) || (light && lightEligible))
            wakeMask |= 1u << affinity;
    }
    return wakeMask;
}

void CpuTaskScheduler::notifyWorkers(const u32 wakeMask)noexcept{
    // Masks compute under the queue mutex; claimed tasks wake the next parked worker first.
    for(u32 affinity = 0u; affinity < 3u; ++affinity){
        if((wakeMask & (1u << affinity)) != 0u)
            m_workerChanged[affinity].notify_one();
    }
}

void CpuTaskScheduler::notifyProgress(const u32 wakeMask)noexcept{
    m_changed.notify_all();
    notifyWorkers(wakeMask);
}

void CpuTaskScheduler::notifyProgress()noexcept{
    u32 wakeMask;
    {
        ScopedLock lock(m_mutex);
        wakeMask = workerWakeMaskLocked();
    }
    notifyProgress(wakeMask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

