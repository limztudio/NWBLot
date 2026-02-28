// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/common.h>
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
    static u32 queryProjectWorkerThreadCount();


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
    using ProjectUpdateCallback = bool(*)(void* userData, f32 delta);

public:
    bool startup();
    void cleanup();
    bool update(float delta);
    bool render();

public:
    inline void setProjectUpdateCallback(ProjectUpdateCallback callback, void* userData){
        m_projectUpdateCallback = callback;
        m_projectUpdateUserData = userData;
    }

    [[nodiscard]] inline Graphics& graphics(){ return m_graphics; }
    [[nodiscard]] inline const Graphics& graphics()const{ return m_graphics; }

    [[nodiscard]] inline Alloc::CustomArena& projectObjectArena(){ return m_projectObjectArena; }
    [[nodiscard]] inline const Alloc::CustomArena& projectObjectArena()const{ return m_projectObjectArena; }

    [[nodiscard]] inline Alloc::ThreadPool& projectThreadPool(){ return m_projectThreadPool; }
    [[nodiscard]] inline const Alloc::ThreadPool& projectThreadPool()const{ return m_projectThreadPool; }

    [[nodiscard]] inline Alloc::JobSystem& projectJobSystem(){ return m_projectJobSystem; }
    [[nodiscard]] inline const Alloc::JobSystem& projectJobSystem()const{ return m_projectJobSystem; }


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

    Alloc::CustomArena m_projectObjectArena;
    Alloc::ThreadPool m_projectThreadPool;
    Alloc::JobSystem m_projectJobSystem;

    Graphics m_graphics;

    ProjectUpdateCallback m_projectUpdateCallback = nullptr;
    void* m_projectUpdateUserData = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

