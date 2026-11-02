// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>

#include <impl/ecs_ui/toolkit/input/commands.adhd.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputBindings final : NoCopy{
public:
    using BindingVector = Vector<InputKeyBinding, Core::Alloc::GlobalArena>;


public:
    [[nodiscard]] static b8 ValidKey(const i32 key)noexcept{
        return IsValidInputKeyCode(key);
    }


public:
    explicit InputBindings(Core::Alloc::GlobalArena& arena)
        : m_bindings(arena)
    {
        m_bindings.reserve(s_InputMaxBindings);
        restoreDefaults();
    }


public:
    // Replacement is atomic, accepts an empty profile, and rejects overlapping chords for the same physical key.
    [[nodiscard]] b8 set(const Span<const InputKeyBinding> bindings){
        if(!ValidateInputKeyBindingProfile(bindings, s_InputBindingModifierMask))
            return false;
        BindingVector candidate(m_bindings.get_allocator());
        candidate.reserve(s_InputMaxBindings);
        if(!bindings.empty())
            candidate.assign(bindings.begin(), bindings.end());
        m_bindings.swap(candidate);
        return true;
    }
    void restoreDefaults(){
        BindingVector candidate(m_bindings.get_allocator());
        candidate.reserve(s_InputMaxBindings);
        for(usize index = 0u; index < s_DefaultInputKeyBindingCount; ++index)
            candidate.push_back(ResolveDefaultInputKeyBinding(index));
        if(!ValidateInputKeyBindingProfile(candidate, s_InputBindingModifierMask))
            TerminateInvariant();
        m_bindings.swap(candidate);
    }
    [[nodiscard]] InputCommandIntent resolve(const i32 key, const i32 modifiers)const noexcept{
        return ResolveInputKeyBinding(m_bindings, key, modifiers, ValidKey(key), s_InputBindingModifierMask, s_InputLockModifierMask, s_InputModifierShift);
    }
    // Borrowed observations remain valid until a successful profile replacement or default restoration.
    [[nodiscard]] const BindingVector& bindings()const noexcept{ return m_bindings; }


private:
    BindingVector m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

