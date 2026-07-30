// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>


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
    [[nodiscard]] bool renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets, bool useLaggedLightingHistory = false);
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline();
    [[nodiscard]] bool createDeferredPresentPipeline(Core::Framebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, bool clearCsgTargets, const Core::Rect& csgClearRect, bool clearSurfelIrradiance);
    void clearCsgIntervalTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::Rect& csgClearRect);
    [[nodiscard]] bool renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredPresent(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer);
    // One-time target-generation upload shared by every consumer of DeferredBindlessResourceSlots. Shadow tracing runs
    // before deferred lighting, so it must be able to make the slot cbuffer resident during shadow preparation.
    [[nodiscard]] bool uploadDeferredBindlessFrameResources(Core::CommandList& commandList, DeferredFrameTargets& targets);
    // Lazily creates the opt-in lighting-history images and their alternate selector cbuffer. Keeping this outside the
    // normal target generation avoids reserving the extra full-resolution images unless the latency trade-off is used.
    [[nodiscard]] bool ensureLaggedLightingHistoryResources(DeferredFrameTargets& targets);


private:
    [[nodiscard]] bool createDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    void resetDeferredBindlessFrameResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool uploadLaggedLightingHistoryResources(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void resetLaggedLightingHistoryResources(DeferredFrameTargets& targets);
    void logCausticClassificationOnce(const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount, u32 causticLightCount, u32 refractiveInstanceCount);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

