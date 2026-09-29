// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_search_combo_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiSearchComboTests;

class UiSearchComboBuilderTests : public SearchFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSearchComboBuilderTests, ClosedFieldUsesFullSelectionWithoutBorrowingTheQueryEditor){
    m_search.combo().select(1u);
    ASSERT_TRUE(m_search.query().setText("Second"));
    ASSERT_TRUE(acceptSearch(1u));
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_FALSE(m_searchResult.combo.selectionChanged);
    EXPECT_EQ(m_searchSource.full.textCalls, 1u);
    EXPECT_EQ(m_host.loans, 0u);
    EXPECT_EQ(m_host.publications, 0u);
    EXPECT_EQ(target(query()), nullptr);
    EXPECT_EQ(target(list()), nullptr);
    EXPECT_EQ(target(host())->control.contentGeneration, m_searchSource.full.generation);
}

TEST_F(UiSearchComboBuilderTests, PopupBorrowsAnExplicitEditorScopeAndDelegatesTheFilteredList){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    ASSERT_NE(target(query()), nullptr);
    ASSERT_NE(target(list()), nullptr);
    EXPECT_EQ(m_host.legacyLoans, 0u);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.publications, 1u);
    EXPECT_TRUE(m_host.lastPopup.valid());
    EXPECT_FALSE(m_host.loanContextPopup.valid());
    EXPECT_EQ(m_host.lastPopup, target(query())->popup);
    EXPECT_EQ(m_host.publishContextPopup, target(query())->popup);
    EXPECT_EQ(m_context.input().focus(), query());
    EXPECT_TRUE(target(query())->textEditable);
    EXPECT_EQ(target(query())->keyboardOwner, list());
    EXPECT_EQ(target(query())->keyboardControl, target(list())->control);
    EXPECT_EQ(target(list())->control.contentGeneration, m_searchSource.view.generation);
    EXPECT_NE(target(list())->control.contentGeneration, target(host())->control.contentGeneration);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
}

TEST_F(UiSearchComboBuilderTests, TabMovesFromQueryToResultsThenExitsWithoutCommittingPreview){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Down);
    ASSERT_TRUE(acceptSearch(3u));
    ASSERT_EQ(m_search.combo().listState().cursorKey(), 2u);
    ASSERT_EQ(m_context.input().focus(), query());
    press(InputKey::Tab);
    EXPECT_EQ(m_context.input().focus(), list());
    press(InputKey::Tab);
    ASSERT_TRUE(acceptSearch(4u));
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 1u);
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiSearchComboBuilderTests, ReverseTabFromQueryCancelsPreeditAndKeepsTheCommittedValue){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    ASSERT_TRUE(m_search.query().beginComposition());
    ASSERT_TRUE(m_search.query().updateComposition("preedit", 0u, 7u));
    ASSERT_TRUE(m_search.query().composition().active);
    const InputEvent reverseDown{ .type = InputEventType::KeyDown, .position = {}, .key = InputKey::Tab, .shift = true };
    const InputEvent reverseUp{ .type = InputEventType::KeyUp, .position = {}, .key = InputKey::Tab, .shift = true };
    EXPECT_TRUE(send(reverseDown).keyboardConsumed);
    EXPECT_TRUE(send(reverseUp).keyboardConsumed);
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_FALSE(m_search.query().composition().active);
    EXPECT_TRUE(m_search.query().text().empty());
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiSearchComboBuilderTests, ClosedArrowOpensUsingTheFilteredLifetimeAndKeepsTheCommittedKey){
    m_search.combo().select(1u);
    ASSERT_TRUE(m_search.query().setText("Second"));
    ASSERT_TRUE(acceptSearch(1u));
    press(InputKey::Tab);
    press(InputKey::Down);
    ASSERT_TRUE(acceptSearch(2u));
    EXPECT_TRUE(m_searchResult.combo.opened);
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 2u);
    EXPECT_EQ(m_context.input().focus(), query());
}

TEST_F(UiSearchComboBuilderTests, QueryNavigationChangesPreviewWhileTypingFocusStaysInTheEditor){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Down);
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 2u);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_FALSE(m_searchResult.combo.selectionChanged);
    EXPECT_EQ(m_context.input().focus(), query());
}

TEST_F(UiSearchComboBuilderTests, EnterIntentionRequiresTheNativeEditHostSubmissionGate){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Down);
    ASSERT_TRUE(acceptSearch(3u));
    press(InputKey::Enter);
    ASSERT_TRUE(acceptSearch(4u));
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_TRUE(m_search.combo().isOpen());
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    m_host.submitted = true;
    press(InputKey::Enter);
    ASSERT_TRUE(acceptSearch(5u));
    EXPECT_TRUE(m_searchResult.combo.committed);
    EXPECT_TRUE(m_searchResult.combo.selectionChanged);
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_EQ(m_search.combo().selectedKey(), 2u);
    EXPECT_FALSE(m_search.editorState().focused);
}

TEST_F(UiSearchComboBuilderTests, DownEnterDownCommitsAtTheAcceptedEnterPosition){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Down);
    press(InputKey::Enter);
    press(InputKey::Down);
    m_host.submitted = true;
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_TRUE(m_searchResult.combo.committed);
    EXPECT_EQ(m_search.combo().selectedKey(), 2u);
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_TRUE(m_context.input().controlActions().empty());
}

TEST_F(UiSearchComboBuilderTests, EnterThenDownCannotMoveTheSelectionPastTheSubmitIntention){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Enter);
    press(InputKey::Down);
    m_host.submitted = true;
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_TRUE(m_searchResult.combo.committed);
    EXPECT_FALSE(m_searchResult.combo.selectionChanged);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_FALSE(m_search.combo().isOpen());
}

TEST_F(UiSearchComboBuilderTests, ChangedQueryFiltersRowsAndDiscardsNavigationFromThePreviousView){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    const ControlToken previous = target(list())->control;
    press(InputKey::Down);
    m_host.text("Second");
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_TRUE(m_searchResult.queryChanged);
    EXPECT_EQ(m_search.query().text(), "Second");
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 0u);
    EXPECT_NE(target(list())->control, previous);
    EXPECT_EQ(target(row(1u)), nullptr);
    EXPECT_NE(target(row(2u)), nullptr);
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_EQ(m_context.input().focus(), query());
    press(InputKey::Down);
    ASSERT_TRUE(acceptSearch(4u));
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 2u);
    press(InputKey::Enter);
    m_host.submitted = true;
    ASSERT_TRUE(acceptSearch(5u));
    EXPECT_TRUE(m_searchResult.combo.committed);
    EXPECT_EQ(m_search.combo().selectedKey(), 2u);
}

TEST_F(UiSearchComboBuilderTests, EmptyFilteredViewCannotCommitAndDoesNotClearTheFullSelection){
    m_search.combo().select(1u);
    ASSERT_TRUE(m_search.query().setText("None"));
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 0u);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    press(InputKey::Enter);
    m_host.submitted = true;
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_FALSE(m_searchResult.combo.selectionChanged);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
}

TEST_F(UiSearchComboBuilderTests, PreeditDisablesDelegationAndCannotCommitQueuedNavigation){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Down);
    press(InputKey::Enter);
    m_host.preedit = true;
    m_host.submitted = true;
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_TRUE(m_search.query().composition().active);
    EXPECT_TRUE(m_host.publishedComposing);
    EXPECT_FALSE(target(query())->keyboardOwner.valid());
    EXPECT_TRUE(target(query())->keyboardControl.empty());
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 1u);
    EXPECT_EQ(m_context.input().focus(), query());
    press(InputKey::Down);
    press(InputKey::Enter);
    EXPECT_TRUE(m_context.input().controlActions().empty());
}

TEST_F(UiSearchComboBuilderTests, OutsideDismissalCancelsQueryCompositionAndKeepsTheCommittedSelection){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(m_search.query().beginComposition());
    ASSERT_TRUE(m_search.query().updateComposition("preedit", 0u, 7u));
    ASSERT_TRUE(acceptSearch(1u));
    click({ 790.0f, 590.0f });
    ASSERT_TRUE(acceptSearch(2u));
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_FALSE(m_search.query().composition().active);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_FALSE(m_search.combo().isOpen());
}

TEST_F(UiSearchComboBuilderTests, HostCancellationClosesThePopupWithoutSelectingItsPreview){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Down);
    ASSERT_TRUE(acceptSearch(3u));
    m_host.cancelled = true;
    ASSERT_TRUE(acceptSearch(4u));
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
}

TEST_F(UiSearchComboBuilderTests, DeferredQueryTextOrSelectionChangesRejectTheCandidate){
    m_search.combo().select(1u);
    ASSERT_TRUE(m_search.query().setText("Second"));
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(1u));
    const Rect displayed = target(query())->rectangle;
    ASSERT_TRUE(declareSearch(2u));
    ASSERT_TRUE(m_search.query().setSelection(1u, 1u));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_search.query().anchor(), 1u);
    EXPECT_EQ(m_search.query().caret(), 1u);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ExpectRect(target(query())->rectangle, displayed);
}

TEST_F(UiSearchComboBuilderTests, BeginningPreeditAfterDeclarationCannotPublishAnUncomposedView){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(declareSearch(2u));
    const u64 revision = m_search.query().revision();
    ASSERT_TRUE(m_search.query().beginComposition());
    ASSERT_TRUE(m_search.query().updateComposition("late", 0u, 4u));
    EXPECT_EQ(m_search.query().revision(), revision);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_search.query().composition().active);
    EXPECT_EQ(m_search.query().composition().text, "late");
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiSearchComboBuilderTests, UpdatingPreeditAfterDeclarationRejectsEvenWhenCommittedTextIsUnchanged){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(m_search.query().beginComposition());
    ASSERT_TRUE(m_search.query().updateComposition("old", 0u, 3u));
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(declareSearch(2u));
    const u64 revision = m_search.query().revision();
    ASSERT_TRUE(m_search.query().updateComposition("new", 1u, 2u));
    EXPECT_EQ(m_search.query().revision(), revision);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_search.query().composition().text, "new");
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiSearchComboBuilderTests, CancellingPreeditAfterDeclarationCannotPublishAComposedView){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(m_search.query().beginComposition());
    ASSERT_TRUE(m_search.query().updateComposition("old", 0u, 3u));
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(declareSearch(2u));
    m_search.query().cancelComposition();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_search.query().composition().active);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiSearchComboBuilderTests, PublishingQueryMutationCannotReplaceTheAcceptedEditorGeometry){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(1u));
    const Rect displayed = target(query())->rectangle;
    ASSERT_TRUE(declareSearch(2u));
    m_host.publishMutation = &m_search.query();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_search.query().text(), "published");
    ExpectRect(target(query())->rectangle, displayed);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiSearchComboBuilderTests, FilterCallbackQueryMutationRejectsBeforeBorrowingTheEditor){
    m_search.combo().select(1u);
    m_search.combo().open();
    m_searchSource.filterMutation = &m_search.query();
    EXPECT_FALSE(declareSearch(1u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_search.query().text(), "callback");
    EXPECT_EQ(m_host.loans, 0u);
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiSearchComboBuilderTests, InitialFilteredReconcileCannotAdoptAQueryChangedBySourceCallbacks){
    m_search.combo().select(1u);
    m_search.combo().open();
    m_searchSource.view.enabledMutation = &m_search.query();
    EXPECT_FALSE(declareSearch(1u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_search.query().text(), "None");
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_host.loans, 0u);
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiSearchComboBuilderTests, FullSourceReconcileQueryMutationRejectsBeforeFilteringOrEditorLoans){
    m_search.combo().select(1u);
    m_search.combo().open();
    m_searchSource.fullMutation = &m_search.query();
    EXPECT_FALSE(declareSearch(1u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_search.query().text(), "None");
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_searchSource.filterCalls, 0u);
    EXPECT_EQ(m_searchSource.full.textCalls, 0u);
    EXPECT_EQ(m_host.loans, 0u);
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiSearchComboBuilderTests, RowTextCallbackQueryMutationRejectsDeferredPopupPaint){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(declareSearch(2u));
    m_searchSource.view.textMutation = &m_search.query();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_search.query().text(), "callback");
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiSearchComboBuilderTests, FilteredViewReplacementRejectsEvenWithCopiedMetadata){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(declareSearch(2u));
    m_searchSource.alternate = m_searchSource.view;
    m_searchSource.alternateView = true;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiSearchComboBuilderTests, OmissionRetiresTheQueryAndCannotReopenItsOldPopupLifetime){
    m_search.combo().select(1u);
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    press(InputKey::Down);
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 280.0f }));
    ASSERT_TRUE(m_builder.label("label", "Search hidden"));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(target(query()), nullptr);
    EXPECT_EQ(target(list()), nullptr);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(acceptSearch(4u));
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(target(query()), nullptr);
}

TEST_F(UiSearchComboBuilderTests, PlainToSearchSwitchRetiresTheOldPopupAndReopensWithEditorFocus){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 280.0f }));
    ASSERT_TRUE(m_builder.comboBox("combo", m_searchSource, m_search.combo(), options().combo).valid);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(list()), nullptr);
    EXPECT_EQ(m_context.input().focus(), list());
    const PopupToken oldPopup = target(popup())->popup;
    const u64 oldDeclaration = target(host())->declarationGeneration;
    press(InputKey::Down);
    ASSERT_TRUE(acceptSearch(2u));
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_FALSE(m_searchResult.combo.committed);
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_NE(target(host())->declarationGeneration, oldDeclaration);
    EXPECT_EQ(target(popup()), nullptr);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(openSearch(3u));
    ASSERT_NE(target(query()), nullptr);
    EXPECT_EQ(m_context.input().focus(), query());
    EXPECT_NE(target(popup())->popup, oldPopup);
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 1u);
}

TEST_F(UiSearchComboBuilderTests, SearchToPlainSwitchRetiresTheEditorAndCannotReplayItsQueuedSubmit){
    m_search.combo().select(1u);
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(1u));
    EXPECT_EQ(m_context.input().focus(), query());
    const PopupToken oldPopup = target(popup())->popup;
    press(InputKey::Down);
    press(InputKey::Enter);
    ASSERT_FALSE(m_context.input().controlActions().empty());
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 280.0f }));
    const ComboResult switched = m_builder.comboBox("combo", m_searchSource, m_search.combo(), options().combo);
    EXPECT_TRUE(switched.valid);
    EXPECT_TRUE(switched.closed);
    EXPECT_FALSE(switched.committed);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(target(query()), nullptr);
    EXPECT_EQ(target(popup()), nullptr);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    click(Center(target(host())->rectangle));
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 280.0f }));
    const ComboResult reopened = m_builder.comboBox("combo", m_searchSource, m_search.combo(), options().combo);
    EXPECT_TRUE(reopened.valid);
    EXPECT_TRUE(reopened.opened);
    EXPECT_FALSE(reopened.committed);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(target(query()), nullptr);
    EXPECT_EQ(m_context.input().focus(), list());
    EXPECT_NE(target(popup())->popup, oldPopup);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 1u);
}


TEST_F(UiSearchComboBuilderTests, CancelledPopupRetiresEditorFocusWhileKeepingQueryAndCommittedKey){
    m_search.combo().select(1u);
    ASSERT_TRUE(m_search.query().setText("First"));
    ASSERT_TRUE(acceptSearch(1u));
    ASSERT_TRUE(openSearch(2u));
    ASSERT_TRUE(acceptSearch(3u));
    ASSERT_TRUE(m_search.editorState().focused);
    m_host.cancelled = true;
    ASSERT_TRUE(acceptSearch(4u));
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_FALSE(m_search.editorState().focused);
    EXPECT_EQ(m_search.combo().selectedKey(), 1u);
    EXPECT_EQ(m_search.query().text(), "First");
    EXPECT_EQ(target(query()), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

