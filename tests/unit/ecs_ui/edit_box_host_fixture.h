// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/edit_box_host.h>

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
    [[nodiscard]] TextInputAdmission::Enum erase(
        usize before, usize after, u64 revision = 0u,
        TextInputDeletionBasis::Enum basis = TextInputDeletionBasis::Caret){
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
    [[nodiscard]] virtual ClipboardStatus::Enum readNativeText(ClipboardChannel::Enum, AString<Alloc::GlobalArena>& text)override{
        text = document;
        return ClipboardStatus::Success;
    }

    [[nodiscard]] virtual ClipboardStatus::Enum writeNativeText(ClipboardChannel::Enum, const AStringView text)override{
        document.assign(text.data(), text.size());
        return ClipboardStatus::Success;
    }

    virtual void startNativeRequest(
        const ClipboardRequestToken token, const ClipboardOperation::Enum operation,
        const ClipboardChannel::Enum channel, const AStringView text)override{
        startedToken = token;
        startedOperation = operation;
        startedText.assign(text.data(), text.size());
        if(!delayed)
            QueuedClipboardService::startNativeRequest(token, operation, channel, text);
    }


public:
    AString<Alloc::GlobalArena> document;
    AString<Alloc::GlobalArena> startedText;
    ClipboardRequestToken startedToken;
    ClipboardOperation::Enum startedOperation = ClipboardOperation::ReadText;
    bool delayed = false;
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

    [[nodiscard]] bool prepare(Ui::EditModel& model, const Ui::EditBoxOptions& options = {},
        const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 30.0f }, Ui::IEditActionSink* actions = nullptr){
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
        m_result = actions ? m_host.editActions(m_widget, model, options, {}, *actions) : m_host.edit(m_widget, model, options);
        if(!m_result.valid)
            return false;
        Ui::EditBoxView view(m_arena);
        Ui::TextLayout layout(m_arena);
        if(!view.snapshot(model) || m_layoutBuilder.layout({ view.displayText() }, layout) != Ui::TextLayoutStatus::Success)
            return false;
        if(!view.adoptLayout(Move(layout)) || !view.arrange(bounds, {}, { 0.0f, 0.0f, 500.0f, 200.0f }, 0.0f, m_placement))
            return false;
        if(!m_host.publish(m_widget, view, m_placement, options))
            return false;
        Ui::HitTarget target;
        target.rectangle = bounds;
        target.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
        target.enabled = options.enabled;
        target.focusable = options.enabled;
        target.pointerGesture = options.enabled;
        target.textEditable = true;
        if(!m_context.addTarget(m_widget, target))
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
        if(!m_context.input().queue(event))
            return false;
        const Ui::InputRoutingResult result = m_context.input().process();
        m_host.input(event, previousCapture);
        m_host.synchronizeFocus();
        return !result.activationOverflow && !result.gestureOverflow;
    }

    [[nodiscard]] bool key(Ui::InputKey::Enum value, bool shift = false, bool control = false){
        return dispatch({ .type = Ui::InputEventType::KeyDown, .key = value, .shift = shift, .control = control })
            && dispatch({ .type = Ui::InputEventType::KeyUp, .key = value, .shift = shift, .control = control });
    }

    [[nodiscard]] bool activate(){
        return frame(m_model) && key(Ui::InputKey::Tab) && frame(m_model) && m_textInput.activeSession().valid();
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

