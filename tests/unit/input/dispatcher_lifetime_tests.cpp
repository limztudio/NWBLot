// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "dispatcher_fixture.h"

#include <global/optional.h>

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputDispatcherTestsImpl{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SelfDestroyingKeyHandler final : public IInputEventHandler{
public:
    SelfDestroyingKeyHandler(InputDispatcher& dispatcher, Optional<SelfDestroyingKeyHandler>& owner)noexcept
        : m_dispatcher(dispatcher), m_owner(owner){}

    virtual KeyboardInputResult keyboardUpdate(i32, i32, i32, i32)override{
        m_dispatcher.removeHandler(*this);
        auto* const owner = &m_owner;
        owner->reset();
        return { true, true };
    }

private:
    InputDispatcher& m_dispatcher;
    Optional<SelfDestroyingKeyHandler>& m_owner;
};

class DestroyOtherKeyHandler final : public IInputEventHandler{
public:
    DestroyOtherKeyHandler(InputDispatcher& dispatcher, Optional<FocusHandler>& victim)noexcept
        : m_dispatcher(dispatcher), m_victim(victim){}

    virtual KeyboardInputResult keyboardUpdate(i32, i32, i32, i32)override{
        if(m_victim){
            m_dispatcher.removeHandler(*m_victim);
            m_victim.reset();
        }
        return false;
    }

private:
    InputDispatcher& m_dispatcher;
    Optional<FocusHandler>& m_victim;
};

class ReplacingKeyHandler final : public IInputEventHandler{
public:
    ReplacingKeyHandler(InputDispatcher& dispatcher, ReplacingKeyHandler*& slot, usize& calls, const b8 replace)noexcept
        : m_dispatcher(dispatcher), m_slot(slot), m_calls(calls), m_replace(replace){}

    virtual KeyboardInputResult keyboardUpdate(i32, i32, i32, i32)override{
        ++m_calls;
        if(!m_replace)
            return { true, false };
        InputDispatcher* const dispatcher = &m_dispatcher;
        ReplacingKeyHandler** const slot = &m_slot;
        usize* const calls = &m_calls;
        void* const storage = this;
        dispatcher->removeHandler(*this);
        this->~ReplacingKeyHandler();
        *slot = new (storage) ReplacingKeyHandler(*dispatcher, *slot, *calls, false);
        dispatcher->addHandlerToBack(**slot);
        return { true, true };
    }

private:
    InputDispatcher& m_dispatcher;
    ReplacingKeyHandler*& m_slot;
    usize& m_calls;
    b8 m_replace;
};

TEST(InputDispatcher, ReplacementAtRemovedHandlerAddressStartsOnNextDispatch){
    InputDispatcher dispatcher;
    alignas(ReplacingKeyHandler) u8 storage[sizeof(ReplacingKeyHandler)];
    ReplacingKeyHandler* handler = nullptr;
    usize calls = 0u;
    handler = new (storage) ReplacingKeyHandler(dispatcher, handler, calls, true);
    dispatcher.addHandlerToBack(*handler);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_EQ(calls, 1u);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(30));
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_EQ(calls, 2u);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(30));
    dispatcher.removeHandler(*handler);
    handler->~ReplacingKeyHandler();
}

TEST(InputDispatcher, KeyHandlerCanRemoveAndDestroyItselfBeforeReturningPolicy){
    InputDispatcher dispatcher;
    Optional<SelfDestroyingKeyHandler> owner;
    owner.emplace(dispatcher, owner);
    dispatcher.addHandlerToBack(*owner);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_FALSE(owner);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(30));
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(30));
}

TEST(InputDispatcher, RemovedHandlerCanBeDestroyedBeforeItsTurn){
    InputDispatcher dispatcher;
    Optional<FocusHandler> victim;
    victim.emplace(dispatcher);
    DestroyOtherKeyHandler killer(dispatcher, victim);
    dispatcher.addHandlerToFront(*victim);
    dispatcher.addHandlerToBack(killer);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_FALSE(victim);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(30));
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


