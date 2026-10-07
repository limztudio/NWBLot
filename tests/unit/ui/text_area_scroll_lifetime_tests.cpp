// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_scroll_lifetime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiTextAreaTests;


struct AreaModelSnapshot{
    AString<Core::Alloc::GlobalArena> text;
    u64 instance = 0u;
    u64 revision = 0u;
    u64 external = 0u;
    u64 selection = 0u;
    u64 composition = 0u;
    usize anchor = 0u;
    usize caret = 0u;
    bool composing = false;
    bool undo = false;
    bool redo = false;

    AreaModelSnapshot(Core::Alloc::GlobalArena& arena, const EditModel& model)
        : text(arena)
        , instance(model.instanceGeneration())
        , revision(model.revision())
        , external(model.externalRevision())
        , selection(model.selectionGeneration())
        , composition(model.compositionGeneration())
        , anchor(model.anchor())
        , caret(model.caret())
        , composing(model.composition().active)
        , undo(model.canUndo())
        , redo(model.canRedo())
    {
        text.assign(model.text().data(), model.text().size());
    }
};

class UiTextAreaScrollLifetimeTests : public TextAreaFixture{
protected:
    [[nodiscard]] static TextAreaOptions SmallOptions(){
        TextAreaOptions options;
        options.width = { LayoutSizePolicy::Fixed, 180.0f };
        options.height = { LayoutSizePolicy::Fixed, 100.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ParentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ChildOptions(){
        PopupOptions options;
        options.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }


public:
    UiTextAreaScrollLifetimeTests()
        : m_dense(m_arena)
    {
        const AStringView line("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789xx");
        for(usize row = 0u; row < 40u; ++row){
            if(row != 0u)
                m_dense.push_back('\n');
            m_dense.append(line.data(), line.size());
        }
    }


protected:
    [[nodiscard]] bool seedModel(){
        return m_model.setText(AStringView(m_dense)) && m_model.setSelection(0u, 0u) && m_state.scrollTo({});
    }

    [[nodiscard]] bool prepareOverflow(const u64 generation = 1u, const TextAreaOptions& options = SmallOptions(),
        const Rect& bounds = { 20.0f, 20.0f, 740.0f, 520.0f }
    ){
        return
            seedModel() && frameArea(generation, options, bounds)
            && m_state.scrollbars().horizontal.visible && m_state.scrollbars().vertical.visible
        ;
    }

    [[nodiscard]] WidgetId popupArea()const{
        const WidgetId parent = MakeWidgetId(MakeRootId(m_root), "parent");
        return MakeWidgetId(MakeWidgetId(parent, "child"), "area");
    }

    [[nodiscard]] bool popupFrame(const u64 generation){
        if(
            !begin(generation) || !m_builder.beginPopup("parent", m_parent, ParentOptions())
            || !m_builder.beginPopup("child", m_child, ChildOptions())
        )
            return false;
        m_result = m_builder.textArea("area", m_model, m_state, SmallOptions());
        return
            m_result.valid && m_builder.endPopup() && m_builder.endPopup()
            && m_context.endRoot() && m_context.finishFrame() && m_context.commitFrame(generation)
        ;
    }

    [[nodiscard]] bool wheel(const f64 delta = -1.0){
        InputEvent event;
        event.type = InputEventType::PointerWheel;
        event.position = UiComboTests::Center(m_state.scrollbars().viewport);
        event.scrollY = delta;
        return send(event).pointerConsumed;
    }

    [[nodiscard]] bool startThumb(const WidgetId area){
        const WidgetId thumbId = MakeWidgetId(area, "scroll.y.thumb");
        const HitTarget* thumb = target(thumbId);
        if(thumb == nullptr)
            return false;
        const Point origin = UiComboTests::Center(thumb->rectangle);
        const Point destination{ origin.x, origin.y + 24.0f };
        return
            send({ InputEventType::PrimaryDown, origin }).pointerConsumed
            && send({ InputEventType::PointerMove, destination }).pointerConsumed
            && m_context.input().capture() == thumbId
        ;
    }

    void expectModel(const EditModel& model, const AreaModelSnapshot& before)const{
        EXPECT_EQ(model.text(), AStringView(before.text));
        EXPECT_EQ(model.instanceGeneration(), before.instance);
        EXPECT_EQ(model.revision(), before.revision);
        EXPECT_EQ(model.externalRevision(), before.external);
        EXPECT_EQ(model.selectionGeneration(), before.selection);
        EXPECT_EQ(model.compositionGeneration(), before.composition);
        EXPECT_EQ(model.anchor(), before.anchor);
        EXPECT_EQ(model.caret(), before.caret);
        EXPECT_EQ(model.composition().active, before.composing);
        EXPECT_EQ(model.canUndo(), before.undo);
        EXPECT_EQ(model.canRedo(), before.redo);
        EXPECT_EQ(m_host.loans, 0u);
        EXPECT_EQ(m_host.publishes, 0u);
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    AString<Core::Alloc::GlobalArena> m_dense;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaScrollLifetimeTests, IdenticalExternalScrollIntentRetiresAnAcceptedTrackPage){
    ASSERT_TRUE(prepareOverflow());
    const AreaModelSnapshot model(m_arena, m_model);
    const WidgetId afterId = MakeWidgetId(id("area", "panel"), "scroll.y.after");
    const HitTarget* after = target(afterId);
    ASSERT_NE(after, nullptr);
    click(UiComboTests::Center(after->rectangle));
    const Point scroll = m_state.scroll();
    const u64 revision = m_state.revision();
    ASSERT_TRUE(m_state.scrollTo(scroll));
    EXPECT_GT(m_state.revision(), revision);
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, scroll.x);
    EXPECT_FLOAT_EQ(m_state.scroll().y, scroll.y);
    expectModel(m_model, model);
    after = target(afterId);
    ASSERT_NE(after, nullptr);
    click(UiComboTests::Center(after->rectangle));
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    EXPECT_GT(m_state.scroll().y, scroll.y);
    expectModel(m_model, model);
}

TEST_F(UiTextAreaScrollLifetimeTests, IdenticalAndAwayBackSelectionIntentsRetireAcceptedWheel){
    ASSERT_TRUE(prepareOverflow());
    u64 generation = 2u;
    for(const bool awayBack : { false, true }){
        ASSERT_TRUE(wheel());
        const u64 revision = m_model.revision();
        const u64 selection = m_model.selectionGeneration();
        if(awayBack){
            ASSERT_TRUE(m_model.setSelection(1u, 1u));
            ASSERT_TRUE(m_model.setSelection(0u, 0u));
        }
        else
            ASSERT_TRUE(m_model.setSelection(0u, 0u));
        const AreaModelSnapshot model(m_arena, m_model);
        EXPECT_EQ(m_model.revision(), revision);
        EXPECT_GT(m_model.selectionGeneration(), selection);
        ASSERT_TRUE(frameArea(generation, SmallOptions()));
        ++generation;
        EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
        expectModel(m_model, model);
        ASSERT_TRUE(wheel());
        ASSERT_TRUE(frameArea(generation, SmallOptions()));
        ++generation;
        EXPECT_GT(m_state.scroll().y, 0.0f);
        expectModel(m_model, model);
        ASSERT_TRUE(m_state.scrollTo({}));
        ASSERT_TRUE(frameArea(generation, SmallOptions()));
        ++generation;
        EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    }
}

TEST_F(UiTextAreaScrollLifetimeTests, SameTextReplacementAndModelRebindingRetireCopiedWheel){
    ASSERT_TRUE(prepareOverflow());
    ASSERT_TRUE(m_model.setSelection(m_model.text().size(), m_model.text().size()));
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    ASSERT_GT(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(wheel(1.0));
    const u64 revision = m_model.revision();
    const u64 external = m_model.externalRevision();
    ASSERT_TRUE(m_model.setText(AStringView(m_dense)));
    const AreaModelSnapshot replaced(m_arena, m_model);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_GT(m_model.externalRevision(), external);
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    EXPECT_NEAR(m_state.scroll().y, m_state.scrollbars().vertical.maximum, 0.001);
    expectModel(m_model, replaced);
    const f32 maximum = m_state.scroll().y;
    ASSERT_TRUE(wheel(1.0));
    ASSERT_TRUE(frameArea(4u, SmallOptions()));
    EXPECT_LT(m_state.scroll().y, maximum);
    expectModel(m_model, replaced);

    ASSERT_TRUE(m_model.setSelection(0u, 0u));
    ASSERT_TRUE(m_state.scrollTo({}));
    ASSERT_TRUE(frameArea(5u, SmallOptions()));
    ASSERT_TRUE(wheel());
    const AreaModelSnapshot original(m_arena, m_model);
    EditModel replacement(m_arena, {}, EditTextMode::Multiline);
    ASSERT_TRUE(replacement.setText(AStringView(m_dense)));
    ASSERT_TRUE(replacement.setSelection(0u, 0u));
    const AreaModelSnapshot rebound(m_arena, replacement);
    ASSERT_TRUE(beginArea(6u));
    ASSERT_TRUE(m_builder.textArea("area", replacement, m_state, SmallOptions()).valid);
    ASSERT_TRUE(acceptArea());
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    expectModel(m_model, original);
    expectModel(replacement, rebound);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(beginArea(7u));
    ASSERT_TRUE(m_builder.textArea("area", replacement, m_state, SmallOptions()).valid);
    ASSERT_TRUE(acceptArea());
    EXPECT_GT(m_state.scroll().y, 0.0f);
    expectModel(m_model, original);
    expectModel(replacement, rebound);
}

TEST_F(UiTextAreaScrollLifetimeTests, ActualViewportResizeRetiresWheelWithUnchangedOptions){
    TextAreaOptions options = SmallOptions();
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    const Rect narrow{ 20.0f, 20.0f, 240.0f, 160.0f };
    const Rect wider{ 20.0f, 20.0f, 320.0f, 160.0f };
    ASSERT_TRUE(prepareOverflow(1u, options, narrow));
    const AreaModelSnapshot model(m_arena, m_model);
    const Rect accepted = m_state.scrollbars().viewport;
    const u64 revision = m_state.revision();
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(beginArea(2u, wider));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, options).valid);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(acceptArea());
    EXPECT_GT(m_state.scrollbars().viewport.width, accepted.width);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
    expectModel(m_model, model);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(frameArea(3u, options, wider));
    EXPECT_GT(m_state.scroll().y, 0.0f);
    expectModel(m_model, model);
}

TEST_F(UiTextAreaScrollLifetimeTests, ChangedWheelLinesRetiresTheCopiedStepAndAcceptsItsNewPolicy){
    ASSERT_TRUE(prepareOverflow());
    const AreaModelSnapshot model(m_arena, m_model);
    const HitTarget* host = target(id("area", "panel"));
    ASSERT_NE(host, nullptr);
    const f64 oldStep = host->scrollStep;
    ASSERT_GT(oldStep, 0.0);
    ASSERT_TRUE(wheel());
    TextAreaOptions options = SmallOptions();
    options.wheelLines = 7.0f;
    ASSERT_TRUE(frameArea(2u, options));
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    expectModel(m_model, model);
    host = target(id("area", "panel"));
    ASSERT_NE(host, nullptr);
    EXPECT_GT(host->scrollStep, oldStep);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(frameArea(3u, options));
    EXPECT_GT(m_state.scroll().y, static_cast<f32>(oldStep));
    expectModel(m_model, model);
}

TEST_F(UiTextAreaScrollLifetimeTests, ClosingAnAncestorRetiresWheelAndCaptureBeforeFreshPopupReuse){
    ASSERT_TRUE(seedModel());
    m_parent.open();
    m_child.open();
    ASSERT_TRUE(popupFrame(1u));
    const AreaModelSnapshot model(m_arena, m_model);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(startThumb(popupArea()));
    const Point release = UiComboTests::Center(m_state.scrollbars().vertical.thumb);
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPopup("parent", m_parent, ParentOptions()));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, SmallOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    m_parent.close();
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, release }).pointerConsumed);
    expectModel(m_model, model);
    m_parent.open();
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPopup("parent", m_parent, ParentOptions()));
    EXPECT_FALSE(m_builder.beginPopup("child", m_child, ChildOptions()));
    EXPECT_FALSE(m_child.isOpen());
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(3u));
    m_child.open();
    ASSERT_TRUE(popupFrame(4u));
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(popupFrame(5u));
    EXPECT_GT(m_state.scroll().y, 0.0f);
    expectModel(m_model, model);
}

TEST_F(UiTextAreaScrollLifetimeTests, CaptureLossCancelsTheUnfinishedThumbAndPreservesCompletedWheel){
    ASSERT_TRUE(prepareOverflow());
    const AreaModelSnapshot model(m_arena, m_model);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(startThumb(id("area", "panel")));
    EXPECT_FALSE(send({ InputEventType::PointerCaptureLost }).capture.valid());
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    ASSERT_GT(m_state.scroll().y, 0.0f);
    const f32 wheelOnly = m_state.scroll().y;
    expectModel(m_model, model);
    ASSERT_TRUE(m_state.scrollTo({}));
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(frameArea(4u, SmallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().y, wheelOnly);
    expectModel(m_model, model);
}

TEST_F(UiTextAreaScrollLifetimeTests, NativeFocusLossCancelsCopiedWheelAndAnActiveThumb){
    ASSERT_TRUE(prepareOverflow());
    const AreaModelSnapshot model(m_arena, m_model);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(startThumb(id("area", "panel")));
    EXPECT_FALSE(send({ InputEventType::FocusLost }).wantsKeyboard);
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    expectModel(m_model, model);
    EXPECT_FALSE(send({ InputEventType::FocusGained }).wantsKeyboard);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    EXPECT_GT(m_state.scroll().y, 0.0f);
    expectModel(m_model, model);
}

TEST_F(UiTextAreaScrollLifetimeTests, DisablingRetiresPendingWheelAndThumbWithoutReplayingOnEnable){
    ASSERT_TRUE(prepareOverflow());
    const AreaModelSnapshot model(m_arena, m_model);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(startThumb(id("area", "panel")));
    const Point release = UiComboTests::Center(m_state.scrollbars().vertical.thumb);
    TextAreaOptions disabled = SmallOptions();
    disabled.enabled = false;
    ASSERT_TRUE(frameArea(2u, disabled));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, release }).pointerConsumed);
    expectModel(m_model, model);
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(frameArea(4u, SmallOptions()));
    EXPECT_GT(m_state.scroll().y, 0.0f);
    expectModel(m_model, model);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

