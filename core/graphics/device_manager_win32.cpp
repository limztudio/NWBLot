// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.h"

#include <logger/client/logger.h>

#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_PLATFORM_WINDOWS


void IDeviceManager::extractPlatformHandles(const Common::FrameData& frameData){
    const auto& winFrame = static_cast<const Common::WinFrame&>(frameData);
    m_hwnd = winFrame.hwnd();
    m_hinstance = winFrame.instance();
}

void IDeviceManager::updateWindowSize(){
    if(!m_hwnd)
        return;

    RECT rect;
    if(!GetClientRect(static_cast<HWND>(m_hwnd), &rect)){
        m_windowVisible = false;
        return;
    }

    i32 width = rect.right - rect.left;
    i32 height = rect.bottom - rect.top;

    if(width == 0 || height == 0){
        m_windowVisible = false;
        return;
    }

    m_windowVisible = true;
    m_windowIsInFocus = (GetForegroundWindow() == static_cast<HWND>(m_hwnd));

    if(static_cast<i32>(m_deviceParams.backBufferWidth) != width ||
        static_cast<i32>(m_deviceParams.backBufferHeight) != height ||
        (m_deviceParams.vsyncEnabled != m_requestedVSync && getGraphicsAPI() == GraphicsAPI::VULKAN))
    {
        backBufferResizing();

        m_deviceParams.backBufferWidth = width;
        m_deviceParams.backBufferHeight = height;
        m_deviceParams.vsyncEnabled = m_requestedVSync;

        resizeSwapChain();
        backBufferResized();
    }

    m_deviceParams.vsyncEnabled = m_requestedVSync;
}

void IDeviceManager::runMessageLoop(){
    m_previousFrameTimestamp = TimerNow();

    MSG msg = {};
    while(true){
        while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)){
            if(msg.message == WM_QUIT)
                goto done;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if(m_callbacks.beforeFrame) m_callbacks.beforeFrame(*this, m_frameIndex);
        updateWindowSize();
        bool presentSuccess = animateRenderPresent();
        if(!presentSuccess)
            break;
    }
done:

    getDevice()->waitForIdle();
}

void IDeviceManager::setWindowTitle(const tchar* title){
    NWB_ASSERT(title);
    if(m_windowTitle == title)
        return;

    m_windowTitle = title;

    if(m_hwnd){
#ifdef NWB_UNICODE
        SetWindowTextW(static_cast<HWND>(m_hwnd), m_windowTitle.c_str());
#else
        SetWindowTextA(static_cast<HWND>(m_hwnd), m_windowTitle.c_str());
#endif
    }
}


#endif // NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
