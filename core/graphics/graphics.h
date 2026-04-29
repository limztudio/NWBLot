// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"
#include "render_pass.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IGraphicsBackend;
class InputDispatcher;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics{
private:
    using BackendOwner = CustomUniquePtr<IGraphicsBackend>;
    using BackendPtr = NotNullUniquePtr<IGraphicsBackend, BackendOwner::deleter_type>;
    using RenderPassListAllocator = Alloc::CustomAllocator<IRenderPass*>;
    using SwapChainFramebufferVectorAllocator = Alloc::CustomAllocator<FramebufferHandle>;


public:
    struct BufferSetupDesc{
        BufferDesc bufferDesc;
        const void* data = nullptr;
        usize dataSize = 0;
        u64 destOffsetBytes = 0;
        CommandQueue::Enum queue = CommandQueue::Graphics;
    };

    struct TextureSetupDesc{
        TextureDesc textureDesc;
        const void* data = nullptr;
        // Total upload payload size in bytes (required for async copy ownership).
        usize uploadDataSize = 0;
        usize rowPitch = 0;
        usize depthPitch = 0;
        u32 arraySlice = 0;
        u32 mipLevel = 0;
        CommandQueue::Enum queue = CommandQueue::Graphics;
    };

    struct MeshSetupDesc{
        const void* vertexData = nullptr;
        usize vertexDataSize = 0;
        u32 vertexStride = 0;
        Name vertexBufferName;

        const void* indexData = nullptr;
        usize indexDataSize = 0;
        bool use32BitIndices = true;
        Name indexBufferName;

        CommandQueue::Enum queue = CommandQueue::Graphics;
    };

    struct MeshResource{
        BufferHandle vertexBuffer;
        BufferHandle indexBuffer;
        u32 vertexStride = 0;
        u32 vertexCount = 0;
        u32 indexCount = 0;
        Format::Enum indexFormat = Format::UNKNOWN;

        [[nodiscard]] bool valid()const noexcept{
            return vertexBuffer != nullptr;
        }
    };

    struct CoopVectorSupport{
        bool inferencingSupported = false;
        bool trainingSupported = false;
        bool fp16InferencingSupported = false;
        bool fp16TrainingSupported = false;
        bool fp32TrainingSupported = false;
    };

    using JobHandle = Alloc::JobSystem::JobHandle;


private:
    static void BackBufferResizingCallback(void* userData);
    static void BackBufferResizedCallback(void* userData);


public:
    Graphics(GraphicsAllocator& allocator, Alloc::ThreadPool& threadPool, Alloc::JobSystem& jobSystem, InputDispatcher& input);
    ~Graphics();


public:
    bool init(const Common::FrameData& data);
    bool createHeadlessDevice();
    bool createInstance(const InstanceParameters& params);
    bool setDebugRuntimeEnabled(bool enabled);
    void setPipelineCacheDirectory(const Path& directory);
    bool runFrame();
    void updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);
    void destroy();

public:
    [[nodiscard]] IDevice* getDevice()const noexcept;
    [[nodiscard]] bool enumerateAdapters(Vector<AdapterInfo>& outAdapters);

    void addRenderPassToFront(IRenderPass& pass);
    void addRenderPassToBack(IRenderPass& pass);
    void removeRenderPass(IRenderPass& pass);

    [[nodiscard]] const tchar* getRendererString()const;
    [[nodiscard]] GraphicsAPI::Enum getGraphicsAPI()const;
    [[nodiscard]] f64 getPreviousFrameTimestamp()const{ return DurationInSeconds<f64>(m_previousFrameTimestamp); }
    [[nodiscard]] bool isVsyncEnabled()const{ return m_swapChainState.vsyncEnabled; }
    void setVSyncEnabled(bool enabled){ m_requestedVSync = enabled; }
    void reportLiveObjects()const;

    void getWindowDimensions(i32& width, i32& height)const;
    void getDPIScaleInfo(f32& x, f32& y)const;
    [[nodiscard]] const tchar* getWindowTitle()const{ return m_windowTitle.c_str(); }
    void setWindowTitle(NotNull<const tchar*> title);

    [[nodiscard]] ITexture* getCurrentBackBuffer()const;
    [[nodiscard]] ITexture* getBackBuffer(u32 index)const;
    [[nodiscard]] u32 getCurrentBackBufferIndex()const;
    [[nodiscard]] u32 getBackBufferCount()const;
    [[nodiscard]] IFramebuffer* getCurrentFramebuffer()const;
    [[nodiscard]] IFramebuffer* getFramebuffer(u32 index)const;

    [[nodiscard]] bool isVulkanInstanceExtensionEnabled(const char* extensionName)const;
    [[nodiscard]] bool isVulkanDeviceExtensionEnabled(const char* extensionName)const;
    [[nodiscard]] bool isVulkanLayerEnabled(const char* layerName)const;
    void getEnabledVulkanInstanceExtensions(Vector<AString>& extensions)const;
    void getEnabledVulkanDeviceExtensions(Vector<AString>& extensions)const;
    void getEnabledVulkanLayers(Vector<AString>& layers)const;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc)const;
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc)const;

    [[nodiscard]] BufferHandle setupBuffer(const BufferSetupDesc& desc)const;
    [[nodiscard]] TextureHandle setupTexture(const TextureSetupDesc& desc)const;
    [[nodiscard]] MeshResource setupMesh(const MeshSetupDesc& desc)const;

    [[nodiscard]] JobHandle setupBufferAsync(const BufferSetupDesc& desc, BufferHandle& outBuffer);
    [[nodiscard]] JobHandle setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture);
    [[nodiscard]] JobHandle setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh);

    [[nodiscard]] CoopVectorSupport queryCoopVecSupport()const;
    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures()const;
    [[nodiscard]] usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const;

    void waitJob(JobHandle handle)const;
    void waitAllJobs()const;


private:
    [[nodiscard]] bool shouldRenderUnfocused()const;

    void backBufferResizing();
    void backBufferResized();
    void displayScaleChanged();

    void animate(f64 elapsedTime);
    void render();
    void updateAverageFrameTime(f64 elapsedTime);
    void syncInputMousePositionScale();
    bool animateRenderPresent();


private:
    GraphicsAllocator& m_allocator;
    Alloc::ThreadPool& m_threadPool;
    Alloc::JobSystem& m_jobSystem;
    InputDispatcher& m_input;
    DeviceCreationParameters m_deviceCreationParams;
    SwapChainRuntimeState m_swapChainState;

private:
    BackendPtr m_backend;

    bool m_skipRenderOnFirstFrame = false;
    bool m_hasPresentedFrame = false;
    bool m_windowVisible = false;
    bool m_windowIsInFocus = true;

    List<IRenderPass*, RenderPassListAllocator> m_renderPasses;
    Timer m_previousFrameTimestamp = {};
    f32 m_dpiScaleFactorX = 1.f;
    f32 m_dpiScaleFactorY = 1.f;
    f32 m_prevDPIScaleFactorX = 0.f;
    f32 m_prevDPIScaleFactorY = 0.f;
    bool m_requestedVSync = false;
    bool m_instanceCreated = false;

    f64 m_averageFrameTime = 0.0;
    f64 m_averageTimeUpdateInterval = s_AverageFrameTimeUpdateIntervalSeconds;
    f64 m_frameTimeSum = 0.0;
    i32 m_numberOfAccumulatedFrames = 0;

    u32 m_frameIndex = 0;

    Vector<FramebufferHandle, SwapChainFramebufferVectorAllocator> m_swapChainFramebuffers;

    BasicString<tchar> m_windowTitle;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

