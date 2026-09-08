// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScheduler::ProfileMeasure::ProfileMeasure(
    CpuTaskScheduler& scheduler,
    const CpuTaskProfileKind::Enum kind,
    const TaskHandle task,
    const CpuTaskProfileLabel label)
    : m_scheduler(scheduler)
{
    if(!m_scheduler.m_profileEnabled.load(MemoryOrder::relaxed))
        return;
    ScopedLock lock(m_scheduler.m_mutex);

    if(m_scheduler.m_profileEnabled.load(MemoryOrder::relaxed)){
        m_sample = m_scheduler.prepareProfileLocked(
            kind,
            task,
            label,
            scheduler.currentWorkerIndex(),
            scheduler.currentWorkerAffinity()
        );
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


u64 CpuTaskScheduler::allocateProfileLabelIdentity()noexcept{
    static Atomic<u64> next{ 1u };
    const u64 identity = next.fetch_add(1u, MemoryOrder::relaxed);
    if(identity == 0u)
        TerminateInvariant();
    return identity;
}

CpuTaskProfileLabel CpuTaskScheduler::registerProfileLabel(const Name& name){
    if(name == NAME_NONE)
        return {};
    ScopedLock lock(m_mutex);

    for(const ProfileLabelRecord& record : m_profileLabels){
        if(record.name == name)
            return record.label;
    }
    const CpuTaskProfileLabel label{ allocateProfileLabelIdentity() };
    m_profileLabels.push_back(ProfileLabelRecord{ label, name });
    return label;
}

void CpuTaskScheduler::setProfiling(const bool enabled, const u64 frameIndex){
    const bool active = enabled && m_profileEventCapacity != 0u;
    if(!active && !m_profileEnabled.load(MemoryOrder::relaxed))
        return;
    NothrowScopedLock lock(m_mutex);

    if(active){
        if(m_profileEvents.empty())
            m_profileEvents.resize(m_profileEventCapacity);
        if(m_readyProfiles.size() < m_nodes.size())
            m_readyProfiles.resize(m_nodes.size());
    }
    if(active != m_profileEnabled.load(MemoryOrder::relaxed)){
        if(++m_profileEpoch == 0u)
            TerminateInvariant();
        m_profileRead = 0u;
        m_profileCount = 0u;
        m_profileRecordedEvents = 0u;
        m_profileDroppedEvents = 0u;
        m_profileEnabled.store(active, MemoryOrder::release);
    }
    m_profileFrameIndex = frameIndex;
}

usize CpuTaskScheduler::readProfileEvents(CpuTaskProfileEvent* const output, const usize capacity)noexcept{
    if(!output || capacity == 0u)
        return 0u;
    NothrowScopedLock lock(m_mutex);

    const usize count = Min(capacity, m_profileCount);
    for(usize index = 0u; index < count; ++index){
        output[index] = m_profileEvents[m_profileRead];
        if(++m_profileRead == m_profileEvents.size())
            m_profileRead = 0u;
    }
    m_profileCount -= count;
    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CpuTaskScheduler::profileReadyLocked(const u32 index)noexcept{
    ReadyProfile& ready = m_readyProfiles[index];
    ready.ready = TimerNow();
    ready.frameIndex = m_profileFrameIndex;
    ready.captureEpoch = m_profileEpoch;
}

CpuTaskScheduler::ProfileSample CpuTaskScheduler::prepareProfileLocked(
    const CpuTaskProfileKind::Enum kind,
    const TaskHandle task,
    CpuTaskProfileLabel label,
    const usize workerIndex,
    const CpuAffinity::Enum affinity)const noexcept{
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
    return { Timer{}, kind, task, label, m_profileFrameIndex, m_profileEpoch, workerIndex, affinity };
}

void CpuTaskScheduler::finishProfileLocked(const ProfileSample& sample, const Timer end)noexcept{
    if(!m_profileEnabled.load(MemoryOrder::relaxed) || sample.captureEpoch != m_profileEpoch)
        return;
    if(m_profileCount == m_profileEvents.size()){
        ++m_profileDroppedEvents;
        return;
    }
    CpuTaskProfileEvent& event = m_profileEvents[(m_profileRead + m_profileCount) % m_profileEvents.size()];
    event.kind = sample.kind;
    event.task = sample.task;
    event.label = NAME_NONE;
    if(sample.label.valid()){
        const auto found = LowerBound(
            m_profileLabels.begin(),
            m_profileLabels.end(),
            sample.label.value,
            [](const ProfileLabelRecord& record, const u64 identity){ return record.label.value < identity; }
        );
        if(found != m_profileLabels.end() && found->label.value == sample.label.value)
            event.label = found->name;
    }
    event.durationNanoseconds = DurationInNS<u64>(end, sample.begin);
    event.frameIndex = sample.frameIndex;
    event.captureEpoch = sample.captureEpoch;
    event.workerIndex = sample.workerIndex;
    event.affinity = sample.affinity;
    ++m_profileCount;
    ++m_profileRecordedEvents;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

