// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/module.h>
#include <core/input/module.h>
#include <core/graphics/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Frame{
private:
    using FrameString = TString<Alloc::GlobalArena>;


private:
    static constexpr u32 s_ReservedCoresForMainThread = 1;


private:
    static void ApplyPointerScale(void* userData, f32 scaleX, f32 scaleY);
    static u32 queryGraphicsWorkerThreadCount();
    static u32 queryProjectWorkerThreadCount();


public:
    Frame(void* inst, u16 width, u16 height);
    ~Frame();


public:
    bool init();
    bool showFrame();
    bool mainLoop();
    void requestQuit();

public:
    template<typename T>
    inline T& data(){ return static_cast<T&>(m_data); }

public:
    using ProjectUpdateCallback = bool(*)(void* userData, f32 delta);

public:
    bool startup();
    void cleanup();
    bool update(f32 delta);
    bool render();

public:
    inline void setProjectUpdateCallback(ProjectUpdateCallback callback, void* userData){
        m_projectUpdateCallback = callback;
        m_projectUpdateUserData = userData;
    }

    [[nodiscard]] inline Graphics& graphics(){ return m_graphics; }
    [[nodiscard]] inline const Graphics& graphics()const{ return m_graphics; }

    [[nodiscard]] inline InputDispatcher& input(){ return m_input; }
    [[nodiscard]] inline const InputDispatcher& input()const{ return m_input; }

    [[nodiscard]] inline Alloc::GlobalArena& projectObjectArena(){ return m_projectObjectArena; }
    [[nodiscard]] inline const Alloc::GlobalArena& projectObjectArena()const{ return m_projectObjectArena; }

    [[nodiscard]] inline Alloc::ThreadPool& projectThreadPool(){ return m_projectThreadPool; }
    [[nodiscard]] inline const Alloc::ThreadPool& projectThreadPool()const{ return m_projectThreadPool; }

    [[nodiscard]] inline Alloc::JobSystem& projectJobSystem(){ return m_projectJobSystem; }
    [[nodiscard]] inline const Alloc::JobSystem& projectJobSystem()const{ return m_projectJobSystem; }

    [[nodiscard]] inline FrameString& appliedWindowTitle(){ return m_appliedWindowTitle; }
    [[nodiscard]] inline const FrameString& appliedWindowTitle()const{ return m_appliedWindowTitle; }

    [[nodiscard]] inline bool quitRequested()const{ return m_quitRequested; }
    [[nodiscard]] const tchar* syncGraphicsWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);


private:
    void setupPlatform(void* inst);
    void cleanupPlatform();


private:
    Common::FrameData m_data;

    Alloc::GlobalArena m_graphicsObjectArena;
    FrameString m_appliedWindowTitle;
    GraphicsAllocator m_graphicsAllocator;
    Alloc::ThreadPool m_graphicsThreadPool;
    Alloc::JobSystem m_graphicsJobSystem;
    InputDispatcher m_input;

    Alloc::GlobalArena m_projectObjectArena;
    Alloc::ThreadPool m_projectThreadPool;
    Alloc::JobSystem m_projectJobSystem;

    Graphics m_graphics;

    ProjectUpdateCallback m_projectUpdateCallback = nullptr;
    void* m_projectUpdateUserData = nullptr;
    bool m_quitRequested = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

