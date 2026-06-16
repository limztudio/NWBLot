// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"
#include "backend_selection.h"

#include "gpu_timing.h"
#include "render_pass.h"

#include <core/telemetry/session.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics{
private:
    using Backend = GraphicsBackend::Backend;


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
    using PointerScaleChangedCallback = void(*)(void* userData, f32 scaleX, f32 scaleY);


private:
    static void BackBufferResizingCallback(void* userData);
    static void BackBufferResizedCallback(void* userData);


public:
    Graphics(
        GraphicsAllocator& allocator,
        Alloc::ThreadPool& threadPool,
        Alloc::JobSystem& jobSystem,
        Perf::TimingSink& gpuTiming
    );
    ~Graphics();


public:
    bool init(const Common::FrameData& data);
    bool createHeadlessDevice();
    bool createInstance(const InstanceParameters& params);
    bool setDebugRuntimeEnabled(bool enabled);
    void setPipelineCacheDirectory(const Path& directory);
    bool runFrame(){ return animateRenderPresent(); }
    void updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);
    void destroy();

public:
    [[nodiscard]] GraphicsBackend::Device* getDevice()const noexcept{ return m_backend.getDevice(); }
    [[nodiscard]] bool enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters){ return m_backend.enumerateAdapters(outAdapters); }
    [[nodiscard]] bool queryFeatureSupport(Feature::Enum feature, void* featureInfo = nullptr, usize featureInfoSize = 0)const;
#if !defined(NWB_FINAL)
    void setFeatureSupportDisabledForTesting(Feature::Enum feature, bool disabled);
    void clearFeatureSupportDisabledForTesting();
#endif

    void addRenderPassToFront(IRenderPass& pass);
    void addRenderPassToBack(IRenderPass& pass);
    void removeRenderPass(IRenderPass& pass);

    [[nodiscard]] const tchar* getRendererString()const{ return m_backend.getRendererString(); }
    [[nodiscard]] GraphicsAPI::Enum getGraphicsAPI()const{ return GraphicsBackend::s_Api; }
    [[nodiscard]] f64 getPreviousFrameTimestamp()const{ return DurationInSeconds<f64>(m_previousFrameTimestamp); }
    [[nodiscard]] u64 getFrameIndex()const{ return m_frameIndex; }
    [[nodiscard]] GpuTimingRecorder& gpuTiming(){ return m_gpuTiming; }
    [[nodiscard]] const GpuTimingRecorder& gpuTiming()const{ return m_gpuTiming; }
    [[nodiscard]] bool isVsyncEnabled()const{ return m_swapChainState.vsyncEnabled; }
    void setVSyncEnabled(bool enabled){ m_requestedVSync = enabled; }
    void reportLiveObjects()const{ m_backend.reportLiveObjects(); }

    void getWindowDimensions(i32& width, i32& height)const;
    void getDPIScaleInfo(f32& x, f32& y)const;
    [[nodiscard]] const tchar* getWindowTitle()const{ return m_windowTitle.c_str(); }
    void setWindowTitle(NotNull<const tchar*> title);
    void setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData);

    [[nodiscard]] Texture* getCurrentBackBuffer()const{ return m_backend.getCurrentBackBuffer(); }
    [[nodiscard]] Texture* getBackBuffer(u32 index)const{ return m_backend.getBackBuffer(index); }
    [[nodiscard]] u32 getCurrentBackBufferIndex()const{ return m_backend.getCurrentBackBufferIndex(); }
    [[nodiscard]] u32 getBackBufferCount()const{ return m_backend.getBackBufferCount(); }
    [[nodiscard]] Framebuffer* getCurrentFramebuffer()const{ return getFramebuffer(getCurrentBackBufferIndex()); }
    [[nodiscard]] Framebuffer* getFramebuffer(u32 index)const;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc)const{ return getDevice()->createBuffer(desc); }
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc)const{ return getDevice()->createTexture(desc); }

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
    void waitAllJobs()const{ m_jobSystem.waitAll(); }

    void backBufferResizing();
    void backBufferResized();
    void invalidateRenderPassResources();
    [[nodiscard]] bool validateRenderPassResources();
    void displayScaleChanged();

    void animate(f64 elapsedTime);
    void render();
    [[nodiscard]] bool recordFrameGraph(Telemetry::CaptureSession& session);
    void updateAverageFrameTime(f64 elapsedTime);
    void notifyPointerScaleChanged()const;
    [[nodiscard]] bool shouldRenderUnfocused()const;
    bool animateRenderPresent();


private:
    GraphicsAllocator& m_allocator;
    Alloc::ThreadPool& m_threadPool;
    Alloc::JobSystem& m_jobSystem;
    DeviceCreationParameters m_deviceCreationParams;
    SwapChainRuntimeState m_swapChainState;
    GpuTimingRecorder m_gpuTiming;

private:
    Backend m_backend;

    bool m_skipRenderOnFirstFrame = false;
    bool m_hasPresentedFrame = false;
    bool m_windowVisible = false;
    bool m_windowIsInFocus = true;

    List<IRenderPass*, Alloc::GlobalArena> m_renderPasses;
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
#if !defined(NWB_FINAL)
    u64 m_disabledFeatureSupportMask = 0u;
#endif

    Vector<FramebufferHandle, Alloc::GlobalArena> m_swapChainFramebuffers;

    GraphicsTString m_windowTitle;
    PointerScaleChangedCallback m_pointerScaleChangedCallback = nullptr;
    void* m_pointerScaleChangedUserData = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

