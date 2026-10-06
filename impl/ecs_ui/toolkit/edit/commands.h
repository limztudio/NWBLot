// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"
#include "../input/commands.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EditCommand{
    enum Enum : u8{
        None,
        Left,
        Right,
        Home,
        End,
        WordLeft,
        WordRight,
        Backspace,
        Delete,
        WordBackspace,
        WordDelete,
        SelectAll,
        Copy,
        Cut,
        Paste,
        Undo,
        Redo,
        Submit,
        Cancel,
        DocumentHome,
        DocumentEnd,
        Newline,
    };
};

namespace EditClipboardAction{
    enum Enum : u8{
        None,
        Copy,
        Cut,
        Paste,
        PublishSelection,
    };
};

struct EditCommandRequest{
    EditCommand::Enum command = EditCommand::None;
    bool extend = false;
    bool repeat = false;
};

struct EditCommandResult{
    EditClipboardAction::Enum clipboard = EditClipboardAction::None;
    bool handled = false;
    bool textChanged = false;
    bool selectionChanged = false;
    bool compositionChanged = false;
    bool submitted = false;
    bool cancelled = false;
};

// Bindings or device adapters supply admitted editing intent; native-consumed input is filtered before translation.
// Accept inserts LF in multiline mode and submits in single-line mode; Submit always submits.
[[nodiscard]] EditCommandRequest TranslateEditCommand(const InputCommandIntent& intent, bool repeat,
    EditTextMode::Enum mode = EditTextMode::SingleLine)noexcept;
// Active preedit owns editing keys and Enter. Escape first cancels preedit without cancelling the widget.
// Clipboard actions describe requests; the host borrows its OS service to perform the actual exchange.
[[nodiscard]] EditCommandResult ApplyEditCommand(EditModel& model, const EditCommandRequest& request, bool readOnly = false);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

