// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/task/cpu/scheduler.h>
#include <global/atomic.h>
#include <global/terminal_entry.h>
#include <global/scope_exit.h>
#include <global/termination.h>
#include <global/type_borrow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::CpuTaskSdk{

template<typename State, typename Action>
class RetiringCall{
public:
    RetiringCall(const State& state, Action action) : m_state(state), m_action(Move(action)){}
    RetiringCall(const RetiringCall&) = delete;
    RetiringCall(RetiringCall&& other)noexcept
        : m_state(Move(other.m_state)), m_action(Move(other.m_action)), m_owns(other.m_owns){ other.m_owns = false; }
    ~RetiringCall()noexcept{ if(m_owns) m_state.retire(); }
    void operator()(){ m_action(m_state, m_owns); }
private:
    State m_state;
    Action m_action;
    bool m_owns = true;
};
template<typename State, typename Action>
auto RetireAfterTask(const State& state, Action&& action){
    return RetiringCall<State, Decay_T<Action>>(state, Forward<Action>(action));
}

struct ThrowingMoveTask{
    explicit ThrowingMoveTask(u32 code)noexcept : code(code){}
    ThrowingMoveTask(ThrowingMoveTask&& other){ throw other.code; }
    void operator()()const noexcept{}
    u32 code;
};
struct ThrowingCopyTask{
    explicit ThrowingCopyTask(u32 code)noexcept : code(code){}
    ThrowingCopyTask(const ThrowingCopyTask& other){ throw other.code; }
    ThrowingCopyTask(ThrowingCopyTask&&)noexcept = default;
    void operator()()const noexcept{}
    u32 code;
};
template<typename T>
constexpr bool TaskConstructible()noexcept{ return IsConstructible_V<InplaceFunction<128u>, T>; }

inline usize NullProfileEvents(Core::CpuTaskScheduler& scheduler, const usize capacity)noexcept{ return scheduler.readProfileEvents(nullptr, capacity); }
template<typename Scheduler, typename Action>
auto SubmitNullDependencies(Scheduler& scheduler, Action&& action, const usize count){ return scheduler.submit(Forward<Action>(action), {}, nullptr, count); }
template<typename Scheduler, typename Action>
auto SubmitAfter(Scheduler& scheduler, Action&& action, const Core::CpuTaskOptions options, const Core::CpuTaskHandle dependency){
    return scheduler.submit(Forward<Action>(action), options, &dependency, 1u);
}

[[nodiscard]] inline b8 VerifyCheckedScopeDrainOnAdmissionFailure(){
    Core::CpuTaskSchedulerConfig config{};
    config.workerCount = 1u;
    Core::CpuTaskScheduler scheduler(config);

    Atomic<u32> runs{ 0u };
    Atomic<u32> canceledRuns{ 0u };
    Atomic<b8> started{ false };
    Atomic<b8> acceptedFinished{ false };
    b8 probeSawFinished = false;
    b8 threwRuntimeError = false;

    struct DrainProbe final : NoCopy{
        const Atomic<b8>& finished;
        b8& sawFinished;
        explicit DrainProbe(const Atomic<b8>& f, b8& s)noexcept : finished(f), sawFinished(s){}
        ~DrainProbe()noexcept{ sawFinished = finished.load(MemoryOrder::acquire); }
    };

    try{
        DrainProbe preProbe(acceptedFinished, probeSawFinished);
        Core::CpuTaskScope scope(scheduler);
        const Core::CpuTaskHandle first = scope.submitChecked([&runs, &started, &acceptedFinished]()noexcept{
            started.store(true, MemoryOrder::release);
            SleepMS(15u);
            runs.fetch_add(1u, MemoryOrder::relaxed);
            acceptedFinished.store(true, MemoryOrder::release);
        });
        Core::CpuTaskScope::JoinGuard joinGuard(scope);
        if(!first.valid())
            return false;
        while(!started.load(MemoryOrder::acquire))
            SleepMS(1u);
        const Core::CpuTaskHandle canceledTask = scope.submitChecked([&canceledRuns]()noexcept{
            canceledRuns.fetch_add(1u, MemoryOrder::relaxed);
        }, Core::CpuTaskOptions{}, &first, 1u);
        if(!canceledTask.valid())
            return false;
        Core::CpuTaskOptions invalidOptions{};
        invalidOptions.cost = static_cast<Core::CpuTaskCost::Enum>(99u);
        scope.submitDetachedChecked([&runs]()noexcept{
            runs.fetch_add(10u, MemoryOrder::relaxed);
        }, invalidOptions);
    }
    catch(const RuntimeException&){
        threwRuntimeError = true;
    }

    return threwRuntimeError && probeSawFinished && runs.load(MemoryOrder::acquire) == 1u && canceledRuns.load(MemoryOrder::acquire) == 0u;
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

