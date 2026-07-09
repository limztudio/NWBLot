// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <global/core/ecs/module.h>
#include <global/core/graphics/backend_selection.h>
#include <global/core/graphics/module.h>
#include <global/core/telemetry/frame_graph_registry.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/mesh/model_renderer.h>
#include <impl/ecs_render/kernel/module.h>
#include <impl/ecs_ui/module.h>
#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { s_DefaultProjectFrameClientWidth, s_DefaultProjectFrameClientHeight };
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
    auto& modelSystem = world->addSystem<NWB::Impl::ModelSystem>(
        *world,
        context.assetManager,
        NWB::Impl::CreateModelObjectRendererHooks()
    );
    static_cast<void>(modelSystem);
    if(!world->getSystem<NWB::Impl::ModelSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: model system was not created"));
        return false;
    }
    auto& meshSkinningSystem = world->addSystem<NWB::Impl::MeshSkinningSystem>(
        *world,
        context.graphics,
        context.assetManager,
        meshSystem,
        context.shaderPathResolver
    );
    if(!world->getSystem<NWB::Impl::MeshSkinningSystem>()){
        NWB_LOGGER_FATAL(NWB_TEXT("CreateInitialProjectWorld failed: mesh skinning system was not created"));
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
    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(rendererSystem);
    context.graphics.addRenderPassToBack(uiSystem);
    context.frameGraphRegistry.registerContributor(rendererSystem);

    outWorld = Move(world);

    return true;
}


void NWB::DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: world is null"));
        return;
    }

    auto* modelSystem = world->getSystem<NWB::Impl::ModelSystem>();
    if(!modelSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: model system is null"));
        return;
    }

    auto* meshSkinningSystem = world->getSystem<NWB::Impl::MeshSkinningSystem>();
    if(!meshSkinningSystem){
        NWB_LOGGER_FATAL(NWB_TEXT("DestroyInitialProjectWorld failed: mesh skinning system is null"));
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

    context.frameGraphRegistry.unregisterContributor(*rendererSystem);
    context.graphics.removeRenderPass(*meshSkinningSystem);
    context.graphics.removeRenderPass(*rendererSystem);
    context.graphics.removeRenderPass(*uiSystem);

    context.graphics.waitAllJobs();

    if(auto* device = context.graphics.getDevice())
        device->waitForIdle();

    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

