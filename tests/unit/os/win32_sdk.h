// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include "service_sdk.h"
#include <core/os/win32/accessibility.h>
#include <core/os/win32/cursor.h>
#include <core/os/win32/text_input.h>
#include <core/os/win32/text_input_context.h>
#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::OsSdk{

using Window = HWND;
using ImeContext = HIMC;

template<typename Handler>
LRESULT CALLBACK ForwardWindowMessage(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam){
    if(message == WM_NCCREATE){
        const auto& creation = *MakeNotNull(reinterpret_cast<const CREATESTRUCTW*>(lParam));
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation.lpCreateParams));
        if(previous == 0 && GetLastError() != ERROR_SUCCESS)
            return FALSE;
    }
    auto* handler = reinterpret_cast<Handler*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if(!handler)
        return DefWindowProcW(window, message, wParam, lParam);
    return handler->message(window, message, wParam, lParam);
}

template<typename Handler>
[[nodiscard]] b8 RegisterWindowClass(const WStringView name, Handler&){
    WNDCLASSW nativeClass{};
    nativeClass.lpfnWndProc = ForwardWindowMessage<Handler>;
    nativeClass.hInstance = GetModuleHandleW(nullptr);
    nativeClass.lpszClassName = name.data();
    return RegisterClassW(&nativeClass) != 0u || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

template<typename Handler>
[[nodiscard]] Window CreateWindowFor(const WStringView name, const u32 style, const i32 width, const i32 height, Handler& handler){
    return CreateWindowExW(0u, name.data(), L"", style, 0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), &handler);
}

inline WindowHandle PlatformWindow(const Window window)noexcept{ return window; }
inline b8 Present(const Window window)noexcept{ return window != nullptr; }
inline b8 Present(const ImeContext context)noexcept{ return context != nullptr; }
inline constexpr Window s_EmptyWindow = nullptr;
inline constexpr isize s_Win32UiaRootObjectId = static_cast<isize>(static_cast<LONG>(-25));
inline constexpr isize s_Win32HitClient = static_cast<isize>(HTCLIENT);
inline constexpr isize s_Win32HitCaption = static_cast<isize>(HTCAPTION);

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
