// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bindings.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_bindings_defaults{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr i32 s_Shift = Core::InputModifier::Shift;
constexpr i32 s_Control = Core::InputModifier::Control;
constexpr i32 s_Alt = Core::InputModifier::Alt;
constexpr i32 s_Super = Core::InputModifier::Super;
constexpr i32 s_All = s_InputBindingModifierMask;

constexpr Array<InputKeyBinding, 55u> s_Defaults = {
    InputKeyBinding{ Core::Key::Tab, 0, s_Control | s_Alt | s_Super, InputCommand::FocusNext, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::Tab, s_Shift, s_Control | s_Alt | s_Super, InputCommand::FocusPrevious, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::Enter, 0, s_Shift | s_Super, InputCommand::Accept },
    InputKeyBinding{ Core::Key::Enter, s_Control, s_Shift | s_Super, InputCommand::Submit },
    InputKeyBinding{ Core::Key::Enter, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Accept, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::KeypadEnter, 0, s_Shift | s_Super, InputCommand::Accept },
    InputKeyBinding{ Core::Key::KeypadEnter, s_Control, s_Shift | s_Super, InputCommand::Submit },
    InputKeyBinding{ Core::Key::KeypadEnter, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Accept, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::Space, 0, s_All, InputCommand::Activate, InputSelectionPolicy::None, false, true },
    InputKeyBinding{ Core::Key::Escape, 0, s_Shift | s_Control | s_Super, InputCommand::Cancel },
    InputKeyBinding{ Core::Key::Escape, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Cancel, InputSelectionPolicy::None, false },

    InputKeyBinding{ Core::Key::Left, 0, s_Shift | s_Super, InputCommand::Left, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Left, s_Control, s_Shift | s_Super, InputCommand::WordLeft, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Left, s_Alt, s_Shift | s_Super, InputCommand::Left, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Left, s_Control | s_Alt, s_Shift | s_Super, InputCommand::WordLeft, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Right, 0, s_Shift | s_Super, InputCommand::Right, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Right, s_Control, s_Shift | s_Super, InputCommand::WordRight, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Right, s_Alt, s_Shift | s_Super, InputCommand::Right, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Right, s_Control | s_Alt, s_Shift | s_Super, InputCommand::WordRight, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Home, 0, s_Shift | s_Super, InputCommand::Home, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Home, s_Control, s_Shift | s_Super, InputCommand::DocumentHome, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Home, s_Alt, s_Shift | s_Super, InputCommand::Home, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Home, s_Control | s_Alt, s_Shift | s_Super, InputCommand::DocumentHome, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::End, 0, s_Shift | s_Super, InputCommand::End, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::End, s_Control, s_Shift | s_Super, InputCommand::DocumentEnd, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::End, s_Alt, s_Shift | s_Super, InputCommand::End, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::End, s_Control | s_Alt, s_Shift | s_Super, InputCommand::DocumentEnd, InputSelectionPolicy::Shift, false },

    InputKeyBinding{ Core::Key::Up, 0, s_Shift | s_Super, InputCommand::Up, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Up, s_Control, s_Shift | s_Super, InputCommand::Up, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Up, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Up, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Down, 0, s_Shift | s_Super, InputCommand::Down, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Down, s_Control, s_Shift | s_Super, InputCommand::Down, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Down, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Down, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageUp, 0, s_Shift | s_Super, InputCommand::PageUp, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::PageUp, s_Control, s_Shift | s_Super, InputCommand::PageUp, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageUp, s_Alt, s_Shift | s_Control | s_Super, InputCommand::PageUp, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageDown, 0, s_Shift | s_Super, InputCommand::PageDown, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::PageDown, s_Control, s_Shift | s_Super, InputCommand::PageDown, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageDown, s_Alt, s_Shift | s_Control | s_Super, InputCommand::PageDown, InputSelectionPolicy::Shift, false },

    InputKeyBinding{ Core::Key::Backspace, 0, s_Shift | s_Super, InputCommand::Backspace },
    InputKeyBinding{ Core::Key::Backspace, s_Control, s_Shift | s_Super, InputCommand::WordBackspace },
    InputKeyBinding{ Core::Key::Delete, 0, s_Super, InputCommand::Delete },
    InputKeyBinding{ Core::Key::Delete, s_Control, s_Shift | s_Super, InputCommand::WordDelete },
    InputKeyBinding{ Core::Key::Delete, s_Shift, s_Super, InputCommand::Cut },
    InputKeyBinding{ Core::Key::Insert, s_Control, s_Super, InputCommand::Copy },
    InputKeyBinding{ Core::Key::Insert, s_Shift, s_Super, InputCommand::Paste },
    InputKeyBinding{ Core::Key::A, s_Control, s_Shift | s_Super, InputCommand::SelectAll },
    InputKeyBinding{ Core::Key::C, s_Control, s_Shift | s_Super, InputCommand::Copy },
    InputKeyBinding{ Core::Key::X, s_Control, s_Shift | s_Super, InputCommand::Cut },
    InputKeyBinding{ Core::Key::V, s_Control, s_Shift | s_Super, InputCommand::Paste },
    InputKeyBinding{ Core::Key::Z, s_Control, s_Super, InputCommand::Undo },
    InputKeyBinding{ Core::Key::Z, s_Control | s_Shift, s_Super, InputCommand::Redo },
    InputKeyBinding{ Core::Key::Y, s_Control, s_Shift | s_Super, InputCommand::Redo },

    InputKeyBinding{ Core::Key::Menu, 0, s_All, InputCommand::ContextMenu, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::F10, s_Shift, s_Super, InputCommand::ContextMenu, InputSelectionPolicy::None, false },
};

static_assert(s_Defaults.size() <= s_InputMaxBindings);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputBindings::restoreDefaults(){
    if(!set(Span<const InputKeyBinding>(__hidden_ui_input_bindings_defaults::s_Defaults)))
        TerminateInvariant();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

