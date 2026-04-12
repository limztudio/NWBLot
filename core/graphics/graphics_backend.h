// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BackBufferResizeCallbacks{
    void* userData = nullptr;
    void (*beforeResize)(void*) = nullptr;
    void (*afterResize)(void*) = nullptr;
};

using GraphicsBackendInterfaceID = u64;

class IGraphicsBackend{
public:
    virtual ~IGraphicsBackend() = default;


public:
    [[nodiscard]] virtual IDevice* getDevice()const = 0;
    [[nodiscard]] virtual GraphicsAPI::Enum getGraphicsAPI()const = 0;
    [[nodiscard]] virtual const tchar* getRendererString()const = 0;

    virtual bool enumerateAdapters(Vector<AdapterInfo>& outAdapters) = 0;

    [[nodiscard]] virtual ITexture* getCurrentBackBuffer() = 0;
    [[nodiscard]] virtual ITexture* getBackBuffer(u32 index) = 0;
    [[nodiscard]] virtual u32 getCurrentBackBufferIndex() = 0;
    [[nodiscard]] virtual u32 getBackBufferCount() = 0;

    virtual void setPlatformFrameParam(const Common::FrameParam& frameParam) = 0;
    virtual bool createInstance() = 0;
    virtual bool createDevice() = 0;
    virtual bool createSwapChain() = 0;
    virtual void destroy() = 0;
    virtual void resizeSwapChain() = 0;
    virtual bool beginFrame(const BackBufferResizeCallbacks& callbacks) = 0;
    virtual bool present() = 0;

    virtual void reportLiveObjects(){}
    [[nodiscard]] virtual void* queryInterface(GraphicsBackendInterfaceID){ return nullptr; }
    [[nodiscard]] virtual const void* queryInterface(GraphicsBackendInterfaceID)const{ return nullptr; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
