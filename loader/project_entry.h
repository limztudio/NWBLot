// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core{
    class Graphics;

    namespace ECS{
        class World;
    };

    namespace Alloc{
        class CustomArena;
        class ThreadPool;
        class JobSystem;
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ProjectFrameClientSize{
    u16 width = 1280;
    u16 height = 900;
};

struct ProjectRuntimeContext{
    Core::Graphics& graphics;
    Core::Alloc::CustomArena& objectArena;
    Core::Alloc::ThreadPool& threadPool;
    Core::Alloc::JobSystem& jobSystem;
};


class IProjectEntryCallbacks{
public:
    virtual ~IProjectEntryCallbacks() = default;


public:
    virtual bool onStartup(){
        return true;
    }
    virtual void onShutdown(){
    }

    virtual bool onUpdate(f32 delta){
        (void)delta;
        return true;
    }
};


ProjectFrameClientSize QueryProjectFrameClientSize();
UniquePtr<IProjectEntryCallbacks> CreateProjectEntryCallbacks(ProjectRuntimeContext& context);

bool CreateInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld);
void DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

