// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/input/module.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_input_pointer_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class PointerLifecycleHandler final : public IInputEventHandler{
public:
    explicit PointerLifecycleHandler(InputDispatcher& dispatcher)noexcept
        : m_dispatcher(dispatcher)
    {}


public:
    virtual void windowFocusUpdate(bool)noexcept override{ ++m_focusChanges; }
    virtual void pointerLeave()override{
        ++m_leaves;
        if(m_addOnLeave){
            m_dispatcher.addHandlerToBack(*m_addOnLeave);
            m_addOnLeave = nullptr;
        }
    }
    virtual void pointerCaptureLost()override{
        ++m_captureLosses;
        if(m_removeOnCaptureLoss){
            m_dispatcher.removeHandler(*m_removeOnCaptureLoss);
            m_removeOnCaptureLoss = nullptr;
        }
    }
    virtual bool keyboardUpdate(i32, i32, const i32 action, i32)noexcept override{
        if(action == InputAction::Release)
            ++m_keyReleases;
        else
            ++m_keyPresses;
        return false;
    }


public:
    InputDispatcher& m_dispatcher;
    IInputEventHandler* m_addOnLeave = nullptr;
    IInputEventHandler* m_removeOnCaptureLoss = nullptr;
    usize m_leaves = 0u;
    usize m_captureLosses = 0u;
    usize m_focusChanges = 0u;
    usize m_keyPresses = 0u;
    usize m_keyReleases = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(InputDispatcher, CaptureLossSkipsPendingRemovalAndKeepsOtherOwners){
    InputDispatcher dispatcher;
    PointerLifecycleHandler scene(dispatcher);
    PointerLifecycleHandler removed(dispatcher);
    PointerLifecycleHandler ui(dispatcher);
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(removed);
    dispatcher.addHandlerToBack(ui);
    ui.m_removeOnCaptureLoss = &removed;
    dispatcher.pointerCaptureLost();
    EXPECT_EQ(ui.m_captureLosses, 1u);
    EXPECT_EQ(scene.m_captureLosses, 1u);
    EXPECT_EQ(removed.m_captureLosses, 0u);
    dispatcher.pointerCaptureLost();
    EXPECT_EQ(ui.m_captureLosses, 2u);
    EXPECT_EQ(scene.m_captureLosses, 2u);
    EXPECT_EQ(removed.m_captureLosses, 0u);
}

TEST(InputDispatcher, OwnerAddedDuringPointerLeaveWaitsForNextEvent){
    InputDispatcher dispatcher;
    PointerLifecycleHandler existing(dispatcher);
    PointerLifecycleHandler added(dispatcher);
    dispatcher.addHandlerToBack(existing);
    existing.m_addOnLeave = &added;
    dispatcher.pointerLeave();
    EXPECT_EQ(existing.m_leaves, 1u);
    EXPECT_EQ(added.m_leaves, 0u);
    dispatcher.pointerLeave();
    EXPECT_EQ(existing.m_leaves, 2u);
    EXPECT_EQ(added.m_leaves, 1u);
}

TEST(InputDispatcher, PointerLifecycleDoesNotSynthesizeFocusOrKeyboardRelease){
    InputDispatcher dispatcher;
    PointerLifecycleHandler handler(dispatcher);
    dispatcher.addHandlerToBack(handler);
    dispatcher.keyboardUpdate(Key::W, 0, InputAction::Press, 0);
    dispatcher.pointerLeave();
    dispatcher.pointerCaptureLost();
    EXPECT_TRUE(dispatcher.windowFocused());
    EXPECT_EQ(handler.m_focusChanges, 0u);
    EXPECT_EQ(handler.m_keyPresses, 1u);
    EXPECT_EQ(handler.m_keyReleases, 0u);
    dispatcher.keyboardUpdate(Key::W, 0, InputAction::Release, 0);
    EXPECT_EQ(handler.m_keyReleases, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

