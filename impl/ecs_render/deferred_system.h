// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"


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
    [[nodiscard]] bool createDeferredLightingPipeline(DeferredFrameTargets& targets);
    // Rebuilds the deferred lighting binding set once the surfel pool / cell-head / params buffers exist. Those are
    // created lazily in the prepare phase (after this set already exists), so the set is first built with null surfel
    // bindings and rebuilt to point at the real buffers when the pool appears. The surfel buffers do not ping-pong, so
    // the rebuild is a one-shot upgrade (idempotent once bound to the current pool).
    void rebuildDeferredLightingGiBindings();
    [[nodiscard]] bool renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline(Core::Framebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, bool clearCsgTargets, const Core::Rect& csgClearRect);
    void clearCsgIntervalTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::Rect& csgClearRect);
    [[nodiscard]] bool renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer);


private:
    void logCausticClassificationOnce(const ECSRenderDetail::SceneLightGpuData* lights, u32 lightCount, u32 causticLightCount, u32 refractiveInstanceCount);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

