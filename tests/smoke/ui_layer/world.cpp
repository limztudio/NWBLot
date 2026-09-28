// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "world.h"
#include "paint_scene.h"

#include <impl/ecs_ui/layer_system.h>

#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_layer_smoke_world{
static constexpr Core::Assets::AssetRef<Impl::UiSkin> s_DefaultSkin{"engine/ui/skins/default/atlas"};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateUiLayerSmokeWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld){
    outWorld.reset();
    if(!context.shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: shader path resolver is unavailable"));
        return false;
    }
    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.cpuTasks);
    if(!world){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSmokeProject: ECS world allocation failed"));
        return false;
    }

    auto& layer = world->addSystem<Impl::UiLayerSystem>(
        *world,
        context.graphics,
        context.clipboard,
        context.assetManager,
        context.shaderPathResolver,
        __hidden_ui_layer_smoke_world::s_DefaultSkin,
        Impl::UiLayerPresentation::Standalone
    );
    auto entity = world->createEntity();
    auto& paint = entity.addComponent<Impl::UiPaintComponent>();
    paint.paint = [paintFrame = 0u](Impl::UiPaintContext& paintContext)mutable{
        if(paintFrame < 3u){
            ++paintFrame;
            if(paintFrame <= 2u)
                return;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiLayerSmokeProject: deterministic solid, skin, alpha and nested clip geometry submitted"));
        }
        PaintUiLayerSmokeScene(paintContext);
    };

    context.graphics.addRenderPassToBack(layer);
    outWorld = Move(world);
    return true;
}

void DestroyUiLayerSmokeWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    if(!world)
        return;

    world->taskScope().wait();
    auto* const layer = world->getSystem<Impl::UiLayerSystem>();
    NWB_FATAL_ASSERT(layer);
    context.graphics.removeRenderPass(*layer);
    context.graphics.waitTasks();
    const bool deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(deviceIdle || context.graphics.isDeviceLost(), NWB_TEXT("UI smoke teardown requires device idle or terminal loss"));
    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

