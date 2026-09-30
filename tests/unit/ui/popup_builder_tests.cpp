// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ExpectBounds(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}

static PopupOptions Anchored(){
    PopupOptions options;
    options.anchor = { 40.0f, 60.0f, 80.0f, 24.0f };
    options.size = { 220.0f, 160.0f };
    return options;
}

static WidgetOptions Control(const f32 width = 120.0f, const f32 height = 30.0f){
    WidgetOptions options;
    options.width = { LayoutSizePolicy::Fixed, width };
    options.height = { LayoutSizePolicy::Fixed, height };
    return options;
}

class CountingEditHost final : public IEditBoxHost{
public:
    virtual ~CountingEditHost()override = default;


public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState&, EditModel& model, const EditBoxOptions&)override{
        ++loans;
        EditBoxResult result;
        result.valid = model.setText("borrowed");
        result.textChanged = result.valid;
        return result;
    }

    [[nodiscard]] virtual bool publish(const WidgetState&, const EditBoxView&,
        const EditBoxPlacement&, const EditBoxOptions&)override{
        ++publications;
        return true;
    }


public:
    u32 loans = 0u;
    u32 publications = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] bool finishPopup(){
        return m_builder.endPopup() && m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] bool finishRoot(){
        return m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetId ownerId(const AStringView key = "popup")const{
        return MakeWidgetId(MakeRootId(m_root), key);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupBuilderTests, ModalBackdropAndBodyRenderAboveAPanelDeclaredByALaterRoot){
    PopupState state;
    state.open();
    PopupOptions options = Anchored();
    options.modal = true;
    options.side = PopupPlacementSide::Center;
    options.size = { 180.0f, 120.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, options));
    EXPECT_FALSE(m_builder.button("apply", "Apply", Control()));
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_context.endRoot());
    const WidgetRoot laterRoot{ 87u, 1u };
    ASSERT_TRUE(m_context.beginRoot(laterRoot));
    ASSERT_TRUE(m_builder.beginPanel("later", { 0.0f, 0.0f, 800.0f, 600.0f }));
    EXPECT_FALSE(m_builder.button("apply", "Later", Control()));
    ASSERT_TRUE(m_builder.endPanel());
    ASSERT_TRUE(finishRoot());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_GE(snapshot.commands().size(), 4u);
    usize firstOverlay = snapshot.commands().size();
    for(usize index = 0u; index < snapshot.commands().size(); ++index){
        if(snapshot.commands()[index].layer == 1u && firstOverlay == snapshot.commands().size())
            firstOverlay = index;
        if(firstOverlay != snapshot.commands().size())
            EXPECT_EQ(snapshot.commands()[index].layer, 1u);
        else
            EXPECT_EQ(snapshot.commands()[index].layer, 0u);
    }
    ASSERT_GT(firstOverlay, 0u);
    ASSERT_LT(firstOverlay, snapshot.commands().size());
    const DrawCommand& backdrop = snapshot.commands()[firstOverlay];
    EXPECT_EQ(backdrop.material, PaintMaterial::Solid);
    EXPECT_EQ(backdrop.firstIndex, 0u);
    EXPECT_EQ(backdrop.indexCount, 6u);
    EXPECT_GT(snapshot.commands()[0u].firstIndex, backdrop.firstIndex);
    ExpectBounds(backdrop.clip, { 0.0f, 0.0f, 800.0f, 600.0f });
    const Vertex& first = snapshot.vertices()[snapshot.indices()[backdrop.firstIndex]];
    const Vertex& opposite = snapshot.vertices()[snapshot.indices()[backdrop.firstIndex + 2u]];
    EXPECT_FLOAT_EQ(first.position.x, 0.0f);
    EXPECT_FLOAT_EQ(first.position.y, 0.0f);
    EXPECT_FLOAT_EQ(opposite.position.x, 800.0f);
    EXPECT_FLOAT_EQ(opposite.position.y, 600.0f);
    EXPECT_FLOAT_EQ(first.color.a, m_builder.popupStyle().backdrop.a);
    ASSERT_TRUE(m_context.commitFrame(1u));
    ExpectBounds(state.placement().bounds, { 310.0f, 240.0f, 180.0f, 120.0f });
    const WidgetId child = id("apply", "popup");
    EXPECT_EQ(m_context.input().hitTest({ 325.0f, 255.0f }), child);
    EXPECT_FALSE(m_context.input().hitTest({ 10.0f, 10.0f }).valid());
    EXPECT_TRUE(m_context.input().wouldConsumePointer({ 10.0f, 10.0f }));
}

TEST_F(UiPopupBuilderTests, ExistingPanelAtlasFallbackSuppliesPopupImageAndBodyPadding){
    PopupState state;
    state.open();
    m_builder.popupStyle().padding = {};
    ASSERT_EQ(m_skin.findRegion(Name("popup.normal")), nullptr);
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    WidgetOptions stretch = Control();
    stretch.width = { LayoutSizePolicy::Stretch, 1.0f };
    stretch.height = { LayoutSizePolicy::Stretch, 1.0f };
    EXPECT_FALSE(m_builder.button("apply", "Apply", stretch));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    Rect background;
    ASSERT_TRUE(skinQuad(snapshot, 5u, background));
    ExpectBounds(background, state.placement().bounds);
    const HitTarget* child = target(id("apply", "popup"));
    ASSERT_NE(child, nullptr);
    ExpectBounds(child->rectangle, { 43.0f, 92.0f, 212.0f, 150.0f });
    ExpectBounds(child->clip, { 43.0f, 92.0f, 212.0f, 150.0f });
}

TEST_F(UiPopupBuilderTests, ClosingFromTheBodySkipsBackdropBodyPaintAndCandidateTargets){
    PopupState state;
    state.open();
    PopupOptions options = Anchored();
    options.modal = true;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, options));
    EXPECT_FALSE(m_builder.button("close", "Close", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot first = m_paint.freeze();
    EXPECT_FALSE(first.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId closeId = id("close", "popup");
    ASSERT_NE(target(closeId), nullptr);
    click({ target(closeId)->rectangle.x + 4.0f, target(closeId)->rectangle.y + 4.0f });
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, options));
    EXPECT_TRUE(m_builder.button("close", "Close", Control()));
    state.close();
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_TRUE(m_builder.balanced());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().hitTest({ 55.0f, 100.0f }).valid());
    ASSERT_TRUE(m_builder.beginPanel("base", { 0.0f, 0.0f, 100.0f, 80.0f }));
    EXPECT_FALSE(m_builder.button("apply", "Base", Control(80.0f, 24.0f)));
    ASSERT_TRUE(m_builder.endPanel());
    ASSERT_TRUE(finishRoot());
    const DrawSnapshot closed = m_paint.freeze();
    ASSERT_FALSE(closed.commands().empty());
    for(const DrawCommand& command : closed.commands()){
        EXPECT_EQ(command.layer, 0u);
        EXPECT_NE(command.material, PaintMaterial::Solid);
    }
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(target(ownerId()), nullptr);
    EXPECT_EQ(target(closeId), nullptr);
    EXPECT_NE(target(id("apply", "base")), nullptr);
}

TEST_F(UiPopupBuilderTests, ClosingAfterTheFirstQueuedButtonPreventsLaterQueuedActionsInTheSameBody){
    PopupState state;
    state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_FALSE(m_builder.button("close", "Close", Control()));
    EXPECT_FALSE(m_builder.button("later", "Later", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot first = m_paint.freeze();
    EXPECT_FALSE(first.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* close = target(id("close", "popup"));
    const HitTarget* later = target(id("later", "popup"));
    ASSERT_NE(close, nullptr);
    ASSERT_NE(later, nullptr);
    click({ close->rectangle.x + 4.0f, close->rectangle.y + 4.0f });
    click({ later->rectangle.x + 4.0f, later->rectangle.y + 4.0f });
    ASSERT_EQ(m_context.input().actions().size(), 2u);
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    ASSERT_TRUE(m_builder.button("close", "Close", Control()));
    state.close();
    EXPECT_FALSE(m_builder.button("later", "Later", Control()));
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot closed = m_paint.freeze();
    EXPECT_TRUE(closed.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiPopupBuilderTests, EditBoxAfterBodyCloseDoesNotLendItsModelOrChangeHostState){
    PopupState state;
    state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_FALSE(m_builder.button("close", "Close", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot first = m_paint.freeze();
    EXPECT_FALSE(first.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* close = target(id("close", "popup"));
    ASSERT_NE(close, nullptr);
    click({ close->rectangle.x + 4.0f, close->rectangle.y + 4.0f });
    EditModel model(m_arena);
    ASSERT_TRUE(model.setText("kept"));
    ASSERT_TRUE(model.setSelection(1u, 3u));
    const u64 revision = model.revision();
    const u64 externalRevision = model.externalRevision();
    EditBoxState editState;
    editState.scroll = 17.0f;
    editState.caretElapsed = 0.25f;
    CountingEditHost host;
    m_builder.setEditHost(&host);
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    ASSERT_TRUE(m_builder.button("close", "Close", Control()));
    state.close();
    const EditBoxResult result = m_builder.editBox("edit", model, editState);
    EXPECT_TRUE(result.valid);
    EXPECT_FALSE(result.textChanged);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_FALSE(result.focused);
    EXPECT_EQ(host.loans, 0u);
    EXPECT_EQ(host.publications, 0u);
    EXPECT_EQ(model.text(), "kept");
    EXPECT_EQ(model.revision(), revision);
    EXPECT_EQ(model.externalRevision(), externalRevision);
    EXPECT_EQ(model.anchor(), 1u);
    EXPECT_EQ(model.caret(), 3u);
    EXPECT_FALSE(model.canUndo());
    EXPECT_FLOAT_EQ(editState.scroll, 17.0f);
    EXPECT_FLOAT_EQ(editState.caretElapsed, 0.25f);
    EXPECT_EQ(editState.modelGeneration, 0u);
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot closed = m_paint.freeze();
    EXPECT_TRUE(closed.commands().empty());
    EXPECT_EQ(host.loans, 0u);
    EXPECT_EQ(host.publications, 0u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(id("edit", "popup")), nullptr);
    m_builder.setEditHost(nullptr);
}

TEST_F(UiPopupBuilderTests, CloseAndReopenInTheBodySkipsOldEpochPaintAndActionsUntilTheNextFrame){
    PopupState state;
    state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_FALSE(m_builder.button("close", "Close", Control()));
    EXPECT_FALSE(m_builder.button("later", "Later", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot first = m_paint.freeze();
    EXPECT_FALSE(first.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(ownerId()), nullptr);
    const PopupToken oldToken = target(ownerId())->popup;
    const HitTarget* close = target(id("close", "popup"));
    const HitTarget* later = target(id("later", "popup"));
    ASSERT_NE(close, nullptr);
    ASSERT_NE(later, nullptr);
    click({ close->rectangle.x + 4.0f, close->rectangle.y + 4.0f });
    click({ later->rectangle.x + 4.0f, later->rectangle.y + 4.0f });
    ASSERT_EQ(m_context.input().actions().size(), 2u);
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    ASSERT_TRUE(m_builder.button("close", "Close", Control()));
    state.close();
    state.open();
    EXPECT_GT(state.openGeneration(), oldToken.openGeneration);
    EXPECT_FALSE(m_builder.button("later", "Later", Control()));
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(finishPopup());
    EXPECT_TRUE(state.isOpen());
    const DrawSnapshot reopening = m_paint.freeze();
    EXPECT_TRUE(reopening.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(m_context.input().targets().empty());
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_FALSE(m_builder.button("close", "Close", Control()));
    EXPECT_FALSE(m_builder.button("later", "Later", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot reopened = m_paint.freeze();
    EXPECT_FALSE(reopened.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(3u));
    ASSERT_NE(target(ownerId()), nullptr);
    EXPECT_EQ(target(ownerId())->popup.instanceGeneration, oldToken.instanceGeneration);
    EXPECT_EQ(target(ownerId())->popup.openGeneration, state.openGeneration());
    EXPECT_TRUE(m_context.input().actions().empty());
    const HitTarget* reopenedLater = target(id("later", "popup"));
    ASSERT_NE(reopenedLater, nullptr);
    click({ reopenedLater->rectangle.x + 4.0f, reopenedLater->rectangle.y + 4.0f });
    ASSERT_TRUE(begin(4u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_FALSE(m_builder.button("close", "Close", Control()));
    EXPECT_TRUE(m_builder.button("later", "Later", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot active = m_paint.freeze();
    EXPECT_FALSE(active.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(4u));
}

TEST_F(UiPopupBuilderTests, NestedPopupEndRestoresItsParentAndPublishesBothScopes){
    PopupState first;
    PopupState nested;
    first.open();
    nested.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", first, Anchored()));
    const PopupToken parent = m_context.popupToken();
    ASSERT_TRUE(m_builder.beginPopup("nested", nested, Anchored()));
    EXPECT_NE(m_context.popupToken(), parent);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_context.popupToken(), parent);
    EXPECT_FALSE(m_builder.balanced());
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_TRUE(m_builder.balanced());
    ASSERT_TRUE(finishRoot());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_FALSE(snapshot.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_context.input().popupCount(), 2u);
    EXPECT_EQ(m_context.input().popupScope(m_context.input().topPopupToken())->parent, parent);
}

TEST_F(UiPopupBuilderTests, PopupRequiresThePrecedingPanelToBeBalanced){
    PopupState state;
    state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("base", { 0.0f, 0.0f, 100.0f, 80.0f }));
    EXPECT_FALSE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.balanced());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiPopupBuilderTests, OpenBodyContainerPreventsEndPopupAndRootPublication){
    PopupState state;
    state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    ASSERT_TRUE(m_builder.beginColumn("body"));
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.balanced());
    EXPECT_FALSE(m_context.endRoot());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiPopupBuilderTests, WrongPanelEndCannotCloseAPopupLoanOrPublishIt){
    PopupState state;
    state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.balanced());
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_builder.balanced());
    EXPECT_FALSE(m_context.endRoot());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiPopupBuilderTests, ReplacementClosedStateImmediatelyFencesPreviousAcceptedPopupEpoch){
    PopupState original;
    original.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", original, Anchored()));
    EXPECT_FALSE(m_builder.button("apply", "Apply", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot accepted = m_paint.freeze();
    EXPECT_FALSE(accepted.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId child = id("apply", "popup");
    ASSERT_NE(target(child), nullptr);
    click({ target(child)->rectangle.x + 4.0f, target(child)->rectangle.y + 4.0f });
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    PopupState replacement;
    ASSERT_NE(replacement.instanceGeneration(), original.instanceGeneration());
    ASSERT_TRUE(begin(2u));
    EXPECT_FALSE(m_builder.beginPopup("popup", replacement, Anchored()));
    EXPECT_FALSE(m_context.failed());
    EXPECT_TRUE(m_builder.balanced());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().hitTest({ 55.0f, 100.0f }).valid());
    EXPECT_TRUE(original.isOpen());
    EXPECT_FALSE(replacement.isOpen());
    ASSERT_TRUE(finishRoot());
    const DrawSnapshot closed = m_paint.freeze();
    EXPECT_TRUE(closed.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiPopupBuilderTests, MissingPopupAndPanelSkinRejectsAdmissionWithoutChangingPreviousPlacement){
    PopupState state;
    state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("popup", state, Anchored()));
    EXPECT_FALSE(m_builder.button("apply", "Apply", Control()));
    ASSERT_TRUE(finishPopup());
    const DrawSnapshot accepted = m_paint.freeze();
    EXPECT_FALSE(accepted.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const Rect previous = state.placement().bounds;
    configureSkin("panel.normal");
    PopupOptions moved = Anchored();
    moved.anchor = { 300.0f, 300.0f, 80.0f, 24.0f };
    ASSERT_TRUE(begin(2u));
    EXPECT_FALSE(m_builder.beginPopup("popup", state, moved));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_builder.balanced());
    ExpectBounds(state.placement().bounds, previous);
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_NE(target(id("apply", "popup")), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

