// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "module_internal.h"

#include "backend_selection.h"

#include <core/common/log.h>
#include <core/telemetry/session.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_lifecycle{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_GraphicsFrameCpuTimingScope("graphics.frame");
inline constexpr Name s_GraphicsAnimateCpuTimingScope("graphics.animate");
inline constexpr Name s_GraphicsBeginFrameCpuTimingScope("graphics.begin_frame");
inline constexpr Name s_GraphicsFramePreambleCpuTimingScope("graphics.frame_preamble");
inline constexpr Name s_GraphicsRenderCpuTimingScope("graphics.render");
inline constexpr Name s_GraphicsPresentCpuTimingScope("graphics.present");
inline constexpr Name s_GraphicsGarbageCollectCpuTimingScope("graphics.garbage_collect");


[[nodiscard]] static bool CopyInstanceParameters(DeviceCreationParameters& dst, const InstanceParameters& src){
    if(src.enableDebugRuntime && !CanEnableDebugRuntime())
        return false;

    static_cast<InstanceParameters&>(dst) = src;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Graphics::CpuTimingPhaseBatch final : NoCopy{
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


void Graphics::BackBufferResizingCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResizing();
}

void Graphics::BackBufferResizedCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResized();
}


Graphics::Graphics(
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool,
    Alloc::JobSystem& jobSystem,
    Perf::TimingSink& gpuTiming
)
    : Graphics(allocator, threadPool, jobSystem, gpuTiming, nullptr)
{}

Graphics::Graphics(
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool,
    Alloc::JobSystem& jobSystem,
    Perf::TimingSink& gpuTiming,
    Perf::TimingSink* const cpuTiming
)
    : m_allocator(allocator)
    , m_threadPool(threadPool)
    , m_jobSystem(jobSystem)
    , m_deviceCreationParams(m_allocator.getObjectArena())
    , m_gpuTiming(m_allocator.getObjectArena(), gpuTiming)
    , m_cpuTiming(cpuTiming)
    , m_backend(MakeNotNullUnique(MakeGlobalUnique<Backend>(m_allocator.getObjectArena(), m_deviceCreationParams, m_swapChainState, m_allocator, m_threadPool)))
    , m_renderPasses(m_allocator.getObjectArena())
    , m_swapChainFramebuffers(m_allocator.getObjectArena())
    , m_windowTitle(m_allocator.getObjectArena())
{
    m_deviceCreationParams.enableRayTracingExtensions = true;
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
}
Graphics::~Graphics(){
    destroy();
}

bool Graphics::init(const Common::FrameData& data){
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

    m_deviceRecreationRequested = false;

    if(!m_backend->createSwapChain())
        return false;

    m_swapChainState.backBufferWidth = 0;
    m_swapChainState.backBufferHeight = 0;
    updateWindowState(data.width(), data.height(), true, true);
    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Graphics: window device and swap chain created ({}x{})")
        , data.width()
        , data.height()
    );
    return validateRenderPassResources();
}

bool Graphics::createHeadlessDevice(){
    m_deviceCreationParams.headlessDevice = true;
    m_hasPresentedFrame = false;

    if(!m_instanceCreated){
        if(!m_backend->createInstance())
            return false;
        m_instanceCreated = true;
    }

    if(!m_backend->createDevice())
        return false;

    m_deviceRecreationRequested = false;

    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Graphics: headless device created"));
    return validateRenderPassResources();
}

bool Graphics::createInstance(const InstanceParameters& params){
    if(!__hidden_graphics_lifecycle::CopyInstanceParameters(m_deviceCreationParams, params)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: debug runtime is only available in non-final builds"));
        return false;
    }

    if(!m_backend->createInstance())
        return false;

    m_instanceCreated = true;
    return true;
}

bool Graphics::setDebugRuntimeEnabled(bool enabled){
    if(enabled && !CanEnableDebugRuntime())
        return false;
    if(m_instanceCreated && m_deviceCreationParams.enableDebugRuntime != enabled)
        return false;

    m_deviceCreationParams.enableDebugRuntime = enabled;
    return true;
}

bool Graphics::setAsyncComputeLaneEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableAsyncComputeLane = enabled;
    return true;
}

bool Graphics::setTransferQueueEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableTransferQueue = enabled;
    return true;
}

bool Graphics::setSameClassMultiQueueEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSameClassMultiQueue = enabled;
    return true;
}

bool Graphics::setCrossFamilySameClassQueueRoutingEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableCrossFamilySameClassQueueRouting = enabled;
    return true;
}

bool Graphics::setAdapterIndex(const i32 index){
    if(index < -1 || m_backend->getDevice())
        return false;

    m_deviceCreationParams.adapterIndex = index;
    return true;
}

bool Graphics::setHDR10OutputEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableHDR10Output = enabled;
    return true;
}

bool Graphics::setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi){
    if(!abi.valid() || m_backend->getDevice())
        return false;

    m_deviceCreationParams.bindlessHeapAbi = abi;
    return true;
}

void Graphics::setPipelineCacheDirectory(const Path& directory){
    m_deviceCreationParams.pipelineCacheDirectory = directory;
}

void Graphics::requestDeviceRecreation()const{
    if(m_deviceRecreationRequested)
        return;

    m_deviceRecreationRequested = true;
    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Graphics: device recreation requested; ending the current graphics session before another submission."));
}

void Graphics::updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    if(m_deviceRecreationRequested)
        return;
    if(auto* device = m_backend->getDevice(); device && device->isDeviceLost()){
        requestDeviceRecreation();
        return;
    }

    m_windowVisible = windowVisible;
    m_windowIsInFocus = windowIsInFocus;

    if(!m_windowVisible)
        return;

    if(width == 0 || height == 0){
        m_windowVisible = false;
        return;
    }

    if(
        static_cast<i32>(m_swapChainState.backBufferWidth) != static_cast<i32>(width)
        || static_cast<i32>(m_swapChainState.backBufferHeight) != static_cast<i32>(height)
        || m_swapChainState.vsyncEnabled != m_requestedVSync
    ){
        backBufferResizing();

        m_swapChainState.backBufferWidth = width;
        m_swapChainState.backBufferHeight = height;
        m_swapChainState.vsyncEnabled = m_requestedVSync;

        m_backend->resizeSwapChain();
        backBufferResized();
    }

    m_swapChainState.vsyncEnabled = m_requestedVSync;
}

void Graphics::destroy(){
    waitAllJobs();
    waitForIdle();

    invalidateRenderPassResources();
    m_renderPasses.clear();
    m_gpuTiming.resetQueries();

    m_swapChainFramebuffers.clear();
    m_backend->destroy();
    m_instanceCreated = false;
    m_deviceRecreationRequested = false;
}

void Graphics::waitForIdle(){
    if(auto* device = m_backend->getDevice())
        device->waitForIdle();
}

GraphicsBackend::Device& Graphics::getDevice()const noexcept{
    GraphicsBackend::Device* const device = m_backend->getDevice();
    NWB_ASSERT(device);
    return *device;
}

bool Graphics::enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters){
    return m_backend->enumerateAdapters(outAdapters);
}

bool Graphics::getSelectedAdapterInfo(AdapterInfo& outAdapter)const{
    return m_backend->getSelectedAdapterInfo(outAdapter);
}

QueueSubmissionPreSubmitHook Graphics::claimFramePresentationSignal()noexcept{
    return m_backend->claimFramePresentationSignal();
}

bool Graphics::confirmFramePresentationSignal(const QueueSubmissionToken& token)noexcept{
    return m_backend->confirmFramePresentationSignal(token);
}

void Graphics::cancelFramePresentationSignal()noexcept{
    m_backend->cancelFramePresentationSignal();
}

void Graphics::addRenderPassToFront(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_front(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("Graphics: front render pass failed to validate resources after registration"));
}

void Graphics::addRenderPassToBack(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_back(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("Graphics: back render pass failed to validate resources after registration"));
}

void Graphics::removeRenderPass(IRenderPass& pass){
    waitAllJobs();
    waitForIdle();

    pass.invalidateResources();
    m_renderPasses.remove(&pass);
}

const tchar* Graphics::getRendererString()const{
    return m_backend->getRendererString();
}

GraphicsAPI::Enum Graphics::getGraphicsAPI()const{
    return GraphicsBackend::s_Api;
}

void Graphics::reportLiveObjects()const{
    m_backend->reportLiveObjects();
}

void Graphics::getWindowDimensions(i32& width, i32& height)const{
    width = m_swapChainState.backBufferWidth;
    height = m_swapChainState.backBufferHeight;
}

void Graphics::getDPIScaleInfo(f32& x, f32& y)const{
    x = m_dpiScaleFactorX;
    y = m_dpiScaleFactorY;
}

void Graphics::setWindowTitle(NotNull<const tchar*> title){
    if(m_windowTitle == title.get())
        return;

    m_windowTitle = title.get();
}

void Graphics::setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData){
    m_pointerScaleChangedCallback = callback;
    m_pointerScaleChangedUserData = userData;
    notifyPointerScaleChanged();
}

Texture* Graphics::getCurrentBackBuffer()const{
    return m_backend->getCurrentBackBuffer();
}

Texture* Graphics::getBackBuffer(u32 index)const{
    return m_backend->getBackBuffer(index);
}

u32 Graphics::getCurrentBackBufferIndex()const{
    return m_backend->getCurrentBackBufferIndex();
}

u32 Graphics::getBackBufferCount()const{
    return m_backend->getBackBufferCount();
}

Framebuffer* Graphics::getFramebuffer(u32 index)const{
    if(index < m_swapChainFramebuffers.size())
        return m_swapChainFramebuffers[index].get();
    return nullptr;
}

BufferHandle Graphics::createBuffer(const BufferDesc& desc)const{
    return getDevice().createBuffer(desc);
}

TextureHandle Graphics::createTexture(const TextureDesc& desc)const{
    return getDevice().createTexture(desc);
}

void Graphics::backBufferResizing(){
    waitAllJobs();
    waitForIdle();

    invalidateRenderPassResources();
    m_swapChainFramebuffers.clear();

    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResizing();
}

void Graphics::backBufferResized(){
    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);

    const u32 backBufferCount = getBackBufferCount();
    m_swapChainFramebuffers.clear();
    m_swapChainFramebuffers.reserve(backBufferCount);
    for(u32 index = 0; index < backBufferCount; ++index)
        m_swapChainFramebuffers.push_back(getDevice().createFramebuffer(FramebufferDesc().addColorAttachment(getBackBuffer(index))));

    if(!validateRenderPassResources())
        NWB_LOGGER_WARNING(NWB_TEXT("Graphics: one or more render passes failed to validate resources after back buffer resize"));
    NWB_LOGGER_INFO(NWB_TEXT("Graphics: Back buffer resized to {}x{}"), m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight);
}

void Graphics::invalidateRenderPassResources(){
    for(auto* renderPass : m_renderPasses)
        renderPass->invalidateResources();
}

bool Graphics::validateRenderPassResources(){
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

void Graphics::displayScaleChanged(){
    notifyPointerScaleChanged();

    for(auto* renderPass : m_renderPasses)
        renderPass->displayScaleChanged(m_dpiScaleFactorX, m_dpiScaleFactorY);
}

void Graphics::animate(f64 elapsedTime){
    for(auto* renderPass : m_renderPasses){
        renderPass->animate(static_cast<f32>(elapsedTime));
        renderPass->setLatewarpOptions();
    }
}

bool Graphics::prepareFramePreamble(){
    auto& device = getDevice();
    if(device.isDeviceLost()){
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
            NWB_LOGGER_WARNING(NWB_TEXT("Graphics: failed to materialize one or more requested GPU-timing query pools"));

        // Do not allow render-pass scopes to reuse a prior frame's reset if graph recording or submission fails.
        // The task's accepted callback reenables only the pools covered by the successfully submitted packet.
        m_gpuTiming.discardFrameReset();
        if(!GraphicsModuleDetail::SubmitGraphOwnedFrameTimingReset(
            *this,
            m_allocator.getObjectArena(),
            m_gpuTiming
        )){
            m_gpuTiming.discardFrameReset();
            NWB_LOGGER_WARNING(NWB_TEXT("Graphics: failed to submit the graph-owned frame GPU-timing reset packet"));
        }
    }

    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return false;
    }

    return true;
}

void Graphics::render(){
    Framebuffer* framebuffer = getCurrentFramebuffer();
    auto& device = getDevice();
    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return;
    }

    // Keep prepare -> render interleaved in registration order. Skinning submits its deformation work before the
    // renderer prepares the dependent mesh, CSG, and ray-tracing packets; a global all-pass prepare phase would
    // observe stale runtime meshes.
    for(auto* renderPass : m_renderPasses){
        if(m_deviceRecreationRequested || device.isDeviceLost()){
            if(device.isDeviceLost())
                requestDeviceRecreation();
            return;
        }
        if(!renderPass->prepareResources(framebuffer)){
            NWB_LOGGER_WARNING(NWB_TEXT("Graphics: render pass skipped after resource preparation failed"));
            continue;
        }

        renderPass->render(framebuffer);

        // A render pass can request recreation after an unrecoverable cross-queue ownership failure. Do not let a
        // later pass record or submit against this device generation in the same frame.
        if(m_deviceRecreationRequested || device.isDeviceLost()){
            if(device.isDeviceLost())
                requestDeviceRecreation();
            return;
        }
    }
}

void Graphics::updateAverageFrameTime(f64 elapsedTime){
    m_frameTimeSum += elapsedTime;
    m_numberOfAccumulatedFrames += 1;

    if(m_frameTimeSum > m_averageTimeUpdateInterval && m_numberOfAccumulatedFrames > 0){
        m_averageFrameTime = m_frameTimeSum / static_cast<f64>(m_numberOfAccumulatedFrames);
        m_numberOfAccumulatedFrames = 0;
        m_frameTimeSum = 0.0;
    }
}

void Graphics::notifyPointerScaleChanged()const{
    if(!m_pointerScaleChangedCallback)
        return;

    if(m_deviceCreationParams.supportExplicitDisplayScaling)
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, 1.f, 1.f);
    else
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, m_dpiScaleFactorX, m_dpiScaleFactorY);
}

bool Graphics::shouldRenderUnfocused()const{
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
}

bool Graphics::runFrame(){
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
    if(device.isDeviceLost()){
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

bool Graphics::animateRenderPresent(){
    return animateRenderPresentInternal(nullptr);
}

bool Graphics::animateRenderPresentInternal(CpuTimingPhaseBatch* const phaseTiming){
    if(m_deviceRecreationRequested)
        return false;

    auto& device = getDevice();
    if(device.isDeviceLost()){
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
            const BackBufferResizeCallbacks resizeCallbacks = {
                this,
                &Graphics::BackBufferResizingCallback,
                &Graphics::BackBufferResizedCallback,
            };
            Timer beginFrameBegin;
            if(phaseTiming)
                beginFrameBegin = TimerNow();
            const bool frameBegan = m_backend->beginFrame(resizeCallbacks);
            if(phaseTiming)
                phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsBeginFrameCpuTimingScope, beginFrameBegin);
            if(frameBegan){
                Timer framePreambleBegin;
                if(phaseTiming)
                    framePreambleBegin = TimerNow();
                const bool preamblePrepared = prepareFramePreamble();
                if(phaseTiming)
                    phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsFramePreambleCpuTimingScope, framePreambleBegin);
                if(!preamblePrepared){
                    if(device.isDeviceLost())
                        requestDeviceRecreation();
                    return false;
                }

                Timer renderBegin;
                if(phaseTiming)
                    renderBegin = TimerNow();
                render();
                if(phaseTiming)
                    phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsRenderCpuTimingScope, renderBegin);

                if(m_deviceRecreationRequested || device.isDeviceLost()){
                    if(device.isDeviceLost())
                        requestDeviceRecreation();
                    return false;
                }

                Timer presentBegin;
                if(phaseTiming)
                    presentBegin = TimerNow();
                const bool presented = m_backend->present();
                if(phaseTiming)
                    phaseTiming->stage(__hidden_graphics_lifecycle::s_GraphicsPresentCpuTimingScope, presentBegin);
                if(!presented){
                    if(device.isDeviceLost())
                        requestDeviceRecreation();
                    return false;
                }

                if(device.isDeviceLost()){
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
    if(device.isDeviceLost()){
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

