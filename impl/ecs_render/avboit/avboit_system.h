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
        usize preparedTransparentCsgMaterialTypedByteCount = 0u,
        bool preparedTransparentCsgIntervalTargetsGraphOwned = false,
        bool preparedTransparentCsgReceiverSurfaceImageStatesGraphOwned = false,
        bool preparedTransparentCsgIntervalPeelTargetStatesGraphOwned = false,
        bool preparedTransparentCsgReceiverSpanOutputImageStatesGraphOwned = false,
        bool preparedTransparentCsgRemovedIntervalOutputImageStatesGraphOwned = false,
        // Prepared graph interval work declares the CSG receiver/cutter SRVs and clip/sample CBVs. Direct and
        // unprepared paths retain their native heap-buffer setup.
        bool preparedTransparentCsgClipBufferStatesGraphOwned = false,
        // The graph can also retain the source-buffer SRVs selected by this frozen stream. Direct and unprepared
        // work retains the material draw thunk's native geometry setup.
        bool preparedTransparentCsgMaterialFrameStatesGraphOwned = false,
        bool preparedTransparentCsgMaterialGeometryStatesGraphOwned = false,
        // The prepared AVBOIT graph can split receiver-span and the final interval-combine dispatch into ordered
        // callbacks. Direct and aggregate compatibility paths leave this false and retain the native in-thunk tail.
        bool deferPreparedTransparentCsgIntervalCombine = false,
        // A split callback preserves the existing aggregate interval timing range across its ordered packet cells.
        // Direct and aggregate compatibility callers leave this null and keep the local timing scope.
        Optional<Core::GpuTimingMeasure>* deferredPreparedTransparentCsgIntervalTiming = nullptr
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
        bool occupancyStatesGraphOwned = false,
        // The prepared transparent interval producer declares the removed-interval outputs before graph-owned
        // occupancy CSG sampling. Other AVBOIT and compatibility consumers retain the native UAV handoff.
        bool occupancyCsgIntervalSampleImageStatesGraphOwned = false,
        // A prepared occupancy CSG stream also has graph-declared clip buffers at the material callback entry.
        bool occupancyCsgClipBufferStatesGraphOwned = false,
        bool occupancyMaterialFrameStatesGraphOwned = false,
        bool occupancyMaterialGeometryStatesGraphOwned = false
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
        usize preparedExtinctionMaterialTypedByteCount = 0u,
        // A prepared graph consumer can receive the interval producer's UAV handoff before extinction sampling.
        // Direct and other compatibility callers retain the native bridge by leaving this false.
        bool extinctionCsgIntervalSampleImageStatesGraphOwned = false,
        bool extinctionCsgClipBufferStatesGraphOwned = false,
        bool extinctionMaterialFrameStatesGraphOwned = false,
        bool extinctionMaterialGeometryStatesGraphOwned = false
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
        usize preparedExtinctionMaterialTypedByteCount = 0u,
        bool extinctionCsgIntervalSampleImageStatesGraphOwned = false,
        bool extinctionCsgClipBufferStatesGraphOwned = false,
        bool extinctionMaterialFrameStatesGraphOwned = false,
        bool extinctionMaterialGeometryStatesGraphOwned = false
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
        bool accumulationFinalStatesGraphOwned = false,
        // The prepared interval producer can hand its StorageImage outputs to graph-owned accumulation sampling.
        // Direct and other compatibility callers retain the native UAV handoff by leaving this false.
        bool accumulationCsgIntervalSampleImageStatesGraphOwned = false,
        bool accumulationCsgClipBufferStatesGraphOwned = false,
        bool accumulationMaterialFrameStatesGraphOwned = false,
        bool accumulationMaterialGeometryStatesGraphOwned = false
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
        bool intervalTargetsGraphOwned,
        bool receiverSurfaceImageStatesGraphOwned,
        bool intervalPeelTargetStatesGraphOwned,
        bool receiverSpanOutputImageStatesGraphOwned,
        bool removedIntervalOutputImageStatesGraphOwned,
        bool csgClipBufferStatesGraphOwned,
        bool materialFrameStatesGraphOwned,
        bool materialGeometryStatesGraphOwned,
        bool deferIntervalCombine,
        Optional<Core::GpuTimingMeasure>* deferredIntervalTiming
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

