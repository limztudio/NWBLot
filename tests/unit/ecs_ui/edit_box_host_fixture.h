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
    using QueuedTextInputService::emitCommit;
    using QueuedTextInputService::caretRect;


public:
    explicit FakeTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}
    virtual ~FakeTextInput()noexcept override = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }

    [[nodiscard]] TextInputAdmission::Enum commit(AStringView text){ return emitCommit(activeSession(), text); }

    [[nodiscard]] TextInputAdmission::Enum preedit(AStringView text, usize anchor, usize caret, b8 visible = true){
        return emitPreedit(activeSession(), text, anchor, caret, visible);
    }

    [[nodiscard]] TextInputAdmission::Enum erase(usize before, usize after, u64 revision, TextInputDeletionBasis::Enum basis){
        return emitDeleteSurrounding(activeSession(), before, after, revision, basis);
    }
};

class FakeClipboard final : public QueuedClipboardService{
public:
    explicit FakeClipboard(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
        , document(arena)
        , startedText(arena)
    {}
    virtual ~FakeClipboard()noexcept override = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] b8 deliver(ClipboardRequestToken token, ClipboardStatus::Enum status, AStringView text = {});


protected:
    virtual void startNativeRequest(ClipboardRequestToken token, ClipboardOperation::Enum operation, ClipboardChannel::Enum channel, AStringView text)override;


public:
    AString<Alloc::GlobalArena> document;
    AString<Alloc::GlobalArena> startedText;
    ClipboardRequestToken startedToken;
    ClipboardOperation::Enum startedOperation = ClipboardOperation::ReadText;
    b8 delayed = false;
};

class UiEditBoxHostTests : public testing::Test{
public:
    UiEditBoxHostTests();


protected:
    virtual void SetUp()override{ ASSERT_TRUE(m_textInput.setFocused(true)); }

    [[nodiscard]] b8 beginEditFrame();
    [[nodiscard]] b8 prepare(Ui::EditModel& model, const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 30.0f }, Ui::IEditActionSink* actions = nullptr);
    [[nodiscard]] b8 publishEdit(Ui::EditModel& model, const Ui::EditBoxOptions& options, const Ui::Rect& bounds, Ui::Point previousScroll = {});
    [[nodiscard]] b8 commit();

    [[nodiscard]] b8 frame(Ui::EditModel& model, const Ui::EditBoxOptions& options = {}){
        return prepare(model, options) && commit();
    }

    [[nodiscard]] Ui::Point atByte(const usize byte)const noexcept{
        return { static_cast<f32>(byte) * 10.0f + m_placement.textOrigin.x + 1.0f, m_placement.textOrigin.y + 5.0f };
    }

    [[nodiscard]] b8 dispatch(const Ui::InputEvent& event);
    [[nodiscard]] b8 dispatchPointer(const Ui::PointerInput& input);

    [[nodiscard]] b8 key(Core::Key::Enum value, b8 shift = false, b8 control = false){
        return dispatch({ .key = value, .type = Ui::InputEventType::KeyDown, .shift = shift, .control = control }) && dispatch({ .key = value, .type = Ui::InputEventType::KeyUp, .shift = shift, .control = control });
    }

    [[nodiscard]] b8 activate(){
        return frame(m_model) && key(Core::Key::Tab) && frame(m_model) && m_textInput.activeSession().valid();
    }

    [[nodiscard]] b8 emptyFrame();


protected:
    Alloc::GlobalArena m_arena;
    Ui::Context m_context;
    FakeTextInput m_textInput;
    FakeClipboard m_clipboard;
    UiEditBoxHost m_host;
    Ui::EditModel m_model;
    UiEditHostFixtureSupport::FixtureShaper m_shaper;
    Ui::TextLayoutBuilder m_layoutBuilder;
    Ui::WidgetState m_widget;
    Ui::EditBoxPlacement m_placement;
    Ui::WidgetState m_otherWidget;
    u64 m_generation = 0u;
    Ui::EditBoxResult m_result;
    b8 m_includeButton = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

