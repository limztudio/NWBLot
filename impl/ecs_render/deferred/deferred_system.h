// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/reflection/composite_inputs.h>
#include <impl/ecs_render/deferred/presentation_settings.h>

#include <core/alloc/global.h>
#include <core/ecs/global.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


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
    RendererDeferredSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::GraphicsRuntime& graphics,
        RendererDeferredState& deferredState,
        RendererShaderSystem& shaderSystem
    );


public:
    [[nodiscard]] DeferredLightingGraphResources lightingGraphResources()const noexcept;
    void invalidateSceneLightingUploadMirrors()noexcept;
    void invalidateResources();
    [[nodiscard]] bool setPresentationSettings(const PresentationSettings& settings);

    // Resolve immutable per-frame data; confirm CPU mirrors only after packet accepts.
    [[nodiscard]] bool prepareSceneShadingBufferUploads(
        f32 fallbackAspectRatio,
        const RayTracingLightingClassificationInput& rayTracingInput,
        ECSRenderDetail::SceneLightGpuData* outLightData,
        usize lightDataCapacity,
        u32& outLightCount,
        RayTracingLightingClassification& outRayTracingClassification,
        bool& outLightUploadRequired,
        ECSRenderDetail::SceneShadingGpuData& outSceneShadingState,
        bool& outSceneShadingUploadRequired
    );
    void confirmSceneShadingBufferUploads(
        const ECSRenderDetail::SceneLightGpuData* lightData,
        u32 lightCount,
        bool lightUploadRequired,
        const ECSRenderDetail::SceneShadingGpuData& sceneShadingState,
        bool sceneShadingUploadRequired
    );
    [[nodiscard]] bool createDeferredLightingResources();
    [[nodiscard]] bool createDeferredLightingPipeline();
    [[nodiscard]] Core::GpuTaskId declareDeferredLightingTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        bool useLaggedLightingHistory,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool renderDeferredLighting(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        bool useLaggedLightingHistory = false
    );
    [[nodiscard]] bool createDeferredFrameTargets(DeferredFrameTargets& outTargets, u32 width, u32 height);
    [[nodiscard]] bool createDeferredFrameTargetResources(DeferredFrameTargets& targets, Core::Sampler& avboitLinearSampler);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline();
    [[nodiscard]] bool createDeferredPresentPipeline(Core::Framebuffer* presentationFramebuffer);
    void resetDeferredFrameTargets(DeferredFrameTargets& targets);
    [[nodiscard]] Core::GpuTaskId declareDeferredCompositeTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket,
        const ReflectionCompositeInputs& reflectionInputs
    );
    [[nodiscard]] bool renderDeferredComposite(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const ReflectionCompositeInputs& reflectionInputs
    );
    [[nodiscard]] bool renderDeferredPresent(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const Core::AcquiredPresentationFrame& presentationFrame
    );


private:
    [[nodiscard]] bool createDeferredBindlessFrameResources(DeferredFrameTargets& targets, Core::Sampler& avboitLinearSampler);
    void resetDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    void resetLaggedLightingHistoryResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool createLaggedLightingHistoryResources(DeferredFrameTargets& targets);


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

