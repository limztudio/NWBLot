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
    // The normal graph path declares its nine CopyDest writes before calling this value-only clear. Keep the full
    // transition helper above for direct compatibility callers that cannot declare those resources yet.
    void clearGraphOwnedAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void buildTransparentCsgIntervals(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameState& csgFrameState);
    void renderAvboitTransparentCsgIntervals(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItems* preparedTransparentCsgReceiverSurfaceDrawItems = nullptr,
        const CsgFrameGpuData* preparedTransparentCsgFrameData = nullptr,
        usize preparedTransparentCsgInstanceCount = 0u,
        usize preparedTransparentCsgMaterialTypedByteCount = 0u,
        bool preparedTransparentCsgIntervalTargetsGraphOwned = false
    );
    void renderAvboitOccupancyPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItemPartitions* preparedOccupancyDrawItems = nullptr,
        const CsgFrameGpuData* preparedOccupancyCsgFrameData = nullptr,
        usize preparedOccupancyInstanceCount = 0u,
        usize preparedOccupancyMaterialTypedByteCount = 0u,
        // The normal task-graph path declares depth as ShaderResource and coverage as UnorderedAccess before this
        // material pass. Direct compatibility callers retain the explicit bridge.
        bool occupancyStatesGraphOwned = false
    );
    void renderAvboitPostOccupancyPasses(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItemPartitions* preparedExtinctionDrawItems = nullptr,
        const CsgFrameGpuData* preparedExtinctionCsgFrameData = nullptr,
        usize preparedExtinctionInstanceCount = 0u,
        usize preparedExtinctionMaterialTypedByteCount = 0u
    );
    // Records the depth-warp, extinction, and integration slices only. The graph uses this boundary to publish
    // the phase-local immutable accumulation stream after integration and before the final raster pass.
    void renderAvboitPostOccupancyPreAccumulationPasses(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItemPartitions* preparedExtinctionDrawItems = nullptr,
        const CsgFrameGpuData* preparedExtinctionCsgFrameData = nullptr,
        usize preparedExtinctionInstanceCount = 0u,
        usize preparedExtinctionMaterialTypedByteCount = 0u
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
    void renderAvboitExtinctionPass(
        Core::CommandList& commandList,
        AvboitFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItemPartitions* preparedExtinctionDrawItems = nullptr,
        const CsgFrameGpuData* preparedExtinctionCsgFrameData = nullptr,
        usize preparedExtinctionInstanceCount = 0u,
        usize preparedExtinctionMaterialTypedByteCount = 0u
    );
    void renderAvboitAccumulatePass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameState& csgFrameState,
        const MaterialPassDrawItemPartitions* preparedAccumulationDrawItems = nullptr,
        const CsgFrameGpuData* preparedAccumulationCsgFrameData = nullptr,
        usize preparedAccumulationInstanceCount = 0u,
        usize preparedAccumulationMaterialTypedByteCount = 0u,
        // The normal task-graph path declares the two accumulation attachments and read-only deferred depth as
        // ShaderResource in a following Graphics finalizer. Direct compatibility callers retain their explicit
        // framebuffer-state bridge.
        bool accumulationFinalStatesGraphOwned = false
    );
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
        usize materialTypedByteCount,
        bool intervalTargetsGraphOwned
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

