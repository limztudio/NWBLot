// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <core/ecs/world.h>
#include <core/common/log.h>

#include <global/simplemath.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


::NWB::Core::KeyboardInputResult UiLayerSystem::keyboardUpdate(const i32 key, [[maybe_unused]] const i32 scancode, const i32 action, const i32 mods){
    if(!Ui::InputBindings::ValidKey(key) || action < Core::InputAction::Release || action > Core::InputAction::Repeat)
        return false;
    synchronizeNativeInput();
    const usize slot = ResolveUiNativeKeySlot(key);
    const u8 storedOwner = m_nativeKeyOwners[slot];
    const b8 held = storedOwner != 0u;
    const u8 previousOwner = held ? static_cast<u8>(storedOwner - 1u) : UiInputOwner::Scene;
    const b8 customOwned = held ? previousOwner == UiInputOwner::Custom : m_context.input().ownsKey(key);
    if(action != Core::InputAction::Release)
        m_blockCommandChars = false;
    if(action == Core::InputAction::Release || customOwned || !held){
        Ui::InputEvent event;
        event.type = action == Core::InputAction::Release ? Ui::InputEventType::KeyUp : Ui::InputEventType::KeyDown;
        event.key = key;
        event.shift = (mods & Core::InputModifier::Shift) != 0;
        event.control = (mods & Core::InputModifier::Control) != 0;
        event.alt = (mods & Core::InputModifier::Alt) != 0;
        event.super = (mods & Core::InputModifier::Super) != 0;
        event.repeat = action != Core::InputAction::Release && (action == Core::InputAction::Repeat || held);
        routeInput(event, action == Core::InputAction::Release ? nullptr : &m_blockCommandChars);
    }
    const u8 owner = held ? previousOwner : m_context.input().hasPopup() || m_context.input().focus().valid() || m_context.input().ownsKey(key) ? UiInputOwner::Custom : UiInputOwner::Scene;
    m_nativeKeyOwners[slot] = action == Core::InputAction::Release ? 0u : static_cast<u8>(owner + 1u);
    if(action != Core::InputAction::Release)
        m_blockNativeChars = owner == UiInputOwner::Custom;
    return { owner == UiInputOwner::Custom, m_blockCommandChars };
}

b8 UiLayerSystem::keyboardCharInput(const u32 unicode, const i32 mods){
    synchronizeNativeInput();
    m_editHost.collectNative();
    if(m_editHost.hasTextFocus()){
        if((mods & Core::InputModifier::Control) != 0 && (mods & Core::InputModifier::Alt) == 0)
            return true;
        return m_editHost.character(unicode);
    }
    if(m_blockNativeChars || m_context.input().hasPopup() || m_context.input().focus().valid())
        return true;
    return false;
}

b8 UiLayerSystem::mousePosUpdate(const f64 xpos, const f64 ypos){
    if(!ValidUiMouseCoordinate(xpos, ypos))
        return false;
    synchronizeNativeInput();
    m_pointer = { static_cast<f32>(xpos), static_cast<f32>(ypos) };
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.position = m_pointer;
    event.hoverCapable = true;
    if(m_pressedButtons != 0u && m_pointerOwner == UiInputOwner::Scene){
        event.phase = Ui::PointerPhase::Leave;
        routePointerInput(event);
        return false;
    }
    event.phase = Ui::PointerPhase::Move;
    b8 consumed = false;
    routePointerInput(event, &consumed);
    return consumed || (m_pressedButtons != 0u && m_pointerOwner == UiInputOwner::Custom);
}

b8 UiLayerSystem::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods){
    if(button < Core::MouseButton::Left || button > Core::MouseButton::Button8 || (action != Core::InputAction::Press && action != Core::InputAction::Release))
        return false;
    synchronizeNativeInput();
    const u32 bit = 1u << static_cast<u32>(button);
    const b8 firstPress = action == Core::InputAction::Press && (m_pressedButtons & bit) == 0u;
    if(action == Core::InputAction::Press && m_pressedButtons == 0u){
        const b8 acceptsSource = m_context.input().acceptsPointerSource(Ui::s_NativePointerSource);
        const b8 nativeActive = m_context.input().activePointerSource() == Ui::s_NativePointerSource;
        const b8 consumed = nativeActive ? m_context.input().wouldConsumePointer(m_pointer) : m_context.input().hasPopup() || m_context.input().hitTest(m_pointer).valid();
        m_pointerOwner = consumed ? UiInputOwner::Custom : UiInputOwner::Scene;
        if(acceptsSource && m_pointerOwner != UiInputOwner::Custom)
            m_context.input().clearFocus();
    }
    if(action == Core::InputAction::Press)
        m_pressedButtons |= bit;
    const u8 owner = m_pointerOwner;
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.position = m_pointer;
    event.extend = (mods & Core::InputModifier::Shift) != 0;
    event.hoverCapable = true;
    if(owner == UiInputOwner::Custom && (button == Core::MouseButton::Left || button == Core::MouseButton::Right)){
        event.phase = action == Core::InputAction::Release ? Ui::PointerPhase::Release : Ui::PointerPhase::Press;
        event.intent = button == Core::MouseButton::Left ? Ui::PointerIntent::Activate : Ui::PointerIntent::ContextMenu;
        if(event.phase == Ui::PointerPhase::Press && event.intent == Ui::PointerIntent::Activate)
            event.timestampMs = Max<u64>(1u, DurationInMS<u64>(TimerNow()));
        routePointerInput(event);
    }
    if(owner == UiInputOwner::Custom && button == Core::MouseButton::Middle && action == Core::InputAction::Press && m_context.input().acceptsPointerSource(Ui::s_NativePointerSource)){
        m_editHost.collectNative();
        const b8 pasted = m_editHost.pastePrimary(m_pointer);
        if(!pasted)
            m_editHost.synchronizeFocus();
    }
    if(firstPress && (owner == UiInputOwner::Scene || (button != Core::MouseButton::Left && button != Core::MouseButton::Right))){
        event.phase = Ui::PointerPhase::Leave;
        routePointerInput(event);
    }
    if(action == Core::InputAction::Release){
        m_pressedButtons &= ~bit;
        if(m_pressedButtons == 0u){
            m_pointerOwner = UiInputOwner::Scene;
            event.phase = Ui::PointerPhase::Move;
            event.timestampMs = 0u;
            routePointerInput(event);
        }
    }
    return owner != UiInputOwner::Scene;
}

b8 UiLayerSystem::mouseScrollUpdate(const f64 xoffset, const f64 yoffset){
    if(!ValidUiMouseScroll(xoffset, yoffset))
        return false;
    synchronizeNativeInput();
    if(m_pressedButtons != 0u && m_pointerOwner == UiInputOwner::Scene)
        return false;
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.phase = Ui::PointerPhase::Scroll;
    event.position = m_pointer;
    event.scrollX = xoffset;
    event.scrollY = yoffset;
    event.hoverCapable = true;
    b8 consumed = false;
    routePointerInput(event, &consumed);
    return consumed || m_pointerOwner == UiInputOwner::Custom;
}

void UiLayerSystem::windowFocusUpdate(const b8 focused){
    Ui::InputEvent event;
    event.type = focused ? Ui::InputEventType::FocusGained : Ui::InputEventType::FocusLost;
    routeInput(event);
    if(!focused)
        resetNativeInputState();
}

void UiLayerSystem::pointerLeave(){
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.phase = Ui::PointerPhase::Leave;
    event.position = m_pointer;
    event.hoverCapable = true;
    routePointerInput(event);
}

void UiLayerSystem::pointerCaptureLost(){
    if(m_pressedButtons == 0u)
        return;
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.phase = Ui::PointerPhase::Cancel;
    event.position = m_pointer;
    event.hoverCapable = true;
    routePointerInput(event);
    m_pressedButtons = 0u;
    m_pointerOwner = UiInputOwner::Scene;
}

b8 UiLayerSystem::wantsPointer()const noexcept{
    if(m_pressedButtons != 0u)
        return m_pointerOwner != UiInputOwner::Scene;
    return m_resourcesReady && m_context.input().wantsPointer();
}

void UiLayerSystem::routeInput(const Ui::InputEvent& event, b8* blockText){
    if(blockText)
        *blockText = false;
    m_editHost.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queue(event);
    if(!normalized){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: invalid or overflowing normalized input"));
        resetNormalizedInput();
        return;
    }
    const b8 submit = m_editHost.prepareSubmit(*normalized);
    const Ui::InputRoutingResult result = m_context.input().process();
    m_editHost.input(*normalized, previousCapture, submit);
    if(blockText){
        // Core captures this policy only if the native handler owns the physical sequence, including inert repeats.
        *blockText = normalized->type == Ui::InputEventType::CommandDown && normalized->command != Ui::InputCommand::None && !normalized->allowText;
    }
    logInputRoutingOverflows(result);
}

void UiLayerSystem::routePointerInput(const Ui::PointerInput& input, b8* consumed){
    if(consumed)
        *consumed = false;
    m_editHost.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queuePointer(input);
    if(!normalized){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: invalid or overflowing pointer input"));
        resetNormalizedInput();
        return;
    }
    const Ui::InputRoutingResult result = m_context.input().process();
    if(result.pointerRouted)
        m_editHost.input(*normalized, previousCapture, false);
    logInputRoutingOverflows(result);
    if(consumed)
        *consumed = result.pointerConsumed;
}

void UiLayerSystem::synchronizeNativeInput(){
    // Native dispatch occurs outside world system execution; use its joined boundary before reading host roots.
    m_world.taskScope().wait();
    synchronizeInput();
}

void UiLayerSystem::logInputRoutingOverflows(const Ui::InputRoutingResult& result)const{
    if(result.activationOverflow)
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: bounded activation queue is full"));
    if(result.gestureOverflow)
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: bounded pointer gesture queue is full"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

