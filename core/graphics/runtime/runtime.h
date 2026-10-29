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

namespace GraphicsModuleDetail{
    class StandaloneGraphWorkspacePool;
};


class GraphicsRuntime{
private:
    using Backend = GraphicsBackend::Backend;
    using BackendOwner = GlobalUniquePtr<Backend>;
    using StandaloneGraphWorkspacesOwner = GlobalUniquePtr<GraphicsModuleDetail::StandaloneGraphWorkspacePool>;


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
        // Total upload payload size in bytes; validates the subresource upload layout.
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
        // D24S8/D32S8 need one caller-selected aspect; Vulkan copies depth/stencil from separate CPU layouts.
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;
    };

    // One immutable CPU payload per subresource. Batch copies regions into graph blobs first, so callers may free decoded memory on return.
    // A 3D mip is one region at arraySlice 0 whose depthPitch covers a single Z slice; 2D/cube uploads provide one region per array slice.
    struct TextureUploadRegion{
        const void* data = nullptr;
        usize dataSize = 0u;
        usize rowPitch = 0u;
        usize depthPitch = 0u;
        u32 arraySlice = 0u;
        u32 mipLevel = 0u;
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;
    };

    // Multi-subresource texture upload via a compiler-owned graph.
    // Explicit `finalState` (keepInitialState: == initialState).
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
        b8 hasPhysicalInitialState = false;
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
        b8 use32BitIndices = true;
    };

    struct MeshResource{
        BufferHandle vertexBuffer;
        BufferHandle indexBuffer;
        u32 vertexStride = 0;
        u32 vertexCount = 0;
        u32 indexCount = 0;
        Format::Enum indexFormat = Format::UNKNOWN;


    public:
        [[nodiscard]] b8 valid()const noexcept{
            return vertexBuffer != nullptr;
        }
    };


public:
    using PointerScaleChangedCallback = void(*)(void* userData, f32 scaleX, f32 scaleY);
    // A synchronous caller may declare one isolated graph through this callback.
    // The graph owns all native recording and submission; the callback keeps only declaration-time inputs and returns its terminal task.
    // `userData` stays caller-owned during the call.
    using StandaloneTaskGraphDeclaration = Expected<GpuTaskId>(*)(void* userData, GpuTaskGraph& graph);


private:
    // runFrame creates this on the stack only for a capture-enabled normal frame.
    // It stages phase durations and registers scope identities; only successful frames flush sample values.
    struct CpuTimingPhaseBatch;


public:
    GraphicsRuntime(GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler, GpuTaskScheduler& gpuTasks, Perf::TimingSink& gpuTiming);
    GraphicsRuntime(GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler, GpuTaskScheduler& gpuTasks, Perf::TimingSink& gpuTiming, Perf::TimingSink* cpuTiming);
    ~GraphicsRuntime()noexcept(false);


public:
    b8 init(const Common::FrameData& data);
    b8 createHeadlessDevice();
    b8 createInstance(const InstanceParameters& params);
    b8 setDebugRuntimeEnabled(b8 enabled)noexcept;
    // Controls actual hardware RT extensions/features for the next logical device. Must precede instance creation.
    b8 setHardwareRayTracingPolicy(HardwareRayTracingPolicy::Enum policy)noexcept;
    // Selects the native mesh-shader path when the backend supports it. Disabled configs use compute emulation.
    // Must be configured before instance creation.
    b8 setNativeMeshShadersEnabled(b8 enabled)noexcept;
    // Must be configured before device creation. Unsupported adapters retain the Graphics-only path.
    b8 setAsyncComputeLaneEnabled(b8 enabled)noexcept;
    // Must be configured before device creation. Unsupported adapters retain the Graphics/Compute copy fallback.
    b8 setTransferQueueEnabled(b8 enabled)noexcept;
    // Must be configured before device creation.
    // May expose extra queues per active family plus one auxiliary family per class when cross-family routing is on.
    b8 setSameClassMultiQueueEnabled(b8 enabled)noexcept;
    // Extends optional same-class discovery to distinct compatible Vulkan families.
    // Individual graph tasks must still explicitly accept the resulting ownership-transfer route.
    b8 setCrossFamilySameClassQueueRoutingEnabled(b8 enabled)noexcept;
    // Selects a Vulkan adapter index, or -1 for the backend default.
    // Must be configured before device creation so paired processes can reproduce a multi-adapter route.
    b8 setAdapterIndex(i32 index)noexcept;
    // Requests HDR10/PQ presentation where the current display surface supports it.
    // Unsupported surfaces automatically retain the normal SDR swap chain.
    // Must be configured before device creation.
    b8 setHDR10OutputEnabled(b8 enabled)noexcept;
    // Requests transfer-source usage for presentation images.
    // Unsupported surfaces retain the normal swap chain and report readback unavailable.
    // Must be configured before device creation.
    b8 setSwapChainReadbackEnabled(b8 enabled)noexcept;
    b8 setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi)noexcept;
    void setPipelineCacheDirectory(const Path& directory);
    b8 setFilesystemFactory(const Filesystem::FilesystemFactory& factory);
    // Holds the update/event loop while runFrame records nothing, so an external capture can sample the last finished frame.
    void setFrameSubmissionSuspended(b8 suspended)noexcept{ m_frameSubmissionSuspended = suspended; }
    b8 runFrame();
    // A render pass calls this when an accepted cross-queue release is unrecoverable. The generation stops; the owner recreates device/resources.
    void requestDeviceRecreation()const;
    [[nodiscard]] b8 isDeviceRecreationRequested()const noexcept{ return m_deviceRecreationRequested; }
    [[nodiscard]] b8 updateWindowState(u32 width, u32 height, b8 windowVisible, b8 windowIsInFocus);
    [[nodiscard]] b8 destroy();
    [[nodiscard]] b8 waitForIdle();
    [[nodiscard]] b8 isDeviceLost()const noexcept;


public:
    [[nodiscard]] GraphicsBackend::Device& getDevice()const noexcept;
    [[nodiscard]] GpuTaskScheduler& gpuTasks()const noexcept{ return m_gpuTasks; }
    [[nodiscard]] Expected<GraphicsVector<AdapterInfo>> enumerateAdapters();
    // Identity comes from the selected physical device, not a later enumeration. Valid after device creation.
    [[nodiscard]] Expected<AdapterInfo> getSelectedAdapterInfo()const;
    [[nodiscard]] b8 queryFeatureSupport(Feature::Enum feature)const noexcept;
    // Wave/subgroup size, or 64 when the device cannot report it. Sizes groupshared reductions and wave-intrinsic specializations.
    [[nodiscard]] u32 queryWaveLaneCount()const noexcept;

    void addRenderPassToFront(IRenderPass& pass);
    void addRenderPassToBack(IRenderPass& pass);
    void removeRenderPass(IRenderPass& pass);

    // The deferred graph claims the current swap-chain binary semaphore and attaches it to its exact terminal packet.
    // Direct/non-graph render paths receive an empty hook and retain BackendContext::present()'s fallback.
    [[nodiscard]] QueueSubmissionPreSubmitHook claimFramePresentationSignal()noexcept;
    [[nodiscard]] b8 confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim);

    // Optional overlays register here instead of coupling a renderer directly to their module.
    // The active contributor may append one final Graphics packet to a renderer-owned task graph before presentation.
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

    [[nodiscard]] b8 isVSyncEnabled()const noexcept{ return m_swapChainState.vsyncEnabled; }

    [[nodiscard]] b8 isHDR10OutputActive()const noexcept{ return m_swapChainState.outputMode == SwapChainOutputMode::HDR10; }

    [[nodiscard]] b8 isSwapChainReadbackAvailable()const noexcept{ return m_swapChainState.swapChainReadbackAvailable; }

    void setVSyncEnabled(b8 enabled)noexcept{ m_requestedVSync = enabled; }

    void getWindowDimensions(i32& width, i32& height)const noexcept;
    void getDPIScaleInfo(f32& x, f32& y)const noexcept;
    [[nodiscard]] TStringView getWindowTitle()const noexcept{ return m_windowTitle; }
    void setWindowTitle(TStringView title);
    void setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData);

    // Valid only while Graphics is preparing, rendering, or presenting one successfully acquired frame.
    // The snapshot owns the exact back buffer and its matching framebuffer so presentation consumers never infer WSI identity from mutable backend state.
    [[nodiscard]] const AcquiredPresentationFrame& acquiredPresentationFrame()const noexcept{ return m_acquiredPresentationFrame; }
    [[nodiscard]] Texture* getBackBuffer(u32 index)const noexcept;
    [[nodiscard]] u32 getBackBufferCount()const noexcept;
    [[nodiscard]] Framebuffer* getFramebuffer(u32 index)const noexcept;

    [[nodiscard]] Expected<BufferHandle> createBuffer(const BufferDesc& desc)const;
    [[nodiscard]] Expected<TextureHandle> createTexture(const TextureDesc& desc)const;

    [[nodiscard]] Expected<BufferHandle> setupBuffer(const BufferSetupDesc& desc)const;
    [[nodiscard]] Expected<TextureHandle> setupTexture(const TextureSetupDesc& desc)const;
    [[nodiscard]] b8 uploadTextureBatch(const TextureUploadBatchDesc& desc)const;
    // Compiles, records, and submits an isolated graph synchronously.
    // This is the graph-owned escape hatch for a standalone caller that has no renderer-owned frame graph but can still provide immutable task inputs.
    [[nodiscard]] Expected<QueueSubmissionToken> submitStandaloneTaskGraph(
        void* userData,
        StandaloneTaskGraphDeclaration declareTask,
        GpuPhysicalQueueId requiredTerminalQueue = {},
        GpuTimingRecorder* timingRecorder = nullptr,
        GpuTimingFrameTransaction* frameTimingTransaction = nullptr
    )const;
    [[nodiscard]] Expected<MeshResource> setupMesh(const MeshSetupDesc& desc)const;

    [[nodiscard]] Expected<CooperativeVectorDeviceFeatures> queryCoopVecFeatures()const;
    [[nodiscard]] Expected<usize> getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const;

    [[nodiscard]] Expected<SwapChainTransitionTicket> backBufferResizing();
    [[nodiscard]] b8 backBufferResized();
    void invalidateRenderPassResources();
    [[nodiscard]] b8 validateRenderPassResources();
    void displayScaleChanged();

    void animate(f64 elapsedTime);
    // Allocation/submission work before each frame's preparation. After acquire, before render(); headless callers use it to order without beginFrame().
    [[nodiscard]] b8 prepareFramePreamble();
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> render();


private:
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> renderWithPhaseTiming(CpuTimingPhaseBatch* phaseTiming);


public:
    void notifyPointerScaleChanged()const;
    [[nodiscard]] b8 shouldRenderUnfocused()const;
    b8 animateRenderPresent();


private:
    b8 animateRenderPresentInternal(CpuTimingPhaseBatch* phaseTiming);
    [[nodiscard]] b8 registerCpuTimingScopes();
    [[nodiscard]] b8 resizeBackBuffer(u32 width, u32 height, b8 vsyncEnabled);


private:
    GraphicsAllocator& m_allocator;
    CpuTaskScheduler& m_cpuScheduler;
    GpuTaskScheduler& m_gpuTasks;
    CpuTaskProfileLabel m_frameTaskProfileLabel;
    DeviceCreationParameters m_deviceCreationParams;
    SwapChainRuntimeState m_swapChainState;
    u32 m_frameIndex = 0;
    GpuTimingRecorder m_gpuTiming;
    // Optional and non-owning: Frame's perf Session owns this sink and outlives Graphics.
    // It is used only by the main-thread runFrame boundary; packet recording and setup workers intentionally remain outside this sink.
    Perf::TimingSink* m_cpuTiming = nullptr;
    // Frame and phase scopes registered once when the sink is attached, not per frame in runFrame().
    Perf::TimingScopeId m_frameTimingScope;
    Perf::TimingScopeId m_animateTimingScope;
    Perf::TimingScopeId m_beginFrameTimingScope;
    Perf::TimingScopeId m_framePreambleTimingScope;
    Perf::TimingScopeId m_prepareResourcesTimingScope;
    Perf::TimingScopeId m_prepareResourcesFailedTimingScope;
    Perf::TimingScopeId m_renderPassesTimingScope;
    Perf::TimingScopeId m_renderTimingScope;
    Perf::TimingScopeId m_presentTimingScope;
    Perf::TimingScopeId m_garbageCollectTimingScope;


private:
    NotNullUniquePtr<Backend, BackendOwner::deleter_type> m_backend;
    // Declared after the backend so its device-bound command-list caches release before the device.
    NotNullUniquePtr<GraphicsModuleDetail::StandaloneGraphWorkspacePool, StandaloneGraphWorkspacesOwner::deleter_type> m_standaloneGraphWorkspaces;

    b8 m_hasPresentedFrame = false;
    b8 m_windowVisible = false;
    b8 m_windowIsInFocus = true;
    b8 m_requestedVSync = false;
    b8 m_instanceCreated = false;
    mutable b8 m_deviceRecreationRequested = false;
    b8 m_frameSubmissionSuspended = false;

    List<NotNull<IRenderPass*>, Alloc::GlobalArena> m_renderPasses;
    // Non-owning: the contributing system unregisters before its lifetime ends.
    IGpuTaskGraphPresentationContributor* m_taskGraphPresentationContributor = nullptr;
    IGpuTaskGraphOutputLayerContributor* m_taskGraphOutputLayerContributor = nullptr;
    Timer m_previousFrameTimestamp = {};
    f32 m_dpiScaleFactorX = 1.f;
    f32 m_dpiScaleFactorY = 1.f;
    f32 m_prevDpiScaleFactorX = 0.f;
    f32 m_prevDpiScaleFactorY = 0.f;

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

