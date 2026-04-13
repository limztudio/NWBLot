// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "platform.h"


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
    LRESULT& outResult)
{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

