// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/reflection/composite_inputs.adhd.h>
#include <impl/ecs_render/deferred/presentation_settings.adhd.h>
#include <impl/ecs_render/deferred/scene_shading_uploads.h>

#include <core/alloc/global.h>
#include <core/ecs/global.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GlobalArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct SceneShadingGpuData;
    struct SceneLightGpuData;
};

class RendererDeferredState;
class RendererShaderSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem final : NoCopy{
public:
    RendererDeferredSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::GraphicsRuntime& graphics, RendererDeferredState& deferredState, RendererShaderSystem& shaderSystem)noexcept;


public:
    [[nodiscard]] DeferredLightingGraphResources lightingGraphResources()const noexcept;
    void invalidateSceneLightingUploadMirrors()noexcept;
    void invalidateResources()noexcept;
    [[nodiscard]] b8 setPresentationSettings(const PresentationSettings& settings)noexcept;

    // Resolve immutable per-frame data; confirm CPU mirrors only after packet accepts.
    [[nodiscard]] SceneShadingBufferUploads prepareSceneShadingBufferUploads(f32 fallbackAspectRatio, const RayTracingLightingClassificationInput& rayTracingInput)noexcept;
    void confirmSceneShadingBufferUploads(Span<const ECSRenderDetail::SceneLightGpuData> lights, b8 lightUploadRequired, const ECSRenderDetail::SceneShadingGpuData& sceneShadingState, b8 sceneShadingUploadRequired)noexcept;
    [[nodiscard]] b8 createDeferredLightingResources();
    [[nodiscard]] b8 createDeferredLightingPipeline();
    [[nodiscard]] Expected<Core::GpuTaskId> declareDeferredLightingTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, b8 useLaggedLightingHistory, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket);
    [[nodiscard]] b8 renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets, b8 useLaggedLightingHistory = false);
    [[nodiscard]] Expected<DeferredFrameTargets> createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] b8 createDeferredFrameTargetResources(DeferredFrameTargets& targets, Core::Sampler& avboitLinearSampler);
    [[nodiscard]] b8 createDeferredCompositeResources();
    [[nodiscard]] b8 createDeferredCompositePipeline();
    [[nodiscard]] b8 createDeferredPresentPipeline(Core::Framebuffer& presentationFramebuffer);
    void resetDeferredFrameTargets(DeferredFrameTargets& targets);
    [[nodiscard]] Expected<Core::GpuTaskId> declareDeferredCompositeTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const ReflectionCompositeInputs& reflectionInputs, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket);
    [[nodiscard]] b8 renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, const ReflectionCompositeInputs& reflectionInputs);
    [[nodiscard]] b8 renderDeferredPresent(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::AcquiredPresentationFrame& presentationFrame, Core::GpuDescriptorHandle outputLayerImage);


private:
    [[nodiscard]] b8 createDeferredBindlessFrameResources(DeferredFrameTargets& targets, Core::Sampler& avboitLinearSampler);
    void resetDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    void resetLaggedLightingHistoryResources(DeferredFrameTargets& targets);
    [[nodiscard]] b8 createLaggedLightingHistoryResources(DeferredFrameTargets& targets);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    RendererDeferredState& m_deferredState;
    RendererShaderSystem& m_shaderSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

