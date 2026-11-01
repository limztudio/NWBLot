// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_host_fixture_support.h"

#include <tests/unit/ui/native_input.h>

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
    using QueuedTextInputService::surroundingText;


public:
    explicit FakeTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}
    virtual ~FakeTextInput()noexcept override = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }

    [[nodiscard]] TextInputAdmission::Enum commit(const TextInputSessionToken token, const AStringView text){
        return emitCommit(token, text);
    }

    [[nodiscard]] TextInputAdmission::Enum preedit(const AStringView text){
        return emitPreedit(activeSession(), text, 0u, text.size());
    }


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(TextInputSessionToken, const TextInputSessionDesc&)noexcept override;

    virtual void endNativeSession(const TextInputSessionToken)noexcept override{ ++ends; }


public:
    u32 starts = 0u;
    u32 ends = 0u;
};

class FakeClipboard final : public QueuedClipboardService{
public:
    explicit FakeClipboard(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
    {}
    virtual ~FakeClipboard()noexcept override = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] b8 complete(const ClipboardRequestToken token, const AStringView text){
        return completeNativeRequest(token, ClipboardStatus::Success, text);
    }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum, const ClipboardChannel::Enum, const AStringView)noexcept override{
        startedToken = token;
    }


public:
    ClipboardRequestToken startedToken;
};

class UiPopupInputTests : public testing::Test{
protected:
    struct DeclaredEdit{
        Ui::WidgetState widget;
        Ui::EditBoxResult result;
    };


private:
    static constexpr AStringView s_PopupKey = "popup";
    static constexpr AStringView s_EditKey = "edit";
    static constexpr AStringView s_ChildKey = "child";


public:
    UiPopupInputTests();


protected:
    virtual void SetUp()override;

    [[nodiscard]] Expected<DeclaredEdit> declareEdit(AStringView key, const Ui::Rect& bounds, Ui::EditModel& model);
    [[nodiscard]] b8 beginPreparedFrame();
    [[nodiscard]] b8 beginBaseFrame();
    [[nodiscard]] b8 endPreparedFrame();
    [[nodiscard]] b8 prepare();
    [[nodiscard]] b8 prepareDeferred();
    [[nodiscard]] b8 prepareNested();

    [[nodiscard]] b8 nestedFrame(){ return prepareNested() && commit(); }

    [[nodiscard]] b8 commit();

    [[nodiscard]] b8 frame(){ return prepare() && commit(); }

    [[nodiscard]] Ui::InputRoutingResult dispatch(const Ui::InputEvent& event);
    void key(Core::Key::Enum value, b8 control = false);
    void focusBase();
    void focusPopup();


protected:
    Alloc::GlobalArena m_arena;
    Ui::Context m_context;
    FakeTextInput m_textInput;
    FakeClipboard m_clipboard;
    UiEditBoxHost m_host;
    Ui::EditModel m_baseModel;
    Ui::EditModel m_popupModel;
    Ui::EditModel m_childModel;
    UiEditHostFixtureSupport::FixtureShaper m_shaper;
    Ui::TextLayoutBuilder m_layoutBuilder;
    Ui::PopupState m_popupState;
    Ui::PopupState m_childState;
    Ui::PopupToken m_childToken;
    Ui::PopupToken m_nestedParentToken;
    Ui::WidgetState m_childWidget;
    Ui::PopupToken m_token;
    Ui::WidgetState m_baseWidget;
    Ui::WidgetState m_popupWidget;
    u64 m_generation = 0u;
    const Ui::Rect m_viewport{ 0.0f, 0.0f, 500.0f, 200.0f };
    Ui::EditBoxResult m_childResult;
    Ui::EditBoxResult m_popupResult;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

