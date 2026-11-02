// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bindings.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_bindings{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr Array<InputKeyBinding, 55u> s_Defaults = {
    InputKeyBinding{ Core::Key::Tab, 0, Core::InputModifier::Control | Core::InputModifier::Alt | Core::InputModifier::Super, InputCommand::FocusNext, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::Tab, Core::InputModifier::Shift, Core::InputModifier::Control | Core::InputModifier::Alt | Core::InputModifier::Super, InputCommand::FocusPrevious, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::Enter, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Accept },
    InputKeyBinding{ Core::Key::Enter, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Submit },
    InputKeyBinding{ Core::Key::Enter, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::Accept, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::KeypadEnter, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Accept },
    InputKeyBinding{ Core::Key::KeypadEnter, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Submit },
    InputKeyBinding{ Core::Key::KeypadEnter, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::Accept, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::Space, 0, s_InputBindingModifierMask, InputCommand::Activate, InputSelectionPolicy::None, false, true },
    InputKeyBinding{ Core::Key::Escape, 0, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::Cancel },
    InputKeyBinding{ Core::Key::Escape, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::Cancel, InputSelectionPolicy::None, false },

    InputKeyBinding{ Core::Key::Left, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Left, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Left, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::WordLeft, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Left, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Left, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Left, Core::InputModifier::Control | Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::WordLeft, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Right, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Right, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Right, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::WordRight, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Right, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Right, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Right, Core::InputModifier::Control | Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::WordRight, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Home, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Home, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Home, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::DocumentHome, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Home, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Home, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Home, Core::InputModifier::Control | Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::DocumentHome, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::End, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::End, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::End, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::DocumentEnd, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::End, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::End, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::End, Core::InputModifier::Control | Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::DocumentEnd, InputSelectionPolicy::Shift, false },

    InputKeyBinding{ Core::Key::Up, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Up, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Up, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Up, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Up, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::Up, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Down, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Down, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::Down, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Down, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::Down, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::Down, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageUp, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::PageUp, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::PageUp, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::PageUp, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageUp, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::PageUp, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageDown, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::PageDown, InputSelectionPolicy::Shift },
    InputKeyBinding{ Core::Key::PageDown, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::PageDown, InputSelectionPolicy::Shift, false },
    InputKeyBinding{ Core::Key::PageDown, Core::InputModifier::Alt, Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Super, InputCommand::PageDown, InputSelectionPolicy::Shift, false },

    InputKeyBinding{ Core::Key::Backspace, 0, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Backspace },
    InputKeyBinding{ Core::Key::Backspace, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::WordBackspace },
    InputKeyBinding{ Core::Key::Delete, 0, Core::InputModifier::Super, InputCommand::Delete },
    InputKeyBinding{ Core::Key::Delete, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::WordDelete },
    InputKeyBinding{ Core::Key::Delete, Core::InputModifier::Shift, Core::InputModifier::Super, InputCommand::Cut },
    InputKeyBinding{ Core::Key::Insert, Core::InputModifier::Control, Core::InputModifier::Super, InputCommand::Copy },
    InputKeyBinding{ Core::Key::Insert, Core::InputModifier::Shift, Core::InputModifier::Super, InputCommand::Paste },
    InputKeyBinding{ Core::Key::A, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::SelectAll },
    InputKeyBinding{ Core::Key::C, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Copy },
    InputKeyBinding{ Core::Key::X, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Cut },
    InputKeyBinding{ Core::Key::V, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Paste },
    InputKeyBinding{ Core::Key::Z, Core::InputModifier::Control, Core::InputModifier::Super, InputCommand::Undo },
    InputKeyBinding{ Core::Key::Z, Core::InputModifier::Control | Core::InputModifier::Shift, Core::InputModifier::Super, InputCommand::Redo },
    InputKeyBinding{ Core::Key::Y, Core::InputModifier::Control, Core::InputModifier::Shift | Core::InputModifier::Super, InputCommand::Redo },

    InputKeyBinding{ Core::Key::Menu, 0, s_InputBindingModifierMask, InputCommand::ContextMenu, InputSelectionPolicy::None, false },
    InputKeyBinding{ Core::Key::F10, Core::InputModifier::Shift, Core::InputModifier::Super, InputCommand::ContextMenu, InputSelectionPolicy::None, false },
};

static_assert(s_Defaults.size() <= s_InputMaxBindings);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 InputBindings::ValidKey(const i32 key)noexcept{
    return
        key == Core::Key::Space || key == Core::Key::Apostrophe || (key >= Core::Key::Comma && key <= Core::Key::Number9)
        || key == Core::Key::Semicolon || key == Core::Key::Equal || (key >= Core::Key::A && key <= Core::Key::RightBracket)
        || key == Core::Key::GraveAccent || (key >= Core::Key::World1 && key <= Core::Key::World2)
        || (key >= Core::Key::Escape && key <= Core::Key::End)
        || (key >= Core::Key::CapsLock && key <= Core::Key::Pause) || (key >= Core::Key::F1 && key <= Core::Key::F25)
        || (key >= Core::Key::Keypad0 && key <= Core::Key::KeypadEqual) || (key >= Core::Key::LeftShift && key <= Core::Key::Menu)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputBindings::InputBindings(Core::Alloc::GlobalArena& arena)
    : m_bindings(arena)
{
    m_bindings.reserve(s_InputMaxBindings);
    restoreDefaults();
}

void InputBindings::restoreDefaults(){
    if(!set(Span<const InputKeyBinding>(__hidden_ui_input_bindings::s_Defaults)))
        TerminateInvariant();
}

b8 InputBindings::set(const Span<const InputKeyBinding> bindings){
    const usize count = bindings.size();
    if(count > s_InputMaxBindings)
        return false;
    for(usize index = 0u; index < count; ++index){
        if(!IsValidInputKeyBinding(bindings[index], ValidKey(bindings[index].key), s_InputBindingModifierMask))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(InputKeyBindingsOverlap(bindings[previous], bindings[index], s_InputBindingModifierMask))
                return false;
        }
    }
    BindingVector candidate(m_bindings.get_allocator());
    candidate.reserve(s_InputMaxBindings);
    if(!bindings.empty())
        candidate.assign(bindings.begin(), bindings.end());
    m_bindings.swap(candidate);
    return true;
}

InputCommandIntent InputBindings::resolve(const i32 key, const i32 modifiers)const noexcept{
    constexpr i32 s_LockModifiers = Core::InputModifier::CapsLock | Core::InputModifier::NumLock;
    return ResolveInputKeyBinding(m_bindings, key, modifiers, ValidKey(key), s_InputBindingModifierMask, s_LockModifiers, Core::InputModifier::Shift);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

