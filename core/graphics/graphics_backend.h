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

    [[nodiscard]] virtual bool isInstanceExtensionEnabled(const char*)const{ return false; }
    [[nodiscard]] virtual bool isDeviceExtensionEnabled(const char*)const{ return false; }
    [[nodiscard]] virtual bool isLayerEnabled(const char*)const{ return false; }
    virtual void getEnabledInstanceExtensions(Vector<AString>& extensions)const{ extensions.clear(); }
    virtual void getEnabledDeviceExtensions(Vector<AString>& extensions)const{ extensions.clear(); }
    virtual void getEnabledLayers(Vector<AString>& layers)const{ layers.clear(); }

    virtual void reportLiveObjects(){}
    [[nodiscard]] virtual void* queryInterface(GraphicsBackendInterfaceID){ return nullptr; }
    [[nodiscard]] virtual const void* queryInterface(GraphicsBackendInterfaceID)const{ return nullptr; }
};


using GraphicsBackendFactory = CustomUniquePtr<IGraphicsBackend> (*)(
    const DeviceCreationParameters& params,
    SwapChainRuntimeState& swapChainState,
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool
);

[[nodiscard]] CustomUniquePtr<IGraphicsBackend> CreateDefaultGraphicsBackend(
    const DeviceCreationParameters& params,
    SwapChainRuntimeState& swapChainState,
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

