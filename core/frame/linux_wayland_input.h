// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


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


// Owns the Wayland seat of one frame: pointer and keyboard objects, the xkb keymap state, key repeat and scroll batching.
class WaylandFrameInput{
private:
    static constexpr i32 s_DefaultKeyRepeatRate = 25;
    static constexpr i32 s_DefaultKeyRepeatDelayMs = 600;


private:
    static void OnSeatCapabilities(void* data, wl_seat* seat, u32 capabilities);
    static void OnSeatName(void* data, wl_seat* seat, const char* name)noexcept;
    static void OnPointerEnter(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
    static void OnPointerLeave(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface);
    static void OnPointerMotion(void* data, wl_pointer* pointer, u32 time, wl_fixed_t sx, wl_fixed_t sy);
    static void OnPointerButton(void* data, wl_pointer* pointer, u32 serial, u32 time, u32 button, u32 state);
    static void OnPointerAxis(void* data, wl_pointer* pointer, u32 time, u32 axis, wl_fixed_t value);
    static void OnPointerFrame(void* data, wl_pointer* pointer);
    static void OnPointerAxisSource(void* data, wl_pointer* pointer, u32 axisSource)noexcept;
    static void OnPointerAxisStop(void* data, wl_pointer* pointer, u32 time, u32 axis)noexcept;
    static void OnPointerAxisDiscrete(void* data, wl_pointer* pointer, u32 axis, i32 discrete)noexcept;
#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
    static void OnPointerAxisValue120(void* data, wl_pointer* pointer, u32 axis, i32 value120)noexcept;
#endif
#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
    static void OnPointerAxisRelativeDirection(void* data, wl_pointer* pointer, u32 axis, u32 direction)noexcept;
#endif
    static void OnKeyboardKeymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size);
    static void OnKeyboardEnter(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface, wl_array* keys);
    static void OnKeyboardLeave(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface);
    static void OnKeyboardKey(void* data, wl_keyboard* keyboard, u32 serial, u32 time, u32 key, u32 state);
    static void OnKeyboardModifiers(void* data, wl_keyboard* keyboard, u32 serial, u32 depressed, u32 latched, u32 locked, u32 group)noexcept;
    static void OnKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, i32 rate, i32 delay)noexcept;


public:
    explicit WaylandFrameInput(Frame& frame)noexcept
        : m_frame(frame)
    {}
    ~WaylandFrameInput()noexcept;


public:
    [[nodiscard]] bool createKeymapContext()noexcept;
    // Binds the first advertised seat; later seats are ignored while one is bound.
    void bindSeat(wl_registry& registry, u32 name, u32 version);
    void removeSeat(u32 name);
    // Keyboard focus is only accepted for this surface, and pointer positions are scaled by its buffer scale.
    void observeSurface(wl_surface* surface, i32 bufferScale)noexcept;
    void attachClipboard(IClipboardService& clipboard)const;
    void attachTextInput(ITextInputService& textInput)const;
    void stopKeyRepeat()noexcept;
    // Shortens an idle poll timeout so a pending key repeat still fires on time.
    [[nodiscard]] i32 pollTimeoutForKeyRepeat(i32 timeoutMs)const;
    void processKeyRepeat();
    // Releases the seat objects; this must run before the display disconnects.
    void destroy()noexcept;


private:
    [[nodiscard]] i32 translateModifiers()const noexcept;
    void dispatchTextInput(u32 keycode, i32 scancode, i32 mods);
    void dispatchScroll();
    void destroyKeyboard()noexcept;
    void destroyPointer()noexcept;


private:
    Frame& m_frame;

    wl_seat* m_seat = nullptr;
    wl_pointer* m_pointer = nullptr;
    wl_keyboard* m_keyboard = nullptr;
    wl_surface* m_surface = nullptr;
    xkb_context* m_xkbContext = nullptr;
    xkb_keymap* m_xkbKeymap = nullptr;
    xkb_state* m_xkbState = nullptr;

    Timer m_nextRepeatTime = {};
    f64 m_scrollX = 0.0;
    f64 m_scrollY = 0.0;

    i32 m_bufferScale = 1;
    i32 m_repeatRate = s_DefaultKeyRepeatRate;
    i32 m_repeatDelayMs = s_DefaultKeyRepeatDelayMs;
    i32 m_repeatKey = Key::Unknown;
    i32 m_repeatScancode = 0;
    i32 m_scrollDiscreteX = 0;
    i32 m_scrollDiscreteY = 0;
    u32 m_seatVersion = 0u;
    u32 m_seatGlobalName = 0u;
    u32 m_inputSerial = 0u;
    u32 m_repeatKeycode = 0u;

    bool m_keyboardFocused = false;
    bool m_repeatPending = false;
    bool m_scrollPending = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

