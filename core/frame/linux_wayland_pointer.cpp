// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_wayland_pointer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_LINUX) && defined(NWB_WITH_WAYLAND)

#include <core/common/log.h>
#include <core/os/linux/wayland/clipboard.h>
#include <global/simd/vector_arithmetic.h>
#include <linux/input-event-codes.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_linux_wayland_input{


static constexpr f64 s_WaylandScrollAxisUnit = 120.0;
static constexpr u32 s_WaylandPointerFrameSeatVersion = 5u;


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



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 WaylandPointerInput::acquire(wl_seat& seat){
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

    if(m_pointer)
        return true;
    m_pointer = wl_seat_get_pointer(&seat);
    if(!m_pointer || wl_pointer_add_listener(m_pointer, &s_PointerListener, this) != 0){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Frame Wayland pointer listener registration failed"));
        return false;
    }
    return true;
}


void WaylandPointerInput::OnPointerEnter(void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 serial, [[maybe_unused]] wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy){
    using namespace __hidden_frame_linux_wayland_input;

    auto& pointerInput = *MakeNotNull(static_cast<WaylandPointerInput*>(data));
    const SIMDVectorDouble pixelCoordinates = VectorDoubleMultiply(VectorDoubleSet(wl_fixed_to_double(sx), wl_fixed_to_double(sy), 0.0, 0.0), VectorDoubleReplicate(static_cast<f64>(Max(pointerInput.m_bufferScale, 1))));
    pointerInput.m_frame.input().mousePosUpdate(VectorDoubleGetX(pixelCoordinates), VectorDoubleGetY(pixelCoordinates));
}

void WaylandPointerInput::OnPointerLeave(void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 serial, [[maybe_unused]] wl_surface* surface){
    auto& pointerInput = *MakeNotNull(static_cast<WaylandPointerInput*>(data));
    pointerInput.m_frame.input().pointerLeave();
    // The compositor retains pointer focus during the implicit grab; leave terminates any remaining grab.
    pointerInput.m_frame.input().pointerCaptureLost();
}

void WaylandPointerInput::OnPointerMotion(void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 time, wl_fixed_t sx, wl_fixed_t sy){
    using namespace __hidden_frame_linux_wayland_input;

    auto& pointerInput = *MakeNotNull(static_cast<WaylandPointerInput*>(data));
    const SIMDVectorDouble pixelCoordinates = VectorDoubleMultiply(VectorDoubleSet(wl_fixed_to_double(sx), wl_fixed_to_double(sy), 0.0, 0.0), VectorDoubleReplicate(static_cast<f64>(Max(pointerInput.m_bufferScale, 1))));
    pointerInput.m_frame.input().mousePosUpdate(VectorDoubleGetX(pixelCoordinates), VectorDoubleGetY(pixelCoordinates));
}

void WaylandPointerInput::OnPointerButton(void* data, [[maybe_unused]] wl_pointer* pointer, u32 serial, [[maybe_unused]] u32 time, u32 button, u32 state){
    using namespace __hidden_frame_linux_wayland_input;

    auto& pointerInput = *MakeNotNull(static_cast<WaylandPointerInput*>(data));
    pointerInput.m_inputSerial = serial;
    if(IClipboardService* const clipboard = pointerInput.m_frame.tryClipboard())
        ObserveWaylandClipboardInputSerial(*clipboard, serial);
    const i32 translatedButton = TranslatePointerButton(button);
    if(translatedButton != -1){
        pointerInput.m_frame.input().mouseButtonUpdate(translatedButton, state == WL_POINTER_BUTTON_STATE_PRESSED ? InputAction::Press : InputAction::Release, GetWaylandKeyboardModifiers(pointerInput.m_xkbState));
    }
}

void WaylandPointerInput::OnPointerAxis(void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 time, u32 axis, wl_fixed_t value){
    using namespace __hidden_frame_linux_wayland_input;

    auto& pointerInput = *MakeNotNull(static_cast<WaylandPointerInput*>(data));
    pointerInput.m_scrollPending = true;

    const f64 delta = wl_fixed_to_double(value);
    if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        pointerInput.m_scrollX += delta;
    else if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        pointerInput.m_scrollY += delta;

    if(pointerInput.m_seatVersion < s_WaylandPointerFrameSeatVersion)
        pointerInput.dispatchScroll();
}

void WaylandPointerInput::OnPointerFrame(void* data, [[maybe_unused]] wl_pointer* pointer){
    MakeNotNull(static_cast<WaylandPointerInput*>(data))->dispatchScroll();
}

void WaylandPointerInput::OnPointerAxisSource([[maybe_unused]] void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 axisSource)noexcept{}

void WaylandPointerInput::OnPointerAxisStop([[maybe_unused]] void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 time, [[maybe_unused]] u32 axis)noexcept{}

void WaylandPointerInput::OnPointerAxisDiscrete(void* data, [[maybe_unused]] wl_pointer* pointer, u32 axis, i32 discrete)noexcept{
    auto& pointerInput = *MakeNotNull(static_cast<WaylandPointerInput*>(data));
    pointerInput.m_scrollPending = true;

    if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        pointerInput.m_scrollDiscreteX += discrete;
    else if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        pointerInput.m_scrollDiscreteY += discrete;
}

#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
void WaylandPointerInput::OnPointerAxisValue120([[maybe_unused]] void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 axis, [[maybe_unused]] i32 value120)noexcept{}
#endif

#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
void WaylandPointerInput::OnPointerAxisRelativeDirection([[maybe_unused]] void* data, [[maybe_unused]] wl_pointer* pointer, [[maybe_unused]] u32 axis, [[maybe_unused]] u32 direction)noexcept{}
#endif


void WaylandPointerInput::dispatchScroll(){
    using namespace __hidden_frame_linux_wayland_input;

    if(!m_scrollPending)
        return;

    const SIMDVectorDouble scroll = VectorDoubleXorInt(
        VectorDoubleSet(m_scrollDiscreteX != 0 ? static_cast<f64>(m_scrollDiscreteX) : m_scrollX, m_scrollDiscreteY != 0 ? static_cast<f64>(m_scrollDiscreteY) : m_scrollY, 0.0, 0.0),
        VectorDoubleSetInt(0u, s_F64SignBitMask, 0u, 0u)
    );
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


void WaylandPointerInput::destroy()noexcept{
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

