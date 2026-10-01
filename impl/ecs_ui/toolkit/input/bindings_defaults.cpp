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

constexpr InputKeyBinding s_Defaults[]{
    { Core::Key::Tab, 0, s_Control | s_Alt | s_Super, InputCommand::FocusNext, InputSelectionPolicy::None, false },
    { Core::Key::Tab, s_Shift, s_Control | s_Alt | s_Super, InputCommand::FocusPrevious, InputSelectionPolicy::None, false },
    { Core::Key::Enter, 0, s_Shift | s_Super, InputCommand::Accept },
    { Core::Key::Enter, s_Control, s_Shift | s_Super, InputCommand::Submit },
    { Core::Key::Enter, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Accept, InputSelectionPolicy::None, false },
    { Core::Key::KeypadEnter, 0, s_Shift | s_Super, InputCommand::Accept },
    { Core::Key::KeypadEnter, s_Control, s_Shift | s_Super, InputCommand::Submit },
    { Core::Key::KeypadEnter, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Accept, InputSelectionPolicy::None, false },
    { Core::Key::Space, 0, s_All, InputCommand::Activate, InputSelectionPolicy::None, false, true },
    { Core::Key::Escape, 0, s_Shift | s_Control | s_Super, InputCommand::Cancel },
    { Core::Key::Escape, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Cancel, InputSelectionPolicy::None, false },

    { Core::Key::Left, 0, s_Shift | s_Super, InputCommand::Left, InputSelectionPolicy::Shift },
    { Core::Key::Left, s_Control, s_Shift | s_Super, InputCommand::WordLeft, InputSelectionPolicy::Shift },
    { Core::Key::Left, s_Alt, s_Shift | s_Super, InputCommand::Left, InputSelectionPolicy::Shift, false },
    { Core::Key::Left, s_Control | s_Alt, s_Shift | s_Super, InputCommand::WordLeft, InputSelectionPolicy::Shift, false },
    { Core::Key::Right, 0, s_Shift | s_Super, InputCommand::Right, InputSelectionPolicy::Shift },
    { Core::Key::Right, s_Control, s_Shift | s_Super, InputCommand::WordRight, InputSelectionPolicy::Shift },
    { Core::Key::Right, s_Alt, s_Shift | s_Super, InputCommand::Right, InputSelectionPolicy::Shift, false },
    { Core::Key::Right, s_Control | s_Alt, s_Shift | s_Super, InputCommand::WordRight, InputSelectionPolicy::Shift, false },
    { Core::Key::Home, 0, s_Shift | s_Super, InputCommand::Home, InputSelectionPolicy::Shift },
    { Core::Key::Home, s_Control, s_Shift | s_Super, InputCommand::DocumentHome, InputSelectionPolicy::Shift },
    { Core::Key::Home, s_Alt, s_Shift | s_Super, InputCommand::Home, InputSelectionPolicy::Shift, false },
    { Core::Key::Home, s_Control | s_Alt, s_Shift | s_Super, InputCommand::DocumentHome, InputSelectionPolicy::Shift, false },
    { Core::Key::End, 0, s_Shift | s_Super, InputCommand::End, InputSelectionPolicy::Shift },
    { Core::Key::End, s_Control, s_Shift | s_Super, InputCommand::DocumentEnd, InputSelectionPolicy::Shift },
    { Core::Key::End, s_Alt, s_Shift | s_Super, InputCommand::End, InputSelectionPolicy::Shift, false },
    { Core::Key::End, s_Control | s_Alt, s_Shift | s_Super, InputCommand::DocumentEnd, InputSelectionPolicy::Shift, false },

    { Core::Key::Up, 0, s_Shift | s_Super, InputCommand::Up, InputSelectionPolicy::Shift },
    { Core::Key::Up, s_Control, s_Shift | s_Super, InputCommand::Up, InputSelectionPolicy::Shift, false },
    { Core::Key::Up, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Up, InputSelectionPolicy::Shift, false },
    { Core::Key::Down, 0, s_Shift | s_Super, InputCommand::Down, InputSelectionPolicy::Shift },
    { Core::Key::Down, s_Control, s_Shift | s_Super, InputCommand::Down, InputSelectionPolicy::Shift, false },
    { Core::Key::Down, s_Alt, s_Shift | s_Control | s_Super, InputCommand::Down, InputSelectionPolicy::Shift, false },
    { Core::Key::PageUp, 0, s_Shift | s_Super, InputCommand::PageUp, InputSelectionPolicy::Shift },
    { Core::Key::PageUp, s_Control, s_Shift | s_Super, InputCommand::PageUp, InputSelectionPolicy::Shift, false },
    { Core::Key::PageUp, s_Alt, s_Shift | s_Control | s_Super, InputCommand::PageUp, InputSelectionPolicy::Shift, false },
    { Core::Key::PageDown, 0, s_Shift | s_Super, InputCommand::PageDown, InputSelectionPolicy::Shift },
    { Core::Key::PageDown, s_Control, s_Shift | s_Super, InputCommand::PageDown, InputSelectionPolicy::Shift, false },
    { Core::Key::PageDown, s_Alt, s_Shift | s_Control | s_Super, InputCommand::PageDown, InputSelectionPolicy::Shift, false },

    { Core::Key::Backspace, 0, s_Shift | s_Super, InputCommand::Backspace },
    { Core::Key::Backspace, s_Control, s_Shift | s_Super, InputCommand::WordBackspace },
    { Core::Key::Delete, 0, s_Super, InputCommand::Delete },
    { Core::Key::Delete, s_Control, s_Shift | s_Super, InputCommand::WordDelete },
    { Core::Key::Delete, s_Shift, s_Super, InputCommand::Cut },
    { Core::Key::Insert, s_Control, s_Super, InputCommand::Copy },
    { Core::Key::Insert, s_Shift, s_Super, InputCommand::Paste },
    { Core::Key::A, s_Control, s_Shift | s_Super, InputCommand::SelectAll },
    { Core::Key::C, s_Control, s_Shift | s_Super, InputCommand::Copy },
    { Core::Key::X, s_Control, s_Shift | s_Super, InputCommand::Cut },
    { Core::Key::V, s_Control, s_Shift | s_Super, InputCommand::Paste },
    { Core::Key::Z, s_Control, s_Super, InputCommand::Undo },
    { Core::Key::Z, s_Control | s_Shift, s_Super, InputCommand::Redo },
    { Core::Key::Y, s_Control, s_Shift | s_Super, InputCommand::Redo },

    { Core::Key::Menu, 0, s_All, InputCommand::ContextMenu, InputSelectionPolicy::None, false },
    { Core::Key::F10, s_Shift, s_Super, InputCommand::ContextMenu, InputSelectionPolicy::None, false },
};

static_assert(sizeof(s_Defaults) / sizeof(s_Defaults[0u]) <= s_InputMaxBindings);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputBindings::restoreDefaults(){
    constexpr usize s_Count = sizeof(__hidden_ui_input_bindings_defaults::s_Defaults)
        / sizeof(__hidden_ui_input_bindings_defaults::s_Defaults[0u]);
    if(!set(__hidden_ui_input_bindings_defaults::s_Defaults, s_Count))
        TerminateInvariant();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

