// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_builder_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/selectable_paint.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_builder_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;
using namespace NWB::UiListTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiListBuilderTests, InitialProgrammaticSelectionSurvivesAndEndNavigatesToVisibleLastKey){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    ASSERT_NE(target(row(50000u)), nullptr);
    press(Core::Key::Tab);
    press(Core::Key::End);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 100000u);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), m_state.placement().maxOffset);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_NE(target(row(100000u)), nullptr);
    EXPECT_LE(m_source.textCalls, 12u);
}

TEST_F(UiListBuilderTests, UnseenTallCursorIsMeasuredBeforeRevealAndFailedRevisionRefreshRetainsTheRequest){
    m_source.specialIndex = m_source.count - 1u;
    m_state.select(1u);
    ListOptions options = Options();
    options.selectOnNavigate = false;
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(row(1u)), nullptr);
    const f32 initialHeight = target(row(1u))->rectangle.height;
    press(Core::Key::Tab);
    ASSERT_EQ(m_context.input().focus(), host());
    press(Core::Key::End);
    m_source.textCalls = 0u;
    ASSERT_TRUE(accept(2u, options));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_state.cursorKey(), m_source.count);
    const HitTarget* cursor = target(row(m_source.count));
    ASSERT_NE(cursor, nullptr);
    EXPECT_GT(cursor->rectangle.height, initialHeight);
    EXPECT_GE(cursor->rectangle.y, cursor->clip.y);
    const SIMDVectorSingle cursorBottom = VectorAdd(VectorSet(cursor->rectangle.y, cursor->clip.y, 0.0f, 0.0f), VectorSet(cursor->rectangle.height, cursor->clip.height, 0.0f, 0.0f));
    EXPECT_LE(VectorGetX(cursorBottom), VectorGetY(cursorBottom));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), m_state.placement().maxOffset);
    EXPECT_LE(m_source.textCalls, 8u);
    ASSERT_TRUE(m_state.scrollTo(0.0));
    ASSERT_TRUE(accept(3u, options));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_EQ(m_state.cursorKey(), m_source.count);
    EXPECT_EQ(target(row(m_source.count)), nullptr);
    ++m_source.contentRevision;
    const f32 fontSize = m_builder.style().fontSize;
    m_builder.style().fontSize = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(prepare(4u, options));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_state.cursorKey(), m_source.count);
    m_context.abandonFrame();
    m_builder.reset();
    m_builder.style().fontSize = fontSize;
    ASSERT_TRUE(accept(5u, options));
    cursor = target(row(m_source.count));
    ASSERT_NE(cursor, nullptr);
    EXPECT_GE(cursor->rectangle.y, cursor->clip.y);
    const SIMDVectorSingle restoredBottom = VectorAdd(VectorSet(cursor->rectangle.y, cursor->clip.y, 0.0f, 0.0f), VectorSet(cursor->rectangle.height, cursor->clip.height, 0.0f, 0.0f));
    EXPECT_LE(VectorGetX(restoredBottom), VectorGetY(restoredBottom));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), m_state.placement().maxOffset);
}

TEST_F(UiListBuilderTests, ReorderPreservesSelectionByKeyAndRemovalClearsIt){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_NE(target(row(50000u)), nullptr);
    m_source.removed = 50000u;
    --m_source.count;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(3u));
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_EQ(m_state.cursorKey(), 0u);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(target(row(50000u)), nullptr);
}

TEST_F(UiListBuilderTests, ProgrammaticSameSelectionFencesQueuedActivationAndWheel){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ASSERT_TRUE(wheel());
    const u64 inputGeneration = m_state.inputGeneration();
    m_state.select(1u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.activated);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiListBuilderTests, SourceRevisionFencesHeldCaptureAndRestoresHostFocusAtAcceptance){
    ASSERT_TRUE(accept(1u));
    const Point point = Center(target(row(1u))->rectangle);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = point }).pointerConsumed);
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = point }).pointerConsumed);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
    ASSERT_TRUE(prepare(3u));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(m_state.selectedKey(), 0u);
}

TEST_F(UiListBuilderTests, EscapeBeforeReleaseCancelsRowSelectionAndHostSubmission){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    const Point point = Center(target(row(2u))->rectangle);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = point }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Escape);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = point }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.activated);
    EXPECT_FALSE(m_result.selectionChanged);
    press(Core::Key::Tab);
    ASSERT_EQ(m_context.input().focus(), host());
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .key = Core::Key::Enter }).commandConsumed);
    ASSERT_TRUE(accept(4u));
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Escape);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .key = Core::Key::Enter }).commandConsumed);
    ASSERT_TRUE(accept(5u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Tab);
    ASSERT_EQ(m_context.input().focus(), host());
    press(Core::Key::Enter);
    ASSERT_TRUE(accept(6u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_TRUE(m_result.activated);
}

TEST_F(UiListBuilderTests, DatasetReplacementWhileSubmitIsHeldCannotSubmitOnRelease){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .key = Core::Key::Enter }).commandConsumed);
    ++m_source.contentRevision;
    ASSERT_TRUE(accept(2u));
    EXPECT_FALSE(m_result.activated);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .key = Core::Key::Enter }).commandConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Enter);
    ASSERT_TRUE(accept(4u));
    EXPECT_TRUE(m_result.activated);
}

TEST_F(UiListBuilderTests, HeldNavigationCannotMutateAnAcceptedReorderedDataset){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = Core::Key::Down;
    EXPECT_TRUE(send(event).commandConsumed);
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    event.repeat = true;
    EXPECT_TRUE(send(event).commandConsumed);
    ASSERT_TRUE(prepare(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.selectionChanged);
    ASSERT_TRUE(m_context.commitFrame(3u));
    event.type = InputEventType::KeyUp;
    EXPECT_TRUE(send(event).commandConsumed);
}

TEST_F(UiListBuilderTests, ReplacementSourceResetsSelectionAndScroll){
    m_state.select(60000u);
    ASSERT_TRUE(accept(1u));
    ASSERT_GT(m_state.scrollOffset(), 0.0);
    ++m_source.generation;
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_EQ(m_state.cursorKey(), 0u);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_TRUE(m_result.selectionChanged);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_NE(target(row(1u)), nullptr);
}

TEST_F(UiListBuilderTests, ReplacementStateRejectsOldActivationEvenForTheSameSourceAndHost){
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ListState replacement;
    ASSERT_TRUE(prepareSource(2u, m_source, replacement));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(replacement.selectedKey(), 0u);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(host())->control.instanceGeneration, replacement.inputGeneration());
}

TEST_F(UiListBuilderTests, DisabledAndOmittedListsRetirePartsWithoutBorrowingQueuedActions){
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ListOptions disabled = Options();
    disabled.enabled = false;
    ASSERT_TRUE(prepare(2u, disabled));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_FALSE(target(host())->enabled);
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
    ASSERT_TRUE(finishPanel());
    EXPECT_EQ(target(host()), nullptr);
    EXPECT_EQ(m_context.states().entries().size(), 1u);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(m_context.input().targets().size(), 1u);
}

TEST_F(UiListBuilderTests, SourceRevisionChangedDuringScopeRejectsTheDeferredLoan){
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
    const ListResult result = m_builder.virtualList(s_ListKey, m_source, m_state, Options());
    ASSERT_TRUE(result.valid);
    ++m_source.contentRevision;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_context.ready());
}

TEST_F(UiListBuilderTests, ContentPreflightRowsCannotHideAChangedSourceBeforePaint){
    ListOptions options = Options();
    options.height = { LayoutSizePolicy::Content, 0.0f };
    ASSERT_TRUE(accept(1u, options));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
    m_source.textCalls = 0u;
    ASSERT_TRUE(m_builder.virtualList(s_ListKey, m_source, m_state, options).valid);
    ASSERT_GT(m_source.textCalls, 0u);
    ++m_source.contentRevision;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_context.ready());
}

TEST_F(UiListBuilderTests, DuplicateOrMalformedVisibleKeysRejectTheFrame){
    m_source.duplicate = true;
    EXPECT_FALSE(prepare(1u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_context.input().targets().empty());
    m_context.abandonFrame();
    m_builder.reset();
    m_source.duplicate = false;
    m_source.malformedLookup = true;
    EXPECT_FALSE(prepare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiListBuilderTests, EmptyDatasetBuildsOnlyItsHostAndProvidesOneKeyboardStop){
    m_source.count = 0u;
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_source.textCalls, 0u);
    EXPECT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_FALSE(m_state.placement().scrollbarVisible);
    press(Core::Key::Tab);
    press(Core::Key::Down);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_FALSE(m_result.activated);
    EXPECT_FALSE(m_result.selectionChanged);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

