// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererDeferredSystem(RendererSystem& renderer);

public:
    [[nodiscard]] bool updateSceneShadingBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    [[nodiscard]] bool createDeferredLightingResources();
    [[nodiscard]] bool createDeferredLightingPipeline(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline(Core::Framebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets, bool clearCsgTargets);
    [[nodiscard]] bool renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

