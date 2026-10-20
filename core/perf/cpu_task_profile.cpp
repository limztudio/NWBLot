// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_profile.h"
#include "timing.h"

#include <core/common/log.h>
#include <core/task/cpu/scheduler.h>

#include <global/simd/vector_arithmetic.h>


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
// One SIMDVectorDouble holds four lanes.
inline constexpr usize s_SecondsBatchLanes = 4u;
inline constexpr f64 s_NanosecondsToSeconds = 1.0e-9;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        for(usize index = 0u; index < count; index += __hidden_cpu_task_profile::s_SecondsBatchLanes){
            const usize batchCount = Min(count - index, __hidden_cpu_task_profile::s_SecondsBatchLanes);
            const Name* scopeNames[__hidden_cpu_task_profile::s_SecondsBatchLanes] = {};
            f64 nanoseconds[__hidden_cpu_task_profile::s_SecondsBatchLanes] = {};
            for(usize lane = 0u; lane < batchCount; ++lane){
                const CpuTaskProfileEvent& event = events[index + lane];
                scopeNames[lane] = __hidden_cpu_task_profile::TimingScope(event);
                if(scopeNames[lane])
                    nanoseconds[lane] = static_cast<f64>(event.durationNanoseconds);
            }
            const SIMDVectorDouble seconds = VectorDoubleMultiply(VectorDoubleSet(nanoseconds[0u], nanoseconds[1u], nanoseconds[2u], nanoseconds[3u]), VectorDoubleReplicate(__hidden_cpu_task_profile::s_NanosecondsToSeconds));
            for(usize lane = 0u; lane < batchCount; ++lane){
                if(!scopeNames[lane])
                    continue;
                const CpuTaskProfileEvent& event = events[index + lane];
                const auto scope = timing.registerScope(*scopeNames[lane]);
                if(!scope){
                    NWB_LOGGER_WARNING(NWB_TEXT("CPU task timing scope registration failed"));
                    continue;
                }
                timing.recordSample(*scope, VectorDoubleGetByIndex(seconds, lane), event.frameIndex);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

