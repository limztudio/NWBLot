// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <impl/ecs_skinned_mesh_render/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_ui/module.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB Testbed");
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

    auto& meshSystem = world->addSystem<NWB::Impl::MeshSystem>(*world);
    if(!world->getSystem<NWB::Impl::MeshSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: core mesh system was not created"));
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
    auto& skinnedMeshSystem = world->addSystem<NWB::Impl::SkinnedMeshSystem>(
        *world,
        context.graphics,
        context.assetManager,
        meshSystem,
        context.shaderPathResolver
    );
    if(!world->getSystem<NWB::Impl::SkinnedMeshSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: skinned mesh system was not created"));
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
    context.graphics.addRenderPassToBack(skinnedMeshSystem);
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

    auto* skinnedMeshSystem = world->getSystem<NWB::Impl::SkinnedMeshSystem>();
    if(!skinnedMeshSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: skinned mesh system is null"));
        return;
    }

    auto* rendererSystem = world->getSystem<NWB::Impl::RendererSystem>();
    if(!rendererSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core renderer system is null"));
        return;
    }

    auto* meshSystem = world->getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core mesh system is null"));
        return;
    }

    auto* uiSystem = world->getSystem<NWB::Impl::UiSystem>();
    if(!uiSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: core UI system is null"));
        return;
    }

    context.graphics.removeRenderPass(*skinnedMeshSystem);
    context.graphics.removeRenderPass(*rendererSystem);
    context.graphics.removeRenderPass(*uiSystem);

    context.graphics.waitAllJobs();

    if(auto* device = context.graphics.getDevice())
        device->waitForIdle();

    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

