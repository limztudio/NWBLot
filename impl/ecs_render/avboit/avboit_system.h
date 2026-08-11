// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>


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
    [[nodiscard]] bool registerAvboitFrameTargetDescriptors(DeferredFrameTargets& createdTargets, AvboitFrameTargets& avboitTargets);
    [[nodiscard]] bool prepareAvboitPassResources(DeferredFrameTargets& targets, const CsgFrameState& csgFrameState);
    void clearAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void buildTransparentCsgIntervals(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameState& csgFrameState);
    void renderAvboitTransparentCsgIntervals(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItems* preparedTransparentCsgReceiverSurfaceDrawItems = nullptr,
        const CsgFrameGpuData* preparedTransparentCsgFrameData = nullptr,
        usize preparedTransparentCsgInstanceCount = 0u,
        usize preparedTransparentCsgMaterialTypedByteCount = 0u
    );
    void renderAvboitOccupancyPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItemPartitions* preparedOccupancyDrawItems = nullptr,
        const CsgFrameGpuData* preparedOccupancyCsgFrameData = nullptr,
        usize preparedOccupancyInstanceCount = 0u,
        usize preparedOccupancyMaterialTypedByteCount = 0u
    );
    void renderAvboitPostOccupancyPasses(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState
    );
    // AVBOIT alternates raster and compute work. The Graphics-routed plan calls the three slices consecutively,
    // while the dedicated AsyncCompute schedule brackets the two compute dispatches with Graphics submissions.
    void renderAvboitPreDepthWarpPasses(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItems* preparedTransparentCsgReceiverSurfaceDrawItems = nullptr,
        const CsgFrameGpuData* preparedTransparentCsgFrameData = nullptr,
        usize preparedTransparentCsgInstanceCount = 0u,
        usize preparedTransparentCsgMaterialTypedByteCount = 0u
    );
    void renderAvboitExtinctionPass(Core::CommandList& commandList, AvboitFrameTargets& targets, const CsgFrameState& csgFrameState);
    void renderAvboitAccumulatePass(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameState& csgFrameState);
    void renderAvboitPasses(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItems* preparedTransparentCsgReceiverSurfaceDrawItems = nullptr,
        const CsgFrameGpuData* preparedTransparentCsgFrameData = nullptr,
        usize preparedTransparentCsgInstanceCount = 0u,
        usize preparedTransparentCsgMaterialTypedByteCount = 0u
    );
    void dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets);

private:
    void renderPreparedTransparentCsgIntervals(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItems& receiverSurfaceDrawItems,
        const CsgFrameGpuData& csgFrameData,
        usize instanceCount,
        usize materialTypedByteCount
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

