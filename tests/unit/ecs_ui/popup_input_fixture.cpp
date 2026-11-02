// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_input_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EcsUiPopupInputTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiPopupInputTests::UiPopupInputTests()
    : m_arena(Name("tests/ecs_ui/popup_input"))
    , m_context(m_arena)
    , m_textInput(m_arena)
    , m_clipboard(m_arena, true)
    , m_host(m_arena, m_context, m_textInput, m_clipboard)
    , m_baseModel(m_arena)
    , m_popupModel(m_arena)
    , m_childModel(m_arena)
    , m_shaper(m_arena)
    , m_layoutBuilder(m_arena, m_shaper)
{}

void UiPopupInputTests::SetUp(){
    ASSERT_TRUE(m_textInput.setFocused(true));
    ASSERT_TRUE(m_baseModel.setText("base"));
    ASSERT_TRUE(m_popupModel.setText(s_PopupKey));
    ASSERT_TRUE(m_childModel.setText(s_ChildKey));
}

Expected<UiPopupInputTests::DeclaredEdit> UiPopupInputTests::declareEdit(const AStringView key, const Ui::Rect& bounds, Ui::EditModel& model){
    const auto widget = m_context.declare(key, Ui::WidgetKind::EditBox);
    if(!widget)
        return MakeUnexpected(Failure{});
    const Ui::WidgetState retained = **widget;
    const Ui::EditBoxOptions options;
    const Ui::EditBoxResult result = m_host.edit(retained, options, model);
    Ui::EditBoxView view(m_arena);
    if(!result.valid || !UiEditHostFixtureSupport::PrepareEditView(model, m_layoutBuilder, view))
        return MakeUnexpected(Failure{});
    const auto placement = view.arrange(bounds, {}, m_viewport, {});
    if(!placement || !m_host.publish(retained, view, *placement, options))
        return MakeUnexpected(Failure{});
    Ui::HitTarget target;
    target.rectangle = bounds;
    target.clip = m_viewport;
    target.focusable = true;
    target.pointerGesture = true;
    target.textEditable = true;
    if(!m_context.addTarget(retained, target))
        return MakeUnexpected(Failure{});
    return DeclaredEdit{ retained, result };
}

b8 UiPopupInputTests::beginPreparedFrame(){
    ++m_generation;
    if(!m_context.beginFrame(m_generation))
        return false;
    m_host.beginFrame(m_generation, { m_viewport.width, m_viewport.height, 1.0f, 1.0f });
    return m_context.beginRoot({ 17u, 1u });
}

b8 UiPopupInputTests::beginBaseFrame(){
    if(!beginPreparedFrame())
        return false;
    const auto baseEdit = declareEdit("base", { 10.0f, 20.0f, 180.0f, 30.0f }, m_baseModel);
    if(!baseEdit)
        return false;
    m_baseWidget = baseEdit->widget;
    return true;
}

b8 UiPopupInputTests::endPreparedFrame(){
    if(!m_context.endRoot() || !m_context.finishFrame())
        return false;
    m_host.finishFrame();
    return true;
}

b8 UiPopupInputTests::prepare(){
    if(!beginBaseFrame())
        return false;
    const auto popup = m_context.declare(s_PopupKey, Ui::WidgetKind::Popup);
    if(!popup)
        return false;
    const Ui::WidgetState owner = **popup;
    m_token = { owner.id, owner.declarationGeneration, m_popupState.instanceGeneration(), m_popupState.openGeneration() };
    m_context.input().fencePopup(m_token);
    if(m_context.input().consumePopupDismissal(m_token))
        m_popupState.close();
    if(m_popupState.isOpen()){
        Ui::PopupScope scope;
        scope.token = m_token;
        scope.bounds = { 230.0f, 40.0f, 240.0f, 100.0f };
        scope.viewport = m_viewport;
        if(!m_context.beginPopupScope(owner, scope) || !m_context.pushScope(s_PopupKey))
            return false;
        Ui::HitTarget barrier;
        barrier.rectangle = scope.bounds;
        barrier.clip = m_viewport;
        if(!m_context.addTarget(owner, barrier))
            return false;
        const auto popupEdit = declareEdit(s_EditKey, { 250.0f, 60.0f, 180.0f, 30.0f }, m_popupModel);
        if(!popupEdit)
            return false;
        m_popupWidget = popupEdit->widget;
        m_popupResult = popupEdit->result;
        if(!m_context.popScope() || !m_context.endPopupScope(true))
            return false;
    }
    else
        m_context.input().closePopup(m_token);
    return endPreparedFrame();
}

b8 UiPopupInputTests::prepareDeferred(){
    if(!beginPreparedFrame())
        return false;
    auto declared = m_context.declare(s_PopupKey, Ui::WidgetKind::Popup);
    if(!declared)
        return false;
    const Ui::WidgetState popup = **declared;
    m_token = { popup.id, popup.declarationGeneration, m_popupState.instanceGeneration(), m_popupState.openGeneration() };
    m_context.input().fencePopup(m_token);
    if(m_popupState.isOpen()){
        declared = m_context.declarePart(popup, s_EditKey, Ui::WidgetKind::EditBox);
        if(!declared)
            return false;
        m_popupWidget = **declared;
        m_popupResult = m_host.editInPopup(m_popupWidget, {}, m_token, m_popupModel);
        if(!m_popupResult.valid || m_context.popupToken().valid())
            return false;
        Ui::EditBoxView view(m_arena);
        if(!UiEditHostFixtureSupport::PrepareEditView(m_popupModel, m_layoutBuilder, view))
            return false;
        const auto placement = view.arrange({ 250.0f, 60.0f, 180.0f, 30.0f }, {}, m_viewport, {});
        if(!placement)
            return false;
        Ui::PopupScope scope;
        scope.token = m_token;
        scope.bounds = { 230.0f, 40.0f, 240.0f, 100.0f };
        scope.viewport = m_viewport;
        if(!m_context.beginPopupScope(popup, scope) || !m_host.publish(m_popupWidget, view, *placement, {}))
            return false;
        Ui::HitTarget barrier;
        barrier.rectangle = scope.bounds;
        barrier.clip = m_viewport;
        if(!m_context.addTarget(popup, barrier))
            return false;
        Ui::HitTarget target;
        target.rectangle = placement->bounds;
        target.clip = m_viewport;
        target.focusable = true;
        target.textEditable = true;
        if(!m_context.addTarget(m_popupWidget, target) || !m_context.endPopupScope(true))
            return false;
    }
    else
        m_context.input().closePopup(m_token);
    return endPreparedFrame();
}

b8 UiPopupInputTests::prepareNested(){
    if(!beginBaseFrame())
        return false;
    auto declared = m_context.declare(s_PopupKey, Ui::WidgetKind::Popup);
    if(!declared)
        return false;
    const Ui::WidgetState parent = **declared;
    m_token = { parent.id, parent.declarationGeneration, m_popupState.instanceGeneration(), m_popupState.openGeneration() };
    m_context.input().fencePopup(m_token);
    if(m_context.input().consumePopupDismissal(m_token))
        m_popupState.close();
    if(m_nestedParentToken.valid() && m_nestedParentToken != m_token)
        m_childState.close();
    m_nestedParentToken = m_token;
    if(m_popupState.isOpen()){
        Ui::PopupScope scope;
        scope.token = m_token;
        scope.bounds = { 230.0f, 40.0f, 240.0f, 100.0f };
        scope.viewport = m_viewport;
        if(!m_context.beginPopupScope(parent, scope) || !m_context.pushScope(s_PopupKey))
            return false;
        Ui::HitTarget barrier;
        barrier.rectangle = scope.bounds;
        barrier.clip = m_viewport;
        if(!m_context.addTarget(parent, barrier))
            return false;
        const auto popupEdit = declareEdit(s_EditKey, { 250.0f, 60.0f, 180.0f, 30.0f }, m_popupModel);
        if(!popupEdit)
            return false;
        m_popupWidget = popupEdit->widget;
        m_popupResult = popupEdit->result;
        declared = m_context.declare(s_ChildKey, Ui::WidgetKind::Popup);
        if(!declared)
            return false;
        const Ui::WidgetState child = **declared;
        m_childToken = { child.id, child.declarationGeneration, m_childState.instanceGeneration(), m_childState.openGeneration() };
        m_context.input().fencePopup(m_childToken);
        if(m_context.input().consumePopupDismissal(m_childToken))
            m_childState.close();
        if(m_childState.isOpen()){
            scope.token = m_childToken;
            scope.parent = m_token;
            scope.bounds = { 60.0f, 80.0f, 240.0f, 100.0f };
            if(!m_context.beginPopupScope(child, scope) || !m_context.pushScope(s_ChildKey))
                return false;
            barrier.rectangle = scope.bounds;
            if(!m_context.addTarget(child, barrier))
                return false;
            const auto childEdit = declareEdit(s_EditKey, { 80.0f, 100.0f, 180.0f, 30.0f }, m_childModel);
            if(!childEdit)
                return false;
            m_childWidget = childEdit->widget;
            m_childResult = childEdit->result;
            if(!m_context.popScope() || !m_context.endPopupScope(true))
                return false;
        }
        else
            m_context.input().closePopup(m_childToken);
        if(!m_context.popScope() || !m_context.endPopupScope(true))
            return false;
    }
    else
        m_context.input().closePopup(m_token);
    return endPreparedFrame();
}

b8 UiPopupInputTests::commit(){
    if(!m_context.commitFrame(m_generation))
        return false;
    m_host.commitFrame(m_generation);
    return true;
}

Ui::InputRoutingResult UiPopupInputTests::dispatch(const Ui::InputEvent& event){
    m_host.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queue(UiTestSupport::NativeInput(event));
    EXPECT_TRUE(normalized);
    if(!normalized)
        return {};
    const b8 submit = m_host.prepareSubmit(*normalized);
    const Ui::InputRoutingResult result = m_context.input().process();
    m_host.input(*normalized, previousCapture, submit);
    return result;
}

void UiPopupInputTests::key(const Core::Key::Enum value, const b8 control){
    EXPECT_TRUE(dispatch({ .position = {}, .key = value, .type = Ui::InputEventType::KeyDown, .control = control }).commandConsumed);
    EXPECT_TRUE(dispatch({ .position = {}, .key = value, .type = Ui::InputEventType::KeyUp, .control = control }).commandConsumed);
}

void UiPopupInputTests::focusBase(){
    ASSERT_TRUE(frame());
    key(Core::Key::Tab);
    ASSERT_EQ(m_context.input().focus(), m_baseWidget.id);
    ASSERT_TRUE(frame());
    ASSERT_TRUE(m_textInput.activeSession().valid());
    ASSERT_EQ(m_textInput.surroundingText(), "base");
}

void UiPopupInputTests::focusPopup(){
    focusBase();
    m_popupState.open();
    ASSERT_TRUE(frame());
    ASSERT_EQ(m_context.input().focus(), m_popupWidget.id);
    ASSERT_TRUE(frame());
    ASSERT_TRUE(m_textInput.activeSession().valid());
    ASSERT_EQ(m_textInput.surroundingText(), s_PopupKey);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

