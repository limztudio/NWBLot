// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"
#include "project_entry.h"

#include <core/ecs/ecs.h>
#include <impl/ecs_graphics/ecs_graphics.h>
#include <impl/ecs_ui/ecs_ui.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld){
    outWorld.reset();

    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.threadPool);
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: ECS world allocation failed"));
        return false;
    }

    if(!context.shaderPathResolver){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: shader path resolver callback is null"));
        return false;
    }

    auto& rendererSystem = world->addSystem<Core::ECSGraphics::RendererSystem>(
        context.objectArena,
        *world,
        context.graphics,
        context.assetManager,
        context.shaderPathResolver
    );
    if(!world->getSystem<Core::ECSGraphics::RendererSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core renderer system was not created"));
        return false;
    }
    auto& deformerSystem = world->addSystem<Core::ECSGraphics::DeformerSystem>(
        context.objectArena,
        *world,
        context.graphics,
        context.assetManager,
        rendererSystem,
        context.shaderPathResolver
    );
    if(!world->getSystem<Core::ECSGraphics::DeformerSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core deformer system was not created"));
        return false;
    }
    auto& uiSystem = world->addSystem<Core::ECSUI::UiSystem>(
        context.objectArena,
        *world,
        context.graphics,
        context.input,
        context.assetManager,
        context.shaderPathResolver
    );
    if(!world->getSystem<Core::ECSUI::UiSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core UI system was not created"));
        return false;
    }
    context.graphics.addRenderPassToBack(deformerSystem);
    context.graphics.addRenderPassToBack(rendererSystem);
    context.graphics.addRenderPassToBack(uiSystem);

    outWorld = Move(world);

    return true;
}

void DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: world is null"));
        return;
    }

    auto* deformerSystem = world->getSystem<Core::ECSGraphics::DeformerSystem>();
    if(!deformerSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core deformer system is null"));
        return;
    }

    auto* rendererSystem = world->getSystem<Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core renderer system is null"));
        return;
    }

    auto* uiSystem = world->getSystem<Core::ECSUI::UiSystem>();
    if(!uiSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core UI system is null"));
        return;
    }

    context.graphics.removeRenderPass(*deformerSystem);
    context.graphics.removeRenderPass(*rendererSystem);
    context.graphics.removeRenderPass(*uiSystem);
    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

