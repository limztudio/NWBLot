// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime.h"
#include "runtime_internal.h"
#include "runtime_standalone_graph.h"
#include "profile_names.h"

#include <core/graphics/backend_contract.h>
#include <core/graphics/backend_selection.h>
#include <core/common/log.h>
#include <core/task/gpu/scheduler.h>
#include <core/telemetry/session.h>

#include <global/exception.h>
#include <global/termination.h>
#include <global/platform.h>

#if defined(NWB_PLATFORM_WINDOWS) && (defined(__x86_64__) || defined(_M_X64))
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(GraphicsContract::BackendApi<GraphicsBackend::Backend>);
static_assert(GraphicsContract::DeviceApi<GraphicsBackend::Device>);
static_assert(GraphicsContract::CommandListApi<GraphicsBackend::CommandList>);

static_assert(GraphicsContract::DescribedResourceApi<GraphicsBackend::Heap, HeapDesc>);
static_assert(GraphicsContract::TextureApi<GraphicsBackend::Texture>);
static_assert(GraphicsContract::DescribedResourceApi<GraphicsBackend::StagingTexture, TextureDesc>);
static_assert(GraphicsContract::BufferApi<GraphicsBackend::Buffer>);
static_assert(GraphicsContract::ShaderApi<GraphicsBackend::Shader>);
static_assert(GraphicsContract::ShaderLibraryApi<GraphicsBackend::ShaderLibrary>);
static_assert(GraphicsContract::DescribedResourceApi<GraphicsBackend::Sampler, SamplerDesc>);
static_assert(GraphicsContract::InputLayoutApi<GraphicsBackend::InputLayout>);
static_assert(GraphicsContract::FramebufferApi<GraphicsBackend::Framebuffer>);
static_assert(GraphicsContract::RayTracingOpacityMicromapApi<GraphicsBackend::RayTracingOpacityMicromap>);
static_assert(GraphicsContract::RayTracingAccelStructApi<GraphicsBackend::RayTracingAccelStruct>);
static_assert(GraphicsContract::BindingLayoutApi<GraphicsBackend::BindingLayout>);
static_assert(GraphicsContract::GraphicsPipelineApi<GraphicsBackend::GraphicsPipeline>);
static_assert(GraphicsContract::ComputePipelineApi<GraphicsBackend::ComputePipeline>);
static_assert(GraphicsContract::MeshletPipelineApi<GraphicsBackend::MeshletPipeline>);
static_assert(GraphicsContract::RayTracingShaderTableApi<GraphicsBackend::RayTracingShaderTable>);
static_assert(GraphicsContract::RayTracingPipelineApi<GraphicsBackend::RayTracingPipeline>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_runtime_contract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
concept GraphicsRuntimeApi = requires(const T& graphics){
    { graphics.getDevice() }->SameAs<GraphicsBackend::Device&>;
};

static_assert(GraphicsRuntimeApi<GraphicsRuntime>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_feature_queries{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_DefaultWaveLaneCount = 64u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
#if defined(NWB_PLATFORM_WINDOWS) && (defined(__x86_64__) || defined(_M_X64))
    // Match the conservative ARM64 default when this x64 process runs under emulation.
    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if(IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) && nativeMachine == IMAGE_FILE_MACHINE_ARM64)
        m_deviceCreationParams.enableNativeMeshShaders = false;
#endif
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

    if(static_cast<i32>(m_swapChainState.backBufferWidth) != static_cast<i32>(width) || static_cast<i32>(m_swapChainState.backBufferHeight) != static_cast<i32>(height)){
        if(!resizeBackBuffer(width, height))
            return false;
    }

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

b8 GraphicsRuntime::queryFeatureSupport(const Feature::Enum feature)const noexcept{
    auto& device = getDevice();
    return device.queryFeatureSupport(feature);
}

u32 GraphicsRuntime::queryWaveLaneCount()const noexcept{
    const auto info = getDevice().getWaveLaneCounts();
    if(info.maxWaveLaneCount > 0u)
        return info.maxWaveLaneCount;
    // No wave size reported: 64 lanes is the safe desktop upper bound and keeps groupshared reductions correct.
    return __hidden_graphics_feature_queries::s_DefaultWaveLaneCount;
}

void GraphicsRuntime::addRenderPassToBack(IRenderPass& pass){
    m_renderPasses.remove(MakeNotNull(&pass));
    m_renderPasses.push_back(MakeNotNull(&pass));

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: back render pass failed to validate resources after registration"));
}

void GraphicsRuntime::removeRenderPass(IRenderPass& pass){
    const b8 deviceIdle = waitForIdle();
    GraphicsBackend::Device* const device = m_backend->getDevice();
    NWB_FATAL_ASSERT_MSG(deviceIdle || (device && device->isDeviceLost()), NWB_TEXT("Render-pass removal requires either a completed device join or terminal device loss"));

    pass.invalidateResources();
    m_renderPasses.remove(MakeNotNull(&pass));
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

void GraphicsRuntime::clearTaskGraphPresentationContributor(const IGpuTaskGraphPresentationContributor& contributor)noexcept{
    if(m_taskGraphPresentationContributor == &contributor)
        m_taskGraphPresentationContributor = nullptr;
}

void GraphicsRuntime::clearTaskGraphOutputLayerContributor(const IGpuTaskGraphOutputLayerContributor& contributor)noexcept{
    if(m_taskGraphOutputLayerContributor == &contributor)
        m_taskGraphOutputLayerContributor = nullptr;
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

Expected<CooperativeVectorDeviceFeatures> GraphicsRuntime::queryCoopVecFeatures()const{
    auto& device = getDevice();
    return device.queryCoopVecFeatures();
}

Expected<usize> GraphicsRuntime::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const{
    auto& device = getDevice();
    return device.getCoopVecMatrixSize(type, layout, rows, columns);
}

void GraphicsRuntime::invalidateRenderPassResources(){
    for(const auto renderPass : m_renderPasses)
        renderPass->invalidateResources();
}

b8 GraphicsRuntime::validateRenderPassResources(){
    b8 valid = true;
    for(const auto renderPass : m_renderPasses){
        valid = renderPass->validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount) && valid;
    }
    return valid;
}

void GraphicsRuntime::animate(const f64 elapsedTime){
    for(const auto renderPass : m_renderPasses)
        renderPass->animate(static_cast<f32>(elapsedTime));
}

void GraphicsRuntime::notifyPointerScaleChanged()const{
    if(!m_pointerScaleChangedCallback)
        return;

    m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, m_dpiScaleFactorX, m_dpiScaleFactorY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

