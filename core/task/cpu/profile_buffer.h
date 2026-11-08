// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "profiling.h"

#include <core/alloc/general.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CpuTaskProfileBuffer final : NoCopy{
public:
    struct Sample{
        Timer begin;
        CpuTaskHandle task;
        u64 frameIndex;
        u64 captureEpoch;
        usize workerIndex;
        CpuTaskProfileLabel label;
        CpuAffinity::Enum affinity;
        CpuTaskProfileKind::Enum kind;
    };


public:
    CpuTaskProfileBuffer(Alloc::GlobalArena& arena, usize capacity);


public:
    [[nodiscard]] usize capacity()const noexcept{ return m_capacity; }
    [[nodiscard]] usize pending()const noexcept{ return m_count; }
    [[nodiscard]] u64 recorded()const noexcept{ return m_recorded; }
    [[nodiscard]] u64 dropped()const noexcept{ return m_dropped; }
    [[nodiscard]] CpuTaskProfileLabel registerLabelLocked(const Name& name);
    void ensureAllocatedLocked();
    void resetLocked()noexcept;
    [[nodiscard]] usize readLocked(CpuTaskProfileEvent* output, usize capacity)noexcept;
    void recordLocked(const Sample& sample, Timer end)noexcept;


private:
    struct LabelRecord{ CpuTaskProfileLabel label; Name name; };
    const usize m_capacity;
    Vector<LabelRecord, Alloc::GlobalArena> m_labels;
    Vector<CpuTaskProfileEvent, Alloc::GlobalArena> m_events;
    u64 m_recorded = 0u;
    u64 m_dropped = 0u;
    usize m_read = 0u;
    usize m_count = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

