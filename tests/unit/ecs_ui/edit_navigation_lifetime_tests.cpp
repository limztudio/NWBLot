// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_navigation_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation_lifetime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditNavigationTestSupport;

class UiEditNavigationLifetimeTests : public UiEditNavigationHostTests{
protected:
    [[nodiscard]] bool seedPreferredColumn(const Ui::EditBoxOptions& options = {}){
        if(
            !m_navigationModel.setText("abcdef\nx\nabcdef") || !m_navigationModel.setSelection(5u, 5u)
            || !activateNavigation(options) || !key(Ui::InputKey::Down) || !navigationFrame(options)
        )
            return false;
        m_resolver.records.clear();
        m_actions.records.clear();
        return m_navigationModel.caret() == 8u && m_navigation.hasPreferredX() && m_navigation.preferredX() == 50.0f;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditNavigationLifetimeTests, NativePreeditSuppressesVerticalKeysAndFirstEscapeAllowsFreshNavigation){
    ASSERT_TRUE(seedPreferredColumn());
    ASSERT_EQ(m_textInput.preedit("IME", 0u, 3u), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(navigationFrame());
    ASSERT_TRUE(m_navigationModel.composition().active);
    UiEditModelSnapshot preedit(m_arena);
    preedit.capture(m_navigationModel);
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    m_resolver.records.clear();
    m_actions.records.clear();

    const Ui::InputKey::Enum keys[]{ Ui::InputKey::Up, Ui::InputKey::Down, Ui::InputKey::PageUp, Ui::InputKey::PageDown };
    for(const auto value : keys)
        ASSERT_TRUE(key(value));
    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_resolver.records.empty());
    EXPECT_TRUE(preedit.matches(m_navigationModel));
    EXPECT_TRUE(m_navigation.matches(preferred));
    EXPECT_TRUE(m_actions.records.empty());

    ASSERT_TRUE(key(Ui::InputKey::Escape));
    ASSERT_TRUE(key(Ui::InputKey::Down));
    ASSERT_TRUE(navigationFrame());
    EXPECT_FALSE(m_navigationModel.composition().active);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_EQ(m_navigationModel.revision(), preedit.revision);
    EXPECT_EQ(m_navigationModel.externalRevision(), preedit.externalRevision);
    EXPECT_EQ(m_navigationModel.anchor(), 10u);
    EXPECT_EQ(m_navigationModel.caret(), 10u);
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_FALSE(m_result.cancelled);
    EXPECT_TRUE(m_result.focused);
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_EQ(m_resolver.records.size(), 1u);
    EXPECT_EQ(m_resolver.records[0u].direction, Ui::EditNavigationDirection::Down);
    EXPECT_FALSE(m_resolver.records[0u].navigation.valid);
    EXPECT_EQ(m_resolver.records[0u].caret, 8u);
    EXPECT_FLOAT_EQ(m_navigation.preferredX(), 10.0f);
}

TEST_F(UiEditNavigationLifetimeTests, ReadOnlyAllowsVerticalSelectionWithoutNativeTextOwnership){
    Ui::EditBoxOptions readOnly;
    readOnly.readOnly = true;
    ASSERT_TRUE(seedPreferredColumn(readOnly));
    const u64 revision = m_navigationModel.revision();
    const u64 external = m_navigationModel.externalRevision();
    EXPECT_FALSE(m_textInput.activeSession().valid());

    ASSERT_TRUE(key(Ui::InputKey::Down, true));
    ASSERT_TRUE(navigationFrame(readOnly));
    EXPECT_TRUE(m_result.focused);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.textChanged);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_EQ(m_navigationModel.anchor(), 8u);
    EXPECT_EQ(m_navigationModel.caret(), 14u);
    EXPECT_EQ(m_navigationModel.revision(), revision);
    EXPECT_EQ(m_navigationModel.externalRevision(), external);
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_EQ(m_resolver.records.size(), 1u);
    EXPECT_TRUE(m_resolver.records[0u].navigation.valid);
    EXPECT_FLOAT_EQ(m_resolver.records[0u].navigation.preferredX, 50.0f);
    EXPECT_FLOAT_EQ(m_navigation.preferredX(), 50.0f);
}

TEST_F(UiEditNavigationLifetimeTests, DisableDiscardsQueuedNavigationAndResetsOnlyTheLentState){
    ASSERT_TRUE(seedPreferredColumn());
    UiEditModelSnapshot before(m_arena);
    before.capture(m_navigationModel);
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(key(Ui::InputKey::Down));
    ASSERT_TRUE(commitNative("late"));
    EXPECT_TRUE(m_navigation.matches(preferred));
    EXPECT_TRUE(before.matches(m_navigationModel));

    Ui::EditBoxOptions disabled;
    disabled.enabled = false;
    ASSERT_TRUE(navigationFrame(disabled));
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_FALSE(m_result.focused);
    EXPECT_TRUE(before.matches(m_navigationModel));
    EXPECT_TRUE(m_resolver.records.empty());
    EXPECT_FALSE(m_navigation.hasPreferredX());
    EXPECT_FALSE(m_navigation.matches(preferred));
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 1u);
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "abcdef\nx\nabcdef");
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(navigationFrame(disabled));
    EXPECT_FALSE(m_result.abandoned);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 1u);
    EXPECT_TRUE(m_resolver.records.empty());
}

TEST_F(UiEditNavigationLifetimeTests, BlurResetsPreferredColumnAtItsOrderedPositionBetweenVerticalKeys){
    ASSERT_TRUE(seedPreferredColumn());
    ASSERT_TRUE(m_navigation.setPreferredX(40.0f));
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    UiEditModelSnapshot before(m_arena);
    before.capture(m_navigationModel);
    ASSERT_TRUE(key(Ui::InputKey::Up));
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(focusNavigation());
    ASSERT_TRUE(key(Ui::InputKey::Down));
    EXPECT_TRUE(before.matches(m_navigationModel));
    EXPECT_TRUE(m_navigation.matches(preferred));

    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_result.blurred);
    EXPECT_TRUE(m_result.focused);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_EQ(m_navigationModel.revision(), before.revision);
    EXPECT_EQ(m_navigationModel.externalRevision(), before.externalRevision);
    EXPECT_EQ(m_navigationModel.anchor(), 8u);
    EXPECT_EQ(m_navigationModel.caret(), 8u);
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_EQ(m_actions.count(Ui::EditAction::Blur), 1u);
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_EQ(m_resolver.records[0u].direction, Ui::EditNavigationDirection::Up);
    EXPECT_EQ(m_resolver.records[0u].caret, 8u);
    EXPECT_TRUE(m_resolver.records[0u].navigation.valid);
    EXPECT_FLOAT_EQ(m_resolver.records[0u].navigation.preferredX, 40.0f);
    EXPECT_EQ(m_resolver.records[0u].result.committedByte, 4u);
    EXPECT_EQ(m_resolver.records[1u].direction, Ui::EditNavigationDirection::Down);
    EXPECT_EQ(m_resolver.records[1u].caret, 4u);
    EXPECT_FALSE(m_resolver.records[1u].navigation.valid);
    EXPECT_EQ(m_resolver.records[1u].result.committedByte, 8u);
    EXPECT_FLOAT_EQ(m_navigation.preferredX(), 40.0f);
}


TEST_F(UiEditNavigationLifetimeTests, BlurPreservesDraftBeforeFreshFocusEpochNavigation){
    ASSERT_TRUE(seedPreferredColumn());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    ASSERT_TRUE(commitNative("!"));
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(focusNavigation());
    ASSERT_TRUE(key(Ui::InputKey::Down));
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_TRUE(m_navigation.matches(preferred));
    EXPECT_TRUE(m_actions.records.empty());

    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_result.blurred);
    EXPECT_TRUE(m_result.focused);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx!\nabcdef");
    EXPECT_EQ(m_navigationModel.anchor(), 12u);
    EXPECT_EQ(m_navigationModel.caret(), 12u);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Blur), 1u);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Cancel), 0u);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 0u);
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "abcdef\nx!\nabcdef");
    ASSERT_EQ(m_resolver.records.size(), 1u);
    EXPECT_EQ(m_resolver.records[0u].text, "abcdef\nx!\nabcdef");
    EXPECT_EQ(m_resolver.records[0u].caret, 9u);
    EXPECT_FALSE(m_resolver.records[0u].navigation.valid);
    EXPECT_FLOAT_EQ(m_navigation.preferredX(), 20.0f);
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    ASSERT_TRUE(m_navigationModel.undo());
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_EQ(m_navigationModel.caret(), 8u);
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationLifetimeTests, OrdinaryCancelRetiresPreferredColumnWhilePreservingTheDraft){
    ASSERT_TRUE(seedPreferredColumn());
    UiEditModelSnapshot before(m_arena);
    before.capture(m_navigationModel);
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(key(Ui::InputKey::Escape));
    EXPECT_TRUE(m_navigation.matches(preferred));
    EXPECT_TRUE(before.matches(m_navigationModel));

    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_FALSE(m_result.focused);
    EXPECT_TRUE(before.matches(m_navigationModel));
    EXPECT_FALSE(m_navigation.snapshot().valid);
    EXPECT_FALSE(m_navigation.matches(preferred));
    EXPECT_TRUE(m_resolver.records.empty());
    EXPECT_EQ(m_actions.count(Ui::EditAction::Cancel), 1u);
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "abcdef\nx\nabcdef");
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}


TEST_F(UiEditNavigationLifetimeTests, OldEpochCancelPreservesDraftAndLaterRefocusNavigation){
    ASSERT_TRUE(seedPreferredColumn());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    const u64 revision = m_navigationModel.revision();
    ASSERT_TRUE(key(Ui::InputKey::Escape));
    ASSERT_TRUE(commitNative("late"));
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(focusNavigation());
    ASSERT_TRUE(key(Ui::InputKey::Up));
    EXPECT_TRUE(m_navigation.matches(preferred));
    EXPECT_EQ(m_navigationModel.caret(), 8u);

    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_TRUE(m_result.focused);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_EQ(m_navigationModel.revision(), revision);
    EXPECT_EQ(m_navigationModel.anchor(), 1u);
    EXPECT_EQ(m_navigationModel.caret(), 1u);
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_EQ(m_actions.count(Ui::EditAction::Cancel), 1u);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Blur), 0u);
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "abcdef\nx\nabcdef");
    ASSERT_EQ(m_resolver.records.size(), 1u);
    EXPECT_FALSE(m_resolver.records[0u].navigation.valid);
    EXPECT_EQ(m_resolver.records[0u].caret, 8u);
    EXPECT_FLOAT_EQ(m_navigation.preferredX(), 10.0f);
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditNavigationLifetimeTests, OmissionLeavesCallerStateExactUntilRedeclarationAbandonsOwnership){
    ASSERT_TRUE(seedPreferredColumn());
    UiEditModelSnapshot before(m_arena);
    before.capture(m_navigationModel);
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    const u64 declaration = m_widget.declarationGeneration;
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(commitNative("late"));
    ASSERT_TRUE(emptyFrame());
    EXPECT_TRUE(m_navigation.matches(preferred));
    EXPECT_TRUE(before.matches(m_navigationModel));
    EXPECT_TRUE(m_actions.records.empty());
    EXPECT_TRUE(m_resolver.records.empty());
    EXPECT_FALSE(m_textInput.activeSession().valid());

    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_NE(m_widget.declarationGeneration, declaration);
    EXPECT_TRUE(before.matches(m_navigationModel));
    EXPECT_FALSE(m_navigation.hasPreferredX());
    EXPECT_FALSE(m_navigation.matches(preferred));
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 1u);
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "abcdef\nx\nabcdef");
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(navigationFrame());
    EXPECT_FALSE(m_result.abandoned);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 1u);
}

TEST_F(UiEditNavigationLifetimeTests, HostResetCannotTouchUnlentDraftOrPreferredColumn){
    ASSERT_TRUE(seedPreferredColumn());
    UiEditModelSnapshot before(m_arena);
    before.capture(m_navigationModel);
    const Ui::EditNavigationSnapshot preferred = m_navigation.snapshot();
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    m_host.reset();
    EXPECT_TRUE(before.matches(m_navigationModel));
    EXPECT_TRUE(m_navigation.matches(preferred));
    EXPECT_TRUE(m_actions.records.empty());
    EXPECT_TRUE(m_resolver.records.empty());
    EXPECT_FALSE(m_textInput.activeSession().valid());

    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_TRUE(before.matches(m_navigationModel));
    EXPECT_FALSE(m_navigation.hasPreferredX());
    EXPECT_FALSE(m_navigation.matches(preferred));
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 1u);
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditNavigationLifetimeTests, RebindingResetsOnlyTheNewlyLentModelAndNavigationOwnership){
    ASSERT_TRUE(seedPreferredColumn());
    UiEditModelSnapshot original(m_arena);
    original.capture(m_navigationModel);
    const Ui::EditNavigationSnapshot originalPreferred = m_navigation.snapshot();
    Ui::EditModel replacement(m_arena, {}, Ui::EditTextMode::Multiline);
    ASSERT_TRUE(replacement.setText("zz\nabc"));
    ASSERT_TRUE(replacement.setSelection(1u, 1u));
    Ui::EditNavigationState replacementNavigation;
    ASSERT_TRUE(replacementNavigation.setPreferredX(70.0f));
    const Ui::EditNavigationSnapshot replacementPreferred = replacementNavigation.snapshot();
    UiEditModelSnapshot replacementBefore(m_arena);
    replacementBefore.capture(replacement);
    ASSERT_TRUE(key(Ui::InputKey::Down));
    EXPECT_TRUE(m_navigation.matches(originalPreferred));
    EXPECT_TRUE(replacementNavigation.matches(replacementPreferred));

    ASSERT_TRUE(prepareNavigationModel(replacement, replacementNavigation));
    ASSERT_TRUE(commit());
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_TRUE(original.matches(m_navigationModel));
    EXPECT_TRUE(replacementBefore.matches(replacement));
    EXPECT_TRUE(m_navigation.matches(originalPreferred));
    EXPECT_FALSE(replacementNavigation.hasPreferredX());
    EXPECT_FALSE(replacementNavigation.matches(replacementPreferred));
    EXPECT_TRUE(m_resolver.records.empty());
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 1u);
    ASSERT_TRUE(key(Ui::InputKey::Down));
    ASSERT_TRUE(prepareNavigationModel(replacement, replacementNavigation));
    ASSERT_TRUE(commit());
    EXPECT_EQ(replacement.caret(), 4u);
    ASSERT_EQ(m_resolver.records.size(), 1u);
    EXPECT_EQ(m_resolver.records[0u].text, "zz\nabc");
    EXPECT_FALSE(m_resolver.records[0u].navigation.valid);
    EXPECT_FLOAT_EQ(replacementNavigation.preferredX(), 10.0f);
    EXPECT_TRUE(m_navigation.matches(originalPreferred));

    const Ui::EditNavigationSnapshot replacementAccepted = replacementNavigation.snapshot();
    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_TRUE(original.matches(m_navigationModel));
    EXPECT_FALSE(m_navigation.hasPreferredX());
    EXPECT_TRUE(replacementNavigation.matches(replacementAccepted));
    EXPECT_EQ(replacement.text(), "zz\nabc");
    EXPECT_EQ(replacement.caret(), 4u);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Abandon), 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

