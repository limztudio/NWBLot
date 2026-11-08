// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "input_event.h"

#include <core/global.h>
#include <core/input/arena_names.adhd.h>
#include <core/alloc/module.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace HandlerMutationType{
    enum Enum : u8{
        AddFront,
        AddBack,
        Remove,
    };
};


class InputDispatcher{
private:
    static constexpr usize s_KeyboardScancodeCount = NWB::Core::InputCodeValues::s_InputCodeValueKeyboardScancodeCount;


private:
    struct HandlerMutation{
        NotNull<IInputEventHandler*> handler;
        HandlerMutationType::Enum type = HandlerMutationType::Remove;
    };


private:
    using HandlerList = List<NotNull<IInputEventHandler*>, Alloc::GlobalArena>;
    using HandlerMutationVector = Vector<HandlerMutation, Alloc::GlobalArena>;


public:
    InputDispatcher()
        : m_arena(InputArenaScope::s_DispatcherArena)
        , m_handlers(m_arena)
        , m_pendingHandlerMutations(m_arena)
    {}


public:
    // Remove a handler before destroying it, including during a callback.
    void addHandlerToFront(IInputEventHandler& handler){ queueOrApplyHandlerMutation(HandlerMutationType::AddFront, MakeNotNull(&handler)); }
    void addHandlerToBack(IInputEventHandler& handler){ queueOrApplyHandlerMutation(HandlerMutationType::AddBack, MakeNotNull(&handler)); }
    void removeHandler(IInputEventHandler& handler){ queueOrApplyHandlerMutation(HandlerMutationType::Remove, MakeNotNull(&handler)); }

    void setMousePositionScale(f32 x, f32 y)noexcept;

    [[nodiscard]] b8 windowFocused()const noexcept{ return m_windowFocused; }
    // Focus is window lifecycle state; every current handler observes each transition.
    void windowFocusUpdate(b8 focused);
    // Native pointer lifecycle reaches every owner and never consumes keyboard state.
    void pointerLeave();
    void pointerCaptureLost();

    void keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods);
    // Releases preserve queued character policy; a supplied native scancode selects its captured press/repeat policy.
    [[nodiscard]] b8 keyboardTextBlocked(i32 scancode = -1)const noexcept;
    void keyboardCharInput(u32 unicode, i32 mods);
    void mousePosUpdate(f64 xpos, f64 ypos);
    void mouseButtonUpdate(i32 button, i32 action, i32 mods);
    void mouseScrollUpdate(f64 xoffset, f64 yoffset);


private:
    template<typename DispatchFunc>
    void dispatchToHandlers(DispatchFunc&& dispatchFunc){
        {
            ++m_dispatchDepth;
            ScopeExit restoreDispatchDepth([this]()noexcept{ --m_dispatchDepth; });

            for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
                const NotNull<IInputEventHandler*> handler = *it;
                if(m_pendingHandlerRemovalCount > 0 && isHandlerPendingRemoval(handler.get()))
                    continue;
                if(dispatchFunc(*handler))
                    break;
            }
        }
        if(m_dispatchDepth == 0 && !m_pendingHandlerMutations.empty())
            applyPendingHandlerMutations();
    }

    void queueOrApplyHandlerMutation(HandlerMutationType::Enum type, NotNull<IInputEventHandler*> handler);
    void applyPendingHandlerMutations();
    b8 isHandlerPendingRemoval(const IInputEventHandler* handler)const noexcept;


private:
    Alloc::GlobalArena m_arena;
    HandlerList m_handlers;
    HandlerMutationVector m_pendingHandlerMutations;
    usize m_pendingHandlerRemovalCount = 0;
    u64 m_keyboardTextPolicyEpoch = 0u;
    Array<b8, s_KeyboardScancodeCount> m_keyboardTextPolicies{};
    f32 m_mousePositionScaleX = 1.f;
    f32 m_mousePositionScaleY = 1.f;
    u32 m_dispatchDepth = 0;
    b8 m_keyboardTextBlocked = false;
    b8 m_windowFocused = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

