// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScheduler::ProfileMeasure::ProfileMeasure(CpuTaskScheduler& scheduler, const CpuTaskProfileKind::Enum kind, const TaskHandle task, const CpuTaskProfileLabel label)noexcept
    : m_scheduler(scheduler)
{
    if(!m_scheduler.m_profileEnabled.load(MemoryOrder::relaxed))
        return;
    NothrowScopedLock lock(m_scheduler.m_mutex);

    if(m_scheduler.m_profileEnabled.load(MemoryOrder::relaxed)){
        m_sample = m_scheduler.prepareProfileLocked(kind, task, label, scheduler.currentWorkerIndex(), scheduler.currentWorkerAffinity());
        m_sample->begin = TimerNow();
    }
}

CpuTaskScheduler::ProfileMeasure::~ProfileMeasure()noexcept{
    if(!m_sample)
        return;
    const Timer end = TimerNow();
    NothrowScopedLock lock(m_scheduler.m_mutex);

    m_scheduler.finishProfileLocked(*m_sample, end);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskProfileLabel CpuTaskScheduler::registerProfileLabel(const Name& name){
    if(name == s_NameNone)
        return {};
    ScopedLock lock(m_mutex);
    return m_profileBuffer.registerLabelLocked(name);
}

void CpuTaskScheduler::setProfiling(const b8 enabled, const u64 frameIndex){
    const b8 active = enabled && m_profileBuffer.capacity() != 0u;
    if(!active && !m_profileEnabled.load(MemoryOrder::relaxed))
        return;
    NothrowScopedLock lock(m_mutex);

    if(active){
        m_profileBuffer.ensureAllocatedLocked();
        if(m_readyProfiles.size() < m_nodes.size())
            m_readyProfiles.resize(m_nodes.size());
    }
    if(active != m_profileEnabled.load(MemoryOrder::relaxed)){
        if(++m_profileEpoch == 0u)
            TerminateInvariant();
        m_profileBuffer.resetLocked();
        m_profileEnabled.store(active, MemoryOrder::release);
    }
    m_profileFrameIndex = frameIndex;
}

usize CpuTaskScheduler::readProfileEvents(CpuTaskProfileEvent* const output, const usize capacity)noexcept{
    if(!output || capacity == 0u)
        return 0u;
    NothrowScopedLock lock(m_mutex);

    return m_profileBuffer.readLocked(output, capacity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CpuTaskScheduler::profileReadyLocked(const u32 index)noexcept{
    ReadyProfile& ready = m_readyProfiles[index];
    ready.ready = TimerNow();
    ready.frameIndex = m_profileFrameIndex;
    ready.captureEpoch = m_profileEpoch;
}

CpuTaskScheduler::ProfileSample CpuTaskScheduler::prepareProfileLocked(const CpuTaskProfileKind::Enum kind, const TaskHandle task, CpuTaskProfileLabel label, const usize workerIndex, const CpuAffinity::Enum affinity)const noexcept{
    TaskHandle ancestor = task;
    while(!label.valid()){
        const TaskNode* const node = resolveLocked(ancestor);
        if(!node)
            break;
        label = node->options.profileLabel;
        if(!label.valid() && node->scope)
            label = node->scope->m_profileLabel;
        ancestor = node->parent;
    }
    return { .begin = Timer{}, .task = task, .frameIndex = m_profileFrameIndex, .captureEpoch = m_profileEpoch, .workerIndex = workerIndex, .label = label, .affinity = affinity, .kind = kind, };
}

void CpuTaskScheduler::finishProfileLocked(const ProfileSample& sample, const Timer end)noexcept{
    if(!m_profileEnabled.load(MemoryOrder::relaxed) || sample.captureEpoch != m_profileEpoch)
        return;
    m_profileBuffer.recordLocked(sample, end);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

