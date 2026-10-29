// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <impl/assets_graphics/bindless/runtime_abi.h>
#include <impl/ecs_mesh/skinning/system.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_ui/layer_system.h>

#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/frame_graph_registry.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_testbed_entry{
static constexpr TStringView s_WindowTitle = NWB_TEXT("NWB Testbed");
static constexpr TStringView s_WorldAllocFailed = NWB_TEXT("CreateInitialProjectWorld failed: ECS world allocation failed");
static constexpr TStringView s_ResolverNull = NWB_TEXT("CreateInitialProjectWorld failed: shader path resolver callback is null");
static constexpr TStringView s_DestroyRequiresIdleOrLoss = NWB_TEXT("Project-world destruction requires either a completed device join or terminal device loss");
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { s_DefaultProjectFrameClientWidth, s_DefaultProjectFrameClientHeight };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return Testbed::__hidden_testbed_entry::s_WindowTitle;
}

b8 NWB::ConfigureProjectRuntime(ProjectStartupContext& context)noexcept{
    Core::GraphicsRuntime& graphics = context.graphics;
    return graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()) && graphics.setHDR10OutputEnabled(true);
}

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<Testbed::Project>(context);
}

Expected<UniquePtr<NWB::Core::ECS::World>, TStringView> Testbed::CreateInitialProjectWorld(NWB::ProjectRuntimeContext& context){
    auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.cpuTasks);
    if(!world){
        NWB_LOGGER_FATAL(__hidden_testbed_entry::s_WorldAllocFailed);
        return MakeUnexpected(__hidden_testbed_entry::s_WorldAllocFailed);
    }

    if(!context.shaderPathResolver){
        NWB_LOGGER_FATAL(__hidden_testbed_entry::s_ResolverNull);
        return MakeUnexpected(__hidden_testbed_entry::s_ResolverNull);
    }

    auto& meshSystem = world->addSystem<NWB::Impl::MeshSystem>(*world);
    auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(*world, context.graphics, context.assetManager, context.shaderPathResolver);
    world->addSystem<NWB::Impl::ModelSystem>(*world, context.assetManager, NWB::Impl::CreateModelObjectRendererHooks());
    auto& meshSkinningSystem = world->addSystem<NWB::Impl::MeshSkinningSystem>(*world, context.graphics, context.assetManager, meshSystem, context.shaderPathResolver);
    auto& uiLayerSystem = world->addSystem<NWB::Impl::UiLayerSystem>(
        *world,
        context.graphics,
        context.input,
        context.clipboard,
        context.textInput,
        context.assetManager,
        context.shaderPathResolver,
        NWB::Core::Assets::AssetRef<NWB::Impl::UiSkin>{},
        NWB::Impl::UiLayerPresentation::Scene
    );
    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(rendererSystem);
    context.graphics.addRenderPassToBack(uiLayerSystem);
    context.frameGraphRegistry.registerContributor(rendererSystem);

    return world;
}

void Testbed::DestroyInitialProjectWorld(NWB::ProjectRuntimeContext& context, UniquePtr<NWB::Core::ECS::World>& world){
    NWB_ASSERT(world);

    NWB::Impl::MeshSkinningSystem& meshSkinningSystem = *MakeNotNull(world->getSystem<NWB::Impl::MeshSkinningSystem>());
    NWB::Impl::RendererSystem& rendererSystem = *MakeNotNull(world->getSystem<NWB::Impl::RendererSystem>());
    NWB::Impl::UiLayerSystem& uiLayerSystem = *MakeNotNull(world->getSystem<NWB::Impl::UiLayerSystem>());

    context.frameGraphRegistry.unregisterContributor(rendererSystem);
    context.graphics.removeRenderPass(meshSkinningSystem);
    context.graphics.removeRenderPass(rendererSystem);
    context.graphics.removeRenderPass(uiLayerSystem);

    const b8 deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(deviceIdle || context.graphics.isDeviceLost(), __hidden_testbed_entry::s_DestroyRequiresIdleOrLoss);

    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

