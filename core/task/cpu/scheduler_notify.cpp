// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CpuTaskScheduler::pushIdleWorkerLocked(const usize slotIndex, const CpuAffinity::Enum affinity)noexcept{
    const u32 slot = static_cast<u32>(slotIndex);
    FOUNDATION_ASSERT(slot < m_workerCount && m_idleNext[slot] == s_WorkerNotIdle);
    m_idleNext[slot] = m_idleHead[affinity];
    m_idleHead[affinity] = slot;
    ++m_idleCount[affinity];
    ++m_totalIdleWorkers;
}

b8 CpuTaskScheduler::popIdleWorkerLocked(const CpuAffinity::Enum affinity, WorkerWakePlan& outPlan, const b8 countWaking)noexcept{
    const u32 head = m_idleHead[affinity];
    if(head == s_IdleTail)
        return false;

    m_idleHead[affinity] = m_idleNext[head];
    m_idleNext[head] = countWaking ? s_WorkerWakingTask : s_WorkerNotIdle;
    --m_idleCount[affinity];
    --m_totalIdleWorkers;
    if(countWaking)
        ++m_wakingWorkers[affinity];
    if(m_slots[head].state.exchange(s_WorkerNotified, MemoryOrder::acq_rel) == s_WorkerParked){
        if(outPlan.parkedCount < s_InlineWakePlanSlots)
            outPlan.parkedSlots[outPlan.parkedCount++] = head;
        else
            outPlan.overflowWakeAll = true;
    }
    return true;
}

void CpuTaskScheduler::takeWorkerWakePlanLocked(WorkerWakePlan& outPlan)noexcept{
    if(m_totalIdleWorkers == 0u || m_stopping || m_nonEmptyReadyMask == 0u)
        return;

    u32 remHeavy = m_readyWorkerCosts[CpuTaskCost::Heavy];
    u32 remLight = m_readyWorkerCosts[CpuTaskCost::Light];
    u32 remAny = m_readyWorkerCosts[CpuTaskCost::Any];
    if((remHeavy | remLight | remAny) == 0u)
        return;

    const b8 effCanRunHeavy = m_busyPerformance >= m_statistics.performanceWorkers;
    const b8 perfCanRunLight = m_busyEfficiency >= m_statistics.efficiencyWorkers;

    u32 wakingPerf = m_wakingWorkers[CpuAffinity::Performance];
    u32 wakingEff = m_wakingWorkers[CpuAffinity::Efficiency];
    u32 wakingAny = m_wakingWorkers[CpuAffinity::Any];
    const auto cover = [](u32& supply, u32& demand)noexcept{
        const u32 taken = Min(supply, demand);
        supply -= taken;
        demand -= taken;
    };
    cover(wakingPerf, remHeavy);
    cover(wakingEff, remLight);
    cover(wakingAny, remAny);
    cover(wakingAny, remHeavy);
    cover(wakingAny, remLight);
    cover(wakingPerf, remAny);
    cover(wakingEff, remAny);
    if(effCanRunHeavy)
        cover(wakingEff, remHeavy);
    if(perfCanRunLight)
        cover(wakingPerf, remLight);
    if((remHeavy | remLight | remAny) == 0u)
        return;

    while(remHeavy > 0u && m_idleCount[CpuAffinity::Performance] > 0u && popIdleWorkerLocked(CpuAffinity::Performance, outPlan, true))
        --remHeavy;
    while(remLight > 0u && m_idleCount[CpuAffinity::Efficiency] > 0u && popIdleWorkerLocked(CpuAffinity::Efficiency, outPlan, true))
        --remLight;

    while(m_idleCount[CpuAffinity::Any] > 0u && (remHeavy | remLight | remAny) != 0u){
        if(!popIdleWorkerLocked(CpuAffinity::Any, outPlan, true))
            break;
        if(remAny > 0u)
            --remAny;
        else if(remHeavy > 0u)
            --remHeavy;
        else
            --remLight;
    }

    if(effCanRunHeavy){
        while(remHeavy > 0u && m_idleCount[CpuAffinity::Efficiency] > 0u && popIdleWorkerLocked(CpuAffinity::Efficiency, outPlan, true))
            --remHeavy;
    }
    if(perfCanRunLight){
        while(remLight > 0u && m_idleCount[CpuAffinity::Performance] > 0u && popIdleWorkerLocked(CpuAffinity::Performance, outPlan, true))
            --remLight;
    }

    while(remAny > 0u && m_totalIdleWorkers > 0u){
        if(popIdleWorkerLocked(CpuAffinity::Performance, outPlan, true) || popIdleWorkerLocked(CpuAffinity::Efficiency, outPlan, true))
            --remAny;
        else
            break;
    }
}

void CpuTaskScheduler::takeRangeWakePlanLocked(ParallelRangeGroup& range, const u32 maxWake, WorkerWakePlan& outPlan)noexcept{
    if(m_totalIdleWorkers == 0u || m_stopping || range.remainingHelperClaims == 0u || maxWake == 0u)
        return;
    if(range.nextChunk.load(MemoryOrder::relaxed) >= range.chunkCount)
        return;

    u32 budget = Min<u32>(range.remainingHelperClaims, maxWake);
    const CpuTaskCost::Enum cost = range.options.cost;
    const b8 effCanRunHeavy = m_busyPerformance >= m_statistics.performanceWorkers;
    const b8 perfCanRunLight = m_busyEfficiency >= m_statistics.efficiencyWorkers;
    while(budget > 0u && m_totalIdleWorkers > 0u){
        b8 woke = false;
        if(cost == CpuTaskCost::Heavy){
            woke = popIdleWorkerLocked(CpuAffinity::Performance, outPlan, false)
                || popIdleWorkerLocked(CpuAffinity::Any, outPlan, false)
                || (effCanRunHeavy && popIdleWorkerLocked(CpuAffinity::Efficiency, outPlan, false));
        }
        else if(cost == CpuTaskCost::Light){
            woke = popIdleWorkerLocked(CpuAffinity::Efficiency, outPlan, false)
                || popIdleWorkerLocked(CpuAffinity::Any, outPlan, false)
                || (perfCanRunLight && popIdleWorkerLocked(CpuAffinity::Performance, outPlan, false));
        }
        else{
            woke = popIdleWorkerLocked(CpuAffinity::Any, outPlan, false)
                || popIdleWorkerLocked(CpuAffinity::Performance, outPlan, false)
                || popIdleWorkerLocked(CpuAffinity::Efficiency, outPlan, false);
        }
        if(!woke)
            break;
        --budget;
    }
}

b8 CpuTaskScheduler::publishJoinWakeLocked(const b8 condition)noexcept{
    if(!condition || m_joinWaiters == 0u)
        return false;
    m_joinEpoch.fetch_add(1u, MemoryOrder::release);
    return true;
}

void CpuTaskScheduler::notifyWorkers(const WorkerWakePlan& wakePlan)noexcept{
    if(!wakePlan.overflowWakeAll){
        for(u32 i = 0u; i < wakePlan.parkedCount; ++i)
            m_slots[wakePlan.parkedSlots[i]].state.notify_one();
        return;
    }
    for(u32 i = 0u; i < m_workerCount; ++i){
        if(m_slots[i].state.load(MemoryOrder::acquire) == s_WorkerNotified)
            m_slots[i].state.notify_one();
    }
}

void CpuTaskScheduler::notifyProgress(const WorkerWakePlan& wakePlan, const b8 wakeJoiners)noexcept{
    if(wakeJoiners)
        m_joinEpoch.notify_all();
    notifyWorkers(wakePlan);
}

void CpuTaskScheduler::notifyProgress()noexcept{
    WorkerWakePlan wakePlan;
    b8 wakeJoiners = false;
    {
        NothrowScopedLock lock(m_mutex);
        takeWorkerWakePlanLocked(wakePlan);
        wakeJoiners = publishJoinWakeLocked(true);
    }
    notifyProgress(wakePlan, wakeJoiners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

