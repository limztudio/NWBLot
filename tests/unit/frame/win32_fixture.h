// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/frame/module.h>
#include <core/graphics/runtime/render_pass_collection.h>

#include <global/compile.h>
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::FrameCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct WindowClassResult{
    b8 registered;
    u32 error;
};

[[nodiscard]] inline HINSTANCE CurrentInstance()noexcept{ return GetModuleHandle(nullptr); }
[[nodiscard]] inline b8 HasInstance(const HINSTANCE instance)noexcept{ return instance != nullptr; }
[[nodiscard]] inline HWND FrameWindow(Core::Frame& frame)noexcept{ return frame.data<Core::Common::WinFrame>().hwnd(); }
inline void DeactivateFrame(Core::Frame& frame)noexcept{ frame.data<Core::Common::WinFrame>().setActive(false); }
[[nodiscard]] inline b8 HasWindow(const HWND window)noexcept{ return window != nullptr; }

[[nodiscard]] inline b8 PerMonitorDpiEnabled()noexcept{
    return AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
}

[[nodiscard]] inline b8 EnablePerMonitorDpi()noexcept{ return SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE; }

[[nodiscard]] inline WindowClassResult QueryFrameClass(const HINSTANCE instance)noexcept{
    WNDCLASSEX info{};
    info.cbSize = sizeof(info);
    const b8 registered = GetClassInfoEx(instance, FOUNDATION_TEXT("NWB_FRAME"), &info) != FALSE;
    return { registered, registered ? ERROR_SUCCESS : GetLastError() };
}

[[nodiscard]] inline b8 RemoveQuitMessage()noexcept{
    MSG message{};
    return PeekMessage(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) != FALSE;
}

[[nodiscard]] inline b8 PostQuitToThread(const u32 thread)noexcept{ return PostThreadMessage(thread, WM_QUIT, 0u, 0) != FALSE; }
[[nodiscard]] inline b8 PostCloseToWindow(const HWND window)noexcept{ return PostMessage(window, WM_CLOSE, 0u, 0) != FALSE; }

template<typename Observer>
void SetFrameUpdateObserver(Core::Frame& frame, Observer& observer)noexcept{
    frame.setProjectUpdateCallback([](void* data, const f32 delta){ return static_cast<Observer*>(data)->update(delta); }, AddressOf(observer));
}

[[nodiscard]] inline usize RenderPassCount(const Core::GraphicsModuleDetail::RenderPassCollection& collection)noexcept{ return collection.passes().size(); }

[[nodiscard]] inline const Core::IRenderPass& FirstRenderPass(const Core::GraphicsModuleDetail::RenderPassCollection& collection)noexcept{ return *collection.passes().front(); }
[[nodiscard]] inline const Core::IRenderPass& LastRenderPass(const Core::GraphicsModuleDetail::RenderPassCollection& collection)noexcept{ return *collection.passes().back(); }

inline void PrintFrameLog(const AStringView message){
    FOUNDATION_CERR << message << '\n';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

