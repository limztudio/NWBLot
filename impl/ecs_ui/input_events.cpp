// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <global/core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static ImGuiKey MapKey(const i32 key){
    if(key >= Core::Key::A && key <= Core::Key::Z)
        return static_cast<ImGuiKey>(ImGuiKey_A + (key - Core::Key::A));
    if(key >= Core::Key::Number0 && key <= Core::Key::Number9)
        return static_cast<ImGuiKey>(ImGuiKey_0 + (key - Core::Key::Number0));
    if(key >= Core::Key::F1 && key <= Core::Key::F12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - Core::Key::F1));

    switch(key){
    case Core::Key::Tab: return ImGuiKey_Tab;
    case Core::Key::Left: return ImGuiKey_LeftArrow;
    case Core::Key::Right: return ImGuiKey_RightArrow;
    case Core::Key::Up: return ImGuiKey_UpArrow;
    case Core::Key::Down: return ImGuiKey_DownArrow;
    case Core::Key::PageUp: return ImGuiKey_PageUp;
    case Core::Key::PageDown: return ImGuiKey_PageDown;
    case Core::Key::Home: return ImGuiKey_Home;
    case Core::Key::End: return ImGuiKey_End;
    case Core::Key::Insert: return ImGuiKey_Insert;
    case Core::Key::Delete: return ImGuiKey_Delete;
    case Core::Key::Backspace: return ImGuiKey_Backspace;
    case Core::Key::Space: return ImGuiKey_Space;
    case Core::Key::Enter: return ImGuiKey_Enter;
    case Core::Key::Escape: return ImGuiKey_Escape;
    case Core::Key::Apostrophe: return ImGuiKey_Apostrophe;
    case Core::Key::Comma: return ImGuiKey_Comma;
    case Core::Key::Minus: return ImGuiKey_Minus;
    case Core::Key::Period: return ImGuiKey_Period;
    case Core::Key::Slash: return ImGuiKey_Slash;
    case Core::Key::Semicolon: return ImGuiKey_Semicolon;
    case Core::Key::Equal: return ImGuiKey_Equal;
    case Core::Key::LeftBracket: return ImGuiKey_LeftBracket;
    case Core::Key::Backslash: return ImGuiKey_Backslash;
    case Core::Key::RightBracket: return ImGuiKey_RightBracket;
    case Core::Key::GraveAccent: return ImGuiKey_GraveAccent;
    case Core::Key::CapsLock: return ImGuiKey_CapsLock;
    case Core::Key::ScrollLock: return ImGuiKey_ScrollLock;
    case Core::Key::NumLock: return ImGuiKey_NumLock;
    case Core::Key::PrintScreen: return ImGuiKey_PrintScreen;
    case Core::Key::Pause: return ImGuiKey_Pause;
    case Core::Key::Keypad0: return ImGuiKey_Keypad0;
    case Core::Key::Keypad1: return ImGuiKey_Keypad1;
    case Core::Key::Keypad2: return ImGuiKey_Keypad2;
    case Core::Key::Keypad3: return ImGuiKey_Keypad3;
    case Core::Key::Keypad4: return ImGuiKey_Keypad4;
    case Core::Key::Keypad5: return ImGuiKey_Keypad5;
    case Core::Key::Keypad6: return ImGuiKey_Keypad6;
    case Core::Key::Keypad7: return ImGuiKey_Keypad7;
    case Core::Key::Keypad8: return ImGuiKey_Keypad8;
    case Core::Key::Keypad9: return ImGuiKey_Keypad9;
    case Core::Key::KeypadDecimal: return ImGuiKey_KeypadDecimal;
    case Core::Key::KeypadDivide: return ImGuiKey_KeypadDivide;
    case Core::Key::KeypadMultiply: return ImGuiKey_KeypadMultiply;
    case Core::Key::KeypadSubtract: return ImGuiKey_KeypadSubtract;
    case Core::Key::KeypadAdd: return ImGuiKey_KeypadAdd;
    case Core::Key::KeypadEnter: return ImGuiKey_KeypadEnter;
    case Core::Key::KeypadEqual: return ImGuiKey_KeypadEqual;
    case Core::Key::LeftShift: return ImGuiKey_LeftShift;
    case Core::Key::LeftControl: return ImGuiKey_LeftCtrl;
    case Core::Key::LeftAlt: return ImGuiKey_LeftAlt;
    case Core::Key::LeftSuper: return ImGuiKey_LeftSuper;
    case Core::Key::RightShift: return ImGuiKey_RightShift;
    case Core::Key::RightControl: return ImGuiKey_RightCtrl;
    case Core::Key::RightAlt: return ImGuiKey_RightAlt;
    case Core::Key::RightSuper: return ImGuiKey_RightSuper;
    case Core::Key::Menu: return ImGuiKey_Menu;
    default: return ImGuiKey_None;
    }
}

static void AddModifierEvents(ImGuiIO& io, const i32 mods){
    io.AddKeyEvent(ImGuiMod_Ctrl, (mods & Core::InputModifier::Control) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mods & Core::InputModifier::Shift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mods & Core::InputModifier::Alt) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mods & Core::InputModifier::Super) != 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSystem::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    __hidden_ui::AddModifierEvents(io, mods);

    const ImGuiKey imguiKey = __hidden_ui::MapKey(key);
    if(imguiKey != ImGuiKey_None)
        io.AddKeyEvent(imguiKey, action != Core::InputAction::Release);

    return io.WantCaptureKeyboard;
}

bool UiSystem::keyboardCharInput(const u32 unicode, const i32 mods){
    static_cast<void>(mods);
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    if(unicode > 0u)
        io.AddInputCharacter(unicode);
    return io.WantTextInput || io.WantCaptureKeyboard;
}

bool UiSystem::mousePosUpdate(const f64 xpos, const f64 ypos){
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(static_cast<f32>(xpos), static_cast<f32>(ypos));
    return io.WantCaptureMouse;
}

bool UiSystem::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods){
    static_cast<void>(mods);
    setCurrentContext();

    if(button < 0 || button >= ImGuiMouseButton_COUNT)
        return false;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, action != Core::InputAction::Release);
    return io.WantCaptureMouse;
}

bool UiSystem::mouseScrollUpdate(const f64 xoffset, const f64 yoffset){
    setCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(static_cast<f32>(xoffset), static_cast<f32>(yoffset));
    return io.WantCaptureMouse;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

