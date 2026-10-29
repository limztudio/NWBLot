// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include "renderer_frame_pipeline.h"

#include <impl/ecs_scene/components.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_pipeline(MakeNotNullUnique(Core::MakeGlobalUnique<RendererFramePipeline>(arena, arena, world, graphics, assetManager, Move(shaderPathResolver))))
{
    readAccess<NWB::Impl::Scene::ActiveCameraComponent>();
    readAccess<NWB::Impl::Scene::TransformComponent>();
    readAccess<NWB::Impl::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    readAccess<MaterialInstanceComponent>();
    readAccess<CsgReceiverComponent>();
    readAccess<StaticCsgMeshComponent>();
    readAccess<SkinnedCsgMeshComponent>();
    readAccess<CsgCutterComponent>();
}

RendererSystem::~RendererSystem()noexcept = default;


b8 RendererSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    return m_pipeline->validateResources(width, height, sampleCount);
}

void RendererSystem::invalidateResources(){
    m_pipeline->invalidateResources();
}

Expected<void, Core::CpuTaskAdmissionError::Enum> RendererSystem::update(Core::ECS::World& world, const f32 delta)noexcept{
    m_pipeline->update(world, delta);
    return {};
}

b8 RendererSystem::prepareResources(Core::Framebuffer* framebuffer){
    return m_pipeline->prepareResources(framebuffer);
}

void RendererSystem::render(Core::Framebuffer* framebuffer){
    m_pipeline->render(framebuffer);
}

b8 RendererSystem::appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder){
    return m_pipeline->appendFrameGraph(builder);
}

void RendererSystem::setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{
    m_pipeline->setActiveSceneSubsystems(subsystems);
}

void RendererSystem::setRefractionEnabled(const b8 enabled)noexcept{
    m_pipeline->setRefractionEnabled(enabled);
}

void RendererSystem::setRefractionHardwareTracingEnabled(const b8 enabled)noexcept{
    m_pipeline->setRefractionHardwareTracingEnabled(enabled);
}

void RendererSystem::setRefractionDiagnosticsEnabled(const b8 enabled)noexcept{
    m_pipeline->setRefractionDiagnosticsEnabled(enabled);
}

Expected<RefractionStatistics> RendererSystem::tryGetLatestRefractionStatistics()const noexcept{
    return m_pipeline->tryGetLatestRefractionStatistics();
}

b8 RendererSystem::setCausticQualitySettings(const CausticQualitySettings& settings)noexcept{
    return m_pipeline->setCausticQualitySettings(settings);
}

b8 RendererSystem::setSurfelGiQualitySettings(const SurfelGiQualitySettings& settings)noexcept{
    return m_pipeline->setSurfelGiQualitySettings(settings);
}

b8 RendererSystem::setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept{
    return m_pipeline->setSoftwareShadowSettings(settings);
}

b8 RendererSystem::setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept{
    return m_pipeline->setShadowQualitySettings(settings);
}

b8 RendererSystem::setReflectionSettings(const ReflectionSettings& settings)noexcept{
    return m_pipeline->setReflectionSettings(settings);
}

Expected<ReflectionStatistics> RendererSystem::tryGetLatestReflectionStatistics()const noexcept{
    return m_pipeline->tryGetLatestReflectionStatistics();
}

b8 RendererSystem::setPresentationSettings(const PresentationSettings& settings)noexcept{
    return m_pipeline->setPresentationSettings(settings);
}

void RendererSystem::setFrameLaggedAsyncLightingEnabled(const b8 enabled)noexcept{
    m_pipeline->setFrameLaggedAsyncLightingEnabled(enabled);
}

b8 RendererSystem::frameLaggedAsyncLightingEnabled()const noexcept{
    return m_pipeline->frameLaggedAsyncLightingEnabled();
}

b8 RendererSystem::setTaskGraphTimingFeedbackPolicy(const Core::GpuTaskTimingFeedbackPolicy& policy){
    return m_pipeline->setTaskGraphTimingFeedbackPolicy(policy);
}

Core::GpuTaskGraphRuntimeStatistics RendererSystem::deferredTaskGraphRuntimeStatistics()const noexcept{
    return m_pipeline->deferredTaskGraphRuntimeStatistics();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

