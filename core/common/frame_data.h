// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/array.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_FrameParamStorageSlotBytes = sizeof(u64);
inline constexpr usize s_FrameParamU32ValuesPerSlot = s_FrameParamStorageSlotBytes / sizeof(u32);
inline constexpr usize s_FrameParamU16ValuesPerSlot = s_FrameParamStorageSlotBytes / sizeof(u16);
inline constexpr usize s_FrameParamU8ValuesPerSlot = s_FrameParamStorageSlotBytes / sizeof(u8);
inline constexpr usize s_FrameParamPointerSlotCount = 4u;


template<usize PointerSlots>
union FrameParamStorage{
    Array<void*, PointerSlots> ptr;
    Array<::u64, PointerSlots> u64;
    Array<::u32, PointerSlots * s_FrameParamU32ValuesPerSlot> u32;
    Array<::u16, PointerSlots * s_FrameParamU16ValuesPerSlot> u16;
    Array<::u8, PointerSlots * s_FrameParamU8ValuesPerSlot> u8;
};

template<usize PointerSlots>
class BasicFrameData{
public:
    using Param = FrameParamStorage<PointerSlots>;


public:
    inline BasicFrameData()noexcept
        : m_data{}
    {}


public:
    inline Param& frameParam()noexcept{ return m_data; }

    inline const Param& frameParam()const noexcept{ return m_data; }


protected:
    Param m_data;
};

using FrameParam = FrameParamStorage<s_FrameParamPointerSlotCount>;
class FrameData : public BasicFrameData<s_FrameParamPointerSlotCount>{
public:
    using BasicFrameData<s_FrameParamPointerSlotCount>::BasicFrameData;


public:
    inline u16& width()noexcept{ return m_data.u16[0]; }

    inline const u16& width()const noexcept{ return m_data.u16[0]; }

    inline u16& height()noexcept{ return m_data.u16[1]; }

    inline const u16& height()const noexcept{ return m_data.u16[1]; }
};
#if defined(NWB_PLATFORM_WINDOWS)
class WinFrame : public FrameData{
private:
    static constexpr usize s_ActiveFlagByteIndex = sizeof(u16) * 2u;
    static constexpr usize s_InstancePointerSlot = 1u;
    static constexpr usize s_WindowPointerSlot = 2u;
    static constexpr usize s_WindowClassSlot = 3u;


public:
    inline b8 isActive()const noexcept{ return m_data.u8[s_ActiveFlagByteIndex] != 0; }

    inline void setActive(b8 value)noexcept{ m_data.u8[s_ActiveFlagByteIndex] = value ? 1u : 0u; }

    inline HINSTANCE instance()const noexcept{ return static_cast<HINSTANCE>(m_data.ptr[s_InstancePointerSlot]); }

    inline void setInstance(HINSTANCE value)noexcept{ m_data.ptr[s_InstancePointerSlot] = value; }

    inline HWND hwnd()const noexcept{ return static_cast<HWND>(m_data.ptr[s_WindowPointerSlot]); }

    inline void setHwnd(HWND value)noexcept{ m_data.ptr[s_WindowPointerSlot] = value; }

    inline ATOM windowClass()const noexcept{ return static_cast<ATOM>(m_data.u64[s_WindowClassSlot]); }

    inline void setWindowClass(ATOM value)noexcept{ m_data.u64[s_WindowClassSlot] = value; }
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
private:
    static constexpr usize s_ActiveFlagByteIndex = sizeof(u16) * 2u;
    static constexpr usize s_BackendByteIndex = s_ActiveFlagByteIndex + 1u;
    static constexpr usize s_NativeDisplayPointerSlot = 1u;
    static constexpr usize s_NativeWindowHandleSlot = 2u;
    static constexpr usize s_NativeStatePointerSlot = 3u;
    static constexpr usize s_NativeAuxValueSlot = 3u;


public:
    inline b8 isActive()const noexcept{ return m_data.u8[s_ActiveFlagByteIndex] != 0; }

    inline void setActive(b8 value)noexcept{ m_data.u8[s_ActiveFlagByteIndex] = value ? 1u : 0u; }

    inline LinuxFrameBackend::Enum backend()const noexcept{ return static_cast<LinuxFrameBackend::Enum>(m_data.u8[s_BackendByteIndex]); }

    inline void setBackend(LinuxFrameBackend::Enum value)noexcept{ m_data.u8[s_BackendByteIndex] = static_cast<u8>(value); }

    inline void*& nativeDisplay()noexcept{ return m_data.ptr[s_NativeDisplayPointerSlot]; }

    inline void* const& nativeDisplay()const noexcept{ return m_data.ptr[s_NativeDisplayPointerSlot]; }

    inline u64& nativeWindowHandle()noexcept{ return m_data.u64[s_NativeWindowHandleSlot]; }

    inline const u64& nativeWindowHandle()const noexcept{ return m_data.u64[s_NativeWindowHandleSlot]; }

    inline void*& nativeState()noexcept{ return m_data.ptr[s_NativeStatePointerSlot]; }

    inline void* const& nativeState()const noexcept{ return m_data.ptr[s_NativeStatePointerSlot]; }

    inline u64& nativeAuxValue()noexcept{ return m_data.u64[s_NativeAuxValueSlot]; }

    inline const u64& nativeAuxValue()const noexcept{ return m_data.u64[s_NativeAuxValueSlot]; }
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

