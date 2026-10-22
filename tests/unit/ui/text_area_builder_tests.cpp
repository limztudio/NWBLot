// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"
#include "widget_id_assertion.h"

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextAreaTests;

class OrdinaryAreaHost final : public IEditBoxHost{
public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState&, EditModel&, const EditBoxOptions&)noexcept override{
        ++ordinaryLoans;
        EditBoxResult result;
        result.valid = true;
        return result;
    }

    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState&, EditModel&, const EditBoxOptions&, const PopupToken&)noexcept override{ return {}; }

    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState&, EditModel&, const EditBoxOptions&, const PopupToken&, IEditActionSink&)noexcept override{ return {}; }

    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState&, EditModel&, const EditBoxOptions&, const PopupToken&, EditNavigationState&, IEditNavigationResolver&, IEditActionSink&)noexcept override{ return {}; }

    [[nodiscard]] virtual bool publish(const WidgetState&, const EditBoxView&, const EditBoxPlacement&, const EditBoxOptions&)noexcept override{
        ++publishes;
        return true;
    }


public:
    usize ordinaryLoans = 0u;
    usize publishes = 0u;
};

class UiTextAreaBuilderTests : public TextAreaFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaBuilderTests, SingleLineIsRejectedBeforeBorrowingOrChangingViewport){
    useHost();
    EditModel singleLine(m_arena);
    ASSERT_TRUE(singleLine.setText("single"));
    ASSERT_TRUE(m_state.scrollTo({ 13.0f, 17.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(beginArea(1u));
    const auto result = m_builder.textArea(s_TextAreaKey, singleLine, m_state);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_host.loans, 0u);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 13.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 17.0f);
    EXPECT_EQ(singleLine.text(), "single");
}

TEST_F(UiTextAreaBuilderTests, UnsupportedNavigatedHostCannotFallBackToOrdinaryEdit){
    OrdinaryAreaHost host;
    m_builder.setEditHost(&host);
    ASSERT_TRUE(beginArea(1u));
    const auto result = m_builder.textArea(s_TextAreaKey, m_model, m_state);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(host.ordinaryLoans, 0u);
    EXPECT_EQ(host.publishes, 0u);
}

TEST_F(UiTextAreaBuilderTests, ReadOnlyNavigatedHostRejectsQueuedText){
    useHost();
    ASSERT_TRUE(m_model.setText("first\nsecond"));
    TextAreaOptions options;
    options.width = { LayoutSizePolicy::Fixed, 140.0f };
    options.height = { LayoutSizePolicy::Fixed, 60.0f };
    options.readOnly = true;
    m_host.text("ignored");
    ASSERT_TRUE(frameArea(1u, options));
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.ordinaryLoans, 0u);
    EXPECT_EQ(m_host.actionLoans, 0u);
    EXPECT_EQ(m_model.text(), "first\nsecond");
}

TEST_F(UiTextAreaBuilderTests, RawPointerCapturePersistsOutsideWithoutGestureRecords){
    ASSERT_TRUE(frameArea(1u));
    const WidgetId widget = id(s_TextAreaKey, s_TextAreaPanelKey);
    const HitTarget* area = target(widget);
    ASSERT_TRUE(area);
    const SIMDVectorSingle originValue = VectorAdd(VectorSet(area->rectangle.x, area->rectangle.y, 0.0f, 0.0f), VectorReplicate(10.0f));
    const Point origin{ VectorGetX(originValue), VectorGetY(originValue) };
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    EXPECT_EQ(m_context.input().capture(), widget);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 780.0f, 580.0f } }).pointerConsumed);
    EXPECT_EQ(m_context.input().capture(), widget);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 780.0f, 580.0f } }).pointerConsumed);
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(m_context.input().consumePointerGesture(widget, area->declarationGeneration));
}

TEST_F(UiTextAreaBuilderTests, DisabledAreaRetainsAPointerBarrierAndDiscardsHostText){
    useHost();
    ASSERT_TRUE(m_model.setText("first\nsecond"));
    m_host.text("ignored");
    TextAreaOptions options;
    options.enabled = false;
    ASSERT_TRUE(frameArea(1u, options));
    const WidgetId widget = id(s_TextAreaKey, s_TextAreaPanelKey);
    const HitTarget* area = target(widget);
    ASSERT_TRUE(area);
    EXPECT_FALSE(area->enabled || area->focusable || area->textEditable || area->pointerGesture);
    const SIMDVectorSingle pointValue = VectorAdd(VectorSet(area->rectangle.x, area->rectangle.y, 0.0f, 0.0f), VectorReplicate(5.0f));
    const Point point{ VectorGetX(pointValue), VectorGetY(pointValue) };
    EXPECT_EQ(m_context.input().hitTest(point), UiTestSupport::RequireWidgetId(MakeWidgetId(UiTestSupport::RequireWidgetId(MakeRootId(m_root)), s_TextAreaPanelKey)));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = point }).pointerConsumed);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = point }).pointerConsumed);
    EXPECT_FALSE(m_state.focused());
    EXPECT_EQ(m_model.text(), "first\nsecond");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaBuilderTests, PopupAreaDefersPublicationUntilItsOwningPopupEnds){
    useHost();
    PopupState popup;
    popup.open();
    PopupOptions options;
    options.anchor = { 20.0f, 20.0f, 40.0f, 20.0f };
    options.size = { 300.0f, 240.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", popup, options));
    const PopupToken token = m_context.popupToken();
    ASSERT_TRUE(m_builder.textArea(s_TextAreaKey, m_model, m_state).valid);
    EXPECT_EQ(m_host.lastPopup, token);
    EXPECT_EQ(m_host.publishes, 0u);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_host.publications.size(), 1u);
    EXPECT_EQ(m_host.publications.front().popup, token);
}

TEST_F(UiTextAreaBuilderTests, AcceptingActionsKeepTextAndHistoryThroughSubmitBlurAbandonAndCancel){
    useHost();
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    m_host.text("X");
    m_host.action(EditAction::Submit);
    m_host.text("Y");
    m_host.action(EditAction::Blur);
    m_host.action(EditAction::Abandon);
    m_host.action(EditAction::Cancel);
    ASSERT_TRUE(frameArea(1u));
    EXPECT_TRUE(m_result.submitted && m_result.blurred && m_result.abandoned && m_result.cancelled);
    EXPECT_FALSE(m_result.focused);
    EXPECT_EQ(m_model.text(), "ab\ncdXY");
    ASSERT_EQ(m_host.actionTexts.size(), 4u);
    EXPECT_EQ(m_host.actionTexts.front(), "ab\ncdX");
    EXPECT_EQ(m_host.actionTexts.back(), "ab\ncdXY");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "ab\ncdX");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "ab\ncd");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaBuilderTests, OrderedHostUsesTheRealResolverAfterEarlierInsertedText){
    useHost();
    ASSERT_TRUE(m_model.setText("aa\naaaa\nxx"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    m_host.text("a");
    m_host.key(Core::Key::Down);
    m_host.text("!");
    ASSERT_TRUE(frameArea(1u));
    ASSERT_EQ(m_host.resolutions.size(), 1u);
    EXPECT_EQ(m_host.resolutions.front().text, "aaa\naaaa\nxx");
    EXPECT_EQ(m_host.resolutions.front().caret, 2u);
    ASSERT_TRUE(m_host.resolutions.front().result);
    EXPECT_EQ(m_host.resolutions.front().result->committedByte, 6u);
    EXPECT_EQ(m_model.text(), "aaa\naa!aa\nxx");
    EXPECT_EQ(m_model.caret(), 7u);
    ASSERT_EQ(m_host.publications.size(), 1u);
    EXPECT_EQ(m_host.publications.front().text, "aaa\naa!aa\nxx");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "aaa\naaaa\nxx");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "aa\naaaa\nxx");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaBuilderTests, BorrowedPreferredColumnSurvivesShortLineThenReturnsToLongLine){
    useHost();
    ASSERT_TRUE(m_model.setText("aaaaaa\na\naaaaaa"));
    ASSERT_TRUE(m_model.setSelection(5u, 5u));
    EditBoxView reference(m_arena);
    ASSERT_TRUE(reference.snapshot(m_model));
    ASSERT_EQ(reference.shape(m_text, { {}, 14.0f }), TextLayoutStatus::Success);
    const auto caret = reference.caretGeometry().caretRect(reference.displayCaret());
    ASSERT_TRUE(caret);
    m_host.seedColumn = true;
    m_host.seededColumn = caret->x;
    m_host.key(Core::Key::Down);
    m_host.key(Core::Key::Down);
    ASSERT_TRUE(frameArea(1u));
    ASSERT_EQ(m_host.resolutions.size(), 2u);
    ASSERT_TRUE(m_host.resolutions[0u].result);
    EXPECT_EQ(m_host.resolutions[0u].result->committedByte, 8u);
    ASSERT_TRUE(m_host.resolutions[1u].result);
    EXPECT_EQ(m_host.resolutions[1u].result->committedByte, 14u);
    EXPECT_TRUE(m_host.resolutions[1u].preferred.valid);
    EXPECT_FLOAT_EQ(m_host.resolutions[1u].preferred.preferredX, caret->x);
    EXPECT_FLOAT_EQ(m_state.navigation().preferredX(), caret->x);
    EXPECT_EQ(m_model.caret(), 14u);
    EXPECT_FALSE(m_model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

