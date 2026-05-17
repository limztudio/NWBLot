// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <core/ecs/ecs.h>
#include <core/graphics/graphics.h>
#include <impl/ecs_skinned_geometry_render/ecs_skinned_geometry_render.h>
#include <impl/ecs_geometry/ecs_geometry.h>
#include <impl/ecs_render/ecs_render.h>
#include <impl/ecs_ui/ecs_ui.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<ProjectTestbed>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool NWB::CreateInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld){
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

    auto& geometrySystem = world->addSystem<NWB::Impl::GeometrySystem>(*world);
    if(!world->getSystem<NWB::Impl::GeometrySystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core geometry system was not created"));
        return false;
    }
    auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
        *world,
        context.graphics,
        context.assetManager,
        context.shaderPathResolver
    );
    if(!world->getSystem<NWB::Impl::RendererSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core renderer system was not created"));
        return false;
    }
    auto& skinnedGeometrySystem = world->addSystem<NWB::Impl::SkinnedGeometrySystem>(
        *world,
        context.graphics,
        context.assetManager,
        geometrySystem,
        context.shaderPathResolver
    );
    if(!world->getSystem<NWB::Impl::SkinnedGeometrySystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: skinned geometry system was not created"));
        return false;
    }
    auto& uiSystem = world->addSystem<NWB::Impl::UiSystem>(
        *world,
        context.graphics,
        context.input,
        context.assetManager,
        context.shaderPathResolver
    );
    if(!world->getSystem<NWB::Impl::UiSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core UI system was not created"));
        return false;
    }
    context.graphics.addRenderPassToBack(skinnedGeometrySystem);
    context.graphics.addRenderPassToBack(rendererSystem);
    context.graphics.addRenderPassToBack(uiSystem);

    outWorld = Move(world);

    return true;
}


void NWB::DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: world is null"));
        return;
    }

    auto* skinnedGeometrySystem = world->getSystem<NWB::Impl::SkinnedGeometrySystem>();
    if(!skinnedGeometrySystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: skinned geometry system is null"));
        return;
    }

    auto* rendererSystem = world->getSystem<NWB::Impl::RendererSystem>();
    if(!rendererSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core renderer system is null"));
        return;
    }

    auto* geometrySystem = world->getSystem<NWB::Impl::GeometrySystem>();
    if(!geometrySystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core geometry system is null"));
        return;
    }

    auto* uiSystem = world->getSystem<NWB::Impl::UiSystem>();
    if(!uiSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core UI system is null"));
        return;
    }

    context.graphics.removeRenderPass(*skinnedGeometrySystem);
    context.graphics.removeRenderPass(*rendererSystem);
    context.graphics.removeRenderPass(*uiSystem);

    context.graphics.waitAllJobs();

    if(auto* device = context.graphics.getDevice())
        device->waitForIdle();

    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

