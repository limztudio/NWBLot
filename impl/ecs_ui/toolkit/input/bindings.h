// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "commands.h"

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputSelectionPolicy{
    enum Enum : u8{ None, Always, Shift };
};

struct InputKeyBinding{
    i32 key = Core::Key::Unknown;
    i32 modifiers = 0;
    i32 ignoredModifiers = 0;
    InputCommand::Enum command = InputCommand::None;
    InputSelectionPolicy::Enum selection = InputSelectionPolicy::None;
    bool edit = true;
    // Allow this command's ordinary native character to reach the editor as well.
    bool allowText = false;
};

inline constexpr usize s_InputMaxBindings = 256u;
inline constexpr i32 s_InputBindingModifierMask = Core::InputModifier::Shift | Core::InputModifier::Control
    | Core::InputModifier::Alt | Core::InputModifier::Super;

class InputBindings final : NoCopy{
public:
    using BindingVector = Vector<InputKeyBinding, Core::Alloc::GlobalArena>;


public:
    [[nodiscard]] static bool validKey(i32 key);


public:
    explicit InputBindings(Core::Alloc::GlobalArena& arena);


public:
    // Replacement is atomic, accepts an empty profile, and rejects overlapping chords for the same physical key.
    [[nodiscard]] bool set(const InputKeyBinding* bindings, usize count);
    void restoreDefaults();
    [[nodiscard]] InputCommandIntent resolve(i32 key, i32 modifiers)const;
    // Borrowed observations remain valid until a successful profile replacement or default restoration.
    [[nodiscard]] const BindingVector& bindings()const{ return m_bindings; }


private:
    BindingVector m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

