// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_profile.h"
#include "timing.h"

#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_profile{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_QueueDelay("cpu.task.queue_delay");
inline constexpr Name s_Execution("cpu.task.execution");
inline constexpr Name s_HandleJoin("cpu.task.handle_join");
inline constexpr Name s_ScopeJoin("cpu.task.scope_join");
inline constexpr Name s_SchedulerJoin("cpu.task.scheduler_join");
inline constexpr Name s_WorkerIdle("cpu.worker.idle");
inline constexpr usize s_ReadBatchSize = 32u;


[[nodiscard]] const Name* TimingScope(const CpuTaskProfileEvent& event)noexcept{
    switch(event.kind){
    case CpuTaskProfileKind::QueueDelay: return &s_QueueDelay;
    case CpuTaskProfileKind::Execution: return event.label ? &event.label : &s_Execution;
    case CpuTaskProfileKind::HandleJoin: return &s_HandleJoin;
    case CpuTaskProfileKind::ScopeJoin: return &s_ScopeJoin;
    case CpuTaskProfileKind::SchedulerJoin: return &s_SchedulerJoin;
    case CpuTaskProfileKind::WorkerIdle: return &s_WorkerIdle;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CollectCpuTaskProfile(CpuTaskScheduler& scheduler, TimingSink& timing){
    usize remaining = scheduler.statistics().profilePendingEvents;
    if(remaining == 0u)
        return;
    const bool enabled = timing.enabled();
    CpuTaskProfileEvent events[__hidden_cpu_task_profile::s_ReadBatchSize];
    while(remaining != 0u){
        const usize count = scheduler.readProfileEvents(events, Min(remaining, __hidden_cpu_task_profile::s_ReadBatchSize));
        if(count == 0u)
            break;
        remaining -= count;
        if(!enabled)
            continue;
        for(usize index = 0u; index < count; ++index){
            const CpuTaskProfileEvent& event = events[index];
            const Name* const scopeName = __hidden_cpu_task_profile::TimingScope(event);
            if(!scopeName)
                continue;
            const TimingScopeId scope = timing.registerScope(*scopeName);
            timing.recordSample(scope, static_cast<f64>(event.durationNanoseconds) * 1.0e-9, event.frameIndex);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

