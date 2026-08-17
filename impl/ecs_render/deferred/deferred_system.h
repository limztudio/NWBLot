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
    struct SceneShadingGpuData;
    struct SceneLightGpuData;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererDeferredSystem(RendererSystem& renderer);

public:
    // Resolves immutable per-frame data before graph declaration. The shared renderer publishes changed payloads
    // through built-in graph uploads and confirms these CPU mirrors only after the packet accepts.
    [[nodiscard]] bool prepareSceneShadingBufferUploads(
        f32 fallbackAspectRatio,
        ECSRenderDetail::SceneLightGpuData* outLightData,
        usize lightDataCapacity,
        u32& outLightCount,
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
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline();
    [[nodiscard]] bool createDeferredPresentPipeline(Core::Framebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearCsgIntervalTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::Rect& csgClearRect);
    [[nodiscard]] Core::GpuTaskId declareDeferredCompositeTask(
        Core::GpuTaskGraph& graph,
        const Core::GpuTaskDesc& desc,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket& timingTicket
    );
    [[nodiscard]] bool renderDeferredComposite(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets
    );
    [[nodiscard]] bool renderDeferredPresent(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        Core::Framebuffer* presentationFramebuffer
    );


private:
    [[nodiscard]] bool createDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    void resetDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    void resetLaggedLightingHistoryResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool createLaggedLightingHistoryResources(DeferredFrameTargets& targets);
    void logCausticClassificationOnce(const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount, u32 causticLightCount, u32 refractiveInstanceCount);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

