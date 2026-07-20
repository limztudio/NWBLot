#include "project.h"

#include <core/ecs/module.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/telemetry/frame_graph_registry.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/mesh/model_renderer.h>
#include <impl/ecs_render/kernel/module.h>
#include <impl/ecs_ui/module.h>
#include <core/common/log.h>


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
    auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
        *world,
        context.graphics,
        context.assetManager,
        context.shaderPathResolver
    );
    world->addSystem<NWB::Impl::ModelSystem>(
        *world,
        context.assetManager,
        NWB::Impl::CreateModelObjectRendererHooks()
    );
    auto& meshSkinningSystem = world->addSystem<NWB::Impl::MeshSkinningSystem>(
        *world,
        context.graphics,
        context.assetManager,
        meshSystem,
        context.shaderPathResolver
    );
    auto& uiSystem = world->addSystem<NWB::Impl::UiSystem>(
        *world,
        context.graphics,
        context.input,
        context.assetManager,
        context.shaderPathResolver
    );
    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(rendererSystem);
    context.graphics.addRenderPassToBack(uiSystem);
    context.frameGraphRegistry.registerContributor(rendererSystem);

    outWorld = Move(world);

    return true;
}


void NWB::DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    NWB_ASSERT(world);

    auto* meshSkinningSystem = world->getSystem<NWB::Impl::MeshSkinningSystem>();
    NWB_ASSERT(meshSkinningSystem);

    auto* rendererSystem = world->getSystem<NWB::Impl::RendererSystem>();
    NWB_ASSERT(rendererSystem);

    auto* uiSystem = world->getSystem<NWB::Impl::UiSystem>();
    NWB_ASSERT(uiSystem);

    context.frameGraphRegistry.unregisterContributor(*rendererSystem);
    context.graphics.removeRenderPass(*meshSkinningSystem);
    context.graphics.removeRenderPass(*rendererSystem);
    context.graphics.removeRenderPass(*uiSystem);

    context.graphics.waitAllJobs();

    auto* device = context.graphics.getDevice();
    NWB_ASSERT(device);
    device->waitForIdle();

    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

