// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#include "runtime.h"
#include "runtime_internal.h"

#include <core/graphics/backend_selection.h>

#include <core/common/log.h>
#include <core/task/gpu/scheduler.h>
#include <core/telemetry/session.h>
#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_lifecycle{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScopedAcquiredPresentationFrameReset final : NoCopy{
public:
    explicit ScopedAcquiredPresentationFrameReset(AcquiredPresentationFrame& frame)
        : m_frame(frame)
    {}
    ~ScopedAcquiredPresentationFrameReset(){ m_frame = {}; }

private:
    AcquiredPresentationFrame& m_frame;
};


inline constexpr Name s_GraphicsFrameCpuTimingScope("graphics.frame");
inline constexpr Name s_GraphicsAnimateCpuTimingScope("graphics.animate");
inline constexpr Name s_GraphicsBeginFrameCpuTimingScope("graphics.begin_frame");
inline constexpr Name s_GraphicsFramePreambleCpuTimingScope("graphics.frame_preamble");
inline constexpr Name s_GraphicsRenderCpuTimingScope("graphics.render");
inline constexpr Name s_GraphicsPresentCpuTimingScope("graphics.present");
inline constexpr Name s_GraphicsGarbageCollectCpuTimingScope("graphics.garbage_collect");
inline constexpr usize s_MaxBeginFrameResizeAttempts = 3u;


[[nodiscard]] static bool CopyInstanceParameters(DeviceCreationParameters& dst, const InstanceParameters& src){
    if(src.enableDebugRuntime && !CanEnableDebugRuntime())
        return false;

    static_cast<InstanceParameters&>(dst) = src;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GraphicsRuntime::CpuTimingPhaseBatch final : NoCopy{
private:
    struct PhaseTiming{
        Name scopeName = NAME_NONE;
        f64 seconds = 0.0;
    };

    static constexpr u32 s_MaxPhaseCount = 6u;
    Array<PhaseTiming, s_MaxPhaseCount> m_phases = {};
    u32 m_phaseCount = 0u;


public:
    void stage(const Name& scopeName, const Timer begin){
        NWB_ASSERT(m_phaseCount < s_MaxPhaseCount);
        if(m_phaseCount >= s_MaxPhaseCount)
            return;

        PhaseTiming& phase = m_phases[m_phaseCount];
        phase.scopeName = scopeName;
        phase.seconds = DurationInSeconds<f64>(TimerNow(), begin);
        ++m_phaseCount;
    }

    void flush(Perf::TimingSink& timing, const u64 sampleFrameIndex)const{
        if(!timing.enabled())
            return;

        for(u32 phaseIndex = 0u; phaseIndex < m_phaseCount; ++phaseIndex){
            const PhaseTiming& phase = m_phases[phaseIndex];
            const Perf::TimingScopeId scope = timing.registerScope(phase.scopeName);
            timing.recordSample(scope, phase.seconds, sampleFrameIndex);
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsRuntime::GraphicsRuntime(
    GraphicsAllocator& allocator,
    CpuTaskScheduler& cpuScheduler,
    GpuTaskScheduler& gpuTasks,
    Perf::TimingSink& gpuTiming
)
    : GraphicsRuntime(allocator, cpuScheduler, gpuTasks, gpuTiming, nullptr)
{}

GraphicsRuntime::GraphicsRuntime(
    GraphicsAllocator& allocator,
    CpuTaskScheduler& cpuScheduler,
    GpuTaskScheduler& gpuTasks,
    Perf::TimingSink& gpuTiming,
    Perf::TimingSink* const cpuTiming
)
    : m_allocator(allocator)
    , m_cpuScheduler(cpuScheduler)
    , m_gpuTasks(gpuTasks)
    , m_tasks(cpuScheduler, cpuScheduler.registerProfileLabel(Name("cpu.task.graphics.setup")))
    , m_frameTaskProfileLabel(cpuScheduler.registerProfileLabel(Name("cpu.task.graphics.frame")))
    , m_deviceCreationParams(m_allocator.getObjectArena())
    , m_gpuTiming(m_allocator.getObjectArena(), gpuTiming)
    , m_cpuTiming(cpuTiming)
    , m_backend(MakeNotNullUnique(MakeGlobalUnique<Backend>(m_allocator.getObjectArena(), m_deviceCreationParams, m_swapChainState, m_allocator, m_cpuScheduler)))
    , m_renderPasses(m_allocator.getObjectArena())
    , m_swapChainFramebuffers(m_allocator.getObjectArena())
    , m_windowTitle(m_allocator.getObjectArena())
{
    m_deviceCreationParams.enableRayTracingExtensions = true;
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
}
GraphicsRuntime::~GraphicsRuntime()noexcept(false){
    // An active unwind is already terminal. Retire CPU captures and the borrowed device binding without native
    // callbacks or a GPU join, so the original exception reaches the application-entry boundary.
    const auto retireTasks = [this]()noexcept{
        m_tasks.drain();
        if(auto* device = m_backend->getDevice(); device && m_gpuTasks.isAttachedTo(*device)){
            if(!m_gpuTasks.detachDevice(*device))
                TerminateInvariant();
        }
    };
    if(UncaughtExceptionCount() > 0){
        retireTasks();
        return;
    }

    ScopeExit drainOnFailure(retireTasks);

    NWB_FATAL_ASSERT_MSG(
        destroy(),
        NWB_TEXT("Graphics destruction requires either a completed device join or terminal device loss")
    );
    drainOnFailure.release();
}

bool GraphicsRuntime::init(const Common::FrameData& data){
    m_acquiredPresentationFrame = {};
    m_deviceCreationParams.headlessDevice = false;
    m_hasPresentedFrame = false;

    m_swapChainState.backBufferWidth = data.width();
    m_swapChainState.backBufferHeight = data.height();
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
    m_swapChainState.outputMode = SwapChainOutputMode::SDR;

    m_backend->setPlatformFrameParam(data.frameParam());

    if(!m_instanceCreated){
        if(!m_backend->createInstance())
            return false;
        m_instanceCreated = true;
    }

    if(!m_backend->createDevice())
        return false;
    if(!m_gpuTasks.attachDevice(getDevice())){
        requestDeviceRecreation();
        return false;
    }

    m_deviceRecreationRequested = false;

    if(!m_backend->createSwapChain())
        return false;

    m_windowVisible = data.width() != 0u && data.height() != 0u;
    m_windowIsInFocus = true;
    if(!backBufferResized()){
        requestDeviceRecreation();
        return false;
    }
    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GraphicsRuntime: window device and swap chain created ({}x{})")
        , data.width()
        , data.height()
    );
    return true;
}

bool GraphicsRuntime::createHeadlessDevice(){
    m_acquiredPresentationFrame = {};
    m_deviceCreationParams.headlessDevice = true;
    m_hasPresentedFrame = false;

    if(!m_instanceCreated){
        if(!m_backend->createInstance())
            return false;
        m_instanceCreated = true;
    }

    if(!m_backend->createDevice())
        return false;
    if(!m_gpuTasks.attachDevice(getDevice())){
        requestDeviceRecreation();
        return false;
    }

    m_deviceRecreationRequested = false;

    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GraphicsRuntime: headless device created"));
    return validateRenderPassResources();
}

bool GraphicsRuntime::createInstance(const InstanceParameters& params){
    if(!__hidden_graphics_lifecycle::CopyInstanceParameters(m_deviceCreationParams, params)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: debug runtime is only available in non-final builds"));
        return false;
    }

    if(!m_backend->createInstance())
        return false;

    m_instanceCreated = true;
    return true;
}

bool GraphicsRuntime::setDebugRuntimeEnabled(bool enabled){
    if(enabled && !CanEnableDebugRuntime())
        return false;
    if(m_instanceCreated && m_deviceCreationParams.enableDebugRuntime != enabled)
        return false;

    m_deviceCreationParams.enableDebugRuntime = enabled;
    return true;
}

bool GraphicsRuntime::setNativeMeshShadersEnabled(const bool enabled){
    if(m_instanceCreated)
        return false;

    m_deviceCreationParams.enableNativeMeshShaders = enabled;
    return true;
}

bool GraphicsRuntime::setAsyncComputeLaneEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableAsyncComputeLane = enabled;
    return true;
}

bool GraphicsRuntime::setTransferQueueEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableTransferQueue = enabled;
    return true;
}

bool GraphicsRuntime::setSameClassMultiQueueEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSameClassMultiQueue = enabled;
    return true;
}

bool GraphicsRuntime::setCrossFamilySameClassQueueRoutingEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableCrossFamilySameClassQueueRouting = enabled;
    return true;
}

bool GraphicsRuntime::setAdapterIndex(const i32 index){
    if(index < -1 || m_backend->getDevice())
        return false;

    m_deviceCreationParams.adapterIndex = index;
    return true;
}

bool GraphicsRuntime::setHDR10OutputEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableHDR10Output = enabled;
    return true;
}

bool GraphicsRuntime::setSwapChainReadbackEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSwapChainReadback = enabled;
    return true;
}

bool GraphicsRuntime::setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi){
    if(!abi.valid() || m_backend->getDevice())
        return false;

    m_deviceCreationParams.bindlessHeapAbi = abi;
    return true;
}

void GraphicsRuntime::setPipelineCacheDirectory(const Path& directory){
    m_deviceCreationParams.pipelineCacheDirectory = directory;
}

bool GraphicsRuntime::setFilesystemFactory(const Filesystem::FilesystemFactory& factory){
    if(m_backend->getDevice())
        return false;
    m_deviceCreationParams.filesystemFactory = factory;
    return true;
}

void GraphicsRuntime::requestDeviceRecreation()const{
    if(m_deviceRecreationRequested)
        return;

    m_deviceRecreationRequested = true;
    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: device recreation requested; ending the current graphics session before another submission."));
}

bool GraphicsRuntime::updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    if(m_deviceRecreationRequested)
        return false;
    if(auto* device = m_backend->getDevice(); device && device->requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    m_windowVisible = windowVisible;
    m_windowIsInFocus = windowIsInFocus;

    if(!m_windowVisible)
        return true;

    if(width == 0 || height == 0){
        m_windowVisible = false;
        return true;
    }

    if(
        static_cast<i32>(m_swapChainState.backBufferWidth) != static_cast<i32>(width)
        || static_cast<i32>(m_swapChainState.backBufferHeight) != static_cast<i32>(height)
        || m_swapChainState.vsyncEnabled != m_requestedVSync
    ){
        if(!resizeBackBuffer(width, height, m_requestedVSync))
            return false;
    }

    m_swapChainState.vsyncEnabled = m_requestedVSync;
    return true;
}

bool GraphicsRuntime::destroy(){
    waitTasks();

    SwapChainTransitionTicket transitionTicket;
    if(!m_backend->prepareSwapChainTransition(SwapChainTransitionKind::Destroy, transitionTicket)){
        m_deviceRecreationRequested = true;
        return false;
    }

    m_acquiredPresentationFrame = {};
    invalidateRenderPassResources();
    m_renderPasses.clear();
    m_gpuTiming.resetQueries();

    m_swapChainFramebuffers.clear();
    GraphicsBackend::Device* const device = m_backend->getDevice();
    const bool detachScheduler = device && m_gpuTasks.isAttachedTo(*device);
    if(detachScheduler && !m_gpuTasks.detachDevice(*device)){
        requestDeviceRecreation();
        return false;
    }
    if(!m_backend->commitDestroy(Move(transitionTicket))){
        // Commit rejection retains the backend device. Restore only our binding; failed initialization must never
        // detach or replace a scheduler belonging to a different runtime.
        if(detachScheduler && m_backend->getDevice() == device && !m_gpuTasks.attachDevice(*device))
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to restore the GPU scheduler after device teardown was rejected."));
        requestDeviceRecreation();
        return false;
    }
    m_instanceCreated = false;
    m_deviceRecreationRequested = false;
    return true;
}

bool GraphicsRuntime::waitForIdle(){
    if(auto* device = m_backend->getDevice()){
        if(m_gpuTasks.isAttachedTo(*device))
            return m_gpuTasks.wait();
        // A failed initialization may have created this device while the injected scheduler belongs elsewhere.
        return device->waitForIdle();
    }
    return true;
}
bool GraphicsRuntime::isDeviceLost()const noexcept{
    if(const auto* device = m_backend->getDevice())
        return device->isDeviceLost();
    return false;
}

GraphicsBackend::Device& GraphicsRuntime::getDevice()const noexcept{
    GraphicsBackend::Device* const device = m_backend->getDevice();
    NWB_ASSERT(device);
    return *device;
}

bool GraphicsRuntime::enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters){
    return m_backend->enumerateAdapters(outAdapters);
}

bool GraphicsRuntime::getSelectedAdapterInfo(AdapterInfo& outAdapter)const{
    return m_backend->getSelectedAdapterInfo(outAdapter);
}

QueueSubmissionPreSubmitHook GraphicsRuntime::claimFramePresentationSignal()noexcept{
    return m_backend->claimFramePresentationSignal();
}

bool GraphicsRuntime::confirmFramePresentationSignal(
    const QueueSubmissionPreSubmitHook& claim,
    const QueueSubmissionToken& token
)noexcept{
    return m_backend->confirmFramePresentationSignal(claim, token);
}

bool GraphicsRuntime::cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim){
    return m_backend->cancelFramePresentationSignal(claim);
}

void GraphicsRuntime::addRenderPassToFront(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_front(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: front render pass failed to validate resources after registration"));
}

void GraphicsRuntime::addRenderPassToBack(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_back(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: back render pass failed to validate resources after registration"));
}

void GraphicsRuntime::removeRenderPass(IRenderPass& pass){
    waitTasks();
    const bool deviceIdle = waitForIdle();
    GraphicsBackend::Device* const device = m_backend->getDevice();
    NWB_FATAL_ASSERT_MSG(
        deviceIdle || (device && device->isDeviceLost()),
        NWB_TEXT("Render-pass removal requires either a completed device join or terminal device loss")
    );

    pass.invalidateResources();
    m_renderPasses.remove(&pass);
}

const tchar* GraphicsRuntime::getRendererString()const{
    return m_backend->getRendererString();
}

GraphicsAPI::Enum GraphicsRuntime::getGraphicsAPI()const{
    return GraphicsBackend::s_Api;
}

void GraphicsRuntime::reportLiveObjects()const{
    m_backend->reportLiveObjects();
}

void GraphicsRuntime::getWindowDimensions(i32& width, i32& height)const{
    width = m_swapChainState.backBufferWidth;
    height = m_swapChainState.backBufferHeight;
}

void GraphicsRuntime::getDPIScaleInfo(f32& x, f32& y)const{
    x = m_dpiScaleFactorX;
    y = m_dpiScaleFactorY;
}

void GraphicsRuntime::setWindowTitle(NotNull<const tchar*> title){
    if(m_windowTitle == title.get())
        return;

    m_windowTitle = title.get();
}

void GraphicsRuntime::setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData){
    m_pointerScaleChangedCallback = callback;
    m_pointerScaleChangedUserData = userData;
    notifyPointerScaleChanged();
}

Texture* GraphicsRuntime::getBackBuffer(u32 index)const{
    return m_backend->getBackBuffer(index);
}

u32 GraphicsRuntime::getBackBufferCount()const{
    return m_backend->getBackBufferCount();
}

Framebuffer* GraphicsRuntime::getFramebuffer(u32 index)const{
    if(index < m_swapChainFramebuffers.size())
        return m_swapChainFramebuffers[index].get();
    return nullptr;
}

BufferHandle GraphicsRuntime::createBuffer(const BufferDesc& desc)const{
    return getDevice().createBuffer(desc);
}

TextureHandle GraphicsRuntime::createTexture(const TextureDesc& desc)const{
    return getDevice().createTexture(desc);
}

bool GraphicsRuntime::backBufferResizing(SwapChainTransitionTicket& outTicket){
    waitTasks();
    if(!m_backend->prepareSwapChainTransition(SwapChainTransitionKind::Resize, outTicket)){
        requestDeviceRecreation();
        return false;
    }

    m_acquiredPresentationFrame = {};
    invalidateRenderPassResources();
    m_swapChainFramebuffers.clear();

    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResizing();
    return true;
}

bool GraphicsRuntime::resizeBackBuffer(
    const u32 width,
    const u32 height,
    const bool vsyncEnabled
){
    SwapChainTransitionTicket transitionTicket;
    if(!backBufferResizing(transitionTicket))
        return false;

    m_swapChainState.backBufferWidth = width;
    m_swapChainState.backBufferHeight = height;
    m_swapChainState.vsyncEnabled = vsyncEnabled;
    if(!m_backend->commitSwapChainResize(Move(transitionTicket))){
        requestDeviceRecreation();
        return false;
    }
    if(!backBufferResized()){
        requestDeviceRecreation();
        return false;
    }
    return true;
}

bool GraphicsRuntime::backBufferResized(){
    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);

    const u32 backBufferCount = getBackBufferCount();
    m_swapChainFramebuffers.clear();
    m_swapChainFramebuffers.reserve(backBufferCount);
    for(u32 index = 0; index < backBufferCount; ++index){
        FramebufferHandle framebuffer = getDevice().createFramebuffer(
            FramebufferDesc().addColorAttachment(getBackBuffer(index))
        );
        if(!framebuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to rebuild swap-chain framebuffer {}"), index);
            m_swapChainFramebuffers.clear();
            invalidateRenderPassResources();
            return false;
        }
        m_swapChainFramebuffers.push_back(Move(framebuffer));
    }

    if(!validateRenderPassResources()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: one or more render passes failed to validate resources after back buffer resize"));
        m_swapChainFramebuffers.clear();
        invalidateRenderPassResources();
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("GraphicsRuntime: Back buffer resized to {}x{}"), m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight);
    return true;
}

void GraphicsRuntime::invalidateRenderPassResources(){
    for(auto* renderPass : m_renderPasses)
        renderPass->invalidateResources();
}

bool GraphicsRuntime::validateRenderPassResources(){
    bool valid = true;
    for(auto* renderPass : m_renderPasses){
        valid =
            renderPass->validateResources(
                m_swapChainState.backBufferWidth,
                m_swapChainState.backBufferHeight,
                m_deviceCreationParams.swapChainSampleCount
            )
            && valid
        ;
    }
    return valid;
}

void GraphicsRuntime::displayScaleChanged(){
    notifyPointerScaleChanged();

    for(auto* renderPass : m_renderPasses)
        renderPass->displayScaleChanged(m_dpiScaleFactorX, m_dpiScaleFactorY);
}

void GraphicsRuntime::animate(f64 elapsedTime){
    for(auto* renderPass : m_renderPasses){
        renderPass->animate(static_cast<f32>(elapsedTime));
        renderPass->setLatewarpOptions();
    }
}

bool GraphicsRuntime::prepareFramePreamble(){
    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    m_gpuTiming.collect(device, m_frameIndex);
    m_gpuTiming.beginFrame(m_frameIndex);

    // Materialize and reset every declared timer-query pool before any pass preparation can record a timestamp.
    // Per-pass preparation submits skinning and shadow packets, so this must remain ahead of it as well as every
    // later render packet on the same GPU timeline.
    if(m_gpuTiming.collectionActive()){
        if(!m_gpuTiming.materializeRequestedQueries(device))
            NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: failed to materialize one or more requested GPU-timing query pools"));

        // Do not allow render-pass scopes to reuse a prior frame's reset if graph recording or submission fails.
        // The task's accepted callback reenables only the pools covered by the successfully submitted packet.
        m_gpuTiming.discardFrameReset();
        if(!GraphicsModuleDetail::SubmitGraphOwnedFrameTimingReset(
            *this,
            m_allocator.getObjectArena(),
            m_gpuTiming
        )){
            m_gpuTiming.discardFrameReset();
            NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: failed to submit the graph-owned frame GPU-timing reset packet"));
        }
    }

    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    return true;
}

void GraphicsRuntime::render(){
    Framebuffer* const framebuffer = m_acquiredPresentationFrame.framebuffer.get();
    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return;
    }

    // Each task pairs preparation with rendering. The dependency chain preserves skinning publication before the
    // next pass prepares dependent meshes, while main-thread execution preserves the platform and UI contracts.
    CpuTaskScope frameTasks(m_cpuScheduler, m_frameTaskProfileLabel);
    const CpuTaskOptions options{
        .priority = CpuTaskPriority::Critical,
        .target = CpuTaskTarget::MainThread,
    };
    TaskHandle previous;
    for(auto* renderPass : m_renderPasses){
        previous = frameTasks.submit([this, &device, framebuffer, renderPass](){
            if(m_deviceRecreationRequested || device.requiresRecreation()){
                if(device.requiresRecreation())
                    requestDeviceRecreation();
                return;
            }
            if(!renderPass->prepareResources(framebuffer)){
                NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: render pass skipped after resource preparation failed"));
                return;
            }

            renderPass->render(framebuffer);

            // Later task callbacks observe this request before touching the invalidated device generation.
            if(device.requiresRecreation())
                requestDeviceRecreation();
        }, options, previous.valid() ? &previous : nullptr, previous.valid() ? 1u : 0u);
        if(!previous.valid())
            throw RuntimeException("CPU task scheduler rejected a render pass");
    }
    frameTasks.wait();
}

void GraphicsRuntime::updateAverageFrameTime(f64 elapsedTime){
    m_frameTimeSum += elapsedTime;
    m_numberOfAccumulatedFrames += 1;

    if(m_frameTimeSum > m_averageTimeUpdateInterval && m_numberOfAccumulatedFrames > 0){
        m_averageFrameTime = m_frameTimeSum / static_cast<f64>(m_numberOfAccumulatedFrames);
        m_numberOfAccumulatedFrames = 0;
        m_frameTimeSum = 0.0;
    }
}

void GraphicsRuntime::notifyPointerScaleChanged()const{
    if(!m_pointerScaleChangedCallback)
        return;

    if(m_deviceCreationParams.supportExplicitDisplayScaling)
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, 1.f, 1.f);
    else
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, m_dpiScaleFactorX, m_dpiScaleFactorY);
}

bool GraphicsRuntime::shouldRenderUnfocused()const{
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
}

bool GraphicsRuntime::runFrame(){
    // This deliberately spans the complete logical graphics frame: normal presentation, headless no-window work,
    // and submission-suspended maintenance. Detailed phase scopes sit within this aggregate, so consumers must not
    // sum them with it. Record only a successful call so a failed frame can never be published with a later one.
    const bool recordFrameTiming = m_cpuTiming && m_cpuTiming->enabled();
    Perf::TimingScopeId frameTimingScope;
    Timer frameTimingBegin;
    const u64 sampleFrameIndex = m_frameIndex;
    if(recordFrameTiming){
        frameTimingScope = m_cpuTiming->registerScope(__hidden_graphics_lifecycle::s_GraphicsFrameCpuTimingScope);
        frameTimingBegin = TimerNow();
    }

    if(!m_frameSubmissionSuspended){
        if(!recordFrameTiming)
            return animateRenderPresent();

        CpuTimingPhaseBatch phaseTiming;
        const bool rendered = animateRenderPresentInternal(&phaseTiming);
        if(rendered){
            m_cpuTiming->recordSample(
                frameTimingScope,
                DurationInSeconds<f64>(TimerNow(), frameTimingBegin),
                sampleFrameIndex
            );
            phaseTiming.flush(*m_cpuTiming, sampleFrameIndex);
        }
        return rendered;
    }

    // Do not let a capture hold hide a device-loss/recreation request. No backend beginFrame, render, or present call
    // is made here, so the last completed frame stays on screen while the platform loop remains responsive.
    if(m_deviceRecreationRequested)
        return false;

    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    YieldThread();
    if(recordFrameTiming)
        m_cpuTiming->recordSample(
            frameTimingScope,
            DurationInSeconds<f64>(TimerNow(), frameTimingBegin),
            sampleFrameIndex
        );
    return true;
}

bool GraphicsRuntime::animateRenderPresent(){
    return animateRenderPresentInternal(nullptr);
}

bool GraphicsRuntime::animateRenderPresentInternal(CpuTimingPhaseBatch* const phaseTiming){
    m_acquiredPresentationFrame = {};
    if(m_deviceRecreationRequested)
        return false;

    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    Timer now = TimerNow();
    const f64 elapsedTime = DurationInSeconds<f64>(now, m_previousFrameTimestamp);
    const bool shouldBootstrapWindowPresentation = !m_hasPresentedFrame;

    if(m_windowVisible && (m_windowIsInFocus || shouldRenderUnfocused() || shouldBootstrapWindowPresentation)){
        if(m_prevDPIScaleFactorX != m_dpiScaleFactorX || m_prevDPIScaleFactorY != m_dpiScaleFactorY){
            displayScaleChanged();
            m_prevDPIScaleFactorX = m_dpiScaleFactorX;
            m_prevDPIScaleFactorY = m_dpiScaleFactorY;
        }

        Timer animateBegin;
        if(phaseTiming)
            animateBegin = TimerNow();
        animate(elapsedTime);
        if(phaseTiming)
            phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsAnimateCpuTimingScope, animateBegin);

        if(m_frameIndex > 0 || !m_skipRenderOnFirstFrame){
            Timer beginFrameBegin;
            if(phaseTiming)
                beginFrameBegin = TimerNow();
            BeginFrameResult beginFrameResult;
            for(usize attempt = 0u; attempt < __hidden_graphics_lifecycle::s_MaxBeginFrameResizeAttempts; ++attempt){
                beginFrameResult = m_backend->beginFrame();
                if(beginFrameResult.status != BeginFrameStatus::ResizeRequired)
                    break;
                if(
                    beginFrameResult.suggestedWidth == 0u
                    || beginFrameResult.suggestedHeight == 0u
                    || !resizeBackBuffer(
                        beginFrameResult.suggestedWidth,
                        beginFrameResult.suggestedHeight,
                        m_requestedVSync
                    )
                )
                    break;
            }
            if(phaseTiming)
                phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsBeginFrameCpuTimingScope, beginFrameBegin);
            if(!beginFrameResult.acquired()){
                if(beginFrameResult.status == BeginFrameStatus::ResizeRequired)
                    NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: swap-chain resize retries were exhausted; requesting device recreation."));
                else
                    NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: failed to acquire a presentation frame; requesting device recreation."));
                requestDeviceRecreation();
                return false;
            }
            AcquiredBackBuffer acquiredBackBuffer = Move(beginFrameResult.backBuffer);
            if(acquiredBackBuffer.valid()){
                const u32 acquiredBackBufferIndex = acquiredBackBuffer.index;
                if(
                    acquiredBackBufferIndex >= m_swapChainFramebuffers.size()
                    || !m_swapChainFramebuffers[acquiredBackBufferIndex]
                ){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: acquired swap-chain image has no matching framebuffer; requesting recreation."));
                    if(!m_backend->abandonAcquiredFrame())
                        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to drain the abandoned acquired-frame wait; device teardown is required."));
                    requestDeviceRecreation();
                    return false;
                }

                Framebuffer* const acquiredFramebuffer = m_swapChainFramebuffers[acquiredBackBufferIndex].get();
                const FramebufferDesc& acquiredFramebufferDesc = acquiredFramebuffer->getDescription();
                if(
                    acquiredFramebufferDesc.colorAttachments.size() != 1u
                    || acquiredFramebufferDesc.colorAttachments[0].texture != acquiredBackBuffer.texture.get()
                ){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: acquired swap-chain image mismatches its framebuffer attachment; requesting recreation."));
                    if(!m_backend->abandonAcquiredFrame())
                        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to drain the abandoned acquired-frame wait; device teardown is required."));
                    requestDeviceRecreation();
                    return false;
                }

                m_acquiredPresentationFrame = {
                    .backBuffer = Move(acquiredBackBuffer),
                    .framebuffer = m_swapChainFramebuffers[acquiredBackBufferIndex],
                };
                const __hidden_graphics_lifecycle::ScopedAcquiredPresentationFrameReset acquiredFrameReset(m_acquiredPresentationFrame);

                Timer framePreambleBegin;
                if(phaseTiming)
                    framePreambleBegin = TimerNow();
                const bool preamblePrepared = prepareFramePreamble();
                if(phaseTiming)
                    phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsFramePreambleCpuTimingScope, framePreambleBegin);
                if(!preamblePrepared){
                    // prepareFramePreamble() returns false only after the device requires recreation. Do not issue
                    // recovery GPU work; required device teardown owns the unresolved acquired image and synchronization.
                    requestDeviceRecreation();
                    return false;
                }

                Timer renderBegin;
                if(phaseTiming)
                    renderBegin = TimerNow();
                render();
                if(phaseTiming)
                    phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsRenderCpuTimingScope, renderBegin);

                if(m_deviceRecreationRequested || device.requiresRecreation()){
                    if(device.requiresRecreation())
                        requestDeviceRecreation();
                    else if(!m_backend->abandonAcquiredFrame())
                        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to quarantine the aborted acquired frame; device teardown is required."));
                    return false;
                }

                Timer presentBegin;
                if(phaseTiming)
                    presentBegin = TimerNow();
                const bool presented = m_backend->present();
                if(phaseTiming)
                    phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsPresentCpuTimingScope, presentBegin);
                if(!presented){
                    // A consumed presentation already cleared acquisition and makes abandonment a no-op. Every
                    // healthy unconsumed failure is drained and quarantined before recreation.
                    if(!device.requiresRecreation() && !m_backend->abandonAcquiredFrame())
                        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to quarantine the unpresented acquired frame; device teardown is required."));
                    requestDeviceRecreation();
                    return false;
                }

                if(device.requiresRecreation()){
                    requestDeviceRecreation();
                    return false;
                }

                m_hasPresentedFrame = true;
            }
        }
    }

    YieldThread();

    Timer garbageCollectionBegin;
    if(phaseTiming)
        garbageCollectionBegin = TimerNow();
    device.runGarbageCollection();
    if(phaseTiming)
        phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsGarbageCollectCpuTimingScope, garbageCollectionBegin);
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    updateAverageFrameTime(elapsedTime);
    m_previousFrameTimestamp = now;

    ++m_frameIndex;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

