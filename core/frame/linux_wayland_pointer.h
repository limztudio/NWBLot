// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_LINUX) && defined(NWB_WITH_WAYLAND)

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


[[nodiscard]] i32 GetWaylandKeyboardModifiers(xkb_state* state)noexcept;


class WaylandPointerInput final : NoCopy{
private:
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

public:
    WaylandPointerInput(Frame& frame, const u32& seatVersion, const i32& bufferScale, u32& inputSerial, xkb_state* const& xkbState)noexcept
        : m_frame(frame)
        , m_seatVersion(seatVersion)
        , m_bufferScale(bufferScale)
        , m_inputSerial(inputSerial)
        , m_xkbState(xkbState)
    {}

    [[nodiscard]] b8 acquire(wl_seat& seat);
    [[nodiscard]] b8 hasPointer()const noexcept{ return m_pointer != nullptr; }
    void destroy()noexcept;


private:
    void dispatchScroll();


private:
    Frame& m_frame;
    const u32& m_seatVersion;
    const i32& m_bufferScale;
    u32& m_inputSerial;
    xkb_state* const& m_xkbState;
    wl_pointer* m_pointer = nullptr;
    f64 m_scrollX = 0.0;
    f64 m_scrollY = 0.0;
    i32 m_scrollDiscreteX = 0;
    i32 m_scrollDiscreteY = 0;
    b8 m_scrollPending = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

