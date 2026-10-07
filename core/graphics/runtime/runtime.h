// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/api.h>
#include <core/task/cpu/scheduler.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/render_pass.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IGpuTaskGraphPresentationContributor;
interface IGpuTaskGraphOutputLayerContributor;
class GpuTaskScheduler;
class GpuTaskGraph;
struct GpuTaskId;


class GraphicsRuntime{
private:
    using Backend = GraphicsBackend::Backend;
    using BackendOwner = GlobalUniquePtr<Backend>;


public:
    struct BufferSetupDesc{
        BufferDesc bufferDesc;
        const void* data = nullptr;
        // Upload sizes and destination offsets must be four-byte aligned.
        usize dataSize = 0;
        u64 destOffsetBytes = 0;
        // Optional output written after producer submission and consumer readiness bridges.
        QueueSubmissionToken* acceptedToken = nullptr;
        // Direct native consumer timeline; kCount resolves from payload size, final state, and available queues.
        // The scheduler independently places the producer and submits readiness bridges for consumers.
        CommandQueue::Enum queue = CommandQueue::kCount;
    };

    struct TextureSetupDesc{
        TextureDesc textureDesc;
        const void* data = nullptr;
        // Total upload payload size in bytes, used to validate the subresource upload layout.
        usize uploadDataSize = 0;
        usize rowPitch = 0;
        usize depthPitch = 0;
        QueueSubmissionToken* acceptedToken = nullptr;
        u32 arraySlice = 0;
        u32 mipLevel = 0;
        // See BufferSetupDesc::queue for the native consumer readiness contract.
        // A non-retained Unknown initial state publishes CopyDest; a retained upload requires a concrete initial state.
        CommandQueue::Enum queue = CommandQueue::kCount;
        // Automatic selects one aspect for color, depth-only, and stencil-only formats.
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

    // Multi-subresource texture upload via a compiler-owned graph. Explicit `finalState` (keepInitialState: == initialState).
    // hasPhysicalInitialState declares the native pre-upload state; explicit Unknown starts fresh images UNDEFINED.
    struct TextureUploadBatchDesc{
        TextureHandle destination;
        const TextureUploadRegion* regions = nullptr;
        usize regionCount = 0u;
        QueueSubmissionToken* acceptedToken = nullptr;
        ResourceStates::Mask finalState = ResourceStates::Unknown;
        ResourceStates::Mask physicalInitialState = ResourceStates::Unknown;
        // See BufferSetupDesc::queue; the resolved native consumer must be admitted by the destination sharing contract.
        CommandQueue::Enum queue = CommandQueue::kCount;
        bool hasPhysicalInitialState = false;
    };

    struct MeshSetupDesc{
        const void* vertexData = nullptr;
        usize vertexDataSize = 0;
        const void* indexData = nullptr;
        usize indexDataSize = 0;
        Name vertexBufferName;
        Name indexBufferName;
        u32 vertexStride = 0;
        // Forwarded to the constituent buffer setups' native consumer readiness contracts.
        CommandQueue::Enum queue = CommandQueue::kCount;
        bool use32BitIndices = true;
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
    GraphicsRuntime(
        GraphicsAllocator& allocator,
        CpuTaskScheduler& cpuScheduler,
        GpuTaskScheduler& gpuTasks,
        Perf::TimingSink& gpuTiming
    );
    GraphicsRuntime(
        GraphicsAllocator& allocator,
        CpuTaskScheduler& cpuScheduler,
        GpuTaskScheduler& gpuTasks,
        Perf::TimingSink& gpuTiming,
        Perf::TimingSink* cpuTiming
    );
    ~GraphicsRuntime()noexcept(false);


public:
    bool init(const Common::FrameData& data);
    bool createHeadlessDevice();
    bool createInstance(const InstanceParameters& params);
    bool setDebugRuntimeEnabled(bool enabled)noexcept;
    // Controls actual hardware RT extensions/features for the next logical device. Must precede instance creation.
    bool setHardwareRayTracingPolicy(HardwareRayTracingPolicy::Enum policy)noexcept;
    // Selects the native mesh-shader path when the backend supports it. Disabled configurations use the renderer's
    // compute-emulation path. Must be configured before instance creation.
    bool setNativeMeshShadersEnabled(bool enabled)noexcept;
    // Must be configured before device creation. Unsupported adapters retain the Graphics-only path.
    bool setAsyncComputeLaneEnabled(bool enabled)noexcept;
    // Must be configured before device creation. Unsupported adapters retain the Graphics/Compute copy fallback.
    bool setTransferQueueEnabled(bool enabled)noexcept;
    // Must be configured before device creation. It may expose every safe additional queue from each active
    // primary family and, with cross-family routing enabled, one auxiliary family for each class; only explicitly
    // opted-in graph tasks may route to them.
    bool setSameClassMultiQueueEnabled(bool enabled)noexcept;
    // Extends optional same-class discovery to distinct compatible Vulkan families. Individual graph tasks must
    // still explicitly accept the resulting ownership-transfer route.
    bool setCrossFamilySameClassQueueRoutingEnabled(bool enabled)noexcept;
    // Selects a Vulkan adapter enumeration index, or -1 for the backend default. Must be configured before device
    // creation so target-hardware probes can reproduce a multi-adapter route on paired processes.
    bool setAdapterIndex(i32 index)noexcept;
    // Requests HDR10/PQ presentation where the current display surface supports it. Unsupported surfaces
    // automatically retain the normal SDR swap chain. Must be configured before device creation.
    bool setHDR10OutputEnabled(bool enabled)noexcept;
    // Requests transfer-source usage for presentation images. Unsupported surfaces retain the normal swap chain
    // and report readback unavailable. Must be configured before device creation.
    bool setSwapChainReadbackEnabled(bool enabled)noexcept;
    bool setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi)noexcept;
    void setPipelineCacheDirectory(const Path& directory);
    bool setFilesystemFactory(const Filesystem::FilesystemFactory& factory);
    // Keeps the host update/event loop alive while preventing runFrame from recording, submitting, or presenting a
    // new frame, so an external capture can sample the last completed temporal frame exactly.
    void setFrameSubmissionSuspended(bool suspended)noexcept{ m_frameSubmissionSuspended = suspended; }
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
    [[nodiscard]] GpuTaskScheduler& gpuTasks()const noexcept{ return m_gpuTasks; }
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
    [[nodiscard]] bool cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim);

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

    // Pre-output layers declare independent producers and join only at final display composition.
    void setTaskGraphOutputLayerContributor(IGpuTaskGraphOutputLayerContributor* contributor)noexcept{
        m_taskGraphOutputLayerContributor = contributor;
    }
    void clearTaskGraphOutputLayerContributor(const IGpuTaskGraphOutputLayerContributor& contributor)noexcept{
        if(m_taskGraphOutputLayerContributor == &contributor)
            m_taskGraphOutputLayerContributor = nullptr;
    }
    [[nodiscard]] IGpuTaskGraphOutputLayerContributor* taskGraphOutputLayerContributor()const noexcept{
        return m_taskGraphOutputLayerContributor;
    }

    [[nodiscard]] TStringView getRendererString()const noexcept;
    [[nodiscard]] u64 getFrameIndex()const noexcept{ return m_frameIndex; }
    // Main-thread lifetime count of accepted native presentations, independent of render callbacks and GPU queries.
    // Preserved across resize, destroy/init and device recreation; this is not a monitor scan-out completion count.
    [[nodiscard]] u64 getSuccessfulPresentationCount()const noexcept{ return m_successfulPresentationCount; }
    // Last actual native present attempt, matched by the complete acquisition identity rather than a later frame.
    [[nodiscard]] const PresentationReceipt& lastPresentationReceipt()const noexcept{ return m_lastPresentationReceipt; }
    [[nodiscard]] GpuTimingRecorder& gpuTiming()noexcept{ return m_gpuTiming; }
    [[nodiscard]] const GpuTimingRecorder& gpuTiming()const noexcept{ return m_gpuTiming; }
    [[nodiscard]] bool isVsyncEnabled()const noexcept{ return m_swapChainState.vsyncEnabled; }
    [[nodiscard]] bool isHDR10OutputActive()const noexcept{ return m_swapChainState.outputMode == SwapChainOutputMode::HDR10; }
    [[nodiscard]] bool isSwapChainReadbackAvailable()const noexcept{ return m_swapChainState.swapChainReadbackAvailable; }
    void setVSyncEnabled(bool enabled)noexcept{ m_requestedVSync = enabled; }

    void getWindowDimensions(i32& width, i32& height)const noexcept;
    void getDPIScaleInfo(f32& x, f32& y)const noexcept;
    [[nodiscard]] TStringView getWindowTitle()const noexcept{ return m_windowTitle; }
    void setWindowTitle(TStringView title);
    void setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData);

    // Valid only while Graphics is preparing, rendering, or presenting one successfully acquired frame. The
    // snapshot owns the exact back buffer and its matching framebuffer so presentation consumers never infer WSI
    // identity from mutable backend state.
    [[nodiscard]] const AcquiredPresentationFrame& acquiredPresentationFrame()const noexcept{ return m_acquiredPresentationFrame; }
    [[nodiscard]] Texture* getBackBuffer(u32 index)const noexcept;
    [[nodiscard]] u32 getBackBufferCount()const noexcept;
    [[nodiscard]] Framebuffer* getFramebuffer(u32 index)const noexcept;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc)const;
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc)const;

    [[nodiscard]] BufferHandle setupBuffer(const BufferSetupDesc& desc)const;
    [[nodiscard]] TextureHandle setupTexture(const TextureSetupDesc& desc)const;
    [[nodiscard]] bool uploadTextureBatch(const TextureUploadBatchDesc& desc)const;
    // Compiles, records, and submits an isolated graph synchronously. This is the graph-owned escape hatch for a
    // standalone caller that has no renderer-owned frame graph but can still provide immutable task inputs.
    [[nodiscard]] bool submitStandaloneTaskGraph(
        void* userData,
        StandaloneTaskGraphDeclaration declareTask,
        QueueSubmissionToken& outSubmissionToken,
        GpuPhysicalQueueId requiredTerminalQueue = {},
        GpuTimingRecorder* timingRecorder = nullptr,
        GpuTimingFrameTransaction* frameTimingTransaction = nullptr
    )const;
    [[nodiscard]] MeshResource setupMesh(const MeshSetupDesc& desc)const;

    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures()const;
    [[nodiscard]] usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const;

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


private:
    void renderWithPhaseTiming(CpuTimingPhaseBatch* phaseTiming);


public:
    void notifyPointerScaleChanged()const;
    [[nodiscard]] bool shouldRenderUnfocused()const;
    bool animateRenderPresent();


private:
    bool animateRenderPresentInternal(CpuTimingPhaseBatch* phaseTiming);
    [[nodiscard]] bool resizeBackBuffer(u32 width, u32 height, bool vsyncEnabled);


private:
    GraphicsAllocator& m_allocator;
    CpuTaskScheduler& m_cpuScheduler;
    GpuTaskScheduler& m_gpuTasks;
    CpuTaskProfileLabel m_frameTaskProfileLabel;
    DeviceCreationParameters m_deviceCreationParams;
    SwapChainRuntimeState m_swapChainState;
    GpuTimingRecorder m_gpuTiming;
    // Optional and non-owning: Frame's perf Session owns this sink and outlives Graphics. It is used only by the
    // main-thread runFrame boundary; packet recording and setup workers intentionally remain outside this sink.
    Perf::TimingSink* m_cpuTiming = nullptr;
    // Frame scope registered once when the sink is attached, not per frame in runFrame().
    Perf::TimingScopeId m_frameTimingScope;

private:
    NotNullUniquePtr<Backend, BackendOwner::deleter_type> m_backend;

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
    IGpuTaskGraphOutputLayerContributor* m_taskGraphOutputLayerContributor = nullptr;
    Timer m_previousFrameTimestamp = {};
    f32 m_dpiScaleFactorX = 1.f;
    f32 m_dpiScaleFactorY = 1.f;
    f32 m_prevDPIScaleFactorX = 0.f;
    f32 m_prevDPIScaleFactorY = 0.f;

    u32 m_frameIndex = 0;
    u64 m_successfulPresentationCount = 0u;

    Vector<FramebufferHandle, Alloc::GlobalArena> m_swapChainFramebuffers;
    AcquiredPresentationFrame m_acquiredPresentationFrame;
    PresentationReceipt m_lastPresentationReceipt;

    GraphicsTString m_windowTitle;
    PointerScaleChangedCallback m_pointerScaleChangedCallback = nullptr;
    void* m_pointerScaleChangedUserData = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

