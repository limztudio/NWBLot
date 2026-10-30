// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/ecs_ui/toolkit/input/commands.adhd.h>

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr i32 s_InputBindingModifierMask = Core::InputModifier::Shift | Core::InputModifier::Control | Core::InputModifier::Alt | Core::InputModifier::Super;

class InputBindings final : NoCopy{
public:
    using BindingVector = Vector<InputKeyBinding, Core::Alloc::GlobalArena>;


public:
    [[nodiscard]] static b8 ValidKey(i32 key)noexcept;


public:
    explicit InputBindings(Core::Alloc::GlobalArena& arena);


public:
    // Replacement is atomic, accepts an empty profile, and rejects overlapping chords for the same physical key.
    [[nodiscard]] b8 set(const InputKeyBinding* bindings, usize count);
    void restoreDefaults();
    [[nodiscard]] InputCommandIntent resolve(i32 key, i32 modifiers)const noexcept;
    // Borrowed observations remain valid until a successful profile replacement or default restoration.
    [[nodiscard]] const BindingVector& bindings()const noexcept{ return m_bindings; }


private:
    BindingVector m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

