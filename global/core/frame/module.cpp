
#include "module.h"
#include "arena_names.h"

#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Frame::ApplyPointerScale(void* userData, f32 scaleX, f32 scaleY){
    auto* frame = static_cast<Frame*>(userData);
    NWB_ASSERT(frame);
    frame->m_input.setMousePositionScale(scaleX, scaleY);
}
u32 Frame::queryGraphicsWorkerThreadCount(){
    u32 coreCount = Alloc::QueryCoreCount(Alloc::CoreAffinity::Performance);
    if(coreCount <= 1)
        coreCount = Alloc::QueryCoreCount(Alloc::CoreAffinity::Any);

    return coreCount > s_ReservedCoresForMainThread ? (coreCount - s_ReservedCoresForMainThread) : 0;
}
u32 Frame::queryProjectWorkerThreadCount(){
    u32 coreCount = Alloc::QueryCoreCount(Alloc::CoreAffinity::Any);
    const u32 graphicsWorkerThreadCount = queryGraphicsWorkerThreadCount();
    const u32 reservedCoreCount = s_ReservedCoresForMainThread + graphicsWorkerThreadCount;

    return coreCount > reservedCoreCount ? (coreCount - reservedCoreCount) : 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Frame::Frame(void* inst, u16 width, u16 height)
    : m_graphicsObjectArena(FrameArenaScope::s_GraphicsObjectArena)
    , m_appliedWindowTitle(m_graphicsObjectArena)
    , m_graphicsAllocator(m_graphicsObjectArena)
    , m_graphicsThreadPool(queryGraphicsWorkerThreadCount(), Alloc::CoreAffinity::Any)
    , m_graphicsJobSystem(m_graphicsThreadPool)
    , m_projectObjectArena(FrameArenaScope::s_ProjectObjectArena)
    , m_perfSession(m_projectObjectArena)
    , m_telemetrySession(m_projectObjectArena)
    , m_frameGraphRegistry(m_projectObjectArena)
    , m_telemetryUploadBytes(m_projectObjectArena)
    , m_projectThreadPool(queryProjectWorkerThreadCount(), Alloc::CoreAffinity::Any)
    , m_projectJobSystem(m_projectThreadPool)
    , m_graphics(m_graphicsAllocator, m_graphicsThreadPool, m_graphicsJobSystem, m_perfSession.gpuTimingSink())
{
    auto& frameData = data<Common::FrameData>();
    frameData.width() = width;
    frameData.height() = height;
    setupPlatform(inst);
    m_graphics.setPointerScaleChangedCallback(&Frame::ApplyPointerScale, this);
    m_graphicsObjectArenaMemoryScope = m_perfSession.registerMemoryScope(FrameArenaScope::s_GraphicsObjectArena);
    m_projectObjectArenaMemoryScope = m_perfSession.registerMemoryScope(FrameArenaScope::s_ProjectObjectArena);
}
Frame::~Frame(){
    cleanup();
    cleanupPlatform();
}


bool Frame::startup(){
    if(!m_graphics.init(data<Common::FrameData>())){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Frame: graphics initialization failed"));
        return false;
    }

    return true;
}
void Frame::cleanup(){
    if(m_telemetryUploadCallback){
        if(!flushTelemetryUpload(true))
            NWB_LOGGER_WARNING(NWB_TEXT("Frame: telemetry upload flush failed during cleanup"));
    }
    m_graphics.destroy();
}
void Frame::requestQuit(){
    m_quitRequested = true;
}
void Frame::setPerfCapture(const Perf::CaptureOptions& options){
    m_perfSession.setCaptureOptions(options);
    m_graphics.gpuTiming().setQueryCollectionEnabled(options.gpuTimingActive());
}
void Frame::setTelemetryCapture(const Telemetry::CaptureOptions& options){
    m_telemetrySession.setCaptureOptions(options);
    if(options.perfEnabled())
        setPerfCapture(Perf::CaptureOptions::All());
}
void Frame::setTelemetryUploadCallback(TelemetryUploadCallback callback, void* userData){
    m_telemetryUploadCallback = callback;
    m_telemetryUploadUserData = userData;
}
bool Frame::flushTelemetryUpload(const bool clearAfterUpload){
    if(m_telemetrySession.eventCount() == 0u)
        return true;
    if(!m_telemetryUploadCallback)
        return false;

    if(!Telemetry::EncodeEventStream(m_telemetrySession.view(), m_telemetryUploadBytes))
        return false;

    if(!m_telemetryUploadCallback(m_telemetryUploadUserData, m_telemetryUploadBytes.data(), m_telemetryUploadBytes.size()))
        return false;

    if(clearAfterUpload)
        m_telemetrySession.clear();
    return true;
}
bool Frame::update(f32 delta){
    const u64 frameIndex = m_graphics.getFrameIndex();
    m_perfSession.beginFrame(frameIndex);
    m_telemetrySession.setFrameIndex(frameIndex);

    if(m_telemetrySession.enabled()){
        Telemetry::CaptureSessionCaptureScope telemetryScope(m_telemetrySession);
        return updateFrame(delta);
    }

    return updateFrame(delta);
}
bool Frame::updateFrame(f32 delta){
    if(m_projectUpdateCallback){
        if(!m_projectUpdateCallback(m_projectUpdateUserData, delta)){
            NWB_LOGGER_ERROR(NWB_TEXT("Frame: project update callback returned false"));
            return false;
        }
    }

    if(quitRequested())
        return true;

    if(!m_graphics.runFrame()){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame: graphics frame update failed"));
        return false;
    }

    if(m_telemetrySession.captureOptions().frameGraphEnabled()){
        if(!m_frameGraphRegistry.record(m_telemetrySession))
            NWB_LOGGER_WARNING(NWB_TEXT("Frame: frame graph telemetry record failed"));
    }

    m_perfSession.recordMemorySnapshot(m_graphicsObjectArenaMemoryScope, m_graphicsObjectArena);
    m_perfSession.recordMemorySnapshot(m_projectObjectArenaMemoryScope, m_projectObjectArena);
    m_perfSession.publishFrame();
    if(m_telemetrySession.captureOptions().perfEnabled()){
        [[maybe_unused]] const Telemetry::PerfSessionRecordResult perfRecordResult =
            m_telemetrySession.recordPerfReport(m_perfSession.report());
    }
    return true;
}
bool Frame::render(){
    return true;
}

const tchar* Frame::windowTitleOrDefault()const{
    const tchar* title = m_graphics.getWindowTitle();
    return title && title[0] != 0 ? title : NWB_TEXT("NWB");
}

const tchar* Frame::syncGraphicsWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    m_graphics.updateWindowState(width, height, windowVisible, windowIsInFocus);

    const tchar* title = m_graphics.getWindowTitle();
    if(!title || m_appliedWindowTitle == title)
        return nullptr;

    m_appliedWindowTitle = title;
    return m_appliedWindowTitle.c_str();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

