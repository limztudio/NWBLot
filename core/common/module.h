// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "arena_names.h"

#include <core/alloc/general.h>

#include <global/frame_data.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CommonDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BaseInitializerable{
public:
    virtual ~BaseInitializerable()noexcept = default;


public:
    virtual bool initialize() = 0;
    virtual void finalize()noexcept = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Initializerable : public CommonDetail::BaseInitializerable{
public:
    Initializerable();
};


class Initializer{
private:
    struct InitializerItem{
        NotNull<CommonDetail::BaseInitializerable*> item;


    public:
        explicit InitializerItem(CommonDetail::BaseInitializerable& value)noexcept
            : item(MakeNotNull(&value))
        {}
    };


public:
    static Initializer& Instance();


private:
    Initializer()
        : m_arena(CommonArenaScope::s_InitializerArena)
        , m_items(m_arena)
        , m_cursor(m_items.before_begin())
    {}


public:
    inline bool initialize(){
        m_initializedItemCount = 0u;
        usize initializedItemCount = 0u;
        ScopeExit rollback([&]()noexcept{ finalizeInitialized(initializedItemCount); });
        for(auto& cur : m_items){
            if(!cur.item->initialize())
                return false;
            ++initializedItemCount;
        }
        m_initializedItemCount = initializedItemCount;
        rollback.release();
        return true;
    }

    inline void finalize()noexcept{
        finalizeInitialized(m_initializedItemCount);
        m_initializedItemCount = 0u;
    }

    [[nodiscard]] inline bool acquire(){
        if(m_activeCount > 0u){
            ++m_activeCount;
            return true;
        }

        if(!initialize())
            return false;

        m_activeCount = 1u;
        return true;
    }

    inline void release()noexcept{
        if(m_activeCount == 0u)
            TerminateInvariant();

        --m_activeCount;
        if(m_activeCount == 0u)
            finalize();
    }


public:
    inline void enqueue(Initializerable& item){ m_cursor = m_items.emplace_after(m_cursor, item); }


private:
    inline void finalizeInitialized(const usize initializedItemCount)noexcept{
        usize itemIndex = 0u;
        for(auto& cur : m_items){
            if(itemIndex >= initializedItemCount)
                return;

            cur.item->finalize();
            ++itemIndex;
        }
    }


private:
    Alloc::GlobalArena m_arena;
    ForwardList<InitializerItem, Alloc::GlobalArena> m_items;
    decltype(m_items)::iterator m_cursor;
    usize m_initializedItemCount = 0;
    usize m_activeCount = 0;
};

class InitializerGuard : NoCopy{
public:
    ~InitializerGuard()noexcept{ finalize(); }


public:
    [[nodiscard]] bool initialize(){
        if(m_initializer)
            return true;

        Initializer& initializer = Initializer::Instance();
        if(!initializer.acquire())
            return false;

        m_initializer = &initializer;
        return true;
    }

    void finalize()noexcept{
        if(!m_initializer)
            return;

        Initializer* initializer = m_initializer;
        m_initializer = nullptr;
        initializer->release();
    }


private:
    Initializer* m_initializer = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_FrameParamPointerSlotCount = 4u;

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
#include <windows.h>
class WinFrame : public FrameData{
private:
    static constexpr usize s_ActiveFlagByteIndex = sizeof(u16) * 2u;
    static constexpr usize s_InstancePointerSlot = 1u;
    static constexpr usize s_WindowPointerSlot = 2u;
    static constexpr usize s_WindowClassSlot = 3u;


public:
    inline bool isActive()const noexcept{ return m_data.u8[s_ActiveFlagByteIndex] != 0; }

    inline void setActive(bool value)noexcept{ m_data.u8[s_ActiveFlagByteIndex] = value ? 1u : 0u; }

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
    inline bool isActive()const noexcept{ return m_data.u8[s_ActiveFlagByteIndex] != 0; }

    inline void setActive(bool value)noexcept{ m_data.u8[s_ActiveFlagByteIndex] = value ? 1u : 0u; }

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

