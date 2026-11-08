// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_wayland_input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && defined(NWB_WITH_WAYLAND)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_detail.h"
#include "input_helpers.h"

#include <core/common/log.h>
#include <core/frame/frame_platform_values.adhd.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_linux_wayland_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_EvdevToXkbKeycodeOffset = FrameDetail::s_FramePlatformEvdevToXkbKeycodeOffset;
static constexpr i32 s_MinKeyRepeatStepMs = FrameDetail::s_FramePlatformMinKeyRepeatStepMs;
static constexpr u32 s_WaylandSeatBindVersion = FrameDetail::s_FramePlatformWaylandSeatBindVersion;
static constexpr u32 s_WaylandSeatReleaseVersion = FrameDetail::s_FramePlatformWaylandSeatReleaseVersion;
static constexpr StringView s_KeymapLifecycleCategory = FrameDetail::s_FramePlatformKeymapLifecycleCategory;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DECLARE_LINUX_KEY_SYMBOLS(WaylandKeySymbols, xkb_keysym_t, XKB_KEY);

static void ReleaseKeymapResources(const i32 fd, void* const map, const u32 size)noexcept{
    const b8 unmapFailed = map && munmap(map, size) != 0;
    const i32 unmapError = unmapFailed ? errno : 0;
    // Linux releases the descriptor before reporting close errors; retrying can close a reused descriptor.
    const b8 closeFailed = close(fd) != 0;
    const i32 closeError = closeFailed ? errno : 0;
    if(unmapFailed){
        const DiagnosticEventText message = MakeDiagnosticEventText("Frame Wayland keymap unmap failed; native_domain=POSIX native_code={}", unmapError);
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_KeymapLifecycleCategory, .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
    if(closeFailed){
        const DiagnosticEventText message = MakeDiagnosticEventText("Frame Wayland keymap descriptor release failed; native_domain=POSIX native_code={}", closeError);
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = s_KeymapLifecycleCategory, .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandFrameInput::OnSeatCapabilities(void* data, wl_seat* seat, u32 capabilities){
    static const wl_keyboard_listener s_KeyboardListener = { &OnKeyboardKeymap, &OnKeyboardEnter, &OnKeyboardLeave, &OnKeyboardKey, &OnKeyboardModifiers, &OnKeyboardRepeatInfo, };

    auto& frameInput = *MakeNotNull(static_cast<WaylandFrameInput*>(data));

    if((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0u){
        if(!frameInput.m_pointerInput.acquire(*seat))
            return;
    }
    else{
        if(frameInput.m_pointerInput.hasPointer()){
            frameInput.m_frame.input().pointerCaptureLost();
            frameInput.m_frame.input().pointerLeave();
        }
        frameInput.m_pointerInput.destroy();
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

void WaylandFrameInput::OnSeatName([[maybe_unused]] void* data, [[maybe_unused]] wl_seat* seat, [[maybe_unused]] const char* name)noexcept{}

void WaylandFrameInput::OnKeyboardKeymap(void* data, [[maybe_unused]] wl_keyboard* keyboard, u32 format, i32 fd, u32 size){
    using namespace __hidden_frame_linux_wayland_input;

    auto& frameInput = *MakeNotNull(static_cast<WaylandFrameInput*>(data));

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

void WaylandFrameInput::OnKeyboardEnter(void* data, [[maybe_unused]] wl_keyboard* keyboard, [[maybe_unused]] u32 serial, wl_surface* surface, [[maybe_unused]] wl_array* keys){
    auto& frameInput = *MakeNotNull(static_cast<WaylandFrameInput*>(data));
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

void WaylandFrameInput::OnKeyboardLeave(void* data, [[maybe_unused]] wl_keyboard* keyboard, [[maybe_unused]] u32 serial, wl_surface* surface){
    auto& frameInput = *MakeNotNull(static_cast<WaylandFrameInput*>(data));
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

void WaylandFrameInput::OnKeyboardKey(void* data, [[maybe_unused]] wl_keyboard* keyboard, u32 serial, [[maybe_unused]] u32 time, u32 key, u32 state){
    using namespace __hidden_frame_linux_wayland_input;

    auto& frameInput = *MakeNotNull(static_cast<WaylandFrameInput*>(data));
    frameInput.m_inputSerial = serial;
    if(IClipboardService* const clipboard = frameInput.m_frame.tryClipboard())
        ObserveWaylandClipboardInputSerial(*clipboard, serial);
    if(!frameInput.m_xkbState)
        return;

    const u32 keycode = key + s_EvdevToXkbKeycodeOffset;
    const xkb_keysym_t keySym = xkb_state_key_get_one_sym(frameInput.m_xkbState, keycode);
    const i32 translatedKey = FrameDetail::TranslateLinuxKeySymbol<WaylandKeySymbols>(keySym);

    if(state == WL_KEYBOARD_KEY_STATE_PRESSED){
        xkb_state_update_key(frameInput.m_xkbState, keycode, XKB_KEY_DOWN);

        const i32 mods = GetWaylandKeyboardModifiers(frameInput.m_xkbState);
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

        const i32 mods = GetWaylandKeyboardModifiers(frameInput.m_xkbState);
        frameInput.m_frame.input().keyboardUpdate(translatedKey, static_cast<i32>(key), InputAction::Release, mods);

        if(frameInput.m_repeatPending && frameInput.m_repeatKeycode == keycode)
            frameInput.stopKeyRepeat();
    }
}

void WaylandFrameInput::OnKeyboardModifiers(void* data, [[maybe_unused]] wl_keyboard* keyboard, [[maybe_unused]] u32 serial, u32 depressed, u32 latched, u32 locked, u32 group)noexcept{
    auto& frameInput = *MakeNotNull(static_cast<WaylandFrameInput*>(data));
    if(frameInput.m_xkbState)
        xkb_state_update_mask(frameInput.m_xkbState, depressed, latched, locked, 0, 0, group);
}

void WaylandFrameInput::OnKeyboardRepeatInfo(void* data, [[maybe_unused]] wl_keyboard* keyboard, i32 rate, i32 delay)noexcept{
    auto& frameInput = *MakeNotNull(static_cast<WaylandFrameInput*>(data));
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


b8 WaylandFrameInput::createKeymapContext()noexcept{
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
    m_pointerInput.destroy();
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

i32 WaylandFrameInput::pollTimeoutForKeyRepeat(const i32 timeoutMs)const noexcept{
    using namespace __hidden_frame_linux_wayland_input;

    if(!m_repeatPending || timeoutMs <= 0)
        return timeoutMs;

    const f64 secondsUntilRepeat = DurationInSeconds<f64>(m_nextRepeatTime, TimerNow());
    const i32 repeatTimeoutMs = secondsUntilRepeat <= 0.0 ? 0 : static_cast<i32>(Ceil(secondsUntilRepeat * static_cast<f64>(s_MillisecondsInSecond)));
    return timeoutMs < repeatTimeoutMs ? timeoutMs : repeatTimeoutMs;
}

void WaylandFrameInput::processKeyRepeat(){
    using namespace __hidden_frame_linux_wayland_input;

    if(!m_repeatPending || !m_xkbState)
        return;

    const Timer now = TimerNow();
    if(now < m_nextRepeatTime)
        return;

    const i32 candidateStepMs = m_repeatRate > 0 ? (static_cast<i32>(s_MillisecondsInSecond) / m_repeatRate) : 0;
    const i32 stepMs = m_repeatRate > 0 ? (candidateStepMs > 0 ? candidateStepMs : s_MinKeyRepeatStepMs) : 0;
    do{
        const i32 mods = GetWaylandKeyboardModifiers(m_xkbState);
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
    m_pointerInput.destroy();

    if(m_seat){
        wl_seat_destroy(m_seat);
        m_seat = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


i32 GetWaylandKeyboardModifiers(xkb_state* const state)noexcept{
    if(!state)
        return 0;

    i32 mods = 0;

    if(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Shift;
    if(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Control;
    if(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Alt;
    if(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Super;
    if(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0)
        mods |= InputModifier::CapsLock;
    if(xkb_state_mod_name_is_active(state, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED) > 0)
        mods |= InputModifier::NumLock;

    return mods;
}

void WaylandFrameInput::dispatchTextInput(const u32 keycode, const i32 scancode, const i32 mods){
    using namespace __hidden_frame_linux_wayland_input;

    InputDispatcher& input = m_frame.input();
    if(input.keyboardTextBlocked(scancode) || !m_xkbState)
        return;

    const u32 unicode = xkb_state_key_get_utf32(m_xkbState, keycode);
    if(unicode < s_UnicodeSpace || unicode == s_UnicodeDelete)
        return;

    if(ITextInputService* const textInput = m_frame.tryTextInput()){
        if(DispatchWaylandDirectTextInput(*textInput, unicode))
            return;
    }
    input.keyboardCharInput(unicode, mods);
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

