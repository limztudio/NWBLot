// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_edit_session.h"

#include <impl/ecs_ui/toolkit/edit/commands.h>

#include <core/os/clipboard.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditClipboardStatus{
    enum Enum : u8{ Idle, Pending, Applied, Published, Cancelled, StaleOwner, StaleModel, InvalidOwner, InvalidAction,
        ReadOnly, NoSelection, Unsupported, CompositionActive, InvalidText, TooLarge, ModelRejected, NativeFailure,
        WrongThread, QueueFull };
};

struct UiEditClipboardResult{
    UiEditClipboardStatus::Enum status = UiEditClipboardStatus::Idle;
    bool textChanged = false;
    bool selectionChanged = false;
};

// The OS owns transfer and native selection. This controller owns copied request fences, never a model pointer.
// Cancel before any local edit/selection change, focus loss, owner removal or model replacement. Drain before host edits.
// End native text input before a drain that may apply cut/paste, and restart it with the resulting surrounding text.
class UiEditClipboardController final : NoCopy{
public:
    UiEditClipboardController(Core::Alloc::GlobalArena& arena, Core::IClipboardService& service);
    ~UiEditClipboardController();


public:
    [[nodiscard]] UiEditClipboardResult request(
        const UiTextEditOwner& owner, const Ui::EditModel& model, Ui::EditClipboardAction::Enum action,
        Core::ClipboardChannel::Enum channel = Core::ClipboardChannel::Clipboard, bool readOnly = false
    );
    [[nodiscard]] UiEditClipboardResult drain(const UiTextEditOwner& owner, Ui::EditModel& model, bool readOnly = false);
    [[nodiscard]] bool cancel();
    [[nodiscard]] bool pending()const noexcept{ return m_token.valid(); }
    [[nodiscard]] Core::ClipboardRequestToken token()const noexcept{ return m_token; }
    [[nodiscard]] Ui::EditClipboardAction::Enum action()const noexcept{ return m_action; }


private:
    void clearRequest()noexcept;
    [[nodiscard]] UiEditClipboardResult applyCompletion(Ui::EditModel& model, bool readOnly);


private:
    Core::IClipboardService& m_service;
    Core::ClipboardCompletion m_completion;
    Ui::EditModelSnapshot m_expectedModel;
    AString<Core::Alloc::GlobalArena> m_insertText;
    UiTextEditOwner m_owner;
    Core::ClipboardRequestToken m_token;
    Core::ClipboardChannel::Enum m_channel = Core::ClipboardChannel::Clipboard;
    Ui::EditClipboardAction::Enum m_action = Ui::EditClipboardAction::None;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

