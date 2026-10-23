// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_wayland_input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && defined(NWB_WITH_WAYLAND)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_detail.h"
#include "input_helpers.h"

#include <core/common/log.h>
#include <core/os/linux/wayland/clipboard.h>
#include <core/os/linux/wayland/text_input.h>

#include <global/diagnostics.h>
#include <global/simd/vector_arithmetic.h>
#include <global/simd/vector_rounding.h>

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#pragma push_macro("interface")
#undef interface
#include <xkbcommon/xkbcommon-keysyms.h>
#pragma pop_macro("interface")
#include <cerrno>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_linux_wayland_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_WaylandScrollAxisUnit = 120.0;
static constexpr i32 s_MillisecondsPerSecond = 1000;
static constexpr u32 s_EvdevToXkbKeycodeOffset = 8u;
static constexpr i32 s_MinKeyRepeatStepMs = 1;
static constexpr u32 s_WaylandSeatBindVersion = 5u;
static constexpr u32 s_WaylandPointerFrameSeatVersion = 5u;
static constexpr u32 s_WaylandSeatReleaseVersion = 5u;
static constexpr StringView s_WarningEventName = "warning";
static constexpr StringView s_KeymapLifecycleCategory = "frame_keymap_lifecycle";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static SIMDVectorDouble SurfaceCoordinatesToPixels(const SIMDVectorDouble surfaceCoordinates, const SIMDVectorDouble bufferScale)noexcept{
    return VectorDoubleMultiply(surfaceCoordinates, bufferScale);
}

static i32 TranslatePointerButton(u32 button)noexcept{
    switch(button){
    case BTN_LEFT: return MouseButton::Left;
    case BTN_MIDDLE: return MouseButton::Middle;
    case BTN_RIGHT: return MouseButton::Right;
    case BTN_SIDE: return MouseButton::Button4;
    case BTN_EXTRA: return MouseButton::Button5;
    case BTN_FORWARD: return MouseButton::Button6;
    case BTN_BACK: return MouseButton::Button7;
    case BTN_TASK: return MouseButton::Button8;
    default:
        return -1;
    }
}

NWB_DECLARE_LINUX_KEY_SYMBOLS(WaylandKeySymbols, xkb_keysym_t, XKB_KEY);

static i32 TranslateKey(xkb_keysym_t keySym)noexcept{
    return FrameDetail::TranslateLinuxKeySymbol<WaylandKeySymbols>(keySym);
}

static void ReleaseKeymapResources(const i32 fd, void* const map, const u32 size)noexcept{
    const bool unmapFailed = map && munmap(map, size) != 0;
    const i32 unmapError = unmapFailed ? errno : 0;
    // Linux releases the descriptor before reporting close errors; retrying can close a reused descriptor.
    const bool closeFailed = close(fd) != 0;
    const i32 closeError = closeFailed ? errno : 0;
    if(unmapFailed){
        const DiagnosticEventText message = MakeDiagnosticEventText("Frame Wayland keymap unmap failed; native_domain=POSIX native_code={}", unmapError);
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_KeymapLifecycleCategory, .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
    if(closeFailed){
        const DiagnosticEventText message = MakeDiagnosticEventText("Frame Wayland keymap descriptor release failed; native_domain=POSIX native_code={}", closeError);
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = s_WarningEventName, .category = s_KeymapLifecycleCategory, .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandFrameInput::OnSeatCapabilities(void* data, wl_seat* seat, u32 capabilities){
    static const wl_pointer_listener s_PointerListener = {
        &OnPointerEnter,
        &OnPointerLeave,
        &OnPointerMotion,
        &OnPointerButton,
        &OnPointerAxis,
        &OnPointerFrame,
        &OnPointerAxisSource,
        &OnPointerAxisStop,
        &OnPointerAxisDiscrete,
#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
        &OnPointerAxisValue120,
#endif
#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
        &OnPointerAxisRelativeDirection,
#endif
    };
    static const wl_keyboard_listener s_KeyboardListener = { &OnKeyboardKeymap, &OnKeyboardEnter, &OnKeyboardLeave, &OnKeyboardKey, &OnKeyboardModifiers, &OnKeyboardRepeatInfo, };

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);

    if((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0u){
        if(!frameInput.m_pointer){
            frameInput.m_pointer = wl_seat_get_pointer(seat);
            if(!frameInput.m_pointer || wl_pointer_add_listener(frameInput.m_pointer, &s_PointerListener, &frameInput) != 0){
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland pointer listener registration failed"));
                return;
            }
        }
    }
    else{
        if(frameInput.m_pointer){
            frameInput.m_frame.input().pointerCaptureLost();
            frameInput.m_frame.input().pointerLeave();
        }
        frameInput.destroyPointer();
    }

    if((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0u){
        if(!frameInput.m_keyboard){
            frameInput.m_keyboard = wl_seat_get_keyboard(seat);
            if(!frameInput.m_keyboard || wl_keyboard_add_listener(frameInput.m_keyboard, &s_KeyboardListener, &frameInput) != 0){
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland keyboard listener registration failed"));
                return;
            }
        }
    }
    else{
        frameInput.m_keyboardFocused = false;
        frameInput.m_inputSerial = 0u;
        if(ITextInputService* const textInput = frameInput.m_frame.tryTextInput()){
            if(!SetWaylandTextInputKeyboardFocus(*textInput, false))
                NWB_FATAL_ASSERT(false);
        }
        if(IClipboardService* const clipboard = frameInput.m_frame.tryClipboard())
            SetWaylandClipboardKeyboardFocus(*clipboard, false);
        frameInput.destroyKeyboard();
        frameInput.m_frame.data<Common::LinuxFrame>().setActive(false);
        frameInput.m_frame.input().windowFocusUpdate(false);
    }
}

void WaylandFrameInput::OnSeatName(void* data, wl_seat* seat, const char* name)noexcept{
    static_cast<void>(data);
    static_cast<void>(seat);
    static_cast<void>(name);
}

void WaylandFrameInput::OnPointerEnter(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy){
    using namespace __hidden_frame_linux_wayland_input;

    static_cast<void>(pointer);
    static_cast<void>(serial);
    static_cast<void>(surface);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    const SIMDVectorDouble pixelCoordinates = SurfaceCoordinatesToPixels(
        VectorDoubleSet(wl_fixed_to_double(sx), wl_fixed_to_double(sy), 0.0, 0.0),
        VectorDoubleReplicate(static_cast<f64>(FrameDetail::ClampBufferScale(frameInput.m_bufferScale)))
    );
    frameInput.m_frame.input().mousePosUpdate(VectorDoubleGetX(pixelCoordinates), VectorDoubleGetY(pixelCoordinates));
}

void WaylandFrameInput::OnPointerLeave(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface){
    static_cast<void>(pointer);
    static_cast<void>(serial);
    static_cast<void>(surface);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    frameInput.m_frame.input().pointerLeave();
    // The compositor retains pointer focus during the implicit grab; leave terminates any remaining grab.
    frameInput.m_frame.input().pointerCaptureLost();
}

void WaylandFrameInput::OnPointerMotion(void* data, wl_pointer* pointer, u32 time, wl_fixed_t sx, wl_fixed_t sy){
    using namespace __hidden_frame_linux_wayland_input;

    static_cast<void>(pointer);
    static_cast<void>(time);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    const SIMDVectorDouble pixelCoordinates = SurfaceCoordinatesToPixels(
        VectorDoubleSet(wl_fixed_to_double(sx), wl_fixed_to_double(sy), 0.0, 0.0),
        VectorDoubleReplicate(static_cast<f64>(FrameDetail::ClampBufferScale(frameInput.m_bufferScale)))
    );
    frameInput.m_frame.input().mousePosUpdate(VectorDoubleGetX(pixelCoordinates), VectorDoubleGetY(pixelCoordinates));
}

void WaylandFrameInput::OnPointerButton(void* data, wl_pointer* pointer, u32 serial, u32 time, u32 button, u32 state){
    using namespace __hidden_frame_linux_wayland_input;

    static_cast<void>(pointer);
    static_cast<void>(time);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    frameInput.m_inputSerial = serial;
    if(IClipboardService* const clipboard = frameInput.m_frame.tryClipboard())
        ObserveWaylandClipboardInputSerial(*clipboard, serial);
    const i32 translatedButton = TranslatePointerButton(button);
    if(translatedButton != -1){
        frameInput.m_frame.input().mouseButtonUpdate(translatedButton, state == WL_POINTER_BUTTON_STATE_PRESSED ? InputAction::Press : InputAction::Release, frameInput.translateModifiers());
    }
}

void WaylandFrameInput::OnPointerAxis(void* data, wl_pointer* pointer, u32 time, u32 axis, wl_fixed_t value){
    using namespace __hidden_frame_linux_wayland_input;

    static_cast<void>(pointer);
    static_cast<void>(time);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    frameInput.m_scrollPending = true;

    const SIMDVectorDouble delta = VectorDoubleReplicate(wl_fixed_to_double(value));
    if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        frameInput.m_scrollX = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(frameInput.m_scrollX), delta));
    else if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        frameInput.m_scrollY = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(frameInput.m_scrollY), delta));

    if(frameInput.m_seatVersion < s_WaylandPointerFrameSeatVersion)
        frameInput.dispatchScroll();
}

void WaylandFrameInput::OnPointerFrame(void* data, wl_pointer* pointer){
    static_cast<void>(pointer);
    static_cast<WaylandFrameInput*>(data)->dispatchScroll();
}

void WaylandFrameInput::OnPointerAxisSource(void* data, wl_pointer* pointer, u32 axisSource)noexcept{
    static_cast<void>(data);
    static_cast<void>(pointer);
    static_cast<void>(axisSource);
}

void WaylandFrameInput::OnPointerAxisStop(void* data, wl_pointer* pointer, u32 time, u32 axis)noexcept{
    static_cast<void>(data);
    static_cast<void>(pointer);
    static_cast<void>(time);
    static_cast<void>(axis);
}

void WaylandFrameInput::OnPointerAxisDiscrete(void* data, wl_pointer* pointer, u32 axis, i32 discrete)noexcept{
    static_cast<void>(pointer);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    frameInput.m_scrollPending = true;

    if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        frameInput.m_scrollDiscreteX += discrete;
    else if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        frameInput.m_scrollDiscreteY += discrete;
}

#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
void WaylandFrameInput::OnPointerAxisValue120(void* data, wl_pointer* pointer, u32 axis, i32 value120)noexcept{
    static_cast<void>(data);
    static_cast<void>(pointer);
    static_cast<void>(axis);
    static_cast<void>(value120);
}
#endif

#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
void WaylandFrameInput::OnPointerAxisRelativeDirection(void* data, wl_pointer* pointer, u32 axis, u32 direction)noexcept{
    static_cast<void>(data);
    static_cast<void>(pointer);
    static_cast<void>(axis);
    static_cast<void>(direction);
}
#endif

void WaylandFrameInput::OnKeyboardKeymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size){
    using namespace __hidden_frame_linux_wayland_input;

    static_cast<void>(keyboard);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);

    if(format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1){
        ReleaseKeymapResources(fd, nullptr, 0u);
        return;
    }

    char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if(map == MAP_FAILED){
        const i32 nativeError = errno;
        ReleaseKeymapResources(fd, nullptr, 0u);
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland keymap mmap failed; native_domain=POSIX native_code={}"), nativeError);
        return;
    }

    xkb_keymap* keymap = xkb_keymap_new_from_string(frameInput.m_xkbContext, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    ReleaseKeymapResources(fd, map, size);

    if(!keymap){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland keymap creation failed"));
        return;
    }

    xkb_state* state = xkb_state_new(keymap);
    if(!state){
        xkb_keymap_unref(keymap);
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland keyboard state creation failed"));
        return;
    }

    if(frameInput.m_xkbState)
        xkb_state_unref(frameInput.m_xkbState);
    if(frameInput.m_xkbKeymap)
        xkb_keymap_unref(frameInput.m_xkbKeymap);

    frameInput.m_xkbKeymap = keymap;
    frameInput.m_xkbState = state;
}

void WaylandFrameInput::OnKeyboardEnter(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface, wl_array* keys){
    static_cast<void>(keyboard);
    static_cast<void>(serial);
    static_cast<void>(keys);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    if(surface != frameInput.m_surface)
        return;
    frameInput.m_keyboardFocused = true;
    if(ITextInputService* const textInput = frameInput.m_frame.tryTextInput()){
        if(!SetWaylandTextInputKeyboardFocus(*textInput, true))
            NWB_FATAL_ASSERT(false);
    }
    if(IClipboardService* const clipboard = frameInput.m_frame.tryClipboard())
        SetWaylandClipboardKeyboardFocus(*clipboard, true);
    frameInput.m_frame.data<Common::LinuxFrame>().setActive(true);
    frameInput.m_frame.input().windowFocusUpdate(true);
}

void WaylandFrameInput::OnKeyboardLeave(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface){
    static_cast<void>(keyboard);
    static_cast<void>(serial);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    if(surface != frameInput.m_surface)
        return;
    frameInput.m_keyboardFocused = false;
    frameInput.m_inputSerial = 0u;
    if(ITextInputService* const textInput = frameInput.m_frame.tryTextInput()){
        if(!SetWaylandTextInputKeyboardFocus(*textInput, false))
            NWB_FATAL_ASSERT(false);
    }
    if(IClipboardService* const clipboard = frameInput.m_frame.tryClipboard())
        SetWaylandClipboardKeyboardFocus(*clipboard, false);
    frameInput.m_frame.data<Common::LinuxFrame>().setActive(false);
    frameInput.stopKeyRepeat();
    frameInput.m_frame.input().windowFocusUpdate(false);
}

void WaylandFrameInput::OnKeyboardKey(void* data, wl_keyboard* keyboard, u32 serial, u32 time, u32 key, u32 state){
    using namespace __hidden_frame_linux_wayland_input;

    static_cast<void>(keyboard);
    static_cast<void>(time);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    frameInput.m_inputSerial = serial;
    if(IClipboardService* const clipboard = frameInput.m_frame.tryClipboard())
        ObserveWaylandClipboardInputSerial(*clipboard, serial);
    if(!frameInput.m_xkbState)
        return;

    const u32 keycode = key + s_EvdevToXkbKeycodeOffset;
    const xkb_keysym_t keySym = xkb_state_key_get_one_sym(frameInput.m_xkbState, keycode);
    const i32 translatedKey = TranslateKey(keySym);

    if(state == WL_KEYBOARD_KEY_STATE_PRESSED){
        xkb_state_update_key(frameInput.m_xkbState, keycode, XKB_KEY_DOWN);

        const i32 mods = frameInput.translateModifiers();
        frameInput.m_frame.input().keyboardUpdate(translatedKey, static_cast<i32>(key), InputAction::Press, mods);
        frameInput.dispatchTextInput(keycode, static_cast<i32>(key), mods);

        if(frameInput.m_repeatRate > 0 && frameInput.m_xkbKeymap && xkb_keymap_key_repeats(frameInput.m_xkbKeymap, keycode) > 0){
            frameInput.m_repeatPending = true;
            frameInput.m_repeatKeycode = keycode;
            frameInput.m_repeatKey = translatedKey;
            frameInput.m_repeatScancode = static_cast<i32>(key);
            frameInput.m_nextRepeatTime = TimerAddMS(TimerNow(), frameInput.m_repeatDelayMs);
        }
    }
    else{
        xkb_state_update_key(frameInput.m_xkbState, keycode, XKB_KEY_UP);

        const i32 mods = frameInput.translateModifiers();
        frameInput.m_frame.input().keyboardUpdate(translatedKey, static_cast<i32>(key), InputAction::Release, mods);

        if(frameInput.m_repeatPending && frameInput.m_repeatKeycode == keycode)
            frameInput.stopKeyRepeat();
    }
}

void WaylandFrameInput::OnKeyboardModifiers(void* data, wl_keyboard* keyboard, u32 serial, u32 depressed, u32 latched, u32 locked, u32 group)noexcept{
    static_cast<void>(keyboard);
    static_cast<void>(serial);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    if(frameInput.m_xkbState)
        xkb_state_update_mask(frameInput.m_xkbState, depressed, latched, locked, 0, 0, group);
}

void WaylandFrameInput::OnKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, i32 rate, i32 delay)noexcept{
    static_cast<void>(keyboard);

    auto& frameInput = *static_cast<WaylandFrameInput*>(data);
    frameInput.m_repeatRate = rate;
    frameInput.m_repeatDelayMs = delay;

    if(rate <= 0)
        frameInput.stopKeyRepeat();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


WaylandFrameInput::~WaylandFrameInput()noexcept{
    // Seat proxies are released by destroy() before the display disconnects; the xkb objects do not depend on the display.
    if(m_xkbState)
        xkb_state_unref(m_xkbState);
    if(m_xkbKeymap)
        xkb_keymap_unref(m_xkbKeymap);
    if(m_xkbContext)
        xkb_context_unref(m_xkbContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WaylandFrameInput::createKeymapContext()noexcept{
    m_xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    return m_xkbContext != nullptr;
}

void WaylandFrameInput::bindSeat(wl_registry& registry, const u32 name, const u32 version){
    using namespace __hidden_frame_linux_wayland_input;

    static const wl_seat_listener s_SeatListener = { &OnSeatCapabilities, &OnSeatName, };

    if(m_seat)
        return;

    const u32 bindVersion = version < s_WaylandSeatBindVersion ? version : s_WaylandSeatBindVersion;
    m_seat = static_cast<wl_seat*>(wl_registry_bind(&registry, name, &wl_seat_interface, bindVersion));
    if(!m_seat){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland seat acquisition failed"));
        return;
    }
    m_seatVersion = bindVersion;
    m_seatGlobalName = name;
    if(wl_seat_add_listener(m_seat, &s_SeatListener, this) != 0)
        NWB_FATAL_ASSERT(false);
    if(IClipboardService* const clipboard = m_frame.tryClipboard())
        AttachWaylandClipboardSeat(*clipboard, m_seat, name);
    if(ITextInputService* const textInput = m_frame.tryTextInput())
        AttachWaylandTextInputSeat(*textInput, m_seat, name);
}

void WaylandFrameInput::removeSeat(const u32 name){
    using namespace __hidden_frame_linux_wayland_input;

    if(name != m_seatGlobalName)
        return;
    m_keyboardFocused = false;
    m_inputSerial = 0u;
    if(ITextInputService* const textInput = m_frame.tryTextInput()){
        if(!SetWaylandTextInputKeyboardFocus(*textInput, false))
            NWB_FATAL_ASSERT(false);
        AttachWaylandTextInputSeat(*textInput, nullptr, 0u);
    }
    m_frame.input().windowFocusUpdate(false);
    if(IClipboardService* const clipboard = m_frame.tryClipboard())
        AttachWaylandClipboardSeat(*clipboard, nullptr, 0u);
    m_frame.input().pointerCaptureLost();
    m_frame.input().pointerLeave();
    destroyKeyboard();
    destroyPointer();
    if(m_seatVersion >= s_WaylandSeatReleaseVersion)
        wl_seat_release(m_seat);
    else
        wl_seat_destroy(m_seat);
    m_seat = nullptr;
    m_seatGlobalName = 0u;
    m_seatVersion = 0u;
    m_frame.data<Common::LinuxFrame>().setActive(false);
}

void WaylandFrameInput::observeSurface(wl_surface* const surface, const i32 bufferScale)noexcept{
    m_surface = surface;
    m_bufferScale = bufferScale;
}

void WaylandFrameInput::attachClipboard(IClipboardService& clipboard)const{
    AttachWaylandClipboardSeat(clipboard, m_seat, m_seatGlobalName);
    SetWaylandClipboardKeyboardFocus(clipboard, m_keyboardFocused);
    if(m_inputSerial)
        ObserveWaylandClipboardInputSerial(clipboard, m_inputSerial);
}

void WaylandFrameInput::attachTextInput(ITextInputService& textInput)const{
    AttachWaylandTextInputSeat(textInput, m_seat, m_seatGlobalName);
    SetWaylandTextInputBufferScale(textInput, m_bufferScale);
    if(!SetWaylandTextInputKeyboardFocus(textInput, m_keyboardFocused))
        NWB_FATAL_ASSERT(false);
}

void WaylandFrameInput::stopKeyRepeat()noexcept{
    m_repeatPending = false;
    m_repeatKeycode = 0;
    m_repeatKey = Key::Unknown;
    m_repeatScancode = 0;
}

i32 WaylandFrameInput::pollTimeoutForKeyRepeat(const i32 timeoutMs)const{
    using namespace __hidden_frame_linux_wayland_input;

    if(!m_repeatPending || timeoutMs <= 0)
        return timeoutMs;

    const f64 secondsUntilRepeat = DurationInSeconds<f64>(m_nextRepeatTime, TimerNow());
    const i32 repeatTimeoutMs = secondsUntilRepeat <= 0.0 ? 0 : static_cast<i32>(VectorDoubleGetX(VectorDoubleCeiling(VectorDoubleScale(VectorDoubleReplicate(secondsUntilRepeat), static_cast<f64>(s_MillisecondsPerSecond)))));
    return timeoutMs < repeatTimeoutMs ? timeoutMs : repeatTimeoutMs;
}

void WaylandFrameInput::processKeyRepeat(){
    using namespace __hidden_frame_linux_wayland_input;

    if(!m_repeatPending || !m_xkbState)
        return;

    const Timer now = TimerNow();
    if(now < m_nextRepeatTime)
        return;

    const i32 candidateStepMs = m_repeatRate > 0 ? (s_MillisecondsPerSecond / m_repeatRate) : 0;
    const i32 stepMs = m_repeatRate > 0 ? (candidateStepMs > 0 ? candidateStepMs : s_MinKeyRepeatStepMs) : 0;
    do{
        const i32 mods = translateModifiers();
        m_frame.input().keyboardUpdate(m_repeatKey, m_repeatScancode, InputAction::Repeat, mods);
        dispatchTextInput(m_repeatKeycode, m_repeatScancode, mods);

        if(stepMs <= 0){
            stopKeyRepeat();
            break;
        }

        m_nextRepeatTime = TimerAddMS(m_nextRepeatTime, stepMs);
    } while(m_repeatPending && m_nextRepeatTime <= now);
}

void WaylandFrameInput::destroy()noexcept{
    destroyKeyboard();
    destroyPointer();

    if(m_seat){
        wl_seat_destroy(m_seat);
        m_seat = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


i32 WaylandFrameInput::translateModifiers()const noexcept{
    if(!m_xkbState)
        return 0;

    i32 mods = 0;

    if(xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Shift;
    if(xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Control;
    if(xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Alt;
    if(xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Super;
    if(xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0)
        mods |= InputModifier::CapsLock;
    if(xkb_state_mod_name_is_active(m_xkbState, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED) > 0)
        mods |= InputModifier::NumLock;

    return mods;
}

void WaylandFrameInput::dispatchTextInput(const u32 keycode, const i32 scancode, const i32 mods){
    using namespace __hidden_frame_linux_wayland_input;

    InputDispatcher& input = m_frame.input();
    if(input.keyboardTextBlocked(scancode) || !m_xkbState)
        return;

    const u32 unicode = xkb_state_key_get_utf32(m_xkbState, keycode);
    if(unicode < s_TextInputFirstPrintableCodePoint || unicode == s_TextInputDeleteCodePoint)
        return;

    if(ITextInputService* const textInput = m_frame.tryTextInput()){
        if(DispatchWaylandDirectTextInput(*textInput, unicode))
            return;
    }
    input.keyboardCharInput(unicode, mods);
}

void WaylandFrameInput::dispatchScroll(){
    using namespace __hidden_frame_linux_wayland_input;

    if(!m_scrollPending)
        return;

    const SIMDVectorDouble scroll = VectorDoubleXorInt(VectorDoubleSet(m_scrollDiscreteX != 0 ? static_cast<f64>(m_scrollDiscreteX) : m_scrollX, m_scrollDiscreteY != 0 ? static_cast<f64>(m_scrollDiscreteY) : m_scrollY, 0.0, 0.0), VectorDoubleSetInt(0u, s_F64SignBitMask, 0u, 0u));
    const SIMDVectorDouble units = VectorDoubleSet(m_scrollDiscreteX != 0 ? 1.0 : s_WaylandScrollAxisUnit, m_scrollDiscreteY != 0 ? 1.0 : s_WaylandScrollAxisUnit, 1.0, 1.0);
    const SIMDVectorDouble offsets = VectorDoubleDivide(scroll, units);
    const f64 xoffset = VectorDoubleGetX(offsets);
    const f64 yoffset = VectorDoubleGetY(offsets);

    if(xoffset != 0.0 || yoffset != 0.0)
        m_frame.input().mouseScrollUpdate(xoffset, yoffset);

    m_scrollX = 0.0;
    m_scrollY = 0.0;
    m_scrollDiscreteX = 0;
    m_scrollDiscreteY = 0;
    m_scrollPending = false;
}

void WaylandFrameInput::destroyKeyboard()noexcept{
    if(m_keyboard){
        wl_keyboard_destroy(m_keyboard);
        m_keyboard = nullptr;
    }

    if(m_xkbState){
        xkb_state_unref(m_xkbState);
        m_xkbState = nullptr;
    }
    if(m_xkbKeymap){
        xkb_keymap_unref(m_xkbKeymap);
        m_xkbKeymap = nullptr;
    }

    stopKeyRepeat();
}

void WaylandFrameInput::destroyPointer()noexcept{
    if(m_pointer){
        wl_pointer_destroy(m_pointer);
        m_pointer = nullptr;
    }

    m_scrollX = 0.0;
    m_scrollY = 0.0;
    m_scrollDiscreteX = 0;
    m_scrollDiscreteY = 0;
    m_scrollPending = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

