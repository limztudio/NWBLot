// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"
#include "project_entry.h"

#include <core/ecs/ecs.h>
#include <core/ecs_graphics/ecs_graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateBasicProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld, Core::ECSGraphics::RendererSystem*& outRendererSystem){
    outRendererSystem = nullptr;
    outWorld.reset();

    auto* deviceManager = context.graphics.getDeviceManager();
    if(!deviceManager)
        return false;

    outWorld = MakeUnique<Core::ECS::World>(context.objectArena, context.threadPool);
    outRendererSystem = &outWorld->addSystem<Core::ECSGraphics::RendererSystem>(*outWorld, context.graphics);
    deviceManager->addRenderPassToBack(*outRendererSystem);

    return true;
}

void DestroyBasicProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world, Core::ECSGraphics::RendererSystem*& rendererSystem){
    if(rendererSystem){
        if(auto* deviceManager = context.graphics.getDeviceManager())
            deviceManager->removeRenderPass(*rendererSystem);
        rendererSystem = nullptr;
    }

    if(world){
        world->clear();
        world.reset();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

