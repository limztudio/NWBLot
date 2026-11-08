// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "profile_buffer.h"

#include <global/algorithm.h>
#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskProfileBuffer::CpuTaskProfileBuffer(Alloc::GlobalArena& arena, const usize capacity)
    : m_capacity(capacity), m_labels(arena), m_events(arena)
{}

CpuTaskProfileLabel CpuTaskProfileBuffer::registerLabelLocked(const Name& name){
    for(const LabelRecord& record : m_labels){
        if(record.name == name)
            return record.label;
    }
    static Atomic<u64> s_Next{ 1u };
    const CpuTaskProfileLabel label{ NextNonWrappingIdentity(s_Next) };
    m_labels.push_back(LabelRecord{ label, name });
    return label;
}

void CpuTaskProfileBuffer::ensureAllocatedLocked(){
    if(m_events.empty())
        m_events.resize(m_capacity);
}

void CpuTaskProfileBuffer::resetLocked()noexcept{
    m_read = 0u;
    m_count = 0u;
    m_recorded = 0u;
    m_dropped = 0u;
}

usize CpuTaskProfileBuffer::readLocked(CpuTaskProfileEvent* const output, const usize capacity)noexcept{
    const usize count = Min(capacity, m_count);
    for(usize index = 0u; index < count; ++index){
        output[index] = m_events[m_read];
        if(++m_read == m_events.size())
            m_read = 0u;
    }
    m_count -= count;
    return count;
}

void CpuTaskProfileBuffer::recordLocked(const Sample& sample, const Timer end)noexcept{
    if(m_count == m_events.size()){
        ++m_dropped;
        return;
    }
    CpuTaskProfileEvent& event = m_events[(m_read + m_count) % m_events.size()];
    event.kind = sample.kind;
    event.task = sample.task;
    event.label = s_NameNone;
    if(sample.label.valid()){
        const auto found = LowerBound(m_labels.begin(), m_labels.end(), sample.label.value, [](const LabelRecord& record, const u64 identity)noexcept{ return record.label.value < identity; });
        if(found != m_labels.end() && found->label.value == sample.label.value)
            event.label = found->name;
    }
    event.durationNanoseconds = DurationInNS<u64>(end, sample.begin);
    event.frameIndex = sample.frameIndex;
    event.captureEpoch = sample.captureEpoch;
    event.workerIndex = sample.workerIndex;
    event.affinity = sample.affinity;
    ++m_count;
    ++m_recorded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

