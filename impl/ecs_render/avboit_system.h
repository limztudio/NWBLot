// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererAvboitSystem(RendererSystem& renderer);

public:
    [[nodiscard]] bool createAvboitResources();
    [[nodiscard]] bool createAvboitPipelines();
    [[nodiscard]] bool createAvboitFrameTargets(
        DeferredFrameTargets& createdTargets,
        Core::Format::Enum lowRasterFormat,
        Core::Format::Enum accumColorFormat,
        Core::Format::Enum accumExtinctionFormat,
        Core::Format::Enum transmittanceFormat
    );
    [[nodiscard]] bool createAvboitFrameTargetBindingSets(DeferredFrameTargets& createdTargets, AvboitFrameTargets& avboitTargets);
    [[nodiscard]] bool prepareAvboitPassResources(DeferredFrameTargets& targets, const CsgFrameState& csgFrameState);
    void clearAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void renderAvboitPasses(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameState& csgFrameState);
    void dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

