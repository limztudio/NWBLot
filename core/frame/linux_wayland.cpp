// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_platform.h"
#include "linux_wayland_input.h"
#include "frame_detail.h"

#include <core/common/log.h>
#include <core/os/linux/wayland/clipboard.h>
#include <core/os/linux/wayland/text_input.h>

#include <global/scope_exit.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && defined(NWB_WITH_WAYLAND)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <poll.h>
#pragma push_macro("interface")
#undef interface
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#pragma pop_macro("interface")
#include <cerrno>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr i32 s_InactivePollTimeoutMs = 10;
static constexpr u32 s_WaylandCompositorBindVersion = 4u;
static constexpr u32 s_WaylandOutputBindVersion = 2u;
static constexpr u32 s_WaylandWmBaseBindVersion = 1u;
static constexpr u32 s_InitialConfigureRoundtripLimit = 2u;
static constexpr TStringView s_PosixNativeDomain = NWB_TEXT("POSIX");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct WaylandContext{
    Frame& frame;

    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_output* output = nullptr;
    wl_surface* surface = nullptr;
    xdg_wm_base* wmBase = nullptr;
    xdg_surface* xdgSurface = nullptr;
    xdg_toplevel* toplevel = nullptr;

    WaylandFrameInput input;

    i32 bufferScale = 1;

    bool configured = false;
    bool visible = false;
    bool shouldClose = false;
    bool initialSizeConstrained = false;


public:
    explicit WaylandContext(Frame& owner)noexcept
        : frame(owner)
        , input(owner)
    {}
};


static wl_display* GetWaylandDisplay(const Common::LinuxFrame& frameData)noexcept{
    return reinterpret_cast<wl_display*>(frameData.nativeDisplay());
}

static void SetWaylandDisplay(Common::LinuxFrame& frameData, wl_display* display)noexcept{
    frameData.nativeDisplay() = display;
}

static void SetWaylandSurface(Common::LinuxFrame& frameData, wl_surface* surface)noexcept{
    frameData.nativeWindowHandle() = static_cast<u64>(reinterpret_cast<usize>(surface));
}

static WaylandContext* GetWaylandContext(const Common::LinuxFrame& frameData)noexcept{
    return static_cast<WaylandContext*>(frameData.nativeState());
}

static void SetWaylandContext(Common::LinuxFrame& frameData, WaylandContext* context)noexcept{
    frameData.nativeState() = context;
}

static void ApplyBufferScale(WaylandContext& context, i32 value){
    context.bufferScale = ClampBufferScale(value);
    if(context.surface)
        wl_surface_set_buffer_scale(context.surface, context.bufferScale);
    context.input.observeSurface(context.surface, context.bufferScale);
    if(ITextInputService* const textInput = context.frame.tryTextInput())
        SetWaylandTextInputBufferScale(*textInput, context.bufferScale);
}

static u16 PixelDimensionForLogical(i32 logicalDimension, i32 bufferScale)noexcept{
    if(logicalDimension <= 0)
        return 0;

    const u64 pixels = static_cast<u64>(logicalDimension) * static_cast<u64>(ClampBufferScale(bufferScale));
    if(pixels > static_cast<u64>(s_MaxU16))
        return static_cast<u16>(s_MaxU16);

    return static_cast<u16>(pixels);
}

static void DestroyWaylandContext(Common::LinuxFrame& frameData)noexcept{
    WaylandContext* context = GetWaylandContext(frameData);
    if(!context){
        SetWaylandDisplay(frameData, nullptr);
        SetWaylandSurface(frameData, nullptr);
        frameData.setActive(false);
        return;
    }

    GlobalUniquePtr<WaylandContext> contextOwner(context, ArenaDeleter<WaylandContext, Alloc::GlobalArena>(context->frame.projectObjectArena()));

    context->input.destroy();

    if(context->output){
        wl_output_destroy(context->output);
        context->output = nullptr;
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

    SetWaylandContext(frameData, nullptr);
    SetWaylandDisplay(frameData, nullptr);
    SetWaylandSurface(frameData, nullptr);
    frameData.setActive(false);
}

static void OnOutputGeometry(void* data, wl_output* output, i32 x, i32 y, i32 physicalWidth, i32 physicalHeight, i32 subpixel, const char* make, const char* model, i32 transform)noexcept{
    static_cast<void>(data);
    static_cast<void>(output);
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(physicalWidth);
    static_cast<void>(physicalHeight);
    static_cast<void>(subpixel);
    static_cast<void>(make);
    static_cast<void>(model);
    static_cast<void>(transform);
}

static void OnOutputMode(void* data, wl_output* output, u32 flags, i32 width, i32 height, i32 refresh)noexcept{
    static_cast<void>(data);
    static_cast<void>(output);
    static_cast<void>(flags);
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(refresh);
}

static void OnOutputDone(void* data, wl_output* output)noexcept{
    static_cast<void>(data);
    static_cast<void>(output);
}

static void OnOutputScale(void* data, wl_output* output, i32 factor){
    static_cast<void>(output);

    auto& context = *static_cast<WaylandContext*>(data);
    ApplyBufferScale(context, factor);
}

// Output is bound at version 2, so newer name/description events cannot be sent.
static const wl_output_listener s_OutputListener = {
    &OnOutputGeometry,
    &OnOutputMode,
    &OnOutputDone,
    &OnOutputScale,
#if defined(WL_OUTPUT_NAME_SINCE_VERSION)
    nullptr,
#endif
#if defined(WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
    nullptr,
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void OnRegistryGlobal(void* data, wl_registry* registry, u32 name, const char* interfaceName, u32 version){
    auto& context = *static_cast<WaylandContext*>(data);

    if(NWB_STRCMP(interfaceName, wl_compositor_interface.name) == 0){
        const u32 bindVersion = version < s_WaylandCompositorBindVersion ? version : s_WaylandCompositorBindVersion;
        context.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, bindVersion));
    }
    else if(NWB_STRCMP(interfaceName, wl_output_interface.name) == 0 && !context.output){
        const u32 bindVersion = version < s_WaylandOutputBindVersion ? version : s_WaylandOutputBindVersion;
        context.output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, bindVersion));
        if(!context.output || wl_output_add_listener(context.output, &s_OutputListener, &context) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland output listener registration failed"));
            return;
        }
    }
    else if(NWB_STRCMP(interfaceName, wl_seat_interface.name) == 0)
        context.input.bindSeat(*registry, name, version);
    else if(NWB_STRCMP(interfaceName, xdg_wm_base_interface.name) == 0){
        const u32 bindVersion = version < s_WaylandWmBaseBindVersion ? version : s_WaylandWmBaseBindVersion;
        context.wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, bindVersion));
    }
}

static void OnRegistryGlobalRemove(void* data, wl_registry* registry, u32 name){
    static_cast<void>(registry);
    static_cast<WaylandContext*>(data)->input.removeSeat(name);
}

static void OnWmBasePing(void* data, xdg_wm_base* wmBase, u32 serial){
    static_cast<void>(data);
    xdg_wm_base_pong(wmBase, serial);
}

static void OnXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, u32 serial){
    auto& context = *static_cast<WaylandContext*>(data);
    xdg_surface_ack_configure(xdgSurface, serial);

    context.configured = true;
    context.visible = !context.shouldClose;

    if(context.initialSizeConstrained && context.toplevel){
        xdg_toplevel_set_min_size(context.toplevel, 0, 0);
        xdg_toplevel_set_max_size(context.toplevel, 0, 0);
        context.initialSizeConstrained = false;
    }

    if(context.surface)
        wl_surface_commit(context.surface);
}

static void OnToplevelConfigure(void* data, xdg_toplevel* toplevel, i32 width, i32 height, wl_array* states)noexcept{
    static_cast<void>(toplevel);

    auto& context = *static_cast<WaylandContext*>(data);
    auto& frameData = context.frame.data<Common::LinuxFrame>();

    if(width > 0)
        frameData.width() = PixelDimensionForLogical(width, context.bufferScale);
    if(height > 0)
        frameData.height() = PixelDimensionForLogical(height, context.bufferScale);

    bool activated = false;
    if(states && states->data && states->size >= sizeof(u32)){
        const auto* state = static_cast<const u32*>(states->data);
        const usize stateCount = states->size / sizeof(u32);
        for(usize i = 0u; i < stateCount; ++i){
            if(state[i] == XDG_TOPLEVEL_STATE_ACTIVATED){
                activated = true;
                break;
            }
        }
    }

    frameData.setActive(activated);
}

static void OnToplevelClose(void* data, xdg_toplevel* toplevel){
    static_cast<void>(toplevel);

    auto& context = *static_cast<WaylandContext*>(data);
    context.frame.input().pointerCaptureLost();
    context.frame.input().pointerLeave();
    if(ITextInputService* const textInput = context.frame.tryTextInput()){
        if(!SetWaylandTextInputKeyboardFocus(*textInput, false))
            NWB_FATAL_ASSERT(false);
    }
    context.shouldClose = true;
    context.visible = false;
    context.input.stopKeyRepeat();
}

#if defined(XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION)
static void OnToplevelConfigureBounds(void* data, xdg_toplevel* toplevel, i32 width, i32 height)noexcept{
    static_cast<void>(data);
    static_cast<void>(toplevel);
    static_cast<void>(width);
    static_cast<void>(height);
}
#endif

#if defined(XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
static void OnToplevelWmCapabilities(void* data, xdg_toplevel* toplevel, wl_array* capabilities)noexcept{
    static_cast<void>(data);
    static_cast<void>(toplevel);
    static_cast<void>(capabilities);
}
#endif

static const wl_registry_listener s_RegistryListener = { &OnRegistryGlobal, &OnRegistryGlobalRemove, };

static const xdg_wm_base_listener s_WmBaseListener = { &OnWmBasePing, };

static const xdg_surface_listener s_XdgSurfaceListener = { &OnXdgSurfaceConfigure, };

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct WaylandEventFailure{
    TStringView operation{};
    TStringView nativeDomain{};
    i32 nativeCode = 0;
};

[[nodiscard]] static Expected<void, i32> FlushDisplay(wl_display& display)noexcept{
    i32 result;
    i32 nativeError = 0;
    do{
        result = wl_display_flush(&display);
        if(result < 0)
            nativeError = errno;
    }while(result < 0 && nativeError == EINTR);
    if(result < 0 && nativeError != EAGAIN && nativeError != EWOULDBLOCK)
        return MakeUnexpected(nativeError);
    return {};
}

[[nodiscard]] static Expected<void, i32> RoundtripDisplay(wl_display& display){
    if(wl_display_roundtrip(&display) == -1)
        return MakeUnexpected(errno);
    return {};
}

[[nodiscard]] static Expected<void, WaylandEventFailure> PumpEvents(WaylandContext& context, const i32 timeoutMs){
    if(wl_display_dispatch_pending(context.display) == -1)
        return MakeUnexpected(WaylandEventFailure{ .operation = NWB_TEXT("dispatch pending"), .nativeDomain = s_PosixNativeDomain, .nativeCode = errno, });

    while(wl_display_prepare_read(context.display) != 0){
        if(wl_display_dispatch_pending(context.display) == -1)
            return MakeUnexpected(WaylandEventFailure{ .operation = NWB_TEXT("dispatch pending during prepare_read"), .nativeDomain = s_PosixNativeDomain, .nativeCode = errno, });
    }

    const auto flushed = FlushDisplay(*context.display);
    if(!flushed){
        wl_display_cancel_read(context.display);
        return MakeUnexpected(WaylandEventFailure{ .operation = NWB_TEXT("display flush"), .nativeDomain = s_PosixNativeDomain, .nativeCode = flushed.error(), });
    }

    pollfd fd;
    fd.fd = wl_display_get_fd(context.display);
    fd.events = POLLIN;

    const i32 pollResult = poll(&fd, 1, timeoutMs);
    if(pollResult < 0){
        const i32 nativeError = errno;
        wl_display_cancel_read(context.display);
        if(nativeError == EINTR)
            return {};
        return MakeUnexpected(WaylandEventFailure{ .operation = NWB_TEXT("poll"), .nativeDomain = s_PosixNativeDomain, .nativeCode = nativeError, });
    }

    if(pollResult == 0){
        wl_display_cancel_read(context.display);
        return {};
    }

    if((fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0){
        wl_display_cancel_read(context.display);
        return MakeUnexpected(WaylandEventFailure{ .operation = NWB_TEXT("display connection"), .nativeDomain = NWB_TEXT("poll.revents"), .nativeCode = fd.revents, });
    }

    if((fd.revents & POLLIN) != 0){
        if(wl_display_read_events(context.display) == -1)
            return MakeUnexpected(WaylandEventFailure{ .operation = NWB_TEXT("read events"), .nativeDomain = s_PosixNativeDomain, .nativeCode = errno, });
    }
    else
        wl_display_cancel_read(context.display);

    if(wl_display_dispatch_pending(context.display) == -1)
        return MakeUnexpected(WaylandEventFailure{ .operation = NWB_TEXT("dispatch pending after poll"), .nativeDomain = s_PosixNativeDomain, .nativeCode = errno, });
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InitWaylandFrame(Frame& frame){
    const TStringView windowTitle = frame.windowTitleOrDefault();

    auto& frameData = frame.data<Common::LinuxFrame>();

    ScopeExit cleanupOnFailure([&frame]()noexcept{ CleanupWaylandFrame(frame); });
    auto contextOwner = MakeGlobalUnique<WaylandContext>(frame.projectObjectArena(), frame);
    if(!contextOwner){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland context allocation failed"));
        return false;
    }
    auto& context = *contextOwner;
    SetWaylandContext(frameData, &context);
    contextOwner.release();

    context.display = wl_display_connect(nullptr);
    if(!context.display){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland display connection failed"));
        return false;
    }
    SetWaylandDisplay(frameData, context.display);

    if(!context.input.createKeymapContext()){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland xkb context creation failed"));
        return false;
    }

    context.registry = wl_display_get_registry(context.display);
    if(!context.registry){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland registry acquisition failed"));
        return false;
    }
    if(wl_registry_add_listener(context.registry, &s_RegistryListener, &context) != 0){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland registry listener registration failed"));
        return false;
    }

    if(const auto roundtrip = RoundtripDisplay(*context.display); !roundtrip){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland registry roundtrip failed; native_domain=POSIX native_code={}"), roundtrip.error());
        return false;
    }

    if(!context.compositor || !context.wmBase){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland compositor/xdg-shell globals are unavailable"));
        return false;
    }

    if(xdg_wm_base_add_listener(context.wmBase, &s_WmBaseListener, &context) != 0){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland xdg-shell listener registration failed"));
        return false;
    }

    context.surface = wl_compositor_create_surface(context.compositor);
    if(!context.surface){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland surface creation failed"));
        return false;
    }
    ApplyBufferScale(context, context.bufferScale);
    SetWaylandSurface(frameData, context.surface);

    context.xdgSurface = xdg_wm_base_get_xdg_surface(context.wmBase, context.surface);
    if(!context.xdgSurface){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland xdg_surface creation failed"));
        return false;
    }
    if(xdg_surface_add_listener(context.xdgSurface, &s_XdgSurfaceListener, &context) != 0){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland xdg_surface listener registration failed"));
        return false;
    }

    context.toplevel = xdg_surface_get_toplevel(context.xdgSurface);
    if(!context.toplevel){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland toplevel creation failed"));
        return false;
    }
    if(xdg_toplevel_add_listener(context.toplevel, &s_ToplevelListener, &context) != 0){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland toplevel listener registration failed"));
        return false;
    }
    xdg_toplevel_set_title(context.toplevel, windowTitle.data());
    xdg_toplevel_set_app_id(context.toplevel, windowTitle.data());
    xdg_toplevel_set_min_size(context.toplevel, LogicalDimensionForPixels(frameData.width(), context.bufferScale), LogicalDimensionForPixels(frameData.height(), context.bufferScale));
    xdg_toplevel_set_max_size(context.toplevel, LogicalDimensionForPixels(frameData.width(), context.bufferScale), LogicalDimensionForPixels(frameData.height(), context.bufferScale));
    context.initialSizeConstrained = true;

    wl_surface_commit(context.surface);
    if(const auto flushed = FlushDisplay(*context.display); !flushed){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland initial display flush failed; native_domain=POSIX native_code={}"), flushed.error());
        return false;
    }

    for(u32 i = 0u; i < s_InitialConfigureRoundtripLimit && !context.configured; ++i){
        if(const auto roundtrip = RoundtripDisplay(*context.display); !roundtrip){
            CleanupWaylandFrame(frame);
            cleanupOnFailure.release();
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland initial configure roundtrip failed; native_domain=POSIX native_code={}"), roundtrip.error());
            return false;
        }
    }

    if(!context.configured){
        CleanupWaylandFrame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland compositor did not send an initial configure"));
        return false;
    }

    context.visible = true;
    cleanupOnFailure.release();
    return true;
}

Expected<GlobalUniquePtr<IClipboardService>> CreateWaylandFrameClipboard(Frame& frame){
    WaylandContext* const context = GetWaylandContext(frame.data<Common::LinuxFrame>());
    NWB_FATAL_ASSERT(context && context->display);
    auto service = CreateWaylandClipboardService(frame.projectObjectArena(), *context->display);
    if(service)
        context->input.attachClipboard(**service);
    return service;
}

Expected<GlobalUniquePtr<ITextInputService>> CreateWaylandFrameTextInput(Frame& frame){
    WaylandContext* const context = GetWaylandContext(frame.data<Common::LinuxFrame>());
    NWB_FATAL_ASSERT(context && context->display && context->surface);
    auto service = CreateWaylandTextInputService(frame.projectObjectArena(), *context->display, *context->surface);
    if(service)
        context->input.attachTextInput(**service);
    return service;
}

bool ShowWaylandFrame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();
    if(wl_display* const display = GetWaylandDisplay(frameData)){
        if(const auto flushed = FlushDisplay(*display); !flushed){
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland show display flush failed; native_domain=POSIX native_code={}"), flushed.error());
            return false;
        }
    }
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
        if(frame.quitRequested())
            return true;

        bool windowVisible = context->visible && frameData.width() > 0 && frameData.height() > 0;
        bool windowIsInFocus = frameData.isActive();

        const i32 timeoutMs = context->input.pollTimeoutForKeyRepeat((!windowVisible || !windowIsInFocus) ? s_InactivePollTimeoutMs : 0);
        if(const auto pumped = PumpEvents(*context, timeoutMs); !pumped){
            const WaylandEventFailure& error = pumped.error();
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland {} failed; native_domain={} native_code={}"), error.operation, error.nativeDomain, error.nativeCode);
            return false;
        }

        if(context->shouldClose)
            return true;

        context->input.processKeyRepeat();

        windowVisible = context->visible && frameData.width() > 0 && frameData.height() > 0;
        windowIsInFocus = frameData.isActive();

        if(const Optional<TStringView> title = frame.syncGraphicsWindowState(frameData.width(), frameData.height(), windowVisible, windowIsInFocus)){
            xdg_toplevel_set_title(context->toplevel, title->data());
            if(const auto flushed = FlushDisplay(*context->display); !flushed){
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Wayland title display flush failed; native_domain=POSIX native_code={}"), flushed.error());
                return false;
            }
        }

        Timer currentTime(TimerNow());
        const f32 delta = DurationInSeconds<f32>(currentTime, lateTime);
        lateTime = currentTime;

        if(!frame.update(delta))
            return false;
        if(frame.quitRequested())
            return true;
    }

    return false;
}

void CleanupWaylandFrame(Frame& frame)noexcept{
    DestroyWaylandContext(frame.data<Common::LinuxFrame>());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

