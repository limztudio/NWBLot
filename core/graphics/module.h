// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"

#include "gpu_timing.h"
#include "render_pass.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IGpuTaskGraphPresentationContributor;
class GpuTaskGraph;
struct GpuTaskId;


class Graphics{
private:
    using Backend = GraphicsBackend::Backend;
    using BackendOwner = GlobalUniquePtr<Backend>;


public:
    struct BufferSetupDesc{
        BufferDesc bufferDesc;
        const void* data = nullptr;
        usize dataSize = 0;
        u64 destOffsetBytes = 0;
        // kCount selects the automatic setup-upload route: sizeable uploads prefer a real dedicated Transfer
        // transport, then dedicated Compute, then Graphics. Supplying a concrete queue preserves an explicit
        // caller preference; Transfer and Compute still fall back to an available physical transport. Upload
        // offsets and byte sizes must be 4-byte aligned; a non-retained Unknown initial state publishes CopyDest,
        // while a retained upload requires a concrete initial state.
        CommandQueue::Enum queue = CommandQueue::kCount;
        // Written only after the upload submission and every declared consumer-queue readiness bridge have been
        // accepted. Async callers must keep this storage alive until their setup job completes.
        QueueSubmissionToken* acceptedToken = nullptr;
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
        // See BufferSetupDesc::queue. A non-retained Unknown initial state publishes CopyDest; a retained upload
        // requires a concrete initial state and is rejected otherwise.
        CommandQueue::Enum queue = CommandQueue::kCount;
        QueueSubmissionToken* acceptedToken = nullptr;
        // Automatic preserves the legacy one-plane behavior for color, depth-only, and stencil-only formats.
        // D24S8/D32S8 require the caller to select one concrete aspect because Vulkan copies their depth and
        // stencil planes from independently laid out CPU payloads.
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;
    };

    // One immutable CPU payload for a texture subresource upload.  uploadTextureBatch() copies every region into
    // graph-owned blobs before native recording, so the caller may release its decoded asset memory when the call
    // returns.  A 3D mip is one region at arraySlice 0 whose depthPitch covers a single Z slice; 2D/cube uploads
    // provide one region per array slice.
    struct TextureUploadRegion{
        const void* data = nullptr;
        usize dataSize = 0u;
        usize rowPitch = 0u;
        usize depthPitch = 0u;
        u32 arraySlice = 0u;
        u32 mipLevel = 0u;
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;
    };

    // Uploads every listed region into an existing texture through a compiler-owned task graph.  This is the
    // multi-subresource companion to TextureSetupDesc for decoded/static assets.  `finalState` is explicit so a
    // caller cannot publish an opaque post-write layout; for keepInitialState textures it must equal initialState.
    // Leave hasPhysicalInitialState false to preserve the legacy descriptor-state import.  Set it true to declare
    // the actual native state of the destination before the upload; an explicit Unknown means a fresh Vulkan image
    // begins in UNDEFINED rather than TextureDesc::initialState.
    struct TextureUploadBatchDesc{
        TextureHandle destination;
        const TextureUploadRegion* regions = nullptr;
        usize regionCount = 0u;
        ResourceStates::Mask finalState = ResourceStates::Unknown;
        CommandQueue::Enum queue = CommandQueue::kCount;
        QueueSubmissionToken* acceptedToken = nullptr;
        ResourceStates::Mask physicalInitialState = ResourceStates::Unknown;
        bool hasPhysicalInitialState = false;
    };

    struct MeshSetupDesc{
        const void* vertexData = nullptr;
        usize vertexDataSize = 0;
        Name vertexBufferName;
        const void* indexData = nullptr;
        usize indexDataSize = 0;
        Name indexBufferName;
        u32 vertexStride = 0;
        bool use32BitIndices = true;
        // Forwarded to the constituent buffer setups. kCount enables the automatic upload transport policy.
        CommandQueue::Enum queue = CommandQueue::kCount;
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
    // A synchronous caller may declare one isolated graph through this callback. The graph owns all native command
    // recording and submission; the callback must only retain declaration-time inputs and return its terminal task.
    // `userData` remains caller-owned for the duration of this synchronous call.
    using StandaloneTaskGraphDeclaration = GpuTaskId(*)(void* userData, GpuTaskGraph& graph);


private:
    // runFrame creates this on the stack only for a capture-enabled normal frame. It stages phase values without
    // touching the TimingSink, so a failed frame cannot make a partial phase scope observable.
    struct CpuTimingPhaseBatch;

public:
    Graphics(
        GraphicsAllocator& allocator,
        Alloc::ThreadPool& threadPool,
        Alloc::JobSystem& jobSystem,
        Perf::TimingSink& gpuTiming
    );
    Graphics(
        GraphicsAllocator& allocator,
        Alloc::ThreadPool& threadPool,
        Alloc::JobSystem& jobSystem,
        Perf::TimingSink& gpuTiming,
        Perf::TimingSink* cpuTiming
    );
    ~Graphics();


public:
    bool init(const Common::FrameData& data);
    bool createHeadlessDevice();
    bool createInstance(const InstanceParameters& params);
    bool setDebugRuntimeEnabled(bool enabled);
    // Selects the native mesh-shader path when the backend supports it. Disabled configurations use the renderer's
    // compute-emulation path. Must be configured before instance creation.
    bool setNativeMeshShadersEnabled(bool enabled);
    // Must be configured before device creation. Unsupported adapters retain the Graphics-only path.
    bool setAsyncComputeLaneEnabled(bool enabled);
    // Must be configured before device creation. Unsupported adapters retain the Graphics/Compute copy fallback.
    bool setTransferQueueEnabled(bool enabled);
    // Must be configured before device creation. It may expose every safe additional queue from each active
    // primary family and, with cross-family routing enabled, one auxiliary family for each class; only explicitly
    // opted-in graph tasks may route to them.
    bool setSameClassMultiQueueEnabled(bool enabled);
    // Extends optional same-class discovery to distinct compatible Vulkan families. Individual graph tasks must
    // still explicitly accept the resulting ownership-transfer route.
    bool setCrossFamilySameClassQueueRoutingEnabled(bool enabled);
    // Selects a Vulkan adapter enumeration index, or -1 for the backend default. Must be configured before device
    // creation so target-hardware probes can reproduce a multi-adapter route on paired processes.
    bool setAdapterIndex(i32 index);
    // Requests HDR10/PQ presentation where the current display surface supports it. Unsupported surfaces
    // automatically retain the normal SDR swap chain. Must be configured before device creation.
    bool setHDR10OutputEnabled(bool enabled);
    // Requests transfer-source usage for presentation images. Unsupported surfaces retain the normal swap chain
    // and report readback unavailable. Must be configured before device creation.
    bool setSwapChainReadbackEnabled(bool enabled);
    bool setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi);
    void setPipelineCacheDirectory(const Path& directory);
    // Keeps the host update/event loop alive while preventing runFrame from recording, submitting, or presenting a
    // new frame. This is useful when an external capture must sample the last completed temporal frame exactly.
    void setFrameSubmissionSuspended(bool suspended)noexcept{ m_frameSubmissionSuspended = suspended; }
    [[nodiscard]] bool isFrameSubmissionSuspended()const noexcept{ return m_frameSubmissionSuspended; }
    bool runFrame();
    // A render pass uses this when an accepted cross-queue release cannot be recovered safely. The current graphics
    // generation then stops before another pass or presentation can use indeterminate ownership; its owner must
    // tear down and recreate the device/resources before resuming.
    void requestDeviceRecreation()const;
    [[nodiscard]] bool isDeviceRecreationRequested()const noexcept{ return m_deviceRecreationRequested; }
    [[nodiscard]] bool updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);
    [[nodiscard]] bool destroy();
    [[nodiscard]] bool waitForIdle();
    [[nodiscard]] bool isDeviceLost()const noexcept;

public:
    [[nodiscard]] GraphicsBackend::Device& getDevice()const noexcept;
    [[nodiscard]] bool enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters);
    // Returns identity from the physical device selected for the current logical device, rather than from a later
    // adapter enumeration. Available only after successful device creation.
    [[nodiscard]] bool getSelectedAdapterInfo(AdapterInfo& outAdapter)const;
    [[nodiscard]] bool queryFeatureSupport(Feature::Enum feature, void* featureInfo = nullptr, usize featureInfoSize = 0)const;
    // Resolves the GPU wave/subgroup size, or returns a conservative fallback (64) when the device cannot report it.
    // Use the returned value to size groupshared reductions and wave-intrinsic shader specializations.
    [[nodiscard]] u32 queryWaveLaneCount()const noexcept;

    void addRenderPassToFront(IRenderPass& pass);
    void addRenderPassToBack(IRenderPass& pass);
    void removeRenderPass(IRenderPass& pass);

    // The deferred graph claims the current swap-chain binary semaphore and attaches it to its exact terminal
    // packet. Direct/non-graph render paths receive an empty hook and retain BackendContext::present()'s fallback.
    [[nodiscard]] QueueSubmissionPreSubmitHook claimFramePresentationSignal()noexcept;
    [[nodiscard]] bool confirmFramePresentationSignal(
        const QueueSubmissionPreSubmitHook& claim,
        const QueueSubmissionToken& token
    )noexcept;
    [[nodiscard]] bool cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim)noexcept;

    // Optional overlays register here instead of coupling a renderer directly to their module. The active
    // contributor may append one final Graphics packet to a renderer-owned task graph before presentation.
    void setTaskGraphPresentationContributor(IGpuTaskGraphPresentationContributor* contributor)noexcept{
        m_taskGraphPresentationContributor = contributor;
    }
    void clearTaskGraphPresentationContributor(const IGpuTaskGraphPresentationContributor& contributor)noexcept{
        if(m_taskGraphPresentationContributor == &contributor)
            m_taskGraphPresentationContributor = nullptr;
    }
    [[nodiscard]] IGpuTaskGraphPresentationContributor* taskGraphPresentationContributor()const noexcept{
        return m_taskGraphPresentationContributor;
    }

    [[nodiscard]] const tchar* getRendererString()const;
    // Compatibility query: the renderer is Vulkan-only and this always returns GraphicsAPI::VULKAN.
    [[nodiscard]] GraphicsAPI::Enum getGraphicsAPI()const;
    [[nodiscard]] f64 getPreviousFrameTimestamp()const{ return DurationInSeconds<f64>(m_previousFrameTimestamp); }
    [[nodiscard]] u64 getFrameIndex()const{ return m_frameIndex; }
    [[nodiscard]] GpuTimingRecorder& gpuTiming(){ return m_gpuTiming; }
    [[nodiscard]] const GpuTimingRecorder& gpuTiming()const{ return m_gpuTiming; }
    [[nodiscard]] bool isVsyncEnabled()const{ return m_swapChainState.vsyncEnabled; }
    [[nodiscard]] bool isHDR10OutputActive()const{ return m_swapChainState.outputMode == SwapChainOutputMode::HDR10; }
    [[nodiscard]] bool isSwapChainReadbackAvailable()const{ return m_swapChainState.swapChainReadbackAvailable; }
    void setVSyncEnabled(bool enabled){ m_requestedVSync = enabled; }
    void reportLiveObjects()const;

    void getWindowDimensions(i32& width, i32& height)const;
    void getDPIScaleInfo(f32& x, f32& y)const;
    [[nodiscard]] const tchar* getWindowTitle()const{ return m_windowTitle.c_str(); }
    void setWindowTitle(NotNull<const tchar*> title);
    void setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData);

    // Valid only while Graphics is preparing, rendering, or presenting one successfully acquired frame. The
    // snapshot owns the exact back buffer and its matching framebuffer so presentation consumers never infer WSI
    // identity from mutable backend state.
    [[nodiscard]] const AcquiredPresentationFrame& acquiredPresentationFrame()const noexcept{ return m_acquiredPresentationFrame; }
    [[nodiscard]] Texture* getBackBuffer(u32 index)const;
    [[nodiscard]] u32 getBackBufferCount()const;
    [[nodiscard]] Framebuffer* getFramebuffer(u32 index)const;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc)const;
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc)const;

    [[nodiscard]] BufferHandle setupBuffer(const BufferSetupDesc& desc)const;
    [[nodiscard]] TextureHandle setupTexture(const TextureSetupDesc& desc)const;
    [[nodiscard]] bool uploadTextureBatch(const TextureUploadBatchDesc& desc)const;
    // Compiles, records, and submits an isolated graph synchronously. This is the graph-owned escape hatch for a
    // compatibility caller that has no renderer-owned frame graph but can still provide immutable task inputs.
    [[nodiscard]] bool submitStandaloneTaskGraph(
        void* userData,
        StandaloneTaskGraphDeclaration declareTask,
        QueueSubmissionToken& outSubmissionToken,
        GpuPhysicalQueueId requiredTerminalQueue = {}
    )const;
    [[nodiscard]] MeshResource setupMesh(const MeshSetupDesc& desc)const;

    [[nodiscard]] JobHandle setupBufferAsync(const BufferSetupDesc& desc, BufferHandle& outBuffer);
    [[nodiscard]] JobHandle setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture);
    [[nodiscard]] JobHandle setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh);

    [[nodiscard]] CoopVectorSupport queryCoopVecSupport()const;
    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures()const;
    [[nodiscard]] usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const;

    // Schedules CPU-side graphics work on the graphics worker pool. Callers must wait for the returned job before
    // submitting or destroying any command lists/resources the work touches.
    template<typename Func>
    [[nodiscard]] JobHandle scheduleGraphicsJob(Func&& task){
        return m_jobSystem.submit(Forward<Func>(task));
    }

    template<typename Func>
    [[nodiscard]] JobHandle scheduleGraphicsJob(Func&& task, const JobHandle dependency){
        return m_jobSystem.submit(Forward<Func>(task), dependency);
    }

    void waitJob(JobHandle handle)const;
    void waitAllJobs()const{ m_jobSystem.waitAll(); }

    [[nodiscard]] bool backBufferResizing(SwapChainTransitionTicket& outTicket);
    [[nodiscard]] bool backBufferResized();
    void invalidateRenderPassResources();
    [[nodiscard]] bool validateRenderPassResources();
    void displayScaleChanged();

    void animate(f64 elapsedTime);
    // Runs the allocation/submission work that must precede every frame's render-pass preparation. Call this after
    // the backend has acquired a frame and before render(); direct headless callers use it to establish the same
    // ordering without a swap-chain beginFrame().
    [[nodiscard]] bool prepareFramePreamble();
    void render();
    void updateAverageFrameTime(f64 elapsedTime);
    void notifyPointerScaleChanged()const;
    [[nodiscard]] bool shouldRenderUnfocused()const;
    bool animateRenderPresent();


private:
    bool animateRenderPresentInternal(CpuTimingPhaseBatch* phaseTiming);
    [[nodiscard]] bool resizeBackBuffer(u32 width, u32 height, bool vsyncEnabled);


private:
    GraphicsAllocator& m_allocator;
    Alloc::ThreadPool& m_threadPool;
    Alloc::JobSystem& m_jobSystem;
    DeviceCreationParameters m_deviceCreationParams;
    SwapChainRuntimeState m_swapChainState;
    GpuTimingRecorder m_gpuTiming;
    // Optional and non-owning: Frame's perf Session owns this sink and outlives Graphics. It is used only by the
    // main-thread runFrame boundary; packet recording and setup workers intentionally remain outside this sink.
    Perf::TimingSink* m_cpuTiming = nullptr;

private:
    NotNullUniquePtr<Backend, BackendOwner::deleter_type> m_backend;

    bool m_skipRenderOnFirstFrame = false;
    bool m_hasPresentedFrame = false;
    bool m_windowVisible = false;
    bool m_windowIsInFocus = true;
    bool m_requestedVSync = false;
    bool m_instanceCreated = false;
    mutable bool m_deviceRecreationRequested = false;
    bool m_frameSubmissionSuspended = false;

    List<IRenderPass*, Alloc::GlobalArena> m_renderPasses;
    // Non-owning: the contributing system unregisters before its lifetime ends.
    IGpuTaskGraphPresentationContributor* m_taskGraphPresentationContributor = nullptr;
    Timer m_previousFrameTimestamp = {};
    f32 m_dpiScaleFactorX = 1.f;
    f32 m_dpiScaleFactorY = 1.f;
    f32 m_prevDPIScaleFactorX = 0.f;
    f32 m_prevDPIScaleFactorY = 0.f;

    f64 m_averageFrameTime = 0.0;
    f64 m_averageTimeUpdateInterval = s_AverageFrameTimeUpdateIntervalSeconds;
    f64 m_frameTimeSum = 0.0;
    i32 m_numberOfAccumulatedFrames = 0;

    u32 m_frameIndex = 0;

    Vector<FramebufferHandle, Alloc::GlobalArena> m_swapChainFramebuffers;
    AcquiredPresentationFrame m_acquiredPresentationFrame;

    GraphicsTString m_windowTitle;
    PointerScaleChangedCallback m_pointerScaleChangedCallback = nullptr;
    void* m_pointerScaleChangedUserData = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

