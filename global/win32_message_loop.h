// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "platform.h"
#include "timer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Win32MessagePumpResult{
    enum Enum : u8{
        Continue,
        SkipUpdate,
        Quit
    };
};

template<typename OnDestroyFunc, typename SetActiveFunc>
[[nodiscard]] inline bool HandleWin32FrameLifecycleMessage(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    OnDestroyFunc&& onDestroy,
    SetActiveFunc&& setActive,
    LRESULT& outResult){
    switch(message){
    case WM_DESTROY:
        onDestroy();
        PostQuitMessage(0);
        outResult = 0;
        return true;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        outResult = 0;
        return true;
    case WM_ACTIVATE:
        setActive(LOWORD(wParam) != WA_INACTIVE);
        outResult = 0;
        return true;
    default:
        return false;
    }
}


template<typename IsActiveFunc>
[[nodiscard]] inline Win32MessagePumpResult::Enum PumpWin32FrameMessages(IsActiveFunc&& isActive){
    MSG message = {};

    if(isActive()){
        while(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)){
            if(message.message == WM_QUIT)
                return Win32MessagePumpResult::Quit;

            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        return Win32MessagePumpResult::Continue;
    }

    if(GetMessage(&message, nullptr, 0, 0) <= 0)
        return Win32MessagePumpResult::Quit;

    TranslateMessage(&message);
    DispatchMessage(&message);
    return Win32MessagePumpResult::SkipUpdate;
}

template<typename IsActiveFunc, typename BeforeUpdateFunc, typename UpdateFunc>
[[nodiscard]] inline bool RunWin32TimedFrameLoop(
    IsActiveFunc&& isActive,
    BeforeUpdateFunc&& beforeUpdate,
    UpdateFunc&& update
){
    Timer lateTime(TimerNow());

    for(;;){
        switch(PumpWin32FrameMessages(isActive)){
        case Win32MessagePumpResult::Quit:
            return true;
        case Win32MessagePumpResult::SkipUpdate:
            continue;
        case Win32MessagePumpResult::Continue:
            break;
        }

        beforeUpdate();

        const f32 timeDifference = ConsumeTimerDeltaSeconds<f32>(lateTime);
        if(!update(timeDifference))
            break;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

