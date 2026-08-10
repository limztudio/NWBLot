// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct SceneLightGpuData;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererDeferredSystem(RendererSystem& renderer);

public:
    [[nodiscard]] bool updateSceneShadingBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    [[nodiscard]] bool createDeferredLightingResources();
    [[nodiscard]] bool createDeferredLightingPipeline();
    [[nodiscard]] Core::GpuTaskId declareDeferredLightingTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        bool useLaggedLightingHistory,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets, bool useLaggedLightingHistory = false);
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline();
    [[nodiscard]] bool createDeferredPresentPipeline(Core::Framebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void clearCsgIntervalTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::Rect& csgClearRect);
    [[nodiscard]] Core::GpuTaskId declareDeferredCompositeTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredPresent(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer);
    // One-time target-generation upload shared by every consumer of DeferredBindlessResourceSlots. Shadow tracing runs
    // before deferred lighting, so it must be able to make the slot cbuffer resident during shadow preparation.
    [[nodiscard]] bool uploadDeferredBindlessFrameResources(Core::CommandList& commandList, DeferredFrameTargets& targets);


private:
    [[nodiscard]] bool createDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    void resetDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    void resetLaggedLightingHistoryResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool createLaggedLightingHistoryResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool uploadLaggedLightingHistoryResources(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void logCausticClassificationOnce(const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount, u32 causticLightCount, u32 refractiveInstanceCount);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

