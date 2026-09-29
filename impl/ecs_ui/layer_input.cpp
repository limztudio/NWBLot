// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <core/ecs/world.h>
#include <core/common/log.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_layer_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u8 s_Scene = 0u;
inline constexpr u8 s_Custom = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Ui::InputKey::Enum TranslateKey(const i32 key){
    switch(key){
    case Core::Key::Tab: return Ui::InputKey::Tab;
    case Core::Key::Enter:
    case Core::Key::KeypadEnter: return Ui::InputKey::Enter;
    case Core::Key::Space: return Ui::InputKey::Space;
    case Core::Key::Escape: return Ui::InputKey::Escape;
    case Core::Key::Left: return Ui::InputKey::Left;
    case Core::Key::Right: return Ui::InputKey::Right;
    case Core::Key::Home: return Ui::InputKey::Home;
    case Core::Key::End: return Ui::InputKey::End;
    case Core::Key::Backspace: return Ui::InputKey::Backspace;
    case Core::Key::Delete: return Ui::InputKey::Delete;
    case Core::Key::A: return Ui::InputKey::A;
    case Core::Key::C: return Ui::InputKey::C;
    case Core::Key::X: return Ui::InputKey::X;
    case Core::Key::V: return Ui::InputKey::V;
    case Core::Key::Z: return Ui::InputKey::Z;
    case Core::Key::Y: return Ui::InputKey::Y;
    case Core::Key::Up: return Ui::InputKey::Up;
    case Core::Key::Down: return Ui::InputKey::Down;
    case Core::Key::PageUp: return Ui::InputKey::PageUp;
    case Core::Key::PageDown: return Ui::InputKey::PageDown;
    case Core::Key::Menu: return Ui::InputKey::Menu;
    case Core::Key::F10: return Ui::InputKey::F10;
    default: return Ui::InputKey::None;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiLayerSystem::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    synchronizeNativeInput();
    const Ui::InputKey::Enum translated = __hidden_layer_input::TranslateKey(key);
    const usize slot = key >= -1 && key < 511 ? static_cast<usize>(key + 1) : m_nativeKeyOwners.size();
    const u8 storedOwner = slot < m_nativeKeyOwners.size() ? m_nativeKeyOwners[slot] : 0u;
    const bool held = storedOwner != 0u;
    const u8 previousOwner = held ? static_cast<u8>(storedOwner - 1u) : __hidden_layer_input::s_Scene;
    const bool customOwned = held ? previousOwner == __hidden_layer_input::s_Custom : m_context.input().ownsKey(translated);
    bool releaseRouter = true;
    if(translated == Ui::InputKey::Enter && action == Core::InputAction::Release){
        const i32 otherKey = key == Core::Key::Enter ? Core::Key::KeypadEnter : Core::Key::Enter;
        const u8 otherOwner = m_nativeKeyOwners[static_cast<usize>(otherKey + 1)];
        releaseRouter = otherOwner == 0u;
    }
    if(
        translated != Ui::InputKey::None
        && (action == Core::InputAction::Release ? releaseRouter : customOwned || !held)
    ){
        Ui::InputEvent event;
        event.type = action == Core::InputAction::Release ? Ui::InputEventType::KeyUp : Ui::InputEventType::KeyDown;
        event.key = translated;
        event.shift = (mods & Core::InputModifier::Shift) != 0;
        event.control = (mods & Core::InputModifier::Control) != 0;
        event.alt = (mods & Core::InputModifier::Alt) != 0;
        event.repeat = action == Core::InputAction::Repeat || held;
        routeInput(event);
    }
    const u8 owner = held ? previousOwner : m_context.input().hasPopup() || m_context.input().focus().valid() || m_context.input().ownsKey(translated)
        ? __hidden_layer_input::s_Custom : __hidden_layer_input::s_Scene;
    if(slot < m_nativeKeyOwners.size())
        m_nativeKeyOwners[slot] = action == Core::InputAction::Release ? 0u : static_cast<u8>(owner + 1u);
    if(action != Core::InputAction::Release)
        m_blockNativeChars = owner == __hidden_layer_input::s_Custom;
    return owner == __hidden_layer_input::s_Custom;
}

bool UiLayerSystem::keyboardCharInput(const u32 unicode, const i32 mods){
    synchronizeNativeInput();
    m_editHost.collectNative();
    if(m_editHost.hasTextFocus()){
        if((mods & Core::InputModifier::Control) != 0 && (mods & Core::InputModifier::Alt) == 0)
            return true;
        return m_editHost.character(unicode);
    }
    if(m_blockNativeChars || m_context.input().hasPopup() || m_context.input().focus().valid())
        return true;
    const auto& input = m_context.input();
    if(
        (unicode == 32u && input.ownsKey(Ui::InputKey::Space))
        || ((unicode == 10u || unicode == 13u) && input.ownsKey(Ui::InputKey::Enter))
        || (unicode == 9u && input.ownsKey(Ui::InputKey::Tab))
        || (unicode == 27u && input.ownsKey(Ui::InputKey::Escape))
    )
        return true;
    return false;
}

bool UiLayerSystem::mousePosUpdate(const f64 xpos, const f64 ypos){
    if(!IsFinite(xpos) || !IsFinite(ypos) || Abs(xpos) > Limit<f32>::s_Max || Abs(ypos) > Limit<f32>::s_Max)
        return false;
    synchronizeNativeInput();
    m_pointer = { static_cast<f32>(xpos), static_cast<f32>(ypos) };
    if(m_pressedButtons != 0u && m_pointerOwner == __hidden_layer_input::s_Scene){
        Ui::InputEvent leave;
        leave.type = Ui::InputEventType::PointerLeave;
        routeInput(leave);
        return false;
    }
    Ui::InputEvent event;
    event.type = Ui::InputEventType::PointerMove;
    event.position = m_pointer;
    routeInput(event);
    const bool consumed = m_context.input().wantsPointer() || (m_pressedButtons != 0u && m_pointerOwner == __hidden_layer_input::s_Custom);
    return consumed;
}

bool UiLayerSystem::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods){
    if(button < Core::MouseButton::Left || button > Core::MouseButton::Button8)
        return false;
    synchronizeNativeInput();
    const u32 bit = 1u << static_cast<u32>(button);
    const bool firstPress = action != Core::InputAction::Release && (m_pressedButtons & bit) == 0u;
    if(action != Core::InputAction::Release && m_pressedButtons == 0u){
        m_pointerOwner = m_context.input().wouldConsumePointer(m_pointer)
            ? __hidden_layer_input::s_Custom : __hidden_layer_input::s_Scene;
        if(m_pointerOwner != __hidden_layer_input::s_Custom)
            m_context.input().clearFocus();
    }
    if(action != Core::InputAction::Release)
        m_pressedButtons |= bit;
    const u8 owner = m_pointerOwner;
    if(owner == __hidden_layer_input::s_Custom && button == Core::MouseButton::Left){
        Ui::InputEvent event;
        event.type = action == Core::InputAction::Release ? Ui::InputEventType::PrimaryUp : Ui::InputEventType::PrimaryDown;
        event.position = m_pointer;
        event.shift = (mods & Core::InputModifier::Shift) != 0;
        routeInput(event);
    }
    if(owner == __hidden_layer_input::s_Custom && button == Core::MouseButton::Right){
        Ui::InputEvent event;
        event.type = action == Core::InputAction::Release ? Ui::InputEventType::SecondaryUp : Ui::InputEventType::SecondaryDown;
        event.position = m_pointer;
        event.shift = (mods & Core::InputModifier::Shift) != 0;
        event.control = (mods & Core::InputModifier::Control) != 0;
        event.alt = (mods & Core::InputModifier::Alt) != 0;
        routeInput(event);
    }
    if(owner == __hidden_layer_input::s_Custom && button == Core::MouseButton::Middle && action != Core::InputAction::Release){
        m_editHost.collectNative();
        const bool pasted = m_editHost.pastePrimary(m_pointer);
        if(!pasted)
            m_editHost.synchronizeFocus();
    }
    if(
        firstPress && (owner == __hidden_layer_input::s_Scene
            || (button != Core::MouseButton::Left && button != Core::MouseButton::Right))
    ){
        Ui::InputEvent leave;
        leave.type = Ui::InputEventType::PointerLeave;
        routeInput(leave);
    }
    if(action == Core::InputAction::Release){
        m_pressedButtons &= ~bit;
        if(m_pressedButtons == 0u){
            m_pointerOwner = __hidden_layer_input::s_Scene;
            Ui::InputEvent event;
            event.type = Ui::InputEventType::PointerMove;
            event.position = m_pointer;
            routeInput(event);
        }
    }
    return owner != __hidden_layer_input::s_Scene;
}

bool UiLayerSystem::mouseScrollUpdate(const f64 xoffset, const f64 yoffset){
    if(!IsFinite(xoffset) || !IsFinite(yoffset))
        return false;
    synchronizeNativeInput();
    if(m_pressedButtons != 0u && m_pointerOwner == __hidden_layer_input::s_Scene)
        return false;
    Ui::InputEvent event;
    event.type = Ui::InputEventType::PointerWheel;
    event.position = m_pointer;
    event.scrollX = xoffset;
    event.scrollY = yoffset;
    routeInput(event);
    return m_pointerOwner == __hidden_layer_input::s_Custom || m_context.input().wouldConsumePointer(m_pointer);
}

void UiLayerSystem::windowFocusUpdate(const bool focused){
    Ui::InputEvent event;
    event.type = focused ? Ui::InputEventType::FocusGained : Ui::InputEventType::FocusLost;
    routeInput(event);
    if(!focused){
        m_pressedButtons = 0u;
        m_pointerOwner = __hidden_layer_input::s_Scene;
        m_blockNativeChars = false;
        m_nativeKeyOwners.fill(0u);
    }
}

void UiLayerSystem::pointerLeave(){
    Ui::InputEvent event;
    event.type = Ui::InputEventType::PointerLeave;
    routeInput(event);
}

void UiLayerSystem::pointerCaptureLost(){
    if(m_pressedButtons == 0u)
        return;
    Ui::InputEvent event;
    event.type = Ui::InputEventType::PointerCaptureLost;
    routeInput(event);
    m_pressedButtons = 0u;
    m_pointerOwner = __hidden_layer_input::s_Scene;
}

bool UiLayerSystem::wantsKeyboard()const{
    return m_resourcesReady && m_context.input().wantsKeyboard();
}

bool UiLayerSystem::wantsPointer()const{
    if(m_pressedButtons != 0u)
        return m_pointerOwner != __hidden_layer_input::s_Scene;
    return m_resourcesReady && m_context.input().wantsPointer();
}

bool UiLayerSystem::wantsTextInput()const{
    return m_resourcesReady && m_editHost.wantsTextInput();
}

void UiLayerSystem::routeInput(const Ui::InputEvent& event){
    m_editHost.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    if(!m_context.input().queue(event)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: invalid or overflowing normalized input"));
        m_context.resetInput();
        m_editHost.reset();
        return;
    }
    const Ui::InputRoutingResult result = m_context.input().process();
    m_editHost.input(event, previousCapture);
    m_editHost.synchronizeFocus();
    if(result.activationOverflow)
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: bounded activation queue is full"));
    if(result.gestureOverflow)
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: bounded pointer gesture queue is full"));
}

void UiLayerSystem::synchronizeNativeInput(){
    // Native dispatch occurs outside world system execution; use its joined boundary before reading host roots.
    m_world.taskScope().wait();
    synchronizeInput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

