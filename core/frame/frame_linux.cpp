// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_linux_platform.h"

#include <logger/client/logger.h>

#include <cstdlib>
#include <cstring>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const char* BackendName(Common::LinuxFrameBackend backend){
    switch(backend){
    case Common::LinuxFrameBackend::X11: return "X11";
    case Common::LinuxFrameBackend::Wayland: return "Wayland";
    case Common::LinuxFrameBackend::None:
    default:
        return "None";
    }
}

static bool HasEnvValue(const char* name){
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

static bool EnvEquals(const char* name, const char* value){
    const char* current = std::getenv(name);
    return current && std::strcmp(current, value) == 0;
}

static usize BuildBackendOrder(Common::LinuxFrameBackend (&outOrder)[2]){
    usize count = 0;

    const char* requestedBackend = std::getenv("NWB_LINUX_BACKEND");
    if(requestedBackend){
        if(std::strcmp(requestedBackend, "x11") == 0){
            outOrder[count++] = Common::LinuxFrameBackend::X11;
#if defined(NWB_WITH_WAYLAND)
            outOrder[count++] = Common::LinuxFrameBackend::Wayland;
#endif
            return count;
        }

#if defined(NWB_WITH_WAYLAND)
        if(std::strcmp(requestedBackend, "wayland") == 0){
            outOrder[count++] = Common::LinuxFrameBackend::Wayland;
            outOrder[count++] = Common::LinuxFrameBackend::X11;
            return count;
        }
#endif

        NWB_LOGGER_WARNING(NWB_TEXT("Frame: Ignoring unsupported NWB_LINUX_BACKEND='{}'."), StringConvert(requestedBackend));
    }

#if defined(NWB_WITH_WAYLAND)
    const bool preferWayland =
        EnvEquals("XDG_SESSION_TYPE", "wayland")
        || HasEnvValue("WAYLAND_DISPLAY")
        ;
    if(preferWayland){
        outOrder[count++] = Common::LinuxFrameBackend::Wayland;
        outOrder[count++] = Common::LinuxFrameBackend::X11;
        return count;
    }
#endif

    outOrder[count++] = Common::LinuxFrameBackend::X11;
#if defined(NWB_WITH_WAYLAND)
    outOrder[count++] = Common::LinuxFrameBackend::Wayland;
#endif
    return count;
}

static bool TryInitBackend(Frame& frame, Common::LinuxFrameBackend backend){
    switch(backend){
    case Common::LinuxFrameBackend::X11:
        return InitX11Frame(frame);
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Wayland:
        return InitWaylandFrame(frame);
#endif
    case Common::LinuxFrameBackend::None:
    default:
        return false;
    }
}

static bool ShowBackendFrame(Frame& frame, Common::LinuxFrameBackend backend){
    switch(backend){
    case Common::LinuxFrameBackend::X11:
        return ShowX11Frame(frame);
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Wayland:
        return ShowWaylandFrame(frame);
#endif
    case Common::LinuxFrameBackend::None:
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Frame: No Linux window backend has been initialized."));
        return false;
    }
}

static bool RunBackendFrame(Frame& frame, Common::LinuxFrameBackend backend){
    switch(backend){
    case Common::LinuxFrameBackend::X11:
        return RunX11Frame(frame);
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Wayland:
        return RunWaylandFrame(frame);
#endif
    case Common::LinuxFrameBackend::None:
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Frame: No Linux window backend is available for the main loop."));
        return false;
    }
}

static void CleanupBackendFrame(Frame& frame, Common::LinuxFrameBackend backend){
    switch(backend){
    case Common::LinuxFrameBackend::X11:
        CleanupX11Frame(frame);
        break;
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Wayland:
        CleanupWaylandFrame(frame);
        break;
#endif
    case Common::LinuxFrameBackend::None:
    default:
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Frame::init(){
    auto& frameData = data<Common::LinuxFrame>();

    Common::LinuxFrameBackend backendOrder[2] = {};
    const usize backendCount = __hidden_frame::BuildBackendOrder(backendOrder);
    for(usize i = 0; i < backendCount; ++i){
        const Common::LinuxFrameBackend backend = backendOrder[i];
        if(__hidden_frame::TryInitBackend(*this, backend)){
            frameData.backend() = backend;
            NWB_LOGGER_INFO(NWB_TEXT("Frame: Using Linux {} backend."), StringConvert(__hidden_frame::BackendName(backend)));

            if(!startup())
                return false;

            return true;
        }

        NWB_LOGGER_WARNING(NWB_TEXT("Frame: Failed to initialize Linux {} backend."), StringConvert(__hidden_frame::BackendName(backend)));
    }

    NWB_LOGGER_FATAL(NWB_TEXT("Frame: Failed to initialize any Linux window backend."));
    return false;
}
bool Frame::showFrame(){
    return __hidden_frame::ShowBackendFrame(*this, data<Common::LinuxFrame>().backend());
}
bool Frame::mainLoop(){
    return __hidden_frame::RunBackendFrame(*this, data<Common::LinuxFrame>().backend());
}

void Frame::setupPlatform(void* inst){
    (void)inst;

    auto& frameData = data<Common::LinuxFrame>();
    frameData.isActive() = false;
    frameData.backend() = Common::LinuxFrameBackend::None;
    frameData.nativeDisplay() = nullptr;
    frameData.nativeWindowHandle() = 0;
    frameData.nativeState() = nullptr;
    frameData.nativeAuxValue() = 0;
}
void Frame::cleanupPlatform(){
    auto& frameData = data<Common::LinuxFrame>();
    __hidden_frame::CleanupBackendFrame(*this, frameData.backend());

    frameData.isActive() = false;
    frameData.backend() = Common::LinuxFrameBackend::None;
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

