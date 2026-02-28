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

    namespace ECSGraphics{
        class RendererSystem;
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
    virtual ProjectFrameClientSize queryFrameClientSize()const{
        return {};
    }

    virtual bool onStartup(ProjectRuntimeContext& context){
        (void)context;
        return true;
    }

    virtual bool onUpdate(ProjectRuntimeContext& context, f32 delta){
        (void)context;
        (void)delta;
        return true;
    }

    virtual void onShutdown(ProjectRuntimeContext& context){
        (void)context;
    }
};


IProjectEntryCallbacks& QueryProjectEntryCallbacks();

bool CreateBasicProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld, Core::ECSGraphics::RendererSystem*& outRendererSystem);
void DestroyBasicProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world, Core::ECSGraphics::RendererSystem*& rendererSystem);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

