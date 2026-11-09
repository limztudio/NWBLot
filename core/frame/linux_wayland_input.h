// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "linux_wayland_pointer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && defined(NWB_WITH_WAYLAND)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/timer.h>

#pragma push_macro("interface")
#undef interface
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#pragma pop_macro("interface")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns the Wayland seat and keyboard state of one frame.
class WaylandFrameInput{
private:
    static constexpr i32 s_DefaultKeyRepeatRate = 25;
    static constexpr i32 s_DefaultKeyRepeatDelayMs = 600;


private:
    static void OnSeatCapabilities(void* data, wl_seat* seat, u32 capabilities);
    static void OnSeatName(void* data, wl_seat* seat, const char* name)noexcept;
    static void OnKeyboardKeymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size);
    static void OnKeyboardEnter(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface, wl_array* keys);
    static void OnKeyboardLeave(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface);
    static void OnKeyboardKey(void* data, wl_keyboard* keyboard, u32 serial, u32 time, u32 key, u32 state);
    static void OnKeyboardModifiers(void* data, wl_keyboard* keyboard, u32 serial, u32 depressed, u32 latched, u32 locked, u32 group)noexcept;
    static void OnKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, i32 rate, i32 delay)noexcept;


public:
    explicit WaylandFrameInput(Frame& frame)noexcept
        : m_frame(frame)
        , m_pointerInput(frame, m_seatVersion, m_bufferScale, m_inputSerial, m_xkbState)
    {}
    ~WaylandFrameInput()noexcept;


public:
    [[nodiscard]] b8 createKeymapContext()noexcept;
    // Binds the first advertised seat; later seats are ignored while one is bound.
    void bindSeat(wl_registry& registry, u32 name, u32 version);
    void removeSeat(u32 name);
    // Keyboard focus is only accepted for this surface, and pointer positions are scaled by its buffer scale.
    void observeSurface(wl_surface* surface, i32 bufferScale)noexcept;
    void attachClipboard(IClipboardService& clipboard)const;
    void attachTextInput(ITextInputService& textInput)const;
    void stopKeyRepeat()noexcept;
    // Shortens an idle poll timeout so a pending key repeat still fires on time.
    [[nodiscard]] i32 pollTimeoutForKeyRepeat(i32 timeoutMs)const noexcept;
    void processKeyRepeat();
    // Releases the seat objects; this must run before the display disconnects.
    void destroy()noexcept;


private:
    void dispatchTextInput(u32 keycode, i32 scancode, i32 mods);
    void destroyKeyboard()noexcept;


private:
    Frame& m_frame;

    wl_seat* m_seat = nullptr;
    wl_keyboard* m_keyboard = nullptr;
    wl_surface* m_surface = nullptr;
    xkb_context* m_xkbContext = nullptr;
    xkb_keymap* m_xkbKeymap = nullptr;
    xkb_state* m_xkbState = nullptr;

    Timer m_nextRepeatTime = {};

    i32 m_bufferScale = 1;
    i32 m_repeatRate = s_DefaultKeyRepeatRate;
    i32 m_repeatDelayMs = s_DefaultKeyRepeatDelayMs;
    i32 m_repeatKey = Key::Unknown;
    i32 m_repeatScancode = 0;
    u32 m_seatVersion = 0u;
    u32 m_seatGlobalName = 0u;
    u32 m_inputSerial = 0u;
    u32 m_repeatKeycode = 0u;

    b8 m_keyboardFocused = false;
    b8 m_repeatPending = false;

    // Pointer callbacks borrow the seat version, scale, serial and keyboard state above.
    WaylandPointerInput m_pointerInput;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

