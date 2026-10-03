// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "horizontal_navigation_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiHorizontalNavigationTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(HorizontalNavigationFixture, OptedInHostPublishesOrderedHorizontalAndExistingKeyIntentions){
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Left).keyboardConsumed);
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    press(Core::Key::Right);
    press(Core::Key::Down);
    press(Core::Key::PageUp);
    press(Core::Key::Home);
    press(Core::Key::End);
    EXPECT_TRUE(keyDown(Core::Key::Enter).keyboardConsumed);
    EXPECT_TRUE(keyDown(Core::Key::Enter, true).keyboardConsumed);
    EXPECT_TRUE(keyUp(Core::Key::Enter).keyboardConsumed);
    EXPECT_TRUE(keyUp(Core::Key::Left).keyboardConsumed);
    const Array<ControlActionKind::Enum, 8u> kinds{
        ControlActionKind::Left, ControlActionKind::Left, ControlActionKind::Right, ControlActionKind::Down,
        ControlActionKind::PageUp, ControlActionKind::Home, ControlActionKind::End, ControlActionKind::Submit
    };
    ASSERT_EQ(m_router.controlActions().size(), kinds.size());
    u64 previous = 0u;
    for(const auto kind : kinds){
        ControlAction action;
        ASSERT_TRUE(take(action));
        EXPECT_EQ(action.kind, kind);
        EXPECT_EQ(action.id.target, m_targets[0u].id);
        EXPECT_EQ(action.id.declarationGeneration, m_targets[0u].declarationGeneration);
        EXPECT_EQ(action.id.layoutGeneration, 1u);
        EXPECT_GT(action.id.sequence, previous);
        EXPECT_EQ(action.source, m_targets[0u].id);
        EXPECT_EQ(action.sourceDeclarationGeneration, m_targets[0u].declarationGeneration);
        EXPECT_EQ(action.sourceControl, m_targets[0u].control);
        EXPECT_EQ(action.control, m_targets[0u].control);
        EXPECT_EQ(action.pageRows, 4u);
        previous = action.id.sequence;
    }
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(HorizontalNavigationFixture, VerticalOnlyNavigableHostKeepsHorizontalKeysLocalAndVerticalKeysActive){
    HitTarget host;
    host.id = m_targets[0u].id;
    host.declarationGeneration = m_targets[0u].declarationGeneration;
    host.rectangle = m_targets[0u].rectangle;
    host.clip = m_targets[0u].clip;
    host.focusable = true;
    host.control = m_targets[0u].control;
    host.navigable = true;
    m_targets[0u] = host;
    ASSERT_TRUE(publish(1u));
    focusTarget();
    press(Core::Key::Left);
    press(Core::Key::Right);
    EXPECT_TRUE(m_router.controlActions().empty());
    press(Core::Key::Up);
    press(Core::Key::Down);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Up);
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Down);
    EXPECT_FALSE(take(action));
}

TEST_F(HorizontalNavigationFixture, CapabilityUsesAcceptedCopiedTargetsUntilSuccessfulPublication){
    ASSERT_TRUE(publish());
    focusTarget();
    m_targets[0u].horizontalNavigation = false;
    press(Core::Key::Left);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    EXPECT_TRUE(m_router.targets()[0u].horizontalNavigation);
    const u64 sequence = m_router.controlActions()[0u].id.sequence;
    ASSERT_TRUE(publish());
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    press(Core::Key::Right);
    EXPECT_TRUE(m_router.controlActions().empty());
    m_targets[0u].horizontalNavigation = true;
    ASSERT_TRUE(publish());
    press(Core::Key::Right);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_EQ(action.id.layoutGeneration, 3u);
    EXPECT_GT(action.id.sequence, sequence);
}

TEST_F(HorizontalNavigationFixture, DelegatedEditorRetainsCaretKeysWhileVerticalAndEnterUseOwner){
    bindEditor();
    ASSERT_TRUE(publish(3u));
    focusTarget(2u);
    const Array<Core::Key::Enum, 6u> localKeys{
        Core::Key::Left, Core::Key::Right, Core::Key::Home, Core::Key::End, Core::Key::Space, Core::Key::Backspace
    };
    for(const auto key : localKeys){
        press(key);
        EXPECT_TRUE(m_router.controlActions().empty());
    }
    EXPECT_TRUE(keyDown(Core::Key::Left).keyboardConsumed);
    EXPECT_TRUE(keyDown(Core::Key::Left, true).keyboardConsumed);
    EXPECT_TRUE(m_router.ownsKey(Core::Key::Left));
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(keyUp(Core::Key::Left).keyboardConsumed);
    const Array<Core::Key::Enum, 5u> keys{
        Core::Key::Up, Core::Key::Down, Core::Key::PageUp, Core::Key::PageDown, Core::Key::Enter
    };
    const Array<ControlActionKind::Enum, 5u> kinds{
        ControlActionKind::Up, ControlActionKind::Down, ControlActionKind::PageUp,
        ControlActionKind::PageDown, ControlActionKind::Submit
    };
    for(usize index = 0u; index < keys.size(); ++index){
        press(keys[index]);
        ControlAction action;
        ASSERT_TRUE(take(action));
        EXPECT_EQ(action.kind, kinds[index]);
        EXPECT_EQ(action.id.target, m_targets[0u].id);
        EXPECT_EQ(action.source, m_targets[2u].id);
        EXPECT_EQ(action.sourceDeclarationGeneration, m_targets[2u].declarationGeneration);
        EXPECT_EQ(action.sourceControl, m_targets[2u].control);
        EXPECT_EQ(m_router.focus(), m_targets[2u].id);
    }
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(HorizontalNavigationFixture, InvalidHorizontalHostPublicationPreservesAcceptedFocusAndHeldAction){
    ASSERT_TRUE(publish());
    focusTarget();
    EXPECT_TRUE(keyDown(Core::Key::Right).keyboardConsumed);
    const HitTarget accepted = m_targets[0u];
    for(usize variant = 0u; variant < 4u; ++variant){
        m_targets[0u] = accepted;
        switch(variant){
        case 0u: m_targets[0u].navigable = false; break;
        case 1u: m_targets[0u].focusable = false; break;
        case 2u: m_targets[0u].control = {}; break;
        case 3u:
            m_targets[0u].owner = m_targets[1u].id;
            m_targets[0u].ownerDeclarationGeneration = m_targets[1u].declarationGeneration;
            break;
        }
        EXPECT_FALSE(publish());
        EXPECT_EQ(m_router.layoutGeneration(), 1u);
        EXPECT_EQ(m_router.focus(), accepted.id);
        ASSERT_EQ(m_router.controlActions().size(), 1u);
        EXPECT_TRUE(m_router.targets()[0u].horizontalNavigation);
        EXPECT_TRUE(m_router.ownsKey(Core::Key::Right));
    }
    m_targets[0u] = accepted;
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_EQ(action.control, accepted.control);
    EXPECT_TRUE(keyDown(Core::Key::Right, true).keyboardConsumed);
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_TRUE(keyUp(Core::Key::Right).keyboardConsumed);
}

TEST_F(HorizontalNavigationFixture, OwnedPartsRejectHorizontalCapabilityWithoutReplacingAcceptedActions){
    ASSERT_TRUE(publish());
    focusTarget();
    press(Core::Key::Right);
    m_targets[2u] = Part(m_targets[0u]);
    m_targets[2u].horizontalNavigation = true;
    EXPECT_FALSE(publish(3u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    m_targets[2u].horizontalNavigation = false;
    ASSERT_TRUE(publish(3u));
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 30.0f, 30.0f } }).pointerConsumed);
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Activate);
    EXPECT_EQ(action.source, m_targets[2u].id);
    EXPECT_EQ(action.value, 91u);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

