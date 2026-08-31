// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include "renderer_frame_pipeline.h"

#include <impl/ecs_render/components.h>
#include <impl/ecs_render/material/material_instance.h>

#include <impl/ecs_scene/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_pipeline(MakeNotNullUnique(Core::MakeGlobalUnique<RendererFramePipeline>(
        arena,
        arena,
        world,
        graphics,
        assetManager,
        Move(shaderPathResolver)
    )))
{
    readAccess<NWB::Impl::Scene::ActiveCameraComponent>();
    readAccess<NWB::Impl::Scene::TransformComponent>();
    readAccess<NWB::Impl::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    readAccess<MaterialInstanceComponent>();
    readAccess<StaticCsgMeshComponent>();
    readAccess<SkinnedCsgMeshComponent>();
    readAccess<CsgCutterComponent>();
}
RendererSystem::~RendererSystem() = default;


bool RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    return m_pipeline->validateResources(width, height, sampleCount);
}

void RendererSystem::invalidateResources(){
    m_pipeline->invalidateResources();
}

void RendererSystem::update(Core::ECS::World& world, const f32 delta){
    m_pipeline->update(world, delta);
}

bool RendererSystem::prepareResources(Core::Framebuffer* framebuffer){
    return m_pipeline->prepareResources(framebuffer);
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    m_pipeline->render(framebuffer);
}

bool RendererSystem::appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder){
    return m_pipeline->appendFrameGraph(builder);
}

void RendererSystem::setFrameLaggedAsyncLightingEnabled(const bool enabled)noexcept{
    m_pipeline->setFrameLaggedAsyncLightingEnabled(enabled);
}

bool RendererSystem::frameLaggedAsyncLightingEnabled()const noexcept{
    return m_pipeline->frameLaggedAsyncLightingEnabled();
}

bool RendererSystem::setTaskGraphTimingFeedbackPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy){
    return m_pipeline->setTaskGraphTimingFeedbackPolicy(policy);
}

Core::GpuTaskGraphRuntimeStatistics RendererSystem::deferredTaskGraphRuntimeStatistics()const noexcept{
    return m_pipeline->deferredTaskGraphRuntimeStatistics();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

