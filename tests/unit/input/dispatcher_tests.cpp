// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "dispatcher_fixture.h"


#include <gtest/gtest.h>



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputDispatcherTestsImpl{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class ThrowingHandler final : public IInputEventHandler{
public:
    static constexpr u32 s_Failure = 0xD15A7C4u;


public:
    ThrowingHandler(InputDispatcher& dispatcher, const b8 nestedDispatch)noexcept
        : m_dispatcher(dispatcher)
        , m_nestedDispatch(nestedDispatch)
    {}


public:
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32, i32, i32, i32)override{
        ++m_keyUpdates;
        if(m_nestedDispatch)
            m_dispatcher.pointerLeave();
        throw s_Failure;
    }

    virtual void pointerLeave()override{ throw s_Failure; }


public:
    InputDispatcher& m_dispatcher;
    usize m_keyUpdates = 0u;
    const b8 m_nestedDispatch;
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

    dispatcher.windowFocusUpdate(false);
    EXPECT_FALSE(dispatcher.windowFocused());
    dispatcher.windowFocusUpdate(false);
    dispatcher.windowFocusUpdate(true);
    dispatcher.windowFocusUpdate(true);
    EXPECT_TRUE(dispatcher.windowFocused());
    for(const auto handler : { MakeNotNull(&scene), MakeNotNull(&legacyUi), MakeNotNull(&customUi) }){
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

TEST(InputDispatcher, KeyboardTextPolicyComesOnlyFromTheFirstConsumingHandler){
    InputDispatcher dispatcher;
    FocusHandler lower(dispatcher);
    FocusHandler higher(dispatcher);
    lower.m_consumeKeys = true;
    lower.m_blockText = true;
    higher.m_consumeKeys = true;
    dispatcher.addHandlerToFront(lower);
    dispatcher.addHandlerToBack(higher);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(30));
    EXPECT_EQ(higher.m_textPolicyQueries, 1u);
    EXPECT_EQ(lower.m_textPolicyQueries, 0u);

    higher.m_consumeKeys = false;
    higher.m_blockText = true;
    lower.m_blockText = false;
    dispatcher.keyboardUpdate(Key::B, 48, InputAction::Press, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(48));
    EXPECT_EQ(higher.m_textPolicyQueries, 1u);
    EXPECT_EQ(lower.m_textPolicyQueries, 1u);
    lower.m_blockText = true;
    dispatcher.keyboardUpdate(Key::B, 48, InputAction::Repeat, 0);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(48));
    lower.m_consumeKeys = false;
    dispatcher.keyboardUpdate(Key::B, 48, InputAction::Repeat, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(48));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked());
    EXPECT_EQ(higher.m_textPolicyQueries, 1u);
    EXPECT_EQ(lower.m_textPolicyQueries, 2u);
}

TEST(InputDispatcher, KeyboardTextScancodesRetainIndependentQueuedPoliciesAcrossReleases){
    InputDispatcher dispatcher;
    FocusHandler handler(dispatcher);
    handler.m_consumeKeys = true;
    handler.m_blockText = true;
    dispatcher.addHandlerToBack(handler);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    handler.m_blockText = false;
    dispatcher.keyboardUpdate(Key::B, 48, InputAction::Press, 0);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(30));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(48));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked());
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Release, 0);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(30));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked());
    EXPECT_EQ(handler.m_textPolicyQueries, 2u);

    handler.m_blockText = true;
    dispatcher.keyboardUpdate(Key::B, 48, InputAction::Repeat, 0);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(48));
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(-1));
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(512));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(511));
    handler.m_blockText = false;
    dispatcher.keyboardUpdate(Key::C, 512, InputAction::Press, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(512));
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(30));
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(48));
    dispatcher.keyboardUpdate(Key::Unknown, 48, InputAction::Press, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(48));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked());
}

TEST(InputDispatcher, FocusLossClearsKeyboardTextPoliciesAndNestedRegainCannotReviveThem){
    InputDispatcher dispatcher;
    FocusHandler handler(dispatcher);
    handler.m_consumeKeys = true;
    handler.m_blockText = true;
    dispatcher.addHandlerToBack(handler);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    dispatcher.keyboardUpdate(Key::B, 48, InputAction::Press, 0);
    ASSERT_TRUE(dispatcher.keyboardTextBlocked(30));
    ASSERT_TRUE(dispatcher.keyboardTextBlocked(48));
    dispatcher.windowFocusUpdate(false);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(30));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(48));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked());
    dispatcher.keyboardUpdate(Key::B, 48, InputAction::Repeat, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(48));
    dispatcher.windowFocusUpdate(true);
    handler.m_loseFocusOnPress = true;
    handler.m_regainFocusOnPress = true;
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_TRUE(dispatcher.windowFocused());
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(30));
    EXPECT_FALSE(dispatcher.keyboardTextBlocked());
}

TEST(InputDispatcher, RemovingTheConsumingHandlerPreservesOnlyItsQueuedCharacterPolicy){
    InputDispatcher dispatcher;
    FocusHandler scene(dispatcher);
    FocusHandler transient(dispatcher);
    scene.m_consumeKeys = true;
    transient.m_consumeKeys = true;
    transient.m_blockText = true;
    transient.m_removeSelfOnPress = true;
    dispatcher.addHandlerToFront(scene);
    dispatcher.addHandlerToBack(transient);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Press, 0);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(30));
    EXPECT_EQ(transient.m_textPolicyQueries, 1u);
    EXPECT_EQ(scene.m_keyPresses, 0u);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Release, 0);
    EXPECT_TRUE(dispatcher.keyboardTextBlocked(30));
    EXPECT_EQ(transient.m_keyReleases, 0u);
    EXPECT_EQ(scene.m_keyReleases, 1u);
    dispatcher.keyboardUpdate(Key::A, 30, InputAction::Repeat, 0);
    EXPECT_FALSE(dispatcher.keyboardTextBlocked(30));
    EXPECT_EQ(transient.m_textPolicyQueries, 1u);
    EXPECT_EQ(scene.m_textPolicyQueries, 1u);
}

TEST(InputDispatcher, HandlerExceptionsRestoreDirectAndNestedDispatchDepth){
    for(const b8 nestedDispatch : { false, true }){
        InputDispatcher dispatcher;
        ThrowingHandler throwing(dispatcher, nestedDispatch);
        FocusHandler added(dispatcher);
        dispatcher.addHandlerToBack(throwing);

        EXPECT_THROW(dispatcher.keyboardUpdate(Key::W, 0, InputAction::Press, 0), u32);
        EXPECT_EQ(throwing.m_keyUpdates, 1u);
        dispatcher.removeHandler(throwing);
        dispatcher.addHandlerToBack(added);
        dispatcher.keyboardUpdate(Key::W, 0, InputAction::Press, 0);
        EXPECT_EQ(throwing.m_keyUpdates, 1u);
        EXPECT_EQ(added.m_keyPresses, 1u);
    }
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

