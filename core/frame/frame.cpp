// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* GraphicsObjectArenaAlloc(usize size){
    return Alloc::CoreAlloc(size, "NWB::Core::Frame::GraphicsObjectArenaAlloc");
}
void GraphicsObjectArenaFree(void* ptr){
    Alloc::CoreFree(ptr, "NWB::Core::Frame::GraphicsObjectArenaFree");
}
void* GraphicsObjectArenaAllocAligned(usize size, usize align){
    return Alloc::CoreAllocAligned(size, align, "NWB::Core::Frame::GraphicsObjectArenaAllocAligned");
}
void GraphicsObjectArenaFreeAligned(void* ptr){
    Alloc::CoreFreeAligned(ptr, "NWB::Core::Frame::GraphicsObjectArenaFreeAligned");
}

void* ProjectObjectArenaAlloc(usize size){
    return Alloc::CoreAlloc(size, "NWB::Core::Frame::ProjectObjectArenaAlloc");
}
void ProjectObjectArenaFree(void* ptr){
    Alloc::CoreFree(ptr, "NWB::Core::Frame::ProjectObjectArenaFree");
}
void* ProjectObjectArenaAllocAligned(usize size, usize align){
    return Alloc::CoreAllocAligned(size, align, "NWB::Core::Frame::ProjectObjectArenaAllocAligned");
}
void ProjectObjectArenaFreeAligned(void* ptr){
    Alloc::CoreFreeAligned(ptr, "NWB::Core::Frame::ProjectObjectArenaFreeAligned");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    : m_graphicsPersistentArena(s_GraphicsPersistentArenaSize)
    , m_graphicsObjectArena(
        &__hidden_frame::GraphicsObjectArenaAlloc,
        &__hidden_frame::GraphicsObjectArenaFree,
        &__hidden_frame::GraphicsObjectArenaAllocAligned,
        &__hidden_frame::GraphicsObjectArenaFreeAligned
    )
    , m_graphicsAllocator(m_graphicsPersistentArena, m_graphicsObjectArena)
    , m_graphicsThreadPool(queryGraphicsWorkerThreadCount(), Alloc::CoreAffinity::Any)
    , m_graphicsJobSystem(m_graphicsThreadPool)
    , m_projectObjectArena(
        &__hidden_frame::ProjectObjectArenaAlloc,
        &__hidden_frame::ProjectObjectArenaFree,
        &__hidden_frame::ProjectObjectArenaAllocAligned,
        &__hidden_frame::ProjectObjectArenaFreeAligned
    )
    , m_projectThreadPool(queryProjectWorkerThreadCount(), Alloc::CoreAffinity::Any)
    , m_projectJobSystem(m_projectThreadPool)
    , m_graphics(m_graphicsAllocator, m_graphicsThreadPool, m_graphicsJobSystem)
{
    auto& frameData = data<Common::FrameData>();
    frameData.width() = width;
    frameData.height() = height;
    setupPlatform(inst);
    m_graphics.setPointerScaleChangedCallback(&Frame::ApplyPointerScale, this);
}
Frame::~Frame(){
    cleanup();
    cleanupPlatform();
}


bool Frame::startup(){
    if(!m_graphics.init(data<Common::FrameData>())){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame: graphics initialization failed"));
        return false;
    }

    return true;
}
void Frame::cleanup(){
    m_graphics.destroy();
}
bool Frame::update(f32 delta){
    if(m_projectUpdateCallback){
        if(!m_projectUpdateCallback(m_projectUpdateUserData, delta)){
            NWB_LOGGER_ERROR(NWB_TEXT("Frame: project update callback returned false"));
            return false;
        }
    }

    if(!m_graphics.runFrame()){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame: graphics frame update failed"));
        return false;
    }

    return true;
}
bool Frame::render(){
    return true;
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

