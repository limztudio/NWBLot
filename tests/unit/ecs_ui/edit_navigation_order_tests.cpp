// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_navigation_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation_order_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditNavigationTestSupport;

TEST_F(UiEditNavigationHostTests, CopiedCommitDownAndCommitResolveTheCurrentEventPosition){
    ASSERT_TRUE(m_navigationModel.setText("ab\ncdef\nxy"));
    ASSERT_TRUE(m_navigationModel.setSelection(1u, 1u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(commitNative("Q"));
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(commitNative("!"));
    EXPECT_EQ(m_navigationModel.text(), "ab\ncdef\nxy");
    EXPECT_EQ(m_navigationModel.caret(), 1u);
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 1u);
    const auto& record = m_resolver.records.front();
    EXPECT_EQ(record.text, "aQb\ncdef\nxy");
    EXPECT_EQ(record.caret, 2u);
    EXPECT_EQ(record.direction, Ui::EditNavigationDirection::Down);
    EXPECT_FALSE(record.navigation.valid);
    EXPECT_FLOAT_EQ(record.result.preferredX, 20.0f);
    EXPECT_EQ(record.result.committedByte, 6u);
    EXPECT_EQ(m_navigationModel.text(), "aQb\ncd!ef\nxy");
    EXPECT_EQ(m_navigationModel.caret(), 7u);
    EXPECT_TRUE(m_result.textChanged);
    EXPECT_TRUE(m_result.selectionChanged);
    ASSERT_TRUE(m_navigationModel.undo());
    EXPECT_EQ(m_navigationModel.text(), "aQb\ncdef\nxy");
    ASSERT_TRUE(m_navigationModel.undo());
    EXPECT_EQ(m_navigationModel.text(), "ab\ncdef\nxy");
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, RepeatedVerticalMovementPreservesTheColumnAcrossShortLines){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nx\nabcdef"));
    ASSERT_TRUE(m_navigationModel.setSelection(5u, 5u));
    ASSERT_TRUE(activateNavigation());
    const u64 revision = m_navigationModel.revision();
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Down, .shift = true }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Down, .shift = true, .repeat = true }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Down, .shift = true }));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_EQ(m_resolver.records[0u].result.committedByte, 8u);
    EXPECT_EQ(m_resolver.records[1u].result.committedByte, 14u);
    EXPECT_TRUE(m_resolver.records[1u].navigation.valid);
    EXPECT_FLOAT_EQ(m_resolver.records[1u].navigation.preferredX, 50.0f);
    EXPECT_EQ(m_navigationModel.anchor(), 5u);
    EXPECT_EQ(m_navigationModel.caret(), 14u);
    ASSERT_TRUE(key(Core::Key::Up));
    ASSERT_TRUE(key(Core::Key::Up));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 4u);
    EXPECT_EQ(m_resolver.records[2u].result.committedByte, 8u);
    EXPECT_EQ(m_resolver.records[3u].result.committedByte, 5u);
    EXPECT_FLOAT_EQ(m_resolver.records[3u].navigation.preferredX, 50.0f);
    EXPECT_EQ(m_navigationModel.anchor(), 5u);
    EXPECT_EQ(m_navigationModel.caret(), 5u);
    EXPECT_EQ(m_navigationModel.revision(), revision);
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, AcceptedEndpointNoOpResetsThePreferredColumn){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nx\nabcdef"));
    ASSERT_TRUE(m_navigationModel.setSelection(5u, 5u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_navigationModel.caret(), 8u);
    const u64 selectionGeneration = m_navigationModel.selectionGeneration();
    ASSERT_TRUE(key(Core::Key::End));
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_GT(m_resolver.records.back().selectionGeneration, selectionGeneration);
    EXPECT_FALSE(m_resolver.records.back().navigation.valid);
    EXPECT_FLOAT_EQ(m_resolver.records.back().result.preferredX, 10.0f);
    EXPECT_EQ(m_navigationModel.caret(), 10u);
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, CopyAndUnrewrittenSubmitPreserveThePreferredColumn){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nx\nabcdef"));
    ASSERT_TRUE(m_navigationModel.setSelection(5u, 5u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(key(Core::Key::Down, true));
    ASSERT_TRUE(navigationFrame());
    ASSERT_TRUE(key(Core::Key::C, false, true));
    ASSERT_TRUE(key(Core::Key::Enter, false, true));
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_TRUE(m_resolver.records.back().navigation.valid);
    EXPECT_FLOAT_EQ(m_resolver.records.back().navigation.preferredX, 50.0f);
    EXPECT_EQ(m_navigationModel.caret(), 14u);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_TRUE(m_result.submitted);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Submit), 1u);
    EXPECT_FALSE(m_navigationModel.canUndo());
    ASSERT_TRUE(m_clipboard.pump());
    EXPECT_EQ(m_clipboard.startedOperation, ClipboardOperation::WriteText);
    EXPECT_EQ(m_clipboard.startedText, "f\nx");
}

TEST_F(UiEditNavigationHostTests, AcceptedNativeInsertionReseedsTheNextVerticalMove){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nx\nabcdef"));
    ASSERT_TRUE(m_navigationModel.setSelection(5u, 5u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    ASSERT_TRUE(commitNative("z"));
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_FALSE(m_resolver.records.back().navigation.valid);
    EXPECT_EQ(m_resolver.records.back().text, "abcdef\nxz\nabcdef");
    EXPECT_FLOAT_EQ(m_resolver.records.back().result.preferredX, 20.0f);
    EXPECT_EQ(m_navigationModel.caret(), 12u);
    ASSERT_TRUE(m_navigationModel.undo());
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, AppliedDelayedPasteResetsTheColumnAtItsCompletionLoan){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nx\nabcdef"));
    ASSERT_TRUE(m_navigationModel.setSelection(5u, 5u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Core::Key::V, false, true));
    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_navigation.snapshot().valid);
    EXPECT_FLOAT_EQ(m_navigation.snapshot().preferredX, 50.0f);
    ASSERT_TRUE(m_clipboard.pump());
    const ClipboardRequestToken token = m_clipboard.startedToken;
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(m_clipboard.deliver(token, ClipboardStatus::Success, "zz"));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nxzz\nabcdef");
    EXPECT_FALSE(m_navigation.snapshot().valid);
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_FALSE(m_resolver.records.back().navigation.valid);
    EXPECT_FLOAT_EQ(m_resolver.records.back().result.preferredX, 30.0f);
    EXPECT_EQ(m_navigationModel.caret(), 14u);
}

TEST_F(UiEditNavigationHostTests, FailedAndCancelledPastePreserveTheAcceptedColumn){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nx\nabcdef"));
    ASSERT_TRUE(m_navigationModel.setSelection(5u, 5u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Core::Key::V, false, true));
    ASSERT_TRUE(navigationFrame());
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_TRUE(m_clipboard.deliver(m_clipboard.startedToken, ClipboardStatus::NativeFailure));
    ASSERT_TRUE(navigationFrame());
    EXPECT_TRUE(m_navigation.snapshot().valid);
    EXPECT_FLOAT_EQ(m_navigation.snapshot().preferredX, 50.0f);
    EXPECT_EQ(m_navigationModel.caret(), 8u);
    ASSERT_TRUE(key(Core::Key::V, false, true));
    ASSERT_TRUE(navigationFrame());
    ASSERT_TRUE(m_clipboard.pump());
    const ClipboardRequestToken cancelled = m_clipboard.startedToken;
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_TRUE(m_resolver.records.back().navigation.valid);
    EXPECT_FLOAT_EQ(m_resolver.records.back().navigation.preferredX, 50.0f);
    EXPECT_EQ(m_navigationModel.caret(), 14u);
    EXPECT_EQ(m_navigationModel.text(), "abcdef\nx\nabcdef");
    EXPECT_FALSE(m_clipboard.deliver(cancelled, ClipboardStatus::Success, "late"));
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, PageMovementCarriesTheViewportAcceptedAtInputAdmission){
    ASSERT_TRUE(m_navigationModel.setText("a\nb\nc\nd\ne\nf\ng"));
    ASSERT_TRUE(m_navigationModel.setSelection(1u, 1u));
    ASSERT_TRUE(activateNavigation({}, { 10.0f, 20.0f, 180.0f, 24.0f }));
    const u64 displayed = m_context.input().layoutGeneration();
    ASSERT_TRUE(prepareNavigation({}, { 10.0f, 20.0f, 180.0f, 36.0f }));
    EXPECT_EQ(m_context.input().layoutGeneration(), displayed);
    ASSERT_TRUE(key(Core::Key::PageDown));
    ASSERT_TRUE(commit());
    ASSERT_TRUE(navigationFrame({}, { 10.0f, 20.0f, 180.0f, 36.0f }));
    ASSERT_EQ(m_resolver.records.size(), 1u);
    EXPECT_FLOAT_EQ(m_resolver.records.front().viewportHeight, 24.0f);
    EXPECT_EQ(m_navigationModel.caret(), 5u);
    ASSERT_TRUE(key(Core::Key::PageDown));
    ASSERT_TRUE(navigationFrame({}, { 10.0f, 20.0f, 180.0f, 36.0f }));
    ASSERT_EQ(m_resolver.records.size(), 2u);
    EXPECT_FLOAT_EQ(m_resolver.records.back().viewportHeight, 36.0f);
    EXPECT_EQ(m_navigationModel.caret(), 11u);
}

TEST_F(UiEditNavigationHostTests, NewlineThenDownShapesTheNewHardLineBeforeLaterText){
    ASSERT_TRUE(m_navigationModel.setText("ab\ncdef"));
    ASSERT_TRUE(m_navigationModel.setSelection(1u, 1u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(key(Core::Key::Down));
    ASSERT_TRUE(commitNative("!"));
    ASSERT_TRUE(navigationFrame());
    ASSERT_EQ(m_resolver.records.size(), 1u);
    EXPECT_EQ(m_resolver.records.front().text, "a\nb\ncdef");
    EXPECT_EQ(m_resolver.records.front().caret, 2u);
    EXPECT_EQ(m_resolver.records.front().result.committedByte, 4u);
    EXPECT_EQ(m_navigationModel.text(), "a\nb\n!cdef");
    EXPECT_EQ(m_navigationModel.caret(), 5u);
    EXPECT_FALSE(m_result.submitted);
    EXPECT_EQ(m_actions.count(Ui::EditAction::Submit), 0u);
    ASSERT_TRUE(m_navigationModel.undo());
    EXPECT_EQ(m_navigationModel.text(), "a\nb\ncdef");
    ASSERT_TRUE(m_navigationModel.undo());
    EXPECT_EQ(m_navigationModel.text(), "ab\ncdef");
    EXPECT_FALSE(m_navigationModel.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

