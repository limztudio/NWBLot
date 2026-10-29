// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime.h"
#include "runtime_internal.h"
#include "runtime_standalone_graph.h"
#include "profile_names.h"

#include <core/graphics/backend_selection.h>
#include <core/common/log.h>
#include <core/task/gpu/scheduler.h>
#include <core/telemetry/session.h>

#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 CopyInstanceParameters(DeviceCreationParameters& dst, const InstanceParameters& src){
    if(src.enableDebugRuntime && !CanEnableDebugRuntime())
        return false;

    static_cast<InstanceParameters&>(dst) = src;
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsRuntime::GraphicsRuntime(GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler, GpuTaskScheduler& gpuTasks, Perf::TimingSink& gpuTiming)
    : GraphicsRuntime(allocator, cpuScheduler, gpuTasks, gpuTiming, nullptr)
{}

GraphicsRuntime::GraphicsRuntime(GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler, GpuTaskScheduler& gpuTasks, Perf::TimingSink& gpuTiming, Perf::TimingSink* const cpuTiming)
    : m_allocator(allocator)
    , m_cpuScheduler(cpuScheduler)
    , m_gpuTasks(gpuTasks)
    , m_frameTaskProfileLabel(cpuScheduler.registerProfileLabel(GraphicsProfileScope::s_GraphicsFrameTaskProfileName))
    , m_deviceCreationParams(m_allocator.getObjectArena())
    , m_gpuTiming(m_allocator.getObjectArena(), gpuTiming)
    , m_cpuTiming(cpuTiming)
    , m_backend(MakeNotNullUnique(MakeGlobalUnique<Backend>(m_allocator.getObjectArena(), m_deviceCreationParams, m_swapChainState, m_allocator, m_cpuScheduler)))
    , m_standaloneGraphWorkspaces(MakeNotNullUnique(MakeGlobalUnique<GraphicsModuleDetail::StandaloneGraphWorkspacePool>(m_allocator.getObjectArena(), m_allocator.getObjectArena())))
    , m_renderPasses(m_allocator.getObjectArena())
    , m_swapChainFramebuffers(m_allocator.getObjectArena())
    , m_windowTitle(m_allocator.getObjectArena())
{
    m_deviceCreationParams.hardwareRayTracingPolicy = HardwareRayTracingPolicy::Automatic;
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
}

GraphicsRuntime::~GraphicsRuntime()noexcept(false){
    // An active unwind is already terminal.
    // Detach the borrowed device binding without native callbacks or a GPU join, so the original exception reaches the application-entry boundary.
    const auto detachDevice = [this]()noexcept{
        if(auto* device = m_backend->getDevice(); device && m_gpuTasks.isAttachedTo(*device)){
            if(!m_gpuTasks.detachDevice(*device))
                TerminateInvariant();
        }
    };
    if(UncaughtExceptionCount() > 0){
        detachDevice();
        return;
    }

    ScopeExit detachOnFailure(detachDevice);

    NWB_FATAL_ASSERT_MSG(destroy(), NWB_TEXT("Graphics destruction requires either a completed device join or terminal device loss"));
    detachOnFailure.release();
}

b8 GraphicsRuntime::init(const Common::FrameData& data){
    if(!registerCpuTimingScopes())
        return false;

    m_lastPresentationReceipt.reset();
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
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GraphicsRuntime: created device '{}'"), m_backend->getRendererString());
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

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GraphicsRuntime: window device and swap chain created ({}x{})"), data.width(), data.height());
    return true;
}

b8 GraphicsRuntime::createHeadlessDevice(){
    if(!registerCpuTimingScopes())
        return false;

    m_lastPresentationReceipt.reset();
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
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GraphicsRuntime: created device '{}'"), m_backend->getRendererString());
    if(!m_gpuTasks.attachDevice(getDevice())){
        requestDeviceRecreation();
        return false;
    }

    m_deviceRecreationRequested = false;

    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GraphicsRuntime: headless device created"));
    return validateRenderPassResources();
}

b8 GraphicsRuntime::createInstance(const InstanceParameters& params){
    if(!__hidden_graphics_runtime::CopyInstanceParameters(m_deviceCreationParams, params)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: debug runtime is only available in non-final builds"));
        return false;
    }

    if(!m_backend->createInstance())
        return false;

    m_instanceCreated = true;
    return true;
}

void GraphicsRuntime::requestDeviceRecreation()const{
    if(m_deviceRecreationRequested)
        return;

    m_deviceRecreationRequested = true;
    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: device recreation requested; ending the current graphics session before another submission."));
}

b8 GraphicsRuntime::updateWindowState(u32 width, u32 height, b8 windowVisible, b8 windowIsInFocus){
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

    if(static_cast<i32>(m_swapChainState.backBufferWidth) != static_cast<i32>(width) || static_cast<i32>(m_swapChainState.backBufferHeight) != static_cast<i32>(height) || m_swapChainState.vsyncEnabled != m_requestedVSync){
        if(!resizeBackBuffer(width, height, m_requestedVSync))
            return false;
    }

    m_swapChainState.vsyncEnabled = m_requestedVSync;
    return true;
}

b8 GraphicsRuntime::destroy(){
    m_lastPresentationReceipt.reset();

    auto transitionTicket = m_backend->prepareSwapChainTransition(SwapChainTransitionKind::Destroy);
    if(!transitionTicket){
        m_deviceRecreationRequested = true;
        return false;
    }

    m_acquiredPresentationFrame = {};
    invalidateRenderPassResources();
    m_renderPasses.clear();
    m_gpuTiming.resetQueries();

    m_swapChainFramebuffers.clear();
    if(!m_standaloneGraphWorkspaces->releaseCommandListCaches()){
        requestDeviceRecreation();
        return false;
    }
    GraphicsBackend::Device* const device = m_backend->getDevice();
    const b8 detachScheduler = device && m_gpuTasks.isAttachedTo(*device);
    if(detachScheduler && !m_gpuTasks.detachDevice(*device)){
        requestDeviceRecreation();
        return false;
    }
    if(!m_backend->commitDestroy(Move(*transitionTicket))){
        // Commit rejection retains the backend device.
        // Restore only our binding; failed initialization must never detach or replace a scheduler belonging to a different runtime.
        if(detachScheduler && m_backend->getDevice() == device && !m_gpuTasks.attachDevice(*device))
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to restore the GPU scheduler after device teardown was rejected."));
        requestDeviceRecreation();
        return false;
    }
    m_instanceCreated = false;
    m_deviceRecreationRequested = false;
    return true;
}

b8 GraphicsRuntime::waitForIdle(){
    if(auto* device = m_backend->getDevice()){
        if(m_gpuTasks.isAttachedTo(*device))
            return m_gpuTasks.wait();
        // A failed initialization may have created this device while the injected scheduler belongs elsewhere.
        return device->waitForIdle();
    }
    return true;
}

b8 GraphicsRuntime::isDeviceLost()const noexcept{
    if(const auto* device = m_backend->getDevice())
        return device->isDeviceLost();
    return false;
}

GraphicsBackend::Device& GraphicsRuntime::getDevice()const noexcept{
    return *MakeNotNull(m_backend->getDevice());
}

Expected<GraphicsVector<AdapterInfo>> GraphicsRuntime::enumerateAdapters(){
    return m_backend->enumerateAdapters();
}

Expected<AdapterInfo> GraphicsRuntime::getSelectedAdapterInfo()const{
    return m_backend->getSelectedAdapterInfo();
}

QueueSubmissionPreSubmitHook GraphicsRuntime::claimFramePresentationSignal()noexcept{
    return m_backend->claimFramePresentationSignal();
}

b8 GraphicsRuntime::confirmFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim, const QueueSubmissionToken& token)noexcept{
    return m_backend->confirmFramePresentationSignal(claim, token);
}


b8 GraphicsRuntime::cancelFramePresentationSignal(const QueueSubmissionPreSubmitHook& claim){
    return m_backend->cancelFramePresentationSignal(claim);
}

TStringView GraphicsRuntime::getRendererString()const noexcept{
    return m_backend->getRendererString();
}

void GraphicsRuntime::getWindowDimensions(i32& width, i32& height)const noexcept{
    width = m_swapChainState.backBufferWidth;
    height = m_swapChainState.backBufferHeight;
}

void GraphicsRuntime::getDPIScaleInfo(f32& x, f32& y)const noexcept{
    x = m_dpiScaleFactorX;
    y = m_dpiScaleFactorY;
}

void GraphicsRuntime::setWindowTitle(const TStringView title){
    if(m_windowTitle == title)
        return;

    m_windowTitle = title;
}

void GraphicsRuntime::setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData){
    m_pointerScaleChangedCallback = callback;
    m_pointerScaleChangedUserData = userData;
    notifyPointerScaleChanged();
}

void GraphicsRuntime::notifyPointerScaleChanged()const{
    if(!m_pointerScaleChangedCallback)
        return;

    if(m_deviceCreationParams.supportExplicitDisplayScaling)
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, 1.f, 1.f);
    else
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, m_dpiScaleFactorX, m_dpiScaleFactorY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

