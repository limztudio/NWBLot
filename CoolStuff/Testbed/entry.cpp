// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/frame_graph_registry.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_ui/module.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_testbed_entry{
static constexpr tchar s_WindowTitle[] = NWB_TEXT("NWB Testbed");
static constexpr tchar s_WorldAllocFailed[] = NWB_TEXT("CreateInitialProjectWorld failed: ECS world allocation failed");
static constexpr tchar s_ResolverNull[] = NWB_TEXT("CreateInitialProjectWorld failed: shader path resolver callback is null");
static constexpr tchar s_DestroyRequiresIdleOrLoss[] = NWB_TEXT("Project-world destruction requires either a completed device join or terminal device loss");
static constexpr NWB::Core::Assets::AssetRef<NWB::Impl::UiSkin> s_DefaultUiSkin{"engine/ui/skins/default/atlas"};
static constexpr NWB::Core::Assets::AssetRef<NWB::Impl::Font> s_DefaultLatin{"engine/ui/fonts/default/latin"};
static constexpr NWB::Core::Assets::AssetRef<NWB::Impl::Font> s_DefaultKorean{"engine/ui/fonts/default/korean"};
static constexpr NWB::Core::Assets::AssetRef<NWB::Impl::FontAtlas> s_DefaultLatinAtlas{"engine/ui/fonts/default/latin_atlas"};
static constexpr NWB::Core::Assets::AssetRef<NWB::Impl::FontAtlas> s_DefaultKoreanAtlas{"engine/ui/fonts/default/korean_atlas"};
};


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { s_DefaultProjectFrameClientWidth, s_DefaultProjectFrameClientHeight };
}


const tchar* NWB::QueryProjectWindowTitle(){
    return __hidden_testbed_entry::s_WindowTitle;
}

bool NWB::ConfigureProjectRuntime(ProjectStartupContext& context){
    Core::GraphicsRuntime& graphics = context.graphics;
    return
        graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
        && graphics.setHDR10OutputEnabled(true)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<ProjectTestbed>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool NWB::CreateInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld){
    outWorld.reset();

    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.cpuTasks);
    if(!world){
        NWB_LOGGER_FATAL(__hidden_testbed_entry::s_WorldAllocFailed);
        return false;
    }

    if(!context.shaderPathResolver){
        NWB_LOGGER_FATAL(__hidden_testbed_entry::s_ResolverNull);
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
    const Impl::UiLayerSystem::FontReferences fonts{
        { { __hidden_testbed_entry::s_DefaultLatin, __hidden_testbed_entry::s_DefaultLatinAtlas },
            { __hidden_testbed_entry::s_DefaultKorean, __hidden_testbed_entry::s_DefaultKoreanAtlas } }, context.objectArena
    };
    auto& uiLayerSystem = world->addSystem<NWB::Impl::UiLayerSystem>(
        *world,
        context.graphics,
        context.input,
        context.clipboard,
        context.textInput,
        context.assetManager,
        context.shaderPathResolver,
        __hidden_testbed_entry::s_DefaultUiSkin,
        fonts,
        NWB::Impl::UiLayerPresentation::Scene
    );
    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(rendererSystem);
    context.graphics.addRenderPassToBack(uiLayerSystem);
    context.frameGraphRegistry.registerContributor(rendererSystem);

    outWorld = Move(world);

    return true;
}


void NWB::DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world){
    NWB_ASSERT(world);

    auto* meshSkinningSystemPtr = world->getSystem<NWB::Impl::MeshSkinningSystem>();
    NWB_ASSERT(meshSkinningSystemPtr);
    NWB::Impl::MeshSkinningSystem& meshSkinningSystem = *meshSkinningSystemPtr;

    auto* rendererSystemPtr = world->getSystem<NWB::Impl::RendererSystem>();
    NWB_ASSERT(rendererSystemPtr);
    NWB::Impl::RendererSystem& rendererSystem = *rendererSystemPtr;

    auto* uiLayerSystemPtr = world->getSystem<NWB::Impl::UiLayerSystem>();
    NWB_ASSERT(uiLayerSystemPtr);
    NWB::Impl::UiLayerSystem& uiLayerSystem = *uiLayerSystemPtr;

    context.frameGraphRegistry.unregisterContributor(rendererSystem);
    context.graphics.removeRenderPass(meshSkinningSystem);
    context.graphics.removeRenderPass(rendererSystem);
    context.graphics.removeRenderPass(uiLayerSystem);

    context.graphics.waitTasks();
    const bool deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(
        deviceIdle || context.graphics.isDeviceLost(),
        __hidden_testbed_entry::s_DestroyRequiresIdleOrLoss
    );

    world->clear();
    world.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

