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
    b8 setHardwareRayTracingPolicy(HardwareRayTracingPolicy::Enum policy)noexcept;
    b8 setAsyncComputeLaneEnabled(b8 enabled)noexcept;
    b8 setHDR10OutputEnabled(b8 enabled)noexcept;
    b8 setSwapChainReadbackEnabled(b8 enabled)noexcept;
    b8 setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi)noexcept;
    template<typename ArenaT>
    FOUNDATION_INLINE void setPipelineCacheDirectory(const ::Path<ArenaT>& directory){ m_deviceCreationParams.pipelineCacheDirectory = directory; }
    b8 setFilesystemFactory(const Filesystem::FilesystemFactory& factory);
    void setFrameSubmissionSuspended(b8 suspended)noexcept{ m_frameSubmissionSuspended = suspended; }
    b8 runFrame();
    void requestDeviceRecreation()const;
    [[nodiscard]] b8 isDeviceRecreationRequested()const noexcept{ return m_deviceRecreationRequested; }
    [[nodiscard]] b8 updateWindowState(u32 width, u32 height, b8 windowVisible, b8 windowIsInFocus);
    [[nodiscard]] b8 destroy();
    [[nodiscard]] b8 waitForIdle();
    [[nodiscard]] b8 isDeviceLost()const noexcept;


public:
    [[nodiscard]] GraphicsAllocator& allocator()const noexcept{ return m_allocator; }
    [[nodiscard]] GraphicsBackend::Device& getDevice()const noexcept;
    [[nodiscard]] GpuTaskScheduler& gpuTasks()const noexcept{ return m_gpuTasks; }
    [[nodiscard]] Expected<GraphicsVector<AdapterInfo>> enumerateAdapters();
    [[nodiscard]] Expected<AdapterInfo> getSelectedAdapterInfo()const;
    [[nodiscard]] b8 queryFeatureSupport(Feature::Enum feature)const noexcept;
    [[nodiscard]] u32 queryWaveLaneCount()const noexcept;

    void addRenderPassToBack(IRenderPass& pass);
    void removeRenderPass(IRenderPass& pass);

    [[nodiscard]] QueueSubmissionPreSubmitHook claimFramePresentationSignal()noexcept;
    [[nodiscard]] b8 confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim);

    void setTaskGraphPresentationContributor(IGpuTaskGraphPresentationContributor& contributor)noexcept{
        m_taskGraphPresentationContributor = &contributor;
    }

    void clearTaskGraphPresentationContributor(const IGpuTaskGraphPresentationContributor& contributor)noexcept;

    [[nodiscard]] Optional<NotNull<IGpuTaskGraphPresentationContributor*>> borrowTaskGraphPresentationContributor()const noexcept{
        if(m_taskGraphPresentationContributor)
            return MakeNotNull(m_taskGraphPresentationContributor);
        return {};
    }

    void setTaskGraphOutputLayerContributor(IGpuTaskGraphOutputLayerContributor& contributor)noexcept{
        m_taskGraphOutputLayerContributor = &contributor;
    }

    void clearTaskGraphOutputLayerContributor(const IGpuTaskGraphOutputLayerContributor& contributor)noexcept;

    [[nodiscard]] Optional<NotNull<IGpuTaskGraphOutputLayerContributor*>> borrowTaskGraphOutputLayerContributor()const noexcept{
        if(m_taskGraphOutputLayerContributor)
            return MakeNotNull(m_taskGraphOutputLayerContributor);
        return {};
    }

    [[nodiscard]] TStringView getRendererString()const noexcept;
    [[nodiscard]] u64 getFrameIndex()const noexcept{ return m_frameIndex; }
    [[nodiscard]] u64 getSuccessfulPresentationCount()const noexcept{ return m_successfulPresentationCount; }
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
    Perf::TimingSink* m_cpuTiming = nullptr;
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
    NotNullUniquePtr<GraphicsModuleDetail::StandaloneGraphWorkspacePool, StandaloneGraphWorkspacesOwner::deleter_type> m_standaloneGraphWorkspaces;
    GraphicsStandaloneService m_standaloneGraph;

    b8 m_hasPresentedFrame = false;
    b8 m_windowVisible = false;
    b8 m_windowIsInFocus = true;
    b8 m_instanceCreated = false;
    mutable b8 m_deviceRecreationRequested = false;
    b8 m_frameSubmissionSuspended = false;

    GraphicsModuleDetail::RenderPassCollection m_renderPasses;
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

