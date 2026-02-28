// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"
#include "project_entry.h"

#include <core/ecs/ecs.h>
#include <core/ecs_graphics/ecs_graphics.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld){
    outWorld.reset();

    auto* deviceManager = context.graphics.getDeviceManager();
    if(!deviceManager){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: graphics device manager is null"));
        return false;
    }

    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.threadPool);
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: ECS world allocation failed"));
        return false;
    }

    world->addSystem<Core::ECSGraphics::RendererSystem>(*world, context.graphics);
    auto* rendererSystem = world->getSystem<Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core renderer system was not created"));
        return false;
    }
    deviceManager->addRenderPassToBack(*rendererSystem);

    outWorld = Move(world);

    return true;
}

void DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: world is null"));
        return;
    }

    auto* rendererSystem = world->getSystem<Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core renderer system is null"));
        return;
    }

    auto* deviceManager = context.graphics.getDeviceManager();
    if(!deviceManager){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: graphics device manager is null"));
        return;
    }

    deviceManager->removeRenderPass(*rendererSystem);
    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

