// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bindings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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

