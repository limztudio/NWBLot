// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextAreaTests;

class OrdinaryAreaHost final : public IEditBoxHost{
public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState&, EditModel&, const EditBoxOptions&)override{
        ++ordinaryLoans;
        EditBoxResult result;
        result.valid = true;
        return result;
    }

    [[nodiscard]] virtual bool publish(const WidgetState&, const EditBoxView&, const EditBoxPlacement&,
        const EditBoxOptions&)override{
        ++publishes;
        return true;
    }


public:
    usize ordinaryLoans = 0u;
    usize publishes = 0u;
};

class UiTextAreaBuilderTests : public TextAreaFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaBuilderTests, MultilineWithoutHostPublishesTopAlignedDefaultGeometry){
    ASSERT_TRUE(m_model.setText("first\nsecond\n"));
    ASSERT_TRUE(m_model.setSelection(0u, 0u));
    ASSERT_TRUE(frameArea(1u));
    const HitTarget* area = target(id("area", "panel"));
    ASSERT_TRUE(area);
    EXPECT_TRUE(area->enabled);
    EXPECT_TRUE(area->focusable);
    EXPECT_TRUE(area->textEditable);
    EXPECT_FALSE(area->pointerGesture);
    EXPECT_FLOAT_EQ(area->rectangle.height, 160.0f);
    EXPECT_GT(area->rectangle.width, 120.0f);
    EXPECT_FLOAT_EQ(m_state.placement().bounds.x, area->rectangle.x);
    EXPECT_FLOAT_EQ(m_state.placement().bounds.y, area->rectangle.y);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.y, m_state.placement().content.y);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_FALSE(m_state.focused());
    EXPECT_EQ(m_model.text(), "first\nsecond\n");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_paint.popClip());
    const DrawSnapshot paint = m_paint.freeze();
    EXPECT_FALSE(paint.commands().empty());
}

TEST_F(UiTextAreaBuilderTests, SingleLineIsRejectedBeforeBorrowingOrChangingViewport){
    useHost();
    EditModel singleLine(m_arena);
    ASSERT_TRUE(singleLine.setText("single"));
    ASSERT_TRUE(m_state.scrollTo({ 13.0f, 17.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(beginArea(1u));
    const auto result = m_builder.textArea("area", singleLine, m_state);
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
    const auto result = m_builder.textArea("area", m_model, m_state);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(host.ordinaryLoans, 0u);
    EXPECT_EQ(host.publishes, 0u);
}

TEST_F(UiTextAreaBuilderTests, FlatOptionsReachTheNavigatedHostAndReadOnlyTarget){
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
    EXPECT_TRUE(m_host.lastOptions.enabled);
    EXPECT_TRUE(m_host.lastOptions.readOnly);
    EXPECT_EQ(m_host.lastOptions.width.policy, LayoutSizePolicy::Fixed);
    EXPECT_FLOAT_EQ(m_host.lastOptions.width.value, 140.0f);
    EXPECT_FLOAT_EQ(m_state.placement().bounds.width, 140.0f);
    EXPECT_FLOAT_EQ(m_state.placement().bounds.height, 60.0f);
    EXPECT_EQ(m_model.text(), "first\nsecond");
    EXPECT_TRUE(m_state.focused());
    const HitTarget* area = target(id("area", "panel"));
    ASSERT_TRUE(area);
    EXPECT_TRUE(area->focusable && area->textEditable);
    EXPECT_FALSE(area->pointerGesture);
}

TEST_F(UiTextAreaBuilderTests, RawPointerCapturePersistsOutsideWithoutGestureRecords){
    ASSERT_TRUE(frameArea(1u));
    const WidgetId widget = id("area", "panel");
    const HitTarget* area = target(widget);
    ASSERT_TRUE(area);
    const Point origin{ area->rectangle.x + 10.0f, area->rectangle.y + 10.0f };
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_EQ(m_context.input().capture(), widget);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { 780.0f, 580.0f } }).pointerConsumed);
    EXPECT_EQ(m_context.input().capture(), widget);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, { 780.0f, 580.0f } }).pointerConsumed);
    EXPECT_FALSE(m_context.input().capture().valid());
    PointerGesture gesture;
    EXPECT_FALSE(m_context.input().consumePointerGesture(widget, area->declarationGeneration, gesture));
}

TEST_F(UiTextAreaBuilderTests, DisabledAreaRetainsAPointerBarrierAndDiscardsHostText){
    useHost();
    ASSERT_TRUE(m_model.setText("first\nsecond"));
    m_host.text("ignored");
    TextAreaOptions options;
    options.enabled = false;
    ASSERT_TRUE(frameArea(1u, options));
    const WidgetId widget = id("area", "panel");
    const HitTarget* area = target(widget);
    ASSERT_TRUE(area);
    EXPECT_FALSE(area->enabled || area->focusable || area->textEditable || area->pointerGesture);
    const Point point{ area->rectangle.x + 5.0f, area->rectangle.y + 5.0f };
    EXPECT_EQ(m_context.input().hitTest(point), MakeWidgetId(MakeRootId(m_root), "panel"));
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    EXPECT_FALSE(m_state.focused());
    EXPECT_EQ(m_model.text(), "first\nsecond");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaBuilderTests, PopupAreaBorrowsAndPublishesTheExplicitPopupIdentity){
    useHost();
    PopupState popup;
    popup.open();
    PopupOptions options;
    options.anchor = { 20.0f, 20.0f, 40.0f, 20.0f };
    options.size = { 300.0f, 240.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", popup, options));
    const PopupToken token = m_context.popupToken();
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state).valid);
    EXPECT_EQ(m_host.lastPopup, token);
    EXPECT_EQ(m_host.publishes, 0u);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_host.publications.size(), 1u);
    EXPECT_EQ(m_host.publications.front().popup, token);
    EXPECT_EQ(m_host.publications.front().mode, EditTextMode::Multiline);
    EXPECT_GT(m_state.placement().bounds.height, 0.0f);
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
    m_host.key(EditKey::Down);
    m_host.text("!");
    ASSERT_TRUE(frameArea(1u));
    ASSERT_EQ(m_host.resolutions.size(), 1u);
    EXPECT_EQ(m_host.resolutions.front().text, "aaa\naaaa\nxx");
    EXPECT_EQ(m_host.resolutions.front().caret, 2u);
    EXPECT_EQ(m_host.resolutions.front().result.committedByte, 6u);
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
    Rect caret;
    ASSERT_TRUE(reference.caretGeometry().caretRect(reference.displayCaret(), caret));
    m_host.seedColumn = true;
    m_host.seededColumn = caret.x;
    m_host.key(EditKey::Down);
    m_host.key(EditKey::Down);
    ASSERT_TRUE(frameArea(1u));
    ASSERT_EQ(m_host.resolutions.size(), 2u);
    EXPECT_EQ(m_host.resolutions[0u].result.committedByte, 8u);
    EXPECT_EQ(m_host.resolutions[1u].result.committedByte, 14u);
    EXPECT_TRUE(m_host.resolutions[1u].preferred.valid);
    EXPECT_FLOAT_EQ(m_host.resolutions[1u].preferred.preferredX, caret.x);
    EXPECT_FLOAT_EQ(m_state.navigation().preferredX(), caret.x);
    EXPECT_EQ(m_model.caret(), 14u);
    EXPECT_FALSE(m_model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

