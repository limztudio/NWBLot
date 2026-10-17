// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>

#include <global/frame_data.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_FrameStoragePointerSlotCount = 2u;


using FrameParam = FrameParamStorage<s_FrameStoragePointerSlotCount>;
using FrameData = BasicFrameData<s_FrameStoragePointerSlotCount>;

#if defined(NWB_PLATFORM_WINDOWS)
class WinFrame : public FrameData{
public:
    inline HINSTANCE instance()const noexcept{ return static_cast<HINSTANCE>(m_data.ptr[0]); }
    inline void setInstance(HINSTANCE value)noexcept{ m_data.ptr[0] = value; }

    inline HWND hwnd()const noexcept{ return static_cast<HWND>(m_data.ptr[1]); }
    inline void setHwnd(HWND value)noexcept{ m_data.ptr[1] = value; }
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Frame{
#if defined(NWB_PLATFORM_WINDOWS)
    using NativeFrameData = FrameDetail::WinFrame;
#else
    using NativeFrameData = FrameDetail::FrameData;
#endif


public:
    static void Print(BasicStringView<tchar> str, Core::Common::LogType::Enum type);


public:
    Frame(void* inst)noexcept(IsSame_V<NativeFrameData, FrameDetail::FrameData>);
    ~Frame();


public:
    bool init()noexcept(IsSame_V<NativeFrameData, FrameDetail::FrameData>);
    void showFrame();
    bool mainLoop()noexcept(IsSame_V<NativeFrameData, FrameDetail::FrameData>);


public:
    template<typename T>
    inline T& data()noexcept{ return static_cast<T&>(m_data); }


private:
    NativeFrameData m_data;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

