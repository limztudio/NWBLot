// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"
#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns non-throwing cleanup until the operation explicitly publishes its result.
template<typename Func>
class ScopeExit final : NoCopy{
    static_assert(IsNothrowMoveConstructible_V<Func>, "ScopeExit cleanup must be nothrow move constructible");
    static_assert(IsNothrowDestructible_V<Func>, "ScopeExit cleanup must be nothrow destructible");
    static_assert(requires(Func& function){ { function() }noexcept; }, "ScopeExit cleanup must not throw");


public:
    template<typename Callable>
    requires requires(Callable&& function){ { Func(Forward<Callable>(function)) }noexcept; }
    explicit ScopeExit(Callable&& function)noexcept
        : m_function(Forward<Callable>(function))
    {}
    ScopeExit(ScopeExit&&) = delete;
    ~ScopeExit()noexcept{
        if(m_active)
            m_function();
    }


public:
    void release()noexcept{ m_active = false; }


private:
    Func m_function;
    bool m_active = true;
};


template<typename Func>
ScopeExit(Func&&) -> ScopeExit<Decay_T<Func>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

