// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/frame_data.h>
#include <core/graphics/api.h>
#include <core/task/cpu/scheduler.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/render_pass_collection.h>
#include <core/graphics/runtime/standalone_service.h>


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
public:
    using PointerScaleChangedCallback = void(*)(void* userData, f32 scaleX, f32 scaleY);


private:
    using Backend = GraphicsBackend::Backend;
    using BackendOwner = GlobalUniquePtr<Backend>;
    using StandaloneGraphWorkspacesOwner = GlobalUniquePtr<GraphicsModuleDetail::StandaloneGraphWorkspacePool>;




private:
    // runFrame creates this on the stack only for a capture-enabled normal frame.
    // It stages phase durations and registers scope identities; only successful frames flush sample values.
    struct CpuTimingPhaseBatch;


public:
    GraphicsRuntime(GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler, GpuTaskScheduler& gpuTasks, Perf::TimingSink& gpuTiming)
        : GraphicsRuntime(allocator, cpuScheduler, gpuTasks, gpuTiming, nullptr)
    {}
    GraphicsRuntime(GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler, GpuTaskScheduler& gpuTasks, Perf::TimingSink& gpuTiming, Perf::TimingSink* cpuTiming);
    ~GraphicsRuntime()noexcept(false);


public:
    b8 init(const Common::FrameData& data);
    b8 setDebugRuntimeEnabled(b8 enabled)noexcept;
    // Controls actual hardware RT extensions/features for the next logical device. Must precede instance creation.
    b8 setHardwareRayTracingPolicy(HardwareRayTracingPolicy::Enum policy)noexcept;
    // Must be configured before device creation. Unsupported adapters retain the Graphics-only path.
    b8 setAsyncComputeLaneEnabled(b8 enabled)noexcept;
    // Requests HDR10/PQ presentation where the current display surface supports it.
    // Unsupported surfaces automatically retain the normal SDR swap chain.
    // Must be configured before device creation.
    b8 setHDR10OutputEnabled(b8 enabled)noexcept;
    // Requests transfer-source usage for presentation images.
    // Unsupported surfaces retain the normal swap chain and report readback unavailable.
    // Must be configured before device creation.
    b8 setSwapChainReadbackEnabled(b8 enabled)noexcept;
    b8 setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi)noexcept;
    template<typename ArenaT>
    FOUNDATION_INLINE void setPipelineCacheDirectory(const ::Path<ArenaT>& directory){ m_deviceCreationParams.pipelineCacheDirectory = directory; }
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
    void setTaskGraphPresentationContributor(IGpuTaskGraphPresentationContributor& contributor)noexcept{
        m_taskGraphPresentationContributor = &contributor;
    }

    void clearTaskGraphPresentationContributor(const IGpuTaskGraphPresentationContributor& contributor)noexcept;

    [[nodiscard]] IGpuTaskGraphPresentationContributor* taskGraphPresentationContributor()const noexcept{
        return m_taskGraphPresentationContributor;
    }

    // Pre-output layers declare independent producers and join only at final display composition.
    void setTaskGraphOutputLayerContributor(IGpuTaskGraphOutputLayerContributor* contributor)noexcept{
        m_taskGraphOutputLayerContributor = contributor;
    }
    void setTaskGraphOutputLayerContributor(IGpuTaskGraphOutputLayerContributor& contributor)noexcept{
        m_taskGraphOutputLayerContributor = &contributor;
    }

    void clearTaskGraphOutputLayerContributor(const IGpuTaskGraphOutputLayerContributor& contributor)noexcept;

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

    void getWindowDimensions(i32& width, i32& height)const noexcept;

    void getDPIScaleInfo(f32& x, f32& y)const noexcept;

    [[nodiscard]] TStringView getWindowTitle()const noexcept{ return m_windowTitle; }
    void setWindowTitle(TStringView title);
#if defined(FOUNDATION_UNICODE)
    void setWindowTitle(AStringView title);
#endif
    void setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData);

    // Valid only while Graphics is preparing, rendering, or presenting one successfully acquired frame.
    // The snapshot owns the exact back buffer and its matching framebuffer so presentation consumers never infer WSI identity from mutable backend state.
    [[nodiscard]] const AcquiredPresentationFrame& acquiredPresentationFrame()const noexcept{ return m_acquiredPresentationFrame; }
    [[nodiscard]] Texture* getBackBuffer(u32 index)const noexcept;
    [[nodiscard]] u32 getBackBufferCount()const noexcept;
    [[nodiscard]] Framebuffer* getFramebuffer(u32 index)const noexcept{
        return index < m_swapChainFramebuffers.size() ? m_swapChainFramebuffers[index].get() : nullptr;
    }

    [[nodiscard]] Expected<BufferHandle> createBuffer(const BufferDesc& desc)const;
    [[nodiscard]] Expected<TextureHandle> createTexture(const TextureDesc& desc)const;

    [[nodiscard]] const GraphicsStandaloneService& standaloneGraph()const noexcept{ return m_standaloneGraph; }

    [[nodiscard]] Expected<CooperativeVectorDeviceFeatures> queryCoopVecFeatures()const;
    [[nodiscard]] Expected<usize> getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const;

    [[nodiscard]] Expected<SwapChainTransitionTicket> backBufferResizing();
    [[nodiscard]] b8 backBufferResized();
    void displayScaleChanged();

    // Allocation/submission work before each frame's preparation. After acquire, before render(); headless callers use it to order without beginFrame().
    [[nodiscard]] b8 prepareFramePreamble();
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> render(){ return renderWithPhaseTiming(nullptr); }
    void notifyPointerScaleChanged()const;
    [[nodiscard]] b8 shouldRenderUnfocused()const;


private:
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> renderWithPhaseTiming(CpuTimingPhaseBatch* phaseTiming);
    b8 animateRenderPresentInternal(CpuTimingPhaseBatch* phaseTiming);
    [[nodiscard]] b8 registerCpuTimingScopes();
    [[nodiscard]] b8 resizeBackBuffer(u32 width, u32 height);


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
    GraphicsStandaloneService m_standaloneGraph;

    b8 m_hasPresentedFrame = false;
    b8 m_windowVisible = false;
    b8 m_windowIsInFocus = true;
    b8 m_instanceCreated = false;
    mutable b8 m_deviceRecreationRequested = false;
    b8 m_frameSubmissionSuspended = false;

    GraphicsModuleDetail::RenderPassCollection m_renderPasses;
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

