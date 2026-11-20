// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScheduler::WorkerWakePlan CpuTaskScheduler::workerWakePlanLocked()const noexcept{
    const u32 any = m_readyWorkerCosts[CpuTaskCost::Any];
    const u32 heavy = m_readyWorkerCosts[CpuTaskCost::Heavy];
    const u32 light = m_readyWorkerCosts[CpuTaskCost::Light];
    WorkerWakePlan wakePlan;
    for(u32 affinity = 0u; affinity < s_AffinitySlotCount; ++affinity){
        const u32 sleeping = m_sleepingWorkers[affinity];
        if(sleeping == 0u)
            continue;
        const b8 heavyEligible = affinity != CpuAffinity::Efficiency || m_busyPerformance >= m_statistics.performanceWorkers;
        const b8 lightEligible = affinity != CpuAffinity::Performance || m_busyEfficiency >= m_statistics.efficiencyWorkers;
        const u32 eligible = any + (heavyEligible ? heavy : 0u) + (lightEligible ? light : 0u);
        if(eligible == 0u)
            continue;
        wakePlan.counts[affinity] = Min(sleeping, eligible);
        wakePlan.wakeAll[affinity] = eligible >= sleeping && sleeping > 1u;
    }
    return wakePlan;
}

void CpuTaskScheduler::notifyWorkers(const WorkerWakePlan& wakePlan)noexcept{
    for(u32 affinity = 0u; affinity < s_AffinitySlotCount; ++affinity){
        if(wakePlan.wakeAll[affinity]){
            m_workerChanged[affinity].notify_all();
            continue;
        }
        for(u32 count = 0u; count < wakePlan.counts[affinity]; ++count)
            m_workerChanged[affinity].notify_one();
    }
}

void CpuTaskScheduler::notifyProgress(const WorkerWakePlan& wakePlan, const b8 wakeJoiners)noexcept{
    if(wakeJoiners)
        m_changed.notify_all();
    notifyWorkers(wakePlan);
}

void CpuTaskScheduler::notifyProgress()noexcept{
    WorkerWakePlan wakePlan;
    b8 wakeJoiners;
    {
        ScopedLock lock(m_mutex);
        wakePlan = workerWakePlanLocked();
        wakeJoiners = m_joinWaiters != 0u;
    }
    notifyProgress(wakePlan, wakeJoiners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

