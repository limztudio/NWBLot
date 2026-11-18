// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/task/cpu/scheduler.h>
#include <global/terminal_entry.h>
#include <global/scope_exit.h>
#include <global/termination.h>
#include <type_traits>


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
    return RetiringCall<State, std::decay_t<Action>>(state, Forward<Action>(action));
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

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

