// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_platform.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <global/environment.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr char s_X11BackendName[] = "X11";
inline constexpr char s_WaylandBackendName[] = "Wayland";
inline constexpr char s_NoneBackendName[] = "None";
inline constexpr char s_LinuxBackendEnvName[] = "NWB_LINUX_BACKEND";
inline constexpr char s_X11BackendRequest[] = "x11";
inline constexpr char s_WaylandBackendRequest[] = "wayland";
inline constexpr char s_XdgSessionTypeEnvName[] = "XDG_SESSION_TYPE";
inline constexpr char s_WaylandDisplayEnvName[] = "WAYLAND_DISPLAY";


static const char* BackendName(Common::LinuxFrameBackend::Enum backend){
    switch(backend){
    case Common::LinuxFrameBackend::Enum::X11: return s_X11BackendName;
    case Common::LinuxFrameBackend::Enum::Wayland: return s_WaylandBackendName;
    case Common::LinuxFrameBackend::Enum::None:
    default:
        return s_NoneBackendName;
    }
}


inline constexpr usize s_LinuxBackendOrderCapacity = 2u;

static void AppendBackend(Common::LinuxFrameBackend::Enum (&outOrder)[s_LinuxBackendOrderCapacity], usize& count, const Common::LinuxFrameBackend::Enum backend){
    NWB_ASSERT(count < LengthOf(outOrder));
    outOrder[count] = backend;
    ++count;
}

static usize BuildBackendOrder(Common::LinuxFrameBackend::Enum (&outOrder)[s_LinuxBackendOrderCapacity]){
    Alloc::GlobalArena arena(FrameArenaScope::s_LinuxEnvironmentArena);
    usize count = 0;

    AString<Alloc::GlobalArena> requestedBackend(arena);
    if(ReadEnvironmentVariable(s_LinuxBackendEnvName, requestedBackend)){
        if(requestedBackend == s_X11BackendRequest){
            AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::X11);
#if defined(NWB_WITH_WAYLAND)
            AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::Wayland);
#endif
            return count;
        }

#if defined(NWB_WITH_WAYLAND)
        if(requestedBackend == s_WaylandBackendRequest){
            AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::Wayland);
            AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::X11);
            return count;
        }
#endif

        NWB_LOGGER_WARNING(NWB_TEXT("Frame: Ignoring unsupported NWB_LINUX_BACKEND='{}'."), StringConvert(requestedBackend));
    }

#if defined(NWB_WITH_WAYLAND)
    const bool preferWayland = EnvironmentVariableEquals(arena, s_XdgSessionTypeEnvName, AStringView(s_WaylandBackendRequest)) || HasEnvironmentValue(arena, s_WaylandDisplayEnvName);
    if(preferWayland){
        AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::Wayland);
        AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::X11);
        return count;
    }
#endif

    AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::X11);
#if defined(NWB_WITH_WAYLAND)
    AppendBackend(outOrder, count, Common::LinuxFrameBackend::Enum::Wayland);
#endif
    return count;
}

static bool TryInitBackend(Frame& frame, Common::LinuxFrameBackend::Enum backend){
    switch(backend){
    case Common::LinuxFrameBackend::Enum::X11:
        return InitX11Frame(frame);
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Enum::Wayland:
        return InitWaylandFrame(frame);
#endif
    case Common::LinuxFrameBackend::Enum::None:
    default:
        return false;
    }
}

static bool ShowBackendFrame(Frame& frame, Common::LinuxFrameBackend::Enum backend){
    switch(backend){
    case Common::LinuxFrameBackend::Enum::X11:
        return ShowX11Frame(frame);
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Enum::Wayland:
        return ShowWaylandFrame(frame);
#endif
    case Common::LinuxFrameBackend::Enum::None:
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Frame: No Linux window backend has been initialized."));
        return false;
    }
}

static bool RunBackendFrame(Frame& frame, Common::LinuxFrameBackend::Enum backend){
    switch(backend){
    case Common::LinuxFrameBackend::Enum::X11:
        return RunX11Frame(frame);
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Enum::Wayland:
        return RunWaylandFrame(frame);
#endif
    case Common::LinuxFrameBackend::Enum::None:
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Frame: No Linux window backend is available for the main loop."));
        return false;
    }
}

static void CleanupBackendFrame(Frame& frame, Common::LinuxFrameBackend::Enum backend)noexcept{
    switch(backend){
    case Common::LinuxFrameBackend::Enum::X11:
        CleanupX11Frame(frame);
        break;
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Enum::Wayland:
        CleanupWaylandFrame(frame);
        break;
#endif
    case Common::LinuxFrameBackend::Enum::None:
    default:
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Frame::init(){
    auto& frameData = data<Common::LinuxFrame>();

    Common::LinuxFrameBackend::Enum backendOrder[FrameDetail::s_LinuxBackendOrderCapacity] = {};
    const usize backendCount = FrameDetail::BuildBackendOrder(backendOrder);
    for(usize i = 0; i < backendCount; ++i){
        const Common::LinuxFrameBackend::Enum backend = backendOrder[i];
        if(FrameDetail::TryInitBackend(*this, backend)){
            frameData.setBackend(backend);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Frame: Using Linux {} backend."), StringConvert(FrameDetail::BackendName(backend)));

            if(!startup())
                return false;

            return true;
        }

        NWB_LOGGER_WARNING(NWB_TEXT("Frame: Failed to initialize Linux {} backend."), StringConvert(FrameDetail::BackendName(backend)));
    }

    NWB_LOGGER_FATAL(NWB_TEXT("Frame: Failed to initialize any Linux window backend."));
    return false;
}
bool Frame::showFrame(){
    return FrameDetail::ShowBackendFrame(*this, data<Common::LinuxFrame>().backend());
}
bool Frame::mainLoop(){
    return FrameDetail::RunBackendFrame(*this, data<Common::LinuxFrame>().backend());
}

void Frame::setupPlatform(void* inst){
    static_cast<void>(inst);

    auto& frameData = data<Common::LinuxFrame>();
    frameData.setActive(false);
    frameData.setBackend(Common::LinuxFrameBackend::Enum::None);
    frameData.nativeDisplay() = nullptr;
    frameData.nativeWindowHandle() = 0;
    frameData.nativeState() = nullptr;
    frameData.nativeAuxValue() = 0;
}
void Frame::cleanupPlatform()noexcept{
    auto& frameData = data<Common::LinuxFrame>();
    FrameDetail::CleanupBackendFrame(*this, frameData.backend());

    frameData.setActive(false);
    frameData.setBackend(Common::LinuxFrameBackend::Enum::None);
    frameData.nativeDisplay() = nullptr;
    frameData.nativeWindowHandle() = 0;
    frameData.nativeState() = nullptr;
    frameData.nativeAuxValue() = 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

