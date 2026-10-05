// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/edit_box_host.h>
#include <impl/ecs_ui/toolkit/widgets/popup.h>

#include <core/os/clipboard_service.h>
#include <core/os/text_input_service.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EcsUiPopupInputTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;


// These queued service doubles exercise the borrowed OS contract without claiming native IME qualification.
class FakeTextInput final : public QueuedTextInputService{
public:
    explicit FakeTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}
    virtual ~FakeTextInput()override = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
    [[nodiscard]] TextInputAdmission::Enum commit(const TextInputSessionToken token, const AStringView text){
        return emitCommit(token, text);
    }
    [[nodiscard]] TextInputAdmission::Enum preedit(const AStringView text){
        return emitPreedit(activeSession(), text, 0u, text.size());
    }
    [[nodiscard]] AStringView publishedText()const{ return surroundingText(); }


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(
        const TextInputSessionToken, const TextInputSessionDesc&)override{
        ++starts;
        return TextInputAdmission::Accepted;
    }

    virtual void endNativeSession(const TextInputSessionToken)override{ ++ends; }


public:
    u32 starts = 0u;
    u32 ends = 0u;
};

class FakeClipboard final : public QueuedClipboardService{
public:
    explicit FakeClipboard(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
    {}
    virtual ~FakeClipboard()override = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }
    [[nodiscard]] bool complete(const ClipboardRequestToken token, const AStringView text){
        return completeNativeRequest(token, ClipboardStatus::Success, text);
    }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum,
        const ClipboardChannel::Enum, const AStringView)override{
        startedToken = token;
    }


public:
    ClipboardRequestToken startedToken;
};

class FixtureShaper final : public Ui::ITextShaper{
public:
    explicit FixtureShaper(Alloc::GlobalArena& arena)
        : m_arena(arena)
    {}
    virtual ~FixtureShaper()override = default;


public:
    [[nodiscard]] virtual Ui::TextLayoutStatus::Enum shape(const Ui::ShapeRequest& request, Ui::ShapedRun& output)override{
        Ui::ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        usize begin = 0u;
        while(begin < request.text.size()){
            usize end = begin + 1u;
            while(end < request.text.size() && IsUtf8Continuation(static_cast<u8>(request.text[end])))
                ++end;
            run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {}, { 10.0f, 0.0f }, {} });
            begin = end;
        }
        output = Move(run);
        return Ui::TextLayoutStatus::Success;
    }


private:
    Alloc::GlobalArena& m_arena;
};

class UiPopupInputTests : public testing::Test{
public:
    UiPopupInputTests()
        : m_arena(Name("tests/ecs_ui/popup_input"))
        , m_context(m_arena)
        , m_textInput(m_arena)
        , m_clipboard(m_arena)
        , m_host(m_arena, m_context, m_textInput, m_clipboard)
        , m_baseModel(m_arena)
        , m_popupModel(m_arena)
        , m_childModel(m_arena)
        , m_shaper(m_arena)
        , m_layoutBuilder(m_arena, m_shaper)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(m_textInput.setFocused(true));
        ASSERT_TRUE(m_baseModel.setText("base"));
        ASSERT_TRUE(m_popupModel.setText("popup"));
        ASSERT_TRUE(m_childModel.setText("child"));
    }

    [[nodiscard]] bool declareEdit(const AStringView key, Ui::EditModel& model, const Ui::Rect& bounds,
        Ui::WidgetState& retained, Ui::EditBoxResult& result){
        const Ui::WidgetState* widget = m_context.declare(key, Ui::WidgetKind::EditBox);
        if(!widget)
            return false;
        retained = *widget;
        const Ui::EditBoxOptions options;
        result = m_host.edit(retained, model, options);
        Ui::EditBoxView view(m_arena);
        Ui::TextLayout layout(m_arena);
        Ui::EditBoxPlacement placement;
        if(!result.valid || !view.snapshot(model) || m_layoutBuilder.layout({ view.displayText() }, layout) != Ui::TextLayoutStatus::Success)
            return false;
        if(
            !view.adoptLayout(Move(layout)) || !view.arrange(bounds, {}, m_viewport, {}, placement)
            || !m_host.publish(retained, view, placement, options)
        )
            return false;
        Ui::HitTarget target;
        target.rectangle = bounds;
        target.clip = m_viewport;
        target.focusable = true;
        target.pointerGesture = true;
        target.textEditable = true;
        return m_context.addTarget(retained, target);
    }

    [[nodiscard]] bool prepare(){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { m_viewport.width, m_viewport.height, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }))
            return false;
        if(!declareEdit("base", m_baseModel, { 10.0f, 20.0f, 180.0f, 30.0f }, m_baseWidget, m_baseResult))
            return false;
        const Ui::WidgetState* popup = m_context.declare("popup", Ui::WidgetKind::Popup);
        if(!popup)
            return false;
        const Ui::WidgetState owner = *popup;
        m_token = { owner.id, owner.declarationGeneration, m_popupState.instanceGeneration(), m_popupState.openGeneration() };
        m_context.input().fencePopup(m_token);
        Ui::PopupDismissReason::Enum reason = Ui::PopupDismissReason::None;
        if(m_context.input().consumePopupDismissal(m_token, reason))
            m_popupState.close();
        if(m_popupState.isOpen()){
            Ui::PopupScope scope;
            scope.token = m_token;
            scope.bounds = { 230.0f, 40.0f, 240.0f, 100.0f };
            scope.viewport = m_viewport;
            if(!m_context.beginPopupScope(owner, scope) || !m_context.pushScope("popup"))
                return false;
            Ui::HitTarget barrier;
            barrier.rectangle = scope.bounds;
            barrier.clip = m_viewport;
            if(!m_context.addTarget(owner, barrier))
                return false;
            if(!declareEdit("edit", m_popupModel, { 250.0f, 60.0f, 180.0f, 30.0f }, m_popupWidget, m_popupResult))
                return false;
            if(!m_context.popScope() || !m_context.endPopupScope(true))
                return false;
        }
        else
            m_context.input().closePopup(m_token);
        if(!m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return true;
    }

    [[nodiscard]] bool prepareDeferred(){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { m_viewport.width, m_viewport.height, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }))
            return false;
        const Ui::WidgetState* declared = m_context.declare("popup", Ui::WidgetKind::Popup);
        if(!declared)
            return false;
        const Ui::WidgetState popup = *declared;
        m_token = { popup.id, popup.declarationGeneration, m_popupState.instanceGeneration(), m_popupState.openGeneration() };
        m_context.input().fencePopup(m_token);
        if(m_popupState.isOpen()){
            declared = m_context.declarePart(popup, "edit", Ui::WidgetKind::EditBox);
            if(!declared)
                return false;
            m_popupWidget = *declared;
            m_popupResult = m_host.editInPopup(m_popupWidget, m_popupModel, {}, m_token);
            if(!m_popupResult.valid || m_context.popupToken().valid())
                return false;
            Ui::EditBoxView view(m_arena);
            Ui::TextLayout layout(m_arena);
            Ui::EditBoxPlacement placement;
            if(!view.snapshot(m_popupModel) || m_layoutBuilder.layout({ view.displayText() }, layout) != Ui::TextLayoutStatus::Success)
                return false;
            if(!view.adoptLayout(Move(layout)) || !view.arrange({ 250.0f, 60.0f, 180.0f, 30.0f }, {}, m_viewport, {}, placement))
                return false;
            Ui::PopupScope scope;
            scope.token = m_token;
            scope.bounds = { 230.0f, 40.0f, 240.0f, 100.0f };
            scope.viewport = m_viewport;
            if(!m_context.beginPopupScope(popup, scope) || !m_host.publish(m_popupWidget, view, placement, {}))
                return false;
            Ui::HitTarget barrier;
            barrier.rectangle = scope.bounds;
            barrier.clip = m_viewport;
            if(!m_context.addTarget(popup, barrier))
                return false;
            Ui::HitTarget target;
            target.rectangle = placement.bounds;
            target.clip = m_viewport;
            target.focusable = true;
            target.textEditable = true;
            if(!m_context.addTarget(m_popupWidget, target) || !m_context.endPopupScope(true))
                return false;
        }
        else
            m_context.input().closePopup(m_token);
        if(!m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return true;
    }

    [[nodiscard]] bool prepareNested(){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { m_viewport.width, m_viewport.height, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }))
            return false;
        if(!declareEdit("base", m_baseModel, { 10.0f, 20.0f, 180.0f, 30.0f }, m_baseWidget, m_baseResult))
            return false;
        const Ui::WidgetState* declared = m_context.declare("popup", Ui::WidgetKind::Popup);
        if(!declared)
            return false;
        const Ui::WidgetState parent = *declared;
        m_token = { parent.id, parent.declarationGeneration, m_popupState.instanceGeneration(), m_popupState.openGeneration() };
        m_context.input().fencePopup(m_token);
        Ui::PopupDismissReason::Enum reason = Ui::PopupDismissReason::None;
        if(m_context.input().consumePopupDismissal(m_token, reason))
            m_popupState.close();
        if(m_nestedParentToken.valid() && m_nestedParentToken != m_token)
            m_childState.close();
        m_nestedParentToken = m_token;
        if(m_popupState.isOpen()){
            Ui::PopupScope scope;
            scope.token = m_token;
            scope.bounds = { 230.0f, 40.0f, 240.0f, 100.0f };
            scope.viewport = m_viewport;
            if(!m_context.beginPopupScope(parent, scope) || !m_context.pushScope("popup"))
                return false;
            Ui::HitTarget barrier;
            barrier.rectangle = scope.bounds;
            barrier.clip = m_viewport;
            if(!m_context.addTarget(parent, barrier))
                return false;
            if(!declareEdit("edit", m_popupModel, { 250.0f, 60.0f, 180.0f, 30.0f }, m_popupWidget, m_popupResult))
                return false;
            declared = m_context.declare("child", Ui::WidgetKind::Popup);
            if(!declared)
                return false;
            const Ui::WidgetState child = *declared;
            m_childToken = { child.id, child.declarationGeneration, m_childState.instanceGeneration(), m_childState.openGeneration() };
            m_context.input().fencePopup(m_childToken);
            if(m_context.input().consumePopupDismissal(m_childToken, reason))
                m_childState.close();
            if(m_childState.isOpen()){
                scope.token = m_childToken;
                scope.parent = m_token;
                scope.bounds = { 60.0f, 80.0f, 240.0f, 100.0f };
                if(!m_context.beginPopupScope(child, scope) || !m_context.pushScope("child"))
                    return false;
                barrier.rectangle = scope.bounds;
                if(!m_context.addTarget(child, barrier))
                    return false;
                if(!declareEdit("edit", m_childModel, { 80.0f, 100.0f, 180.0f, 30.0f }, m_childWidget, m_childResult))
                    return false;
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
        if(!m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return true;
    }

    [[nodiscard]] bool nestedFrame(){ return prepareNested() && commit(); }

    [[nodiscard]] bool commit(){
        if(!m_context.commitFrame(m_generation))
            return false;
        m_host.commitFrame(m_generation);
        return true;
    }

    [[nodiscard]] bool frame(){ return prepare() && commit(); }

    [[nodiscard]] Ui::InputRoutingResult dispatch(const Ui::InputEvent& event){
        m_host.collectNative();
        const Ui::WidgetId previousCapture = m_context.input().capture();
        Ui::InputEvent normalized;
        const bool queued = m_context.input().queue(event, &normalized);
        EXPECT_TRUE(queued);
        if(!queued)
            return {};
        const Ui::InputRoutingResult result = m_context.input().process();
        m_host.input(normalized, previousCapture);
        m_host.synchronizeFocus();
        return result;
    }

    void key(const Core::Key::Enum value, const bool control = false){
        EXPECT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .position = {}, .key = value, .control = control }).keyboardConsumed);
        EXPECT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .position = {}, .key = value, .control = control }).keyboardConsumed);
    }

    void focusBase(){
        ASSERT_TRUE(frame());
        key(Core::Key::Tab);
        ASSERT_EQ(m_context.input().focus(), m_baseWidget.id);
        ASSERT_TRUE(frame());
        ASSERT_TRUE(m_textInput.activeSession().valid());
        ASSERT_EQ(m_textInput.publishedText(), "base");
    }

    void focusPopup(){
        focusBase();
        m_popupState.open();
        ASSERT_TRUE(frame());
        ASSERT_EQ(m_context.input().focus(), m_popupWidget.id);
        ASSERT_TRUE(frame());
        ASSERT_TRUE(m_textInput.activeSession().valid());
        ASSERT_EQ(m_textInput.publishedText(), "popup");
    }


protected:
    Alloc::GlobalArena m_arena;
    Ui::Context m_context;
    FakeTextInput m_textInput;
    FakeClipboard m_clipboard;
    UiEditBoxHost m_host;
    Ui::EditModel m_baseModel;
    Ui::EditModel m_popupModel;
    Ui::EditModel m_childModel;
    FixtureShaper m_shaper;
    Ui::TextLayoutBuilder m_layoutBuilder;
    Ui::PopupState m_popupState;
    Ui::PopupState m_childState;
    Ui::PopupToken m_childToken;
    Ui::PopupToken m_nestedParentToken;
    Ui::WidgetState m_childWidget;
    Ui::EditBoxResult m_childResult;
    Ui::PopupToken m_token;
    Ui::WidgetState m_baseWidget;
    Ui::WidgetState m_popupWidget;
    Ui::EditBoxResult m_baseResult;
    Ui::EditBoxResult m_popupResult;
    const Ui::Rect m_viewport{ 0.0f, 0.0f, 500.0f, 200.0f };
    u64 m_generation = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

