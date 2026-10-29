// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/caustic/settings.adhd.h>
#include <impl/ecs_render/gi/quality_settings.adhd.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/reflection/settings.adhd.h>
#include <impl/ecs_render/reflection/statistics.h>
#include <impl/ecs_render/refraction/statistics.h>
#include <impl/ecs_render/deferred/presentation_settings.adhd.h>
#include <impl/ecs_render/shadow/light_space_settings.adhd.h>
#include <impl/ecs_render/shadow/quality_settings.adhd.h>

#include <core/alloc/general.h>
#include <core/ecs/system.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/task/gpu/packet_runtime.h>
#include <core/task/gpu/timing_feedback.h>
#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ActiveSceneSubsystems;
class RendererFramePipeline;


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass, public Core::Telemetry::IFrameGraphContributor{
private:
    using PipelineOwner = Core::GlobalUniquePtr<RendererFramePipeline>;


public:
    using ShaderPathResolveCallback = Function<Expected<Name>(const Name&, AStringView, const Name&)>;


public:
    RendererSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, ShaderPathResolveCallback shaderPathResolver);
    virtual ~RendererSystem()noexcept override;


public:
    virtual b8 validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;

    [[nodiscard]] virtual Expected<void, Core::CpuTaskAdmissionError::Enum> update(Core::ECS::World& world, f32 delta)noexcept override;

    virtual b8 prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual b8 appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder)override;

    void setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept;
    void setRefractionEnabled(b8 enabled)noexcept;
    void setRefractionHardwareTracingEnabled(b8 enabled)noexcept;
    void setRefractionDiagnosticsEnabled(b8 enabled)noexcept;
    [[nodiscard]] Expected<RefractionStatistics> tryGetLatestRefractionStatistics()const noexcept;
    [[nodiscard]] b8 setCausticQualitySettings(const CausticQualitySettings& settings)noexcept;
    [[nodiscard]] b8 setSurfelGiQualitySettings(const SurfelGiQualitySettings& settings)noexcept;
    [[nodiscard]] b8 setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept;
    [[nodiscard]] b8 setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept;
    [[nodiscard]] b8 setReflectionSettings(const ReflectionSettings& settings)noexcept;
    [[nodiscard]] Expected<ReflectionStatistics> tryGetLatestReflectionStatistics()const noexcept;
    [[nodiscard]] b8 setPresentationSettings(const PresentationSettings& settings)noexcept;
    void setFrameLaggedAsyncLightingEnabled(b8 enabled)noexcept;
    [[nodiscard]] Core::GpuTaskGraphRuntimeStatistics deferredTaskGraphRuntimeStatistics()const noexcept;


private:
    NotNullUniquePtr<RendererFramePipeline, PipelineOwner::deleter_type> m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

