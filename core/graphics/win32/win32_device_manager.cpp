// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/common.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void IDeviceManager::extractPlatformHandles(const Common::FrameData& frameData){
    m_platformFrameParam = frameData.frameParam();
}

void IDeviceManager::updateWindowSize(){
    HWND hwnd = static_cast<HWND>(m_platformFrameParam.ptr[2]);
    if(!hwnd)
        return;

    RECT rect;
    if(!GetClientRect(hwnd, &rect)){
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
    m_windowIsInFocus = (GetForegroundWindow() == hwnd);

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

    if(m_platformFrameParam.ptr[2]){
#ifdef NWB_UNICODE
        SetWindowTextW(static_cast<HWND>(m_platformFrameParam.ptr[2]), m_windowTitle.c_str());
#else
        SetWindowTextA(static_cast<HWND>(m_platformFrameParam.ptr[2]), m_windowTitle.c_str());
#endif
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
