// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bindings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_bindings{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidBinding(const InputKeyBinding& binding)noexcept{
    return
        InputBindings::ValidKey(binding.key) && binding.command != InputCommand::None && binding.command <= InputCommand::ContextMenu
        && binding.selection <= InputSelectionPolicy::Shift
        && ((binding.modifiers | binding.ignoredModifiers) & ~s_InputBindingModifierMask) == 0
        && (binding.modifiers & binding.ignoredModifiers) == 0
    ;
}

static bool Overlaps(const InputKeyBinding& first, const InputKeyBinding& second)noexcept{
    const i32 common = s_InputBindingModifierMask & ~(first.ignoredModifiers | second.ignoredModifiers);
    return first.key == second.key && ((first.modifiers ^ second.modifiers) & common) == 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputBindings::ValidKey(const i32 key)noexcept{
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

bool InputBindings::set(const InputKeyBinding* bindings, const usize count){
    if(count > s_InputMaxBindings || (count != 0u && bindings == nullptr))
        return false;
    for(usize index = 0u; index < count; ++index){
        if(!__hidden_ui_input_bindings::ValidBinding(bindings[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(__hidden_ui_input_bindings::Overlaps(bindings[previous], bindings[index]))
                return false;
        }
    }
    BindingVector candidate(m_bindings.get_allocator());
    candidate.reserve(s_InputMaxBindings);
    if(count != 0u)
        candidate.assign(bindings, bindings + count);
    m_bindings.swap(candidate);
    return true;
}

InputCommandIntent InputBindings::resolve(const i32 key, const i32 modifiers)const noexcept{
    constexpr i32 s_LockModifiers = Core::InputModifier::CapsLock | Core::InputModifier::NumLock;
    if(!ValidKey(key) || (modifiers & ~(s_InputBindingModifierMask | s_LockModifiers)) != 0)
        return {};
    const i32 chord = modifiers & s_InputBindingModifierMask;
    for(const auto& binding : m_bindings){
        if(binding.key != key || (chord & ~binding.ignoredModifiers) != binding.modifiers)
            continue;
        return {
            binding.command,
            binding.selection == InputSelectionPolicy::Always
                || (binding.selection == InputSelectionPolicy::Shift && (chord & Core::InputModifier::Shift) != 0),
            binding.edit,
            binding.allowText
        };
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

