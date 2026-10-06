// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_input_scroll_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextAreaTests;


struct ScrollModelRecord{
    AString<Core::Alloc::GlobalArena> text;
    u64 revision;
    u64 external;
    u64 selection;
    u64 composition;
    usize anchor;
    usize caret;
    bool undo;
    bool redo;

    ScrollModelRecord(Core::Alloc::GlobalArena& arena, const EditModel& model)
        : text(arena)
        , revision(model.revision())
        , external(model.externalRevision())
        , selection(model.selectionGeneration())
        , composition(model.compositionGeneration())
        , anchor(model.anchor())
        , caret(model.caret())
        , undo(model.canUndo())
        , redo(model.canRedo())
    { text.assign(model.text().data(), model.text().size()); }

    void expect(const EditModel& model)const{
        EXPECT_EQ(model.text(), AStringView(text));
        EXPECT_EQ(model.revision(), revision);
        EXPECT_EQ(model.externalRevision(), external);
        EXPECT_EQ(model.selectionGeneration(), selection);
        EXPECT_EQ(model.compositionGeneration(), composition);
        EXPECT_EQ(model.anchor(), anchor);
        EXPECT_EQ(model.caret(), caret);
        EXPECT_EQ(model.canUndo(), undo);
        EXPECT_EQ(model.canRedo(), redo);
    }
};


class UiTextAreaInputScrollTests : public TextAreaFixture{
protected:
    [[nodiscard]] static TextAreaOptions ScrollOptions(){
        TextAreaOptions options;
        options.width = { LayoutSizePolicy::Fixed, 180.0f };
        options.height = { LayoutSizePolicy::Fixed, 96.0f };
        return options;
    }


public:
    UiTextAreaInputScrollTests()
        : m_document(m_arena)
    {
        m_document.reserve(24u * 65u);
        for(usize row = 0u; row < 24u; ++row){
            if(row != 0u)
                m_document.push_back('\n');
            m_document.append(64u, 'a');
        }
    }


protected:
    [[nodiscard]] bool prepareDocument(const Point scroll = {}){
        return
            m_model.setText(m_document) && m_model.setSelection(1u, 1u) && m_model.replaceSelection("b")
            && m_model.setSelection(0u, 0u) && m_state.scrollTo(scroll)
        ;
    }

    [[nodiscard]] bool prepareScroll(const TextAreaOptions& options = ScrollOptions(), const Point scroll = {}){
        return prepareDocument(scroll) && frameArea(1u, options);
    }

    [[nodiscard]] bool wheel(const f64 x, const f64 y, const Point position){
        InputEvent event;
        event.type = InputEventType::PointerWheel;
        event.position = position;
        event.scrollX = x;
        event.scrollY = y;
        return send(event).pointerConsumed;
    }

    [[nodiscard]] bool wheel(const f64 x, const f64 y){ return wheel(x, y, Center(m_state.placement().content)); }

    [[nodiscard]] WidgetId area()const{ return id("area", "panel"); }
    [[nodiscard]] WidgetId part(const AStringView key)const{ return MakeWidgetId(area(), key); }

    [[nodiscard]] bool framePopup(const u64 generation, PopupState& popup){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 40.0f, 20.0f };
        options.size = { 300.0f, 180.0f };
        if(!begin(generation) || !m_builder.beginPopup("popup", popup, options))
            return false;
        m_result = m_builder.textArea("area", m_model, m_state, ScrollOptions());
        return
            m_result.valid && m_builder.endPopup() && m_context.endRoot() && m_context.finishFrame()
            && m_context.commitFrame(generation)
        ;
    }


protected:
    AString<Core::Alloc::GlobalArena> m_document;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaInputScrollTests, InvalidWheelLinesRejectTheCandidateBeforeBorrowingOrChangingTheAcceptedViewport){
    useHost();
    m_host.focused = false;
    ASSERT_TRUE(prepareDocument({ 20.0f, 30.0f }));
    const f32 invalid[]{ 0.0f, -1.0f, Limit<f32>::s_QuietNaN };
    u64 generation = 1u;
    for(const f32 wheelLines : invalid){
        ASSERT_TRUE(frameArea(generation++, ScrollOptions()));
        const ScrollModelRecord model(m_arena, m_model);
        const Point scroll = m_state.scroll();
        const EditBoxPlacement placement = m_state.placement();
        const EditNavigationSnapshot navigation = m_state.navigation().snapshot();
        const u64 revision = m_state.revision();
        const usize loans = m_host.loans;
        const usize publishes = m_host.publishes;
        const usize targets = m_context.input().targets().size();
        const HitTarget* displayed = target(area());
        ASSERT_TRUE(displayed);
        const HitTarget accepted = *displayed;
        TextAreaOptions options = ScrollOptions();
        options.wheelLines = wheelLines;
        ASSERT_TRUE(beginArea(generation));
        const EditBoxResult result = m_builder.textArea("area", m_model, m_state, options);
        EXPECT_FALSE(result.valid);
        EXPECT_TRUE(m_context.failed());
        EXPECT_FALSE(m_builder.endPanel());
        EXPECT_FALSE(m_context.finishFrame());
        EXPECT_FALSE(m_context.commitFrame(generation));
        EXPECT_EQ(m_context.input().layoutGeneration(), generation - 1u);
        EXPECT_EQ(m_context.input().targets().size(), targets);
        ASSERT_TRUE(target(area()));
        EXPECT_EQ(target(area())->control, accepted.control);
        ExpectRect(target(area())->rectangle, accepted.rectangle);
        ExpectRect(m_state.placement().bounds, placement.bounds);
        ExpectRect(m_state.placement().content, placement.content);
        EXPECT_FLOAT_EQ(m_state.scroll().x, scroll.x);
        EXPECT_FLOAT_EQ(m_state.scroll().y, scroll.y);
        EXPECT_EQ(m_state.revision(), revision);
        EXPECT_TRUE(m_state.navigation().matches(navigation));
        EXPECT_EQ(m_host.loans, loans);
        EXPECT_EQ(m_host.publishes, publishes);
        model.expect(m_model);
        m_builder.reset();
        m_context.abandonFrame();
        ++generation;
    }
}

TEST_F(UiTextAreaInputScrollTests, CopiedVerticalWheelAppliesOnlyAtFinalPaintAndPreservesTheModel){
    ASSERT_TRUE(prepareScroll());
    const ScrollModelRecord model(m_arena, m_model);
    const u64 revision = m_state.revision();
    const EditNavigationSnapshot navigation = m_state.navigation().snapshot();
    const HitTarget* host = target(area());
    ASSERT_TRUE(host);
    const f64 step = host->scrollStep;
    ASSERT_GT(step, 0.0);
    ASSERT_TRUE(wheel(0.0, -1.0));
    ASSERT_EQ(m_context.input().controlActions().size(), 1u);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(beginArea(2u));
    m_result = m_builder.textArea("area", m_model, m_state, ScrollOptions());
    ASSERT_TRUE(m_result.valid);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(acceptArea());
    EXPECT_NEAR(m_state.scroll().y, step, 0.001);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_TRUE(m_state.navigation().matches(navigation));
    EXPECT_FALSE(m_result.textChanged || m_result.selectionChanged);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().controlActions().empty());
    model.expect(m_model);
}

TEST_F(UiTextAreaInputScrollTests, LargeWheelDeltasClampBothEndpointsWithoutChangingSelectionOrHistory){
    ASSERT_TRUE(prepareScroll());
    const ScrollModelRecord model(m_arena, m_model);
    const ScrollViewportPlacement bars = m_state.scrollbars();
    ASSERT_GT(bars.horizontal.maximum, 0.0);
    ASSERT_GT(bars.vertical.maximum, 0.0);
    ASSERT_TRUE(wheel(1000000.0, -1000000.0));
    ASSERT_TRUE(frameArea(2u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().x, bars.horizontal.maximum, 0.001);
    EXPECT_NEAR(m_state.scroll().y, bars.vertical.maximum, 0.001);
    ASSERT_TRUE(wheel(-1000000.0, 1000000.0));
    ASSERT_TRUE(frameArea(3u, ScrollOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    model.expect(m_model);
}

TEST_F(UiTextAreaInputScrollTests, ReadOnlyWheelAndThumbScrollingLeaveUndoAndRedoIntact){
    TextAreaOptions options = ScrollOptions();
    options.readOnly = true;
    ASSERT_TRUE(prepareScroll(options));
    const ScrollModelRecord model(m_arena, m_model);
    ASSERT_TRUE(wheel(1.0, -1.0));
    ASSERT_TRUE(frameArea(2u, options));
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    const HitTarget* found = target(part("scroll.y.thumb"));
    ASSERT_TRUE(found);
    const HitTarget thumb = *found;
    const Point origin = Center(thumb.rectangle);
    const Point destination{ origin.x, thumb.gestureReference.y + thumb.gestureReference.height + 50.0f };
    drag(origin, destination);
    ASSERT_TRUE(frameArea(3u, options));
    EXPECT_NEAR(m_state.scroll().y, m_state.scrollbars().vertical.maximum, 0.001);
    model.expect(m_model);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), AStringView(m_document));
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), AStringView(model.text));
}

TEST_F(UiTextAreaInputScrollTests, DisabledEditorUsesTheContainingPanelBarrierAndCannotScroll){
    TextAreaOptions options = ScrollOptions();
    options.enabled = false;
    ASSERT_TRUE(prepareScroll(options));
    const ScrollModelRecord model(m_arena, m_model);
    const HitTarget* host = target(area());
    ASSERT_TRUE(host);
    EXPECT_FALSE(host->enabled || host->focusable || host->textEditable || host->scrollable);
    const Point point = Center(m_state.placement().content);
    EXPECT_EQ(m_context.input().hitTest(point), MakeWidgetId(MakeRootId(m_root), "panel"));
    ASSERT_TRUE(wheel(1.0, -1.0, point));
    EXPECT_TRUE(m_context.input().controlActions().empty());
    click(point);
    ASSERT_TRUE(frameArea(2u, options));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_state.focused());
    model.expect(m_model);
}

TEST_F(UiTextAreaInputScrollTests, WheelViewportPersistsUntilAnAcceptedIdenticalCaretIntentResumesReveal){
    ASSERT_TRUE(prepareScroll());
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    ASSERT_TRUE(wheel(1.0, -1.0));
    ASSERT_TRUE(frameArea(2u, ScrollOptions()));
    const Point scroll = m_state.scroll();
    ASSERT_GT(scroll.x, 0.0f);
    ASSERT_GT(scroll.y, 0.0f);
    ASSERT_TRUE(frameArea(3u, ScrollOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, scroll.x);
    EXPECT_FLOAT_EQ(m_state.scroll().y, scroll.y);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    ASSERT_TRUE(m_model.setSelection(m_model.anchor(), m_model.caret()));
    ASSERT_TRUE(frameArea(4u, ScrollOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_GT(m_model.selectionGeneration(), selection);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_TRUE(m_model.canUndo());
}

TEST_F(UiTextAreaInputScrollTests, UnfocusedThumbPressPreservesGrabOffsetAndTheBaselineAcrossRepaints){
    ASSERT_TRUE(prepareScroll(ScrollOptions(), { 30.0f, 50.0f }));
    const ScrollModelRecord model(m_arena, m_model);
    const HitTarget* found = target(part("scroll.y.thumb"));
    ASSERT_TRUE(found);
    const HitTarget thumb = *found;
    const f32 travel = thumb.gestureReference.height - thumb.rectangle.height;
    ASSERT_GT(travel, 0.0f);
    const Point origin{ thumb.rectangle.x + thumb.rectangle.width * 0.5f, thumb.rectangle.y + thumb.rectangle.height * 0.25f };
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_EQ(m_context.input().capture(), thumb.id);
    EXPECT_EQ(m_context.input().focus(), area());
    ASSERT_TRUE(frameArea(2u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().y, 50.0, 0.001);
    EXPECT_NEAR(m_state.scroll().x, 30.0, 0.001);
    const HitTarget* afterPress = target(part("scroll.y.thumb"));
    ASSERT_TRUE(afterPress);
    EXPECT_EQ(afterPress->control, thumb.control);
    const Point quarter{ origin.x, origin.y + travel * 0.25f };
    ASSERT_TRUE(send({ InputEventType::PointerMove, quarter }).pointerConsumed);
    ASSERT_TRUE(frameArea(3u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().y, 50.0 + thumb.gestureMaximum * 0.25, 0.001);
    const HitTarget* afterMove = target(part("scroll.y.thumb"));
    ASSERT_TRUE(afterMove);
    EXPECT_EQ(afterMove->control, thumb.control);
    EXPECT_GT(afterMove->rectangle.y, thumb.rectangle.y);
    const Point half{ origin.x, origin.y + travel * 0.5f };
    ASSERT_TRUE(send({ InputEventType::PointerMove, half }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PrimaryUp, half }).pointerConsumed);
    ASSERT_TRUE(frameArea(4u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().y, 50.0 + thumb.gestureMaximum * 0.5, 0.001);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 30.0f);
    EXPECT_FALSE(m_context.input().capture().valid());
    model.expect(m_model);
}

TEST_F(UiTextAreaInputScrollTests, CompletedHorizontalThumbDragKeepsOutsideReleaseAndClampsItsEndpoint){
    ASSERT_TRUE(prepareScroll());
    const ScrollModelRecord model(m_arena, m_model);
    const HitTarget* found = target(part("scroll.x.thumb"));
    ASSERT_TRUE(found);
    const HitTarget thumb = *found;
    const Point origin{ thumb.rectangle.x + thumb.rectangle.width * 0.25f, thumb.rectangle.y + thumb.rectangle.height * 0.5f };
    const Point outside{ 900.0f, origin.y };
    ASSERT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PointerMove, outside }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PointerLeave, {} }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PrimaryUp, outside }).pointerConsumed);
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    ASSERT_TRUE(frameArea(2u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().x, thumb.gestureMaximum, 0.001);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    model.expect(m_model);
}

TEST_F(UiTextAreaInputScrollTests, TrackPagesOnlyOnReleaseByTheAcceptedViewport){
    ASSERT_TRUE(prepareScroll(ScrollOptions(), { 0.0f, 100.0f }));
    const ScrollModelRecord model(m_arena, m_model);
    const f64 page = m_state.scrollbars().vertical.viewportExtent;
    const HitTarget* found = target(part("scroll.y.after"));
    ASSERT_TRUE(found);
    const HitTarget after = *found;
    const Point point = Center(after.rectangle);
    ASSERT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    ASSERT_TRUE(frameArea(2u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().y, 100.0, 0.001);
    ASSERT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    EXPECT_NEAR(m_state.scroll().y, 100.0, 0.001);
    ASSERT_TRUE(frameArea(3u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().y, 100.0 + page, 0.001);
    model.expect(m_model);
}

TEST_F(UiTextAreaInputScrollTests, LaterCoalescedThumbUpdateRunsAfterAnInterveningWheelAction){
    ASSERT_TRUE(prepareScroll());
    const ScrollModelRecord model(m_arena, m_model);
    const HitTarget* found = target(part("scroll.y.thumb"));
    ASSERT_TRUE(found);
    const HitTarget thumb = *found;
    const Point origin = Center(thumb.rectangle);
    const f32 travel = thumb.gestureReference.height - thumb.rectangle.height;
    ASSERT_GT(travel, 0.0f);
    ASSERT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    ASSERT_TRUE(wheel(0.0, -1.0, origin));
    ASSERT_EQ(m_context.input().controlActions().size(), 1u);
    const Point quarter{ origin.x, origin.y + travel * 0.25f };
    ASSERT_TRUE(send({ InputEventType::PointerMove, quarter }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PrimaryUp, quarter }).pointerConsumed);
    ASSERT_TRUE(frameArea(2u, ScrollOptions()));
    EXPECT_NEAR(m_state.scroll().y, thumb.gestureMaximum * 0.25, 0.001);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    model.expect(m_model);
}

TEST_F(UiTextAreaInputScrollTests, PopupWheelUsesTheAcceptedPopupScopeAndDefersUntilItsEnd){
    ASSERT_TRUE(prepareDocument());
    PopupState popup;
    popup.open();
    ASSERT_TRUE(framePopup(1u, popup));
    const WidgetId popupArea = id("area", "popup");
    const HitTarget* host = target(popupArea);
    ASSERT_TRUE(host);
    const PopupToken token = host->popup;
    ASSERT_TRUE(token.valid());
    const f64 step = host->scrollStep;
    const ScrollModelRecord model(m_arena, m_model);
    ASSERT_TRUE(wheel(0.0, -1.0));
    ASSERT_EQ(m_context.input().controlActions().size(), 1u);
    EXPECT_EQ(m_context.input().controlActions().front().popup, token);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(framePopup(2u, popup));
    EXPECT_NEAR(m_state.scroll().y, step, 0.001);
    model.expect(m_model);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

