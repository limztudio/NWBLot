// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/common.h>
#include <core/ecs/ecs.h>
#include <core/graphics/graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Frame{
private:
    static constexpr u32 s_GraphicsPersistentArenaSize = 64 * 1024 * 1024;
    static constexpr u32 s_ReservedCoresForMainThread = 1;


private:
    static u32 queryGraphicsWorkerThreadCount();
    static u32 queryWorldWorkerThreadCount();


public:
    Frame(void* inst, u16 width, u16 height);
    ~Frame();


public:
    bool init();
    bool showFrame();
    bool mainLoop();

public:
    template<typename T>
    inline T& data(){ return static_cast<T&>(m_data); }

public:
    bool startup();
    void cleanup();
    bool update(float delta);
    bool render();

public:
    inline ECS::World& world(){ return m_world; }
    inline const ECS::World& world()const{ return m_world; }


private:
    void setupPlatform(void* inst);
    void cleanupPlatform();


private:
    Common::FrameData m_data;
    BasicString<tchar> m_appliedWindowTitle;

    Alloc::MemoryArena m_graphicsPersistentArena;
    Alloc::CustomArena m_graphicsObjectArena;
    GraphicsAllocator m_graphicsAllocator;
    Alloc::ThreadPool m_graphicsThreadPool;
    Alloc::JobSystem m_graphicsJobSystem;

    Alloc::CustomArena m_worldObjectArena;
    Alloc::ThreadPool m_worldThreadPool;
    ECS::World m_world;

    Graphics m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

