// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_linux_platform.h"

#include <logger/client/logger.h>

#include <global/thread.h>

#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include <xdg-shell-client-protocol.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && defined(NWB_WITH_WAYLAND)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct WaylandContext{
    Frame* frame = nullptr;

    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_seat* seat = nullptr;
    wl_surface* surface = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xdg_wm_base* wmBase = nullptr;
    xdg_surface* xdgSurface = nullptr;
    xdg_toplevel* toplevel = nullptr;

    xkb_context* xkbContext = nullptr;
    xkb_keymap* xkbKeymap = nullptr;
    xkb_state* xkbState = nullptr;

    bool configured = false;
    bool visible = false;
    bool shouldClose = false;

    u32 seatVersion = 0;

    i32 repeatRate = 25;
    i32 repeatDelayMs = 600;
    u32 repeatKeycode = 0;
    i32 repeatKey = Key::Unknown;
    i32 repeatScancode = 0;
    bool repeatPending = false;
    Timer nextRepeatTime = {};

    f64 scrollX = 0.0;
    f64 scrollY = 0.0;
    i32 scrollDiscreteX = 0;
    i32 scrollDiscreteY = 0;
    bool scrollPending = false;
};


static wl_display* GetWaylandDisplay(const Common::LinuxFrame& frameData){
    return reinterpret_cast<wl_display*>(frameData.nativeDisplay());
}

static void SetWaylandDisplay(Common::LinuxFrame& frameData, wl_display* display){
    frameData.nativeDisplay() = display;
}

static void SetWaylandSurface(Common::LinuxFrame& frameData, wl_surface* surface){
    frameData.nativeWindowHandle() = static_cast<u64>(reinterpret_cast<usize>(surface));
}

static WaylandContext* GetWaylandContext(const Common::LinuxFrame& frameData){
    return static_cast<WaylandContext*>(frameData.nativeState());
}

static void SetWaylandContext(Common::LinuxFrame& frameData, WaylandContext* context){
    frameData.nativeState() = context;
}

static u16 ClampDimension(i32 value){
    if(value <= 0)
        return 0;
    if(static_cast<u32>(value) > s_MaxU16)
        return static_cast<u16>(s_MaxU16);
    return static_cast<u16>(value);
}

static i32 TranslateModifiers(const WaylandContext& context){
    if(!context.xkbState)
        return 0;

    i32 mods = 0;

    if(xkb_state_mod_name_is_active(context.xkbState, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Shift;
    if(xkb_state_mod_name_is_active(context.xkbState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Control;
    if(xkb_state_mod_name_is_active(context.xkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Alt;
    if(xkb_state_mod_name_is_active(context.xkbState, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= InputModifier::Super;
    if(xkb_state_mod_name_is_active(context.xkbState, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0)
        mods |= InputModifier::CapsLock;
    if(xkb_state_mod_name_is_active(context.xkbState, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED) > 0)
        mods |= InputModifier::NumLock;

    return mods;
}

static i32 TranslatePointerButton(u32 button){
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

static i32 TranslateKey(xkb_keysym_t keySym){
    if(keySym >= XKB_KEY_0 && keySym <= XKB_KEY_9)
        return static_cast<i32>(keySym);

    if(keySym >= XKB_KEY_A && keySym <= XKB_KEY_Z)
        return static_cast<i32>(keySym);

    if(keySym >= XKB_KEY_a && keySym <= XKB_KEY_z)
        return static_cast<i32>(Key::A + (keySym - XKB_KEY_a));

    if(keySym >= XKB_KEY_F1 && keySym <= XKB_KEY_F24)
        return static_cast<i32>(Key::F1 + (keySym - XKB_KEY_F1));

    if(keySym >= XKB_KEY_KP_0 && keySym <= XKB_KEY_KP_9)
        return static_cast<i32>(Key::Keypad0 + (keySym - XKB_KEY_KP_0));

    if(keySym == XKB_KEY_ISO_Left_Tab)
        return Key::Tab;
    if(keySym == XKB_KEY_grave || keySym == XKB_KEY_quoteleft)
        return Key::GraveAccent;
    if(keySym == XKB_KEY_Print || keySym == XKB_KEY_Sys_Req)
        return Key::PrintScreen;
    if(keySym == XKB_KEY_Alt_L || keySym == XKB_KEY_Meta_L)
        return Key::LeftAlt;
    if(keySym == XKB_KEY_Alt_R || keySym == XKB_KEY_Meta_R)
        return Key::RightAlt;
    if(keySym == XKB_KEY_Super_L || keySym == XKB_KEY_Hyper_L)
        return Key::LeftSuper;
    if(keySym == XKB_KEY_Super_R || keySym == XKB_KEY_Hyper_R)
        return Key::RightSuper;

    switch(keySym){
    case XKB_KEY_space: return Key::Space;
    case XKB_KEY_apostrophe: return Key::Apostrophe;
    case XKB_KEY_comma: return Key::Comma;
    case XKB_KEY_minus: return Key::Minus;
    case XKB_KEY_period: return Key::Period;
    case XKB_KEY_slash: return Key::Slash;
    case XKB_KEY_semicolon: return Key::Semicolon;
    case XKB_KEY_equal: return Key::Equal;
    case XKB_KEY_bracketleft: return Key::LeftBracket;
    case XKB_KEY_backslash: return Key::Backslash;
    case XKB_KEY_bracketright: return Key::RightBracket;

    case XKB_KEY_Escape: return Key::Escape;
    case XKB_KEY_Return: return Key::Enter;
    case XKB_KEY_Tab: return Key::Tab;
    case XKB_KEY_BackSpace: return Key::Backspace;
    case XKB_KEY_Insert: return Key::Insert;
    case XKB_KEY_Delete: return Key::Delete;
    case XKB_KEY_Right: return Key::Right;
    case XKB_KEY_Left: return Key::Left;
    case XKB_KEY_Down: return Key::Down;
    case XKB_KEY_Up: return Key::Up;
    case XKB_KEY_Prior: return Key::PageUp;
    case XKB_KEY_Next: return Key::PageDown;
    case XKB_KEY_Home: return Key::Home;
    case XKB_KEY_End: return Key::End;
    case XKB_KEY_Caps_Lock: return Key::CapsLock;
    case XKB_KEY_Scroll_Lock: return Key::ScrollLock;
    case XKB_KEY_Num_Lock: return Key::NumLock;
    case XKB_KEY_Pause: return Key::Pause;
    case XKB_KEY_F25: return Key::F25;

    case XKB_KEY_KP_Insert: return Key::Keypad0;
    case XKB_KEY_KP_End: return Key::Keypad1;
    case XKB_KEY_KP_Down: return Key::Keypad2;
    case XKB_KEY_KP_Next: return Key::Keypad3;
    case XKB_KEY_KP_Left: return Key::Keypad4;
    case XKB_KEY_KP_Begin: return Key::Keypad5;
    case XKB_KEY_KP_Right: return Key::Keypad6;
    case XKB_KEY_KP_Home: return Key::Keypad7;
    case XKB_KEY_KP_Up: return Key::Keypad8;
    case XKB_KEY_KP_Prior: return Key::Keypad9;
    case XKB_KEY_KP_Delete: return Key::KeypadDecimal;
    case XKB_KEY_KP_Decimal: return Key::KeypadDecimal;
    case XKB_KEY_KP_Divide: return Key::KeypadDivide;
    case XKB_KEY_KP_Multiply: return Key::KeypadMultiply;
    case XKB_KEY_KP_Subtract: return Key::KeypadSubtract;
    case XKB_KEY_KP_Add: return Key::KeypadAdd;
    case XKB_KEY_KP_Enter: return Key::KeypadEnter;
    case XKB_KEY_KP_Equal: return Key::KeypadEqual;

    case XKB_KEY_Shift_L: return Key::LeftShift;
    case XKB_KEY_Control_L: return Key::LeftControl;
    case XKB_KEY_Shift_R: return Key::RightShift;
    case XKB_KEY_Control_R: return Key::RightControl;
    case XKB_KEY_Menu: return Key::Menu;
    default:
        return Key::Unknown;
    }
}

static void StopKeyRepeat(WaylandContext& context){
    context.repeatPending = false;
    context.repeatKeycode = 0;
    context.repeatKey = Key::Unknown;
    context.repeatScancode = 0;
}

static void DispatchTextInput(InputDispatcher& input, const WaylandContext& context, u32 keycode, i32 mods){
    if(!context.xkbState)
        return;

    const u32 unicode = xkb_state_key_get_utf32(context.xkbState, keycode);
    if(unicode < 32 || unicode == 127)
        return;

    input.keyboardCharInput(unicode, mods);
}

static void DispatchScroll(WaylandContext& context){
    if(!context.scrollPending || !context.frame)
        return;

    f64 xoffset = context.scrollDiscreteX != 0 ? static_cast<f64>(context.scrollDiscreteX) : (context.scrollX / 120.0);
    f64 yoffset = context.scrollDiscreteY != 0 ? -static_cast<f64>(context.scrollDiscreteY) : (-context.scrollY / 120.0);

    if(xoffset != 0.0 || yoffset != 0.0)
        context.frame->input().mouseScrollUpdate(xoffset, yoffset);

    context.scrollX = 0.0;
    context.scrollY = 0.0;
    context.scrollDiscreteX = 0;
    context.scrollDiscreteY = 0;
    context.scrollPending = false;
}

static void DestroyKeyboard(WaylandContext& context){
    if(context.keyboard){
        wl_keyboard_destroy(context.keyboard);
        context.keyboard = nullptr;
    }

    if(context.xkbState){
        xkb_state_unref(context.xkbState);
        context.xkbState = nullptr;
    }
    if(context.xkbKeymap){
        xkb_keymap_unref(context.xkbKeymap);
        context.xkbKeymap = nullptr;
    }

    StopKeyRepeat(context);
}

static void DestroyPointer(WaylandContext& context){
    if(context.pointer){
        wl_pointer_destroy(context.pointer);
        context.pointer = nullptr;
    }

    context.scrollX = 0.0;
    context.scrollY = 0.0;
    context.scrollDiscreteX = 0;
    context.scrollDiscreteY = 0;
    context.scrollPending = false;
}

static void DestroyWaylandContext(Common::LinuxFrame& frameData){
    WaylandContext* context = GetWaylandContext(frameData);
    if(!context){
        SetWaylandDisplay(frameData, nullptr);
        SetWaylandSurface(frameData, nullptr);
        frameData.isActive() = false;
        return;
    }

    DestroyKeyboard(*context);
    DestroyPointer(*context);

    if(context->seat){
        wl_seat_destroy(context->seat);
        context->seat = nullptr;
    }
    if(context->toplevel){
        xdg_toplevel_destroy(context->toplevel);
        context->toplevel = nullptr;
    }
    if(context->xdgSurface){
        xdg_surface_destroy(context->xdgSurface);
        context->xdgSurface = nullptr;
    }
    if(context->surface){
        wl_surface_destroy(context->surface);
        context->surface = nullptr;
    }
    if(context->wmBase){
        xdg_wm_base_destroy(context->wmBase);
        context->wmBase = nullptr;
    }
    if(context->compositor){
        wl_compositor_destroy(context->compositor);
        context->compositor = nullptr;
    }
    if(context->registry){
        wl_registry_destroy(context->registry);
        context->registry = nullptr;
    }
    if(context->display){
        wl_display_disconnect(context->display);
        context->display = nullptr;
    }
    if(context->xkbContext){
        xkb_context_unref(context->xkbContext);
        context->xkbContext = nullptr;
    }

    delete context;

    SetWaylandContext(frameData, nullptr);
    SetWaylandDisplay(frameData, nullptr);
    SetWaylandSurface(frameData, nullptr);
    frameData.isActive() = false;
}

static void OnRegistryGlobal(void* data, wl_registry* registry, u32 name, const char* interface, u32 version){
    auto& context = *static_cast<WaylandContext*>(data);

    if(std::strcmp(interface, wl_compositor_interface.name) == 0){
        const u32 bindVersion = version < 4 ? version : 4;
        context.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, bindVersion));
    }
    else if(std::strcmp(interface, wl_seat_interface.name) == 0){
        const u32 bindVersion = version < 5 ? version : 5;
        context.seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, bindVersion));
        context.seatVersion = bindVersion;
    }
    else if(std::strcmp(interface, xdg_wm_base_interface.name) == 0){
        const u32 bindVersion = version < 1 ? version : 1;
        context.wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, bindVersion));
    }
}

static void OnRegistryGlobalRemove(void* data, wl_registry* registry, u32 name){
    (void)data;
    (void)registry;
    (void)name;
}

static void OnWmBasePing(void* data, xdg_wm_base* wmBase, u32 serial){
    (void)data;
    xdg_wm_base_pong(wmBase, serial);
}

static void OnXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, u32 serial){
    auto& context = *static_cast<WaylandContext*>(data);
    xdg_surface_ack_configure(xdgSurface, serial);

    context.configured = true;
    context.visible = !context.shouldClose;

    if(context.surface)
        wl_surface_commit(context.surface);
}

static void OnToplevelConfigure(void* data, xdg_toplevel* toplevel, i32 width, i32 height, wl_array* states){
    (void)toplevel;

    auto& context = *static_cast<WaylandContext*>(data);
    auto& frameData = context.frame->data<Common::LinuxFrame>();

    if(width > 0)
        frameData.width() = ClampDimension(width);
    if(height > 0)
        frameData.height() = ClampDimension(height);

    bool activated = false;
    if(states && states->data && states->size >= sizeof(u32)){
        const auto* state = static_cast<const u32*>(states->data);
        const usize stateCount = states->size / sizeof(u32);
        for(usize i = 0; i < stateCount; i++){
            if(state[i] == XDG_TOPLEVEL_STATE_ACTIVATED){
                activated = true;
                break;
            }
        }
    }

    frameData.isActive() = activated;
}

static void OnToplevelClose(void* data, xdg_toplevel* toplevel){
    (void)toplevel;

    auto& context = *static_cast<WaylandContext*>(data);
    context.shouldClose = true;
    context.visible = false;
    StopKeyRepeat(context);
}

#if defined(XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION)
static void OnToplevelConfigureBounds(void* data, xdg_toplevel* toplevel, i32 width, i32 height){
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}
#endif

#if defined(XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
static void OnToplevelWmCapabilities(void* data, xdg_toplevel* toplevel, wl_array* capabilities){
    (void)data;
    (void)toplevel;
    (void)capabilities;
}
#endif

static void OnSeatCapabilities(void* data, wl_seat* seat, u32 capabilities);
static void OnSeatName(void* data, wl_seat* seat, const char* name){
    (void)data;
    (void)seat;
    (void)name;
}

static void OnPointerEnter(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy){
    (void)pointer;
    (void)serial;
    (void)surface;

    auto& context = *static_cast<WaylandContext*>(data);
    context.frame->input().mousePosUpdate(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

static void OnPointerLeave(void* data, wl_pointer* pointer, u32 serial, wl_surface* surface){
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
}

static void OnPointerMotion(void* data, wl_pointer* pointer, u32 time, wl_fixed_t sx, wl_fixed_t sy){
    (void)pointer;
    (void)time;

    auto& context = *static_cast<WaylandContext*>(data);
    context.frame->input().mousePosUpdate(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

static void OnPointerButton(void* data, wl_pointer* pointer, u32 serial, u32 time, u32 button, u32 state){
    (void)pointer;
    (void)serial;
    (void)time;

    auto& context = *static_cast<WaylandContext*>(data);
    const i32 translatedButton = TranslatePointerButton(button);
    if(translatedButton != -1){
        context.frame->input().mouseButtonUpdate(
            translatedButton,
            state == WL_POINTER_BUTTON_STATE_PRESSED ? InputAction::Press : InputAction::Release,
            TranslateModifiers(context)
        );
    }
}

static void OnPointerAxis(void* data, wl_pointer* pointer, u32 time, u32 axis, wl_fixed_t value){
    (void)pointer;
    (void)time;

    auto& context = *static_cast<WaylandContext*>(data);
    context.scrollPending = true;

    const f64 delta = wl_fixed_to_double(value);
    if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        context.scrollX += delta;
    else if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        context.scrollY += delta;

    if(context.seatVersion < 5)
        DispatchScroll(context);
}

static void OnPointerFrame(void* data, wl_pointer* pointer){
    (void)pointer;
    DispatchScroll(*static_cast<WaylandContext*>(data));
}

static void OnPointerAxisSource(void* data, wl_pointer* pointer, u32 axisSource){
    (void)data;
    (void)pointer;
    (void)axisSource;
}

static void OnPointerAxisStop(void* data, wl_pointer* pointer, u32 time, u32 axis){
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void OnPointerAxisDiscrete(void* data, wl_pointer* pointer, u32 axis, i32 discrete){
    (void)pointer;

    auto& context = *static_cast<WaylandContext*>(data);
    context.scrollPending = true;

    if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        context.scrollDiscreteX += discrete;
    else if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        context.scrollDiscreteY += discrete;
}

#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
static void OnPointerAxisValue120(void* data, wl_pointer* pointer, u32 axis, i32 value120){
    (void)data;
    (void)pointer;
    (void)axis;
    (void)value120;
}
#endif

#if defined(WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION)
static void OnPointerAxisRelativeDirection(void* data, wl_pointer* pointer, u32 axis, u32 direction){
    (void)data;
    (void)pointer;
    (void)axis;
    (void)direction;
}
#endif

static void OnKeyboardKeymap(void* data, wl_keyboard* keyboard, u32 format, i32 fd, u32 size){
    (void)keyboard;

    auto& context = *static_cast<WaylandContext*>(data);

    if(format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1){
        close(fd);
        return;
    }

    char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if(map == MAP_FAILED){
        close(fd);
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland keymap mmap failed"));
        return;
    }

    xkb_keymap* keymap = xkb_keymap_new_from_string(context.xkbContext, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);

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

    if(context.xkbState)
        xkb_state_unref(context.xkbState);
    if(context.xkbKeymap)
        xkb_keymap_unref(context.xkbKeymap);

    context.xkbKeymap = keymap;
    context.xkbState = state;
}

static void OnKeyboardEnter(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface, wl_array* keys){
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;

    auto& context = *static_cast<WaylandContext*>(data);
    context.frame->data<Common::LinuxFrame>().isActive() = true;
}

static void OnKeyboardLeave(void* data, wl_keyboard* keyboard, u32 serial, wl_surface* surface){
    (void)keyboard;
    (void)serial;
    (void)surface;

    auto& context = *static_cast<WaylandContext*>(data);
    context.frame->data<Common::LinuxFrame>().isActive() = false;
    StopKeyRepeat(context);
}

static void OnKeyboardKey(void* data, wl_keyboard* keyboard, u32 serial, u32 time, u32 key, u32 state){
    (void)keyboard;
    (void)serial;
    (void)time;

    auto& context = *static_cast<WaylandContext*>(data);
    if(!context.xkbState)
        return;

    const u32 keycode = key + 8u;
    const xkb_keysym_t keySym = xkb_state_key_get_one_sym(context.xkbState, keycode);
    const i32 translatedKey = TranslateKey(keySym);

    if(state == WL_KEYBOARD_KEY_STATE_PRESSED){
        xkb_state_update_key(context.xkbState, keycode, XKB_KEY_DOWN);

        const i32 mods = TranslateModifiers(context);
        context.frame->input().keyboardUpdate(translatedKey, static_cast<i32>(key), InputAction::Press, mods);
        DispatchTextInput(context.frame->input(), context, keycode, mods);

        if(context.repeatRate > 0
            && context.xkbKeymap
            && xkb_keymap_key_repeats(context.xkbKeymap, keycode) > 0){
            context.repeatPending = true;
            context.repeatKeycode = keycode;
            context.repeatKey = translatedKey;
            context.repeatScancode = static_cast<i32>(key);
            context.nextRepeatTime = TimerNow() + std::chrono::milliseconds(context.repeatDelayMs);
        }
    }
    else{
        xkb_state_update_key(context.xkbState, keycode, XKB_KEY_UP);

        const i32 mods = TranslateModifiers(context);
        context.frame->input().keyboardUpdate(translatedKey, static_cast<i32>(key), InputAction::Release, mods);

        if(context.repeatPending && context.repeatKeycode == keycode)
            StopKeyRepeat(context);
    }
}

static void OnKeyboardModifiers(void* data, wl_keyboard* keyboard, u32 serial, u32 depressed, u32 latched, u32 locked, u32 group){
    (void)keyboard;
    (void)serial;

    auto& context = *static_cast<WaylandContext*>(data);
    if(context.xkbState)
        xkb_state_update_mask(context.xkbState, depressed, latched, locked, 0, 0, group);
}

static void OnKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, i32 rate, i32 delay){
    (void)keyboard;

    auto& context = *static_cast<WaylandContext*>(data);
    context.repeatRate = rate;
    context.repeatDelayMs = delay;

    if(rate <= 0)
        StopKeyRepeat(context);
}

static const wl_registry_listener s_RegistryListener = {
    &OnRegistryGlobal,
    &OnRegistryGlobalRemove,
};

static const xdg_wm_base_listener s_WmBaseListener = {
    &OnWmBasePing,
};

static const xdg_surface_listener s_XdgSurfaceListener = {
    &OnXdgSurfaceConfigure,
};

static const xdg_toplevel_listener s_ToplevelListener = {
    &OnToplevelConfigure,
    &OnToplevelClose,
#if defined(XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION)
    &OnToplevelConfigureBounds,
#endif
#if defined(XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
    &OnToplevelWmCapabilities,
#endif
};

static const wl_seat_listener s_SeatListener = {
    &OnSeatCapabilities,
    &OnSeatName,
};

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

static const wl_keyboard_listener s_KeyboardListener = {
    &OnKeyboardKeymap,
    &OnKeyboardEnter,
    &OnKeyboardLeave,
    &OnKeyboardKey,
    &OnKeyboardModifiers,
    &OnKeyboardRepeatInfo,
};

static void OnSeatCapabilities(void* data, wl_seat* seat, u32 capabilities){
    auto& context = *static_cast<WaylandContext*>(data);

    if((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0u){
        if(!context.pointer){
            context.pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(context.pointer, &s_PointerListener, &context);
        }
    }
    else{
        DestroyPointer(context);
    }

    if((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0u){
        if(!context.keyboard){
            context.keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(context.keyboard, &s_KeyboardListener, &context);
        }
    }
    else{
        DestroyKeyboard(context);
        context.frame->data<Common::LinuxFrame>().isActive() = false;
    }
}

static bool RoundtripDisplay(wl_display* display, const tchar* operation){
    if(wl_display_roundtrip(display) == -1){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland {} failed"), operation);
        return false;
    }
    return true;
}

static bool PumpEvents(WaylandContext& context, i32 timeoutMs){
    if(wl_display_dispatch_pending(context.display) == -1){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland dispatch pending failed"));
        return false;
    }

    while(wl_display_prepare_read(context.display) != 0){
        if(wl_display_dispatch_pending(context.display) == -1){
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland dispatch pending during prepare_read failed"));
            return false;
        }
    }

    if(wl_display_flush(context.display) == -1 && errno != EAGAIN){
        wl_display_cancel_read(context.display);
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland display flush failed"));
        return false;
    }

    pollfd fd = {};
    fd.fd = wl_display_get_fd(context.display);
    fd.events = POLLIN;

    const i32 pollResult = poll(&fd, 1, timeoutMs);
    if(pollResult < 0){
        wl_display_cancel_read(context.display);
        if(errno == EINTR)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland poll failed"));
        return false;
    }

    if(pollResult == 0){
        wl_display_cancel_read(context.display);
        return true;
    }

    if((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0){
        wl_display_cancel_read(context.display);
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland display connection closed"));
        return false;
    }

    if((fd.revents & POLLIN) != 0){
        if(wl_display_read_events(context.display) == -1){
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland read events failed"));
            return false;
        }
    }
    else{
        wl_display_cancel_read(context.display);
    }

    if(wl_display_dispatch_pending(context.display) == -1){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland dispatch pending after poll failed"));
        return false;
    }

    return true;
}

static void ProcessKeyRepeat(WaylandContext& context){
    if(!context.repeatPending || !context.frame || !context.xkbState)
        return;

    const Timer now = TimerNow();
    if(now < context.nextRepeatTime)
        return;

    const i32 stepMs = context.repeatRate > 0
        ? ((1000 / context.repeatRate) > 0 ? (1000 / context.repeatRate) : 1)
        : 0
    ;
    do{
        const i32 mods = TranslateModifiers(context);
        context.frame->input().keyboardUpdate(context.repeatKey, context.repeatScancode, InputAction::Repeat, mods);
        DispatchTextInput(context.frame->input(), context, context.repeatKeycode, mods);

        if(stepMs <= 0){
            StopKeyRepeat(context);
            break;
        }

        context.nextRepeatTime += std::chrono::milliseconds(stepMs);
    } while(context.repeatPending && context.nextRepeatTime <= now);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InitWaylandFrame(Frame& frame){
    constexpr const char* AppName = "NWBLoader";

    auto& frameData = frame.data<Common::LinuxFrame>();

    auto* context = new WaylandContext();
    context->frame = &frame;
    SetWaylandContext(frameData, context);

    context->display = wl_display_connect(nullptr);
    if(!context->display){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland display connection failed"));
        CleanupWaylandFrame(frame);
        return false;
    }
    SetWaylandDisplay(frameData, context->display);

    context->xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if(!context->xkbContext){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland xkb context creation failed"));
        CleanupWaylandFrame(frame);
        return false;
    }

    context->registry = wl_display_get_registry(context->display);
    if(!context->registry){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland registry acquisition failed"));
        CleanupWaylandFrame(frame);
        return false;
    }
    wl_registry_add_listener(context->registry, &s_RegistryListener, context);

    if(!RoundtripDisplay(context->display, NWB_TEXT("registry roundtrip"))){
        CleanupWaylandFrame(frame);
        return false;
    }

    if(!context->compositor || !context->wmBase){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland compositor/xdg-shell globals are unavailable"));
        CleanupWaylandFrame(frame);
        return false;
    }

    xdg_wm_base_add_listener(context->wmBase, &s_WmBaseListener, context);

    if(context->seat)
        wl_seat_add_listener(context->seat, &s_SeatListener, context);

    context->surface = wl_compositor_create_surface(context->compositor);
    if(!context->surface){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland surface creation failed"));
        CleanupWaylandFrame(frame);
        return false;
    }
    SetWaylandSurface(frameData, context->surface);

    context->xdgSurface = xdg_wm_base_get_xdg_surface(context->wmBase, context->surface);
    if(!context->xdgSurface){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland xdg_surface creation failed"));
        CleanupWaylandFrame(frame);
        return false;
    }
    xdg_surface_add_listener(context->xdgSurface, &s_XdgSurfaceListener, context);

    context->toplevel = xdg_surface_get_toplevel(context->xdgSurface);
    if(!context->toplevel){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland toplevel creation failed"));
        CleanupWaylandFrame(frame);
        return false;
    }
    xdg_toplevel_add_listener(context->toplevel, &s_ToplevelListener, context);
    xdg_toplevel_set_title(context->toplevel, AppName);
    xdg_toplevel_set_app_id(context->toplevel, AppName);

    wl_surface_commit(context->surface);
    if(wl_display_flush(context->display) == -1 && errno != EAGAIN){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland initial display flush failed"));
        CleanupWaylandFrame(frame);
        return false;
    }

    for(u32 i = 0; i < 2 && !context->configured; ++i){
        if(!RoundtripDisplay(context->display, NWB_TEXT("initial configure roundtrip"))){
            CleanupWaylandFrame(frame);
            return false;
        }
    }

    if(!context->configured){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland compositor did not send an initial configure"));
        CleanupWaylandFrame(frame);
        return false;
    }

    context->visible = true;
    return true;
}

bool ShowWaylandFrame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();
    if(GetWaylandDisplay(frameData))
        wl_display_flush(GetWaylandDisplay(frameData));
    return true;
}

bool RunWaylandFrame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();
    auto* context = GetWaylandContext(frameData);
    if(!context || !context->display){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland backend is not initialized"));
        return false;
    }

    Timer lateTime(TimerNow());

    for(;;){
        bool windowVisible = context->visible && frameData.width() > 0 && frameData.height() > 0;
        bool windowIsInFocus = frameData.isActive();

        i32 timeoutMs = (!windowVisible || !windowIsInFocus) ? 10 : 0;
        if(context->repeatPending && timeoutMs > 0){
            const f64 secondsUntilRepeat = DurationInSeconds<f64>(context->nextRepeatTime, TimerNow());
            const i32 repeatTimeoutMs = secondsUntilRepeat <= 0.0 ? 0 : static_cast<i32>(std::ceil(secondsUntilRepeat * 1000.0));
            timeoutMs = timeoutMs < repeatTimeoutMs ? timeoutMs : repeatTimeoutMs;
        }

        if(!PumpEvents(*context, timeoutMs))
            return false;

        if(context->shouldClose)
            return true;

        ProcessKeyRepeat(*context);

        windowVisible = context->visible && frameData.width() > 0 && frameData.height() > 0;
        windowIsInFocus = frameData.isActive();

        if(const tchar* title = frame.syncGraphicsWindowState(frameData.width(), frameData.height(), windowVisible, windowIsInFocus)){
            xdg_toplevel_set_title(context->toplevel, title);
            wl_display_flush(context->display);
        }

        Timer currentTime(TimerNow());
        const f32 delta = DurationInSeconds<f32>(currentTime, lateTime);
        lateTime = currentTime;

        if(!frame.update(delta))
            break;
    }

    return false;
}

void CleanupWaylandFrame(Frame& frame){
    DestroyWaylandContext(frame.data<Common::LinuxFrame>());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

