// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "horizontal_navigation_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiHorizontalNavigationTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(HorizontalNavigationFixture, CapabilityRemovalRetiresHorizontalIntentionsWhileVerticalHoldSurvives){
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Left).keyboardConsumed);
    EXPECT_TRUE(keyDown(Core::Key::Up).keyboardConsumed);
    press(Core::Key::Right);
    ASSERT_EQ(m_router.controlActions().size(), 3u);
    m_targets[0u].horizontalNavigation = false;
    ASSERT_TRUE(publish());
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    EXPECT_EQ(m_router.controlActions()[0u].kind, ControlActionKind::Up);
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(keyDown(Core::Key::Up, true).keyboardConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 2u);
    ControlAction action;
    const auto actionResult1 = take();
    ASSERT_TRUE(actionResult1);
    action = *actionResult1;
    EXPECT_EQ(action.kind, ControlActionKind::Up);
    const auto actionResult2 = take();
    ASSERT_TRUE(actionResult2);
    action = *actionResult2;
    EXPECT_EQ(action.kind, ControlActionKind::Up);
    m_targets[0u].horizontalNavigation = true;
    ASSERT_TRUE(publish());
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyDown(Core::Key::Up, true).keyboardConsumed);
    const auto actionResult3 = take();
    ASSERT_TRUE(actionResult3);
    action = *actionResult3;
    EXPECT_EQ(action.kind, ControlActionKind::Up);
    EXPECT_TRUE(keyUp(Core::Key::Left).keyboardConsumed);
    EXPECT_TRUE(keyUp(Core::Key::Up).keyboardConsumed);
    press(Core::Key::Left);
    const auto actionResult4 = take();
    ASSERT_TRUE(actionResult4);
    action = *actionResult4;
    EXPECT_EQ(action.kind, ControlActionKind::Left);
}

TEST_F(HorizontalNavigationFixture, EnablingCapabilityCannotAdoptAnAlreadyHeldLocalKey){
    m_targets[0u].horizontalNavigation = false;
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    m_targets[0u].horizontalNavigation = true;
    ASSERT_TRUE(publish());
    EXPECT_TRUE(keyDown(Core::Key::Right, true).keyboardConsumed);
    EXPECT_TRUE(keyDown(Core::Key::Right).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Right).keyboardConsumed);
    press(Core::Key::Right);
    ControlAction action;
    const auto actionResult = take();
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.kind, ControlActionKind::Right);
}

TEST_F(HorizontalNavigationFixture, FocusTransferCannotRedirectHeldHorizontalKeyToAnotherHost){
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right).keyboardConsumed);
    ControlAction action;
    const auto actionResult1 = take();
    ASSERT_TRUE(actionResult1);
    action = *actionResult1;
    EXPECT_EQ(action.id.target, m_targets[0u].id);
    focusTarget(1u);
    EXPECT_TRUE(keyDown(Core::Key::Right, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Right).keyboardConsumed);
    press(Core::Key::Right);
    const auto actionResult2 = take(1u);
    ASSERT_TRUE(actionResult2);
    action = *actionResult2;
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_EQ(action.id.target, m_targets[1u].id);
    EXPECT_EQ(action.source, m_targets[1u].id);
}

TEST_F(HorizontalNavigationFixture, RestoringOriginalControlTokenCannotReviveRetiredHorizontalHold){
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Left).keyboardConsumed);
    const ControlToken original = m_targets[0u].control;
    ++m_targets[0u].control.contentRevision;
    ASSERT_TRUE(publish());
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.controlActions().empty());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    m_targets[0u].control = original;
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Left).keyboardConsumed);
    press(Core::Key::Left);
    ControlAction action;
    const auto actionResult = take();
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.kind, ControlActionKind::Left);
    EXPECT_EQ(action.control, original);
}

TEST_F(HorizontalNavigationFixture, RestoringOriginalDeclarationCannotReviveRetiredHorizontalHold){
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right).keyboardConsumed);
    const u64 original = m_targets[0u].declarationGeneration;
    ++m_targets[0u].declarationGeneration;
    ASSERT_TRUE(publish());
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.controlActions().empty());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    m_targets[0u].declarationGeneration = original;
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Right).keyboardConsumed);
    press(Core::Key::Right);
    ControlAction action;
    const auto actionResult = take();
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_EQ(action.id.declarationGeneration, original);
}

TEST_F(HorizontalNavigationFixture, DisabledClippedOrOmittedHostCannotResumeItsPreviousHorizontalHold){
    const HitTarget original = m_targets[0u];
    for(usize variant = 0u; variant < 3u; ++variant){
        m_targets[0u] = original;
        ASSERT_TRUE(publish(1u));
        focusTarget();
        EXPECT_TRUE(keyDown(Core::Key::Left).keyboardConsumed);
        ASSERT_EQ(m_router.controlActions().size(), 1u);
        switch(variant){
        case 0u: m_targets[0u].enabled = false; break;
        case 1u: m_targets[0u].clip = { 200.0f, 0.0f, 10.0f, 100.0f }; break;
        }
        ASSERT_TRUE(publish(variant == 2u ? 0u : 1u));
        EXPECT_FALSE(m_router.focus().valid());
        EXPECT_TRUE(m_router.controlActions().empty());
        m_targets[0u] = original;
        ASSERT_TRUE(publish(1u));
        focusTarget();
        EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
        EXPECT_TRUE(m_router.controlActions().empty());
        EXPECT_TRUE(keyUp(Core::Key::Left).keyboardConsumed);
        press(Core::Key::Left);
        ControlAction action;
        const auto actionResult = take();
        ASSERT_TRUE(actionResult);
        action = *actionResult;
        EXPECT_EQ(action.kind, ControlActionKind::Left);
    }
}

TEST_F(HorizontalNavigationFixture, ClosingAndReopeningPopupRequiresFreshHorizontalPress){
    PopupScope popup = Popup(m_targets[0u]);
    m_targets[0u].popup = popup.token;
    m_targets[0u].layer = popup.layer;
    ASSERT_TRUE(publish(1u, &popup, 1u));
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right).keyboardConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    m_router.closePopup(popup.token);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_FALSE(m_router.focus().valid());
    ASSERT_TRUE(publish(1u, &popup, 1u));
    EXPECT_FALSE(m_router.focus().valid());
    ++popup.token.openGeneration;
    m_targets[0u].popup = popup.token;
    ASSERT_TRUE(publish(1u, &popup, 1u));
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Right).keyboardConsumed);
    press(Core::Key::Right);
    ControlAction action;
    const auto actionResult = take();
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_EQ(action.popup, popup.token);
}

TEST_F(HorizontalNavigationFixture, PopupMaskCannotRedirectOrReviveLowerHostHorizontalHold){
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Left).keyboardConsumed);
    PopupScope popup = Popup(m_targets[1u]);
    m_targets[1u].popup = popup.token;
    m_targets[1u].layer = popup.layer;
    ASSERT_TRUE(publish(2u, &popup, 1u));
    EXPECT_TRUE(m_router.controlActions().empty());
    focusTarget(1u);
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    m_targets[1u].popup = {};
    m_targets[1u].layer = 0u;
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Left).keyboardConsumed);
    press(Core::Key::Left);
    ControlAction action;
    const auto actionResult = take();
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.kind, ControlActionKind::Left);
    EXPECT_TRUE(action.popup.empty());
}

TEST_F(HorizontalNavigationFixture, PreservedTextFenceClearsHorizontalCapabilityAndRetainsMainCapture){
    m_targets[0u].textEditable = true;
    m_targets[0u].scrollable = true;
    ASSERT_TRUE(publish(1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    EXPECT_TRUE(keyDown(Core::Key::Left).keyboardConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    m_router.fenceControl(m_targets[0u].id, m_targets[0u].declarationGeneration, m_targets[0u].control);
    EXPECT_TRUE(m_router.targets()[0u].horizontalNavigation);
    EXPECT_EQ(m_router.controlActions().size(), 1u);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    ++m_targets[0u].control.contentRevision;
    m_router.fenceControl(m_targets[0u].id, m_targets[0u].declarationGeneration, m_targets[0u].control);
    ASSERT_EQ(m_router.targets().size(), 1u);
    EXPECT_FALSE(m_router.targets()[0u].horizontalNavigation);
    EXPECT_FALSE(m_router.targets()[0u].navigable);
    EXPECT_TRUE(m_router.targets()[0u].enabled);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(m_router.controlActions().empty());
    ++m_targets[0u].control.contentRevision;
    m_router.fenceControl(m_targets[0u].id, m_targets[0u].declarationGeneration, m_targets[0u].control);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_FALSE(m_router.targets()[0u].horizontalNavigation);
    ASSERT_TRUE(publish(1u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Left).keyboardConsumed);
    press(Core::Key::Right);
    ControlAction action;
    const auto actionResult = take();
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_EQ(action.control, m_targets[0u].control);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 10.0f, 10.0f } }).pointerConsumed);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

