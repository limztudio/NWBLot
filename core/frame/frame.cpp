// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame.h"

#include <logger/client/logger.h>


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
}
Frame::~Frame(){
    cleanup();
    cleanupPlatform();
}


bool Frame::startup(){
    if(!m_graphics.init(data<Common::FrameData>()))
        return false;

    return true;
}
void Frame::cleanup(){
    m_graphics.destroy();
}
bool Frame::update(float delta){
    if(m_projectUpdateCallback){
        if(!m_projectUpdateCallback(m_projectUpdateUserData, delta))
            return false;
    }

    return m_graphics.runFrame();
}
bool Frame::render(){
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

