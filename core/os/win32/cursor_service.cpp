// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cursor_service.h"
#include "cursor.h"

#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_cursor_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr const wchar* SystemCursorResource(const CursorShape::Enum shape)noexcept{
    switch(shape){
    case CursorShape::Default: return IDC_ARROW;
    case CursorShape::TextInput: return IDC_IBEAM;
    case CursorShape::Hand: return IDC_HAND;
    case CursorShape::Crosshair: return IDC_CROSS;
    case CursorShape::ResizeEW: return IDC_SIZEWE;
    case CursorShape::ResizeNS: return IDC_SIZENS;
    case CursorShape::ResizeNWSE: return IDC_SIZENWSE;
    case CursorShape::ResizeNESW: return IDC_SIZENESW;
    case CursorShape::Move: return IDC_SIZEALL;
    case CursorShape::NotAllowed: return IDC_NO;
    case CursorShape::Wait: return IDC_WAIT;
    case CursorShape::Progress: return IDC_APPSTARTING;
    case CursorShape::Grab: return IDC_HAND;
    case CursorShape::Grabbing: return IDC_SIZEALL;
    case CursorShape::Hidden:
    case CursorShape::kCount:
    default:
        return nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Win32CursorService::Win32CursorService(Alloc::GlobalArena& arena, const NotNull<void*> nativeWindowHandle)noexcept
    : TrackedCursorService(arena)
    , m_nativeWindowHandle(nativeWindowHandle)
{
    void* const arrow = static_cast<void*>(LoadCursorW(nullptr, IDC_ARROW));
    for(usize i = 0u; i < static_cast<usize>(CursorShape::kCount); ++i){
        const CursorShape::Enum shape = static_cast<CursorShape::Enum>(i);
        if(shape == CursorShape::Hidden){
            m_cursors[i] = nullptr;
            continue;
        }
        const wchar* const resourceId = __hidden_win32_cursor_service::SystemCursorResource(shape);
        HCURSOR loaded = resourceId ? LoadCursorW(nullptr, resourceId) : nullptr;
        m_cursors[i] = loaded ? static_cast<void*>(loaded) : arrow;
    }
}

CursorCapabilities Win32CursorService::capabilities()const noexcept{
    return {
        .setShape = true,
        .hide = true,
        .backend = CursorBackend::Win32
    };
}

b8 Win32CursorService::applyNativeCursor(const CursorShape::Enum shape)noexcept{
    const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
    if(!IsWindow(window))
        return false;
    POINT cursorPos = {};
    if(GetCursorPos(&cursorPos) && WindowFromPoint(cursorPos) == window){
        const LRESULT hit = SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(cursorPos.x, cursorPos.y));
        if(hit == HTCLIENT)
            ::SetCursor(static_cast<HCURSOR>(m_cursors[static_cast<usize>(shape)]));
    }
    return true;
}

Optional<isize> Win32CursorService::handleSetCursorMessage(const usize wParam, const isize lParam)noexcept{
    if(!isOwnerThread())
        return {};
    const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
    if(wParam != 0u && reinterpret_cast<HWND>(wParam) != window)
        return {};
    if(LOWORD(static_cast<DWORD>(lParam)) != HTCLIENT)
        return {};
    ::SetCursor(static_cast<HCURSOR>(m_cursors[static_cast<usize>(currentCursor())]));
    return Optional<isize>(TRUE);
}

Optional<isize> DispatchWin32CursorMessage(ICursorService& service, const u32 message, const usize wParam, const isize lParam)noexcept{
    if(message != WM_SETCURSOR || !service.isOwnerThread() || service.capabilities().backend != CursorBackend::Win32)
        return {};
    return MakeNotNull(checked_cast<Win32CursorService*>(&service))->handleSetCursorMessage(wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
