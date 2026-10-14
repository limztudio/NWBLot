// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/unit/ui/native_input.h>

#include "edit_host_fixture_support.h"

#include <core/os/clipboard_service.h>
#include <core/os/text_input_service.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditBoxTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;

class FakeTextInput final : public QueuedTextInputService{
public:
    explicit FakeTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}
    virtual ~FakeTextInput()override = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
    [[nodiscard]] TextInputAdmission::Enum commit(AStringView text){ return emitCommit(activeSession(), text); }
    [[nodiscard]] TextInputAdmission::Enum commitFor(TextInputSessionToken token, AStringView text){ return emitCommit(token, text); }
    [[nodiscard]] TextInputAdmission::Enum preedit(AStringView text, usize anchor, usize caret, bool visible = true){
        return emitPreedit(activeSession(), text, anchor, caret, visible);
    }
    [[nodiscard]] TextInputAdmission::Enum erase(usize before, usize after, u64 revision, TextInputDeletionBasis::Enum basis){
        return emitDeleteSurrounding(activeSession(), before, after, revision, basis);
    }
    [[nodiscard]] TextInputRect nativeCaret()const{ return caretRect(); }
};

class FakeClipboard final : public QueuedClipboardService{
public:
    explicit FakeClipboard(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
        , document(arena)
        , startedText(arena)
    {}
    virtual ~FakeClipboard()override = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] bool deliver(const ClipboardRequestToken token, const ClipboardStatus::Enum status, const AStringView text = {}){
        if(!completeNativeRequest(token, status, text))
            return false;
        if(status == ClipboardStatus::Success && startedOperation == ClipboardOperation::WriteText && token == startedToken)
            document = startedText;
        return true;
    }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum operation, const ClipboardChannel::Enum, const AStringView text)override{
        startedToken = token;
        startedOperation = operation;
        startedText.assign(text.data(), text.size());
        if(!delayed && !deliver(token, ClipboardStatus::Success, operation == ClipboardOperation::ReadText ? AStringView(document) : AStringView{}))
            ADD_FAILURE() << "Clipboard completion failed";
    }


public:
    AString<Alloc::GlobalArena> document;
    AString<Alloc::GlobalArena> startedText;
    ClipboardRequestToken startedToken;
    ClipboardOperation::Enum startedOperation = ClipboardOperation::ReadText;
    bool delayed = false;
};

using FixtureShaper = UiEditHostFixtureSupport::FixtureShaper;

class UiEditBoxHostTests : public testing::Test{
public:
    UiEditBoxHostTests()
        : m_arena(Name("tests/ecs_ui/edit_box_host"))
        , m_context(m_arena)
        , m_textInput(m_arena)
        , m_clipboard(m_arena)
        , m_host(m_arena, m_context, m_textInput, m_clipboard)
        , m_model(m_arena)
        , m_shaper(m_arena)
        , m_layoutBuilder(m_arena, m_shaper)
    {}


protected:
    virtual void SetUp()override{ ASSERT_TRUE(m_textInput.setFocused(true)); }

    [[nodiscard]] bool beginEditFrame(){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { 500.0f, 200.0f, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }))
            return false;
        const Ui::WidgetState* widget = m_context.declare("edit", Ui::WidgetKind::EditBox);
        if(!widget)
            return false;
        m_widget = *widget;
        return true;
    }

    [[nodiscard]] bool prepare(Ui::EditModel& model, const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 30.0f }, Ui::IEditActionSink* actions = nullptr){
        if(!beginEditFrame())
            return false;
        m_result = actions ? m_host.editActions(m_widget, model, options, {}, *actions) : m_host.edit(m_widget, model, options);
        if(!m_result.valid)
            return false;
        if(!publishEdit(model, options, bounds))
            return false;
        if(m_includeButton){
            const Ui::WidgetState* button = m_context.declare("other", Ui::WidgetKind::Button);
            if(!button)
                return false;
            m_otherWidget = *button;
            Ui::HitTarget other;
            other.rectangle = { 230.0f, 20.0f, 100.0f, 30.0f };
            other.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
            other.focusable = true;
            other.activatable = true;
            if(!m_context.addTarget(m_otherWidget, other))
                return false;
        }
        if(!m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return true;
    }

    [[nodiscard]] bool publishEdit(Ui::EditModel& model, const Ui::EditBoxOptions& options, const Ui::Rect& bounds, const Ui::Point previousScroll = {}){
        Ui::EditBoxView view(m_arena);
        if(!UiEditHostFixtureSupport::PrepareEditView(view, model, m_layoutBuilder))
            return false;
        const auto placement = view.arrange(bounds, {}, { 0.0f, 0.0f, 500.0f, 200.0f }, previousScroll);
        if(!placement)
            return false;
        m_placement = *placement;
        if(!m_host.publish(m_widget, view, m_placement, options))
            return false;
        Ui::HitTarget target;
        target.rectangle = bounds;
        target.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
        target.enabled = options.enabled;
        target.focusable = options.enabled;
        target.pointerGesture = options.enabled;
        target.textEditable = true;
        return m_context.addTarget(m_widget, target);
    }

    [[nodiscard]] bool commit(){
        if(!m_context.commitFrame(m_generation))
            return false;
        m_host.commitFrame(m_generation);
        return true;
    }

    [[nodiscard]] bool frame(Ui::EditModel& model, const Ui::EditBoxOptions& options = {}){
        return prepare(model, options) && commit();
    }

    [[nodiscard]] bool dispatch(const Ui::InputEvent& event){
        m_host.collectNative();
        const Ui::WidgetId previousCapture = m_context.input().capture();
        const auto normalized = m_context.input().queue(UiTestSupport::NativeInput(event));
        if(!normalized)
            return false;
        const bool submit = m_host.prepareSubmit(*normalized);
        const Ui::InputRoutingResult result = m_context.input().process();
        m_host.input(*normalized, previousCapture, submit);
        return !result.activationOverflow && !result.gestureOverflow;
    }

    [[nodiscard]] bool dispatchPointer(const Ui::PointerInput& input){
        m_host.collectNative();
        const Ui::WidgetId previousCapture = m_context.input().capture();
        const auto normalized = m_context.input().queuePointer(input);
        if(!normalized)
            return false;
        const Ui::InputRoutingResult result = m_context.input().process();
        if(result.pointerRouted)
            m_host.input(*normalized, previousCapture, false);
        return !result.activationOverflow && !result.gestureOverflow;
    }

    [[nodiscard]] bool key(Core::Key::Enum value, bool shift = false, bool control = false){
        return dispatch({ .type = Ui::InputEventType::KeyDown, .key = value, .shift = shift, .control = control }) && dispatch({ .type = Ui::InputEventType::KeyUp, .key = value, .shift = shift, .control = control });
    }

    [[nodiscard]] bool activate(){
        return frame(m_model) && key(Core::Key::Tab) && frame(m_model) && m_textInput.activeSession().valid();
    }

    [[nodiscard]] bool emptyFrame(){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { 500.0f, 200.0f, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }) || !m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return commit();
    }


protected:
    Alloc::GlobalArena m_arena;
    Ui::Context m_context;
    FakeTextInput m_textInput;
    FakeClipboard m_clipboard;
    UiEditBoxHost m_host;
    Ui::EditModel m_model;
    FixtureShaper m_shaper;
    Ui::TextLayoutBuilder m_layoutBuilder;
    Ui::WidgetState m_widget;
    Ui::EditBoxResult m_result;
    Ui::EditBoxPlacement m_placement;
    Ui::WidgetState m_otherWidget;
    u64 m_generation = 0u;
    bool m_includeButton = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

