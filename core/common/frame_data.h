// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameParam{
    u16 width = 0u;
    u16 height = 0u;
    b8 active = false;
#if defined(NWB_PLATFORM_WINDOWS)
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    ATOM windowClass = 0u;
#elif defined(NWB_PLATFORM_LINUX)
    u8 backend = 0u;
    void* nativeDisplay = nullptr;
    u64 nativeWindowHandle = 0u;
    void* nativeState = nullptr;
    u64 nativeAuxValue = 0u;
#endif
};

class FrameData{
public:
    inline FrameParam& frameParam()noexcept{ return m_data; }

    inline const FrameParam& frameParam()const noexcept{ return m_data; }


public:
    inline u16& width()noexcept{ return m_data.width; }

    inline const u16& width()const noexcept{ return m_data.width; }

    inline u16& height()noexcept{ return m_data.height; }

    inline const u16& height()const noexcept{ return m_data.height; }


protected:
    FrameParam m_data{};
};
#if defined(NWB_PLATFORM_WINDOWS)
class WinFrame : public FrameData{
public:
    inline b8 isActive()const noexcept{ return m_data.active; }

    inline void setActive(b8 value)noexcept{ m_data.active = value; }

    inline HINSTANCE instance()const noexcept{ return m_data.instance; }

    inline void setInstance(HINSTANCE value)noexcept{ m_data.instance = value; }

    inline HWND hwnd()const noexcept{ return m_data.window; }

    inline void setHwnd(HWND value)noexcept{ m_data.window = value; }

    inline ATOM windowClass()const noexcept{ return m_data.windowClass; }

    inline void setWindowClass(ATOM value)noexcept{ m_data.windowClass = value; }
};
#elif defined(NWB_PLATFORM_LINUX)
namespace LinuxFrameBackend{
    static constexpr u8 s_LinuxFrameBackendNoneBase = 0;
    enum Enum : u8{
        None = s_LinuxFrameBackendNoneBase,
        X11,
        Wayland,
    };
};
class LinuxFrame : public FrameData{
public:
    inline b8 isActive()const noexcept{ return m_data.active; }

    inline void setActive(b8 value)noexcept{ m_data.active = value; }

    inline LinuxFrameBackend::Enum backend()const noexcept{ return static_cast<LinuxFrameBackend::Enum>(m_data.backend); }

    inline void setBackend(LinuxFrameBackend::Enum value)noexcept{ m_data.backend = static_cast<u8>(value); }

    inline void*& nativeDisplay()noexcept{ return m_data.nativeDisplay; }

    inline void* const& nativeDisplay()const noexcept{ return m_data.nativeDisplay; }

    inline u64& nativeWindowHandle()noexcept{ return m_data.nativeWindowHandle; }

    inline const u64& nativeWindowHandle()const noexcept{ return m_data.nativeWindowHandle; }

    inline void*& nativeState()noexcept{ return m_data.nativeState; }

    inline void* const& nativeState()const noexcept{ return m_data.nativeState; }

    inline u64& nativeAuxValue()noexcept{ return m_data.nativeAuxValue; }

    inline const u64& nativeAuxValue()const noexcept{ return m_data.nativeAuxValue; }
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

