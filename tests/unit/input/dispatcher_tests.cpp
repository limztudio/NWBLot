// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/input/module.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_input_dispatcher_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class FocusHandler final : public IInputEventHandler{
public:
    explicit FocusHandler(InputDispatcher& dispatcher)
        : m_dispatcher(dispatcher)
    {}


public:
    virtual void windowFocusUpdate(const bool focused)override{
        ASSERT_LT(m_focusCount, m_focusValues.size());
        m_focusValues[m_focusCount] = focused;
        ++m_focusCount;
        if(m_removeTarget){
            m_dispatcher.removeHandler(*m_removeTarget);
            m_removeTarget = nullptr;
        }
        if(m_addTarget){
            m_dispatcher.addHandlerToBack(*m_addTarget);
            m_addTarget = nullptr;
        }
        if(m_removeSelf)
            m_dispatcher.removeHandler(*this);
    }

    virtual bool keyboardUpdate(i32, i32, const i32 action, i32)override{
        if(action == InputAction::Release)
            ++m_keyReleases;
        else
            ++m_keyPresses;
        if(m_loseFocusOnPress && action == InputAction::Press)
            m_dispatcher.windowFocusUpdate(false);
        return m_consumeKeys;
    }

    virtual bool mouseButtonUpdate(i32, const i32 action, i32)override{
        if(action == InputAction::Release)
            ++m_buttonReleases;
        else
            ++m_buttonPresses;
        return m_consumeButtons;
    }


public:
    InputDispatcher& m_dispatcher;
    Array<bool, 8u> m_focusValues{};
    usize m_focusCount = 0u;
    usize m_keyPresses = 0u;
    usize m_keyReleases = 0u;
    usize m_buttonPresses = 0u;
    usize m_buttonReleases = 0u;
    IInputEventHandler* m_removeTarget = nullptr;
    IInputEventHandler* m_addTarget = nullptr;
    bool m_removeSelf = false;
    bool m_loseFocusOnPress = false;
    bool m_consumeKeys = false;
    bool m_consumeButtons = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(InputDispatcher, WindowFocusTransitionsReachEveryHandlerAndDeduplicate){
    InputDispatcher dispatcher;
    FocusHandler scene(dispatcher);
    FocusHandler legacyUi(dispatcher);
    FocusHandler customUi(dispatcher);
    customUi.m_consumeKeys = true;
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(legacyUi);
    dispatcher.addHandlerToBack(customUi);
    EXPECT_TRUE(dispatcher.windowFocused());

    dispatcher.windowFocusUpdate(false);
    EXPECT_FALSE(dispatcher.windowFocused());
    dispatcher.windowFocusUpdate(false);
    dispatcher.windowFocusUpdate(true);
    dispatcher.windowFocusUpdate(true);
    EXPECT_TRUE(dispatcher.windowFocused());
    for(const FocusHandler* handler : { &scene, &legacyUi, &customUi }){
        ASSERT_EQ(handler->m_focusCount, 2u);
        EXPECT_FALSE(handler->m_focusValues[0u]);
        EXPECT_TRUE(handler->m_focusValues[1u]);
    }
}

TEST(InputDispatcher, FocusBroadcastSkipsPendingRemovalWithoutSkippingOtherOwners){
    InputDispatcher dispatcher;
    FocusHandler scene(dispatcher);
    FocusHandler removed(dispatcher);
    FocusHandler router(dispatcher);
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(removed);
    dispatcher.addHandlerToBack(router);
    router.m_removeTarget = &removed;

    dispatcher.windowFocusUpdate(false);
    EXPECT_EQ(router.m_focusCount, 1u);
    EXPECT_EQ(scene.m_focusCount, 1u);
    EXPECT_EQ(removed.m_focusCount, 0u);
    dispatcher.windowFocusUpdate(true);
    EXPECT_EQ(router.m_focusCount, 2u);
    EXPECT_EQ(scene.m_focusCount, 2u);
    EXPECT_EQ(removed.m_focusCount, 0u);
}

TEST(InputDispatcher, HandlerAddedDuringFocusBroadcastWaitsUntilNextTransition){
    InputDispatcher dispatcher;
    FocusHandler existing(dispatcher);
    FocusHandler added(dispatcher);
    dispatcher.addHandlerToBack(existing);
    existing.m_addTarget = &added;

    dispatcher.windowFocusUpdate(false);
    EXPECT_EQ(existing.m_focusCount, 1u);
    EXPECT_EQ(added.m_focusCount, 0u);
    dispatcher.windowFocusUpdate(true);
    EXPECT_EQ(existing.m_focusCount, 2u);
    ASSERT_EQ(added.m_focusCount, 1u);
    EXPECT_TRUE(added.m_focusValues[0u]);
}

TEST(InputDispatcher, HandlerMayRemoveItselfDuringFocusBroadcast){
    InputDispatcher dispatcher;
    FocusHandler scene(dispatcher);
    FocusHandler transient(dispatcher);
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(transient);
    transient.m_removeSelf = true;

    dispatcher.windowFocusUpdate(false);
    dispatcher.windowFocusUpdate(true);
    EXPECT_EQ(scene.m_focusCount, 2u);
    EXPECT_EQ(transient.m_focusCount, 1u);
}

TEST(InputDispatcher, NestedFocusBroadcastDefersHandlerMutationUntilOuterDispatchEnds){
    InputDispatcher dispatcher;
    FocusHandler scene(dispatcher);
    FocusHandler router(dispatcher);
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(router);
    router.m_loseFocusOnPress = true;
    router.m_removeTarget = &scene;

    dispatcher.keyboardUpdate(Key::W, 0, InputAction::Press, 0);
    EXPECT_FALSE(dispatcher.windowFocused());
    EXPECT_EQ(router.m_keyPresses, 1u);
    EXPECT_EQ(router.m_focusCount, 1u);
    EXPECT_EQ(scene.m_keyPresses, 0u);
    EXPECT_EQ(scene.m_focusCount, 0u);
    dispatcher.keyboardUpdate(Key::W, 0, InputAction::Release, 0);
    EXPECT_EQ(router.m_keyReleases, 1u);
    EXPECT_EQ(scene.m_keyReleases, 0u);
}

TEST(InputDispatcher, KeyboardReleaseClearsSceneEvenWhenUiConsumesAfterPress){
    InputDispatcher dispatcher;
    FocusHandler scene(dispatcher);
    FocusHandler ui(dispatcher);
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(ui);

    dispatcher.keyboardUpdate(Key::W, 0, InputAction::Press, 0);
    ASSERT_EQ(scene.m_keyPresses, 1u);
    ui.m_consumeKeys = true;
    dispatcher.keyboardUpdate(Key::W, 0, InputAction::Release, 0);
    EXPECT_EQ(ui.m_keyReleases, 1u);
    EXPECT_EQ(scene.m_keyReleases, 1u);
    dispatcher.keyboardUpdate(Key::W, 0, InputAction::Repeat, 0);
    EXPECT_EQ(ui.m_keyPresses, 2u);
    EXPECT_EQ(scene.m_keyPresses, 1u);
}

TEST(InputDispatcher, MouseReleaseClearsSceneEvenWhenUiConsumesAfterPress){
    InputDispatcher dispatcher;
    FocusHandler scene(dispatcher);
    FocusHandler ui(dispatcher);
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(ui);

    dispatcher.mouseButtonUpdate(MouseButton::Right, InputAction::Press, 0);
    ASSERT_EQ(scene.m_buttonPresses, 1u);
    ui.m_consumeButtons = true;
    dispatcher.mouseButtonUpdate(MouseButton::Right, InputAction::Release, 0);
    EXPECT_EQ(ui.m_buttonReleases, 1u);
    EXPECT_EQ(scene.m_buttonReleases, 1u);
    dispatcher.mouseButtonUpdate(MouseButton::Left, InputAction::Press, 0);
    EXPECT_EQ(ui.m_buttonPresses, 2u);
    EXPECT_EQ(scene.m_buttonPresses, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

