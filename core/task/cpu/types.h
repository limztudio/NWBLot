// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <global/cpu_topology.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CpuTaskCost{
    enum Enum : u8{
        Any,
        Heavy,
        Light
    };
};

namespace CpuTaskPriority{
    enum Enum : u8{
        Critical,
        Normal,
        Background
    };
};

namespace CpuTaskTarget{
    enum Enum : u8{
        Worker,
        MainThread
    };
};


struct CpuTaskProfileLabel{
    u64 value = 0u;

    [[nodiscard]] bool valid()const noexcept{ return value != 0u; }
};

struct CpuTaskOptions{
    CpuTaskCost::Enum cost = CpuTaskCost::Heavy;
    CpuTaskPriority::Enum priority = CpuTaskPriority::Normal;
    CpuTaskTarget::Enum target = CpuTaskTarget::Worker;
    CpuTaskProfileLabel profileLabel = {};
};

struct CpuTaskSchedulerConfig{
    static constexpr u32 s_AutomaticWorkerCount = Limit<u32>::s_Max;

    u32 workerCount = s_AutomaticWorkerCount;
    u32 reservedThreadCount = 1u;
    bool heterogeneous = true;
    usize profileEventCapacity = 4096u;
};

struct CpuTaskHandle{
    static constexpr u32 s_InvalidIndex = Limit<u32>::s_Max;

    u64 domainIdentity = 0u;
    u32 index = s_InvalidIndex;
    u32 generation = 0u;

    [[nodiscard]] bool valid()const noexcept{ return domainIdentity != 0u && index != s_InvalidIndex && generation != 0u; }
    explicit operator bool()const noexcept{ return valid(); }
};

struct CpuTaskSchedulerStatistics{
    u64 completedTasks = 0u;
    u64 canceledTasks = 0u;
    u64 performanceTasks = 0u;
    u64 efficiencyTasks = 0u;
    u64 unclassifiedTasks = 0u;
    u64 cooperativeTasks = 0u;
    usize outstandingTasks = 0u;
    usize peakOutstandingTasks = 0u;
    u32 performanceWorkers = 0u;
    u32 efficiencyWorkers = 0u;
    u32 unclassifiedWorkers = 0u;
    u32 placementFailures = 0u;
    u64 profileRecordedEvents = 0u;
    u64 profileDroppedEvents = 0u;
    usize profilePendingEvents = 0u;
    bool profileEnabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

