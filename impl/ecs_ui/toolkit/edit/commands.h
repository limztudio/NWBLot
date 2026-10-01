// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EditKey{
    enum Enum : u8{
        None,
        Left,
        Right,
        Home,
        End,
        Backspace,
        Delete,
        A,
        C,
        X,
        V,
        Z,
        Y,
        Enter,
        Insert,
        Escape,
        Up,
        Down,
        PageUp,
        PageDown,
    };
};

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

struct EditKeyStroke{
    EditKey::Enum key = EditKey::None;
    bool control = false;
    bool shift = false;
    bool alt = false;
    bool repeat = false;
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

// Filter native-consumed key events before translation. Alt combinations stay with the OS, including AltGr.
// Multiline Enter inserts LF; Ctrl+Enter submits. Multiline Ctrl+Home/End use document movement.
[[nodiscard]] EditCommandRequest TranslateEditCommand(const EditKeyStroke& stroke, EditTextMode::Enum mode = EditTextMode::SingleLine);
// Active preedit owns editing keys and Enter. Escape first cancels preedit without cancelling the widget.
// Clipboard actions describe requests; the host borrows its OS service to perform the actual exchange.
[[nodiscard]] EditCommandResult ApplyEditCommand(EditModel& model, const EditCommandRequest& request, bool readOnly = false);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

