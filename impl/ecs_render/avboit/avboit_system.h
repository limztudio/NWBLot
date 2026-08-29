// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/avboit/task_graph_stage.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/alloc/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GlobalArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem;
class RendererDeferredState;
class RendererMaterialSystem;
class RendererShaderSystem;
class RendererAvboitState;
struct CsgFrameGpuData;
struct CsgFrameState;
struct MaterialPassDrawItemPartitions;
struct MaterialPassDrawItems;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem final : NoCopy{
public:
    RendererAvboitSystem(
        Core::Alloc::GlobalArena& arena,
        Core::Graphics& graphics,
        RendererDeferredState& deferredState,
        RendererAvboitState& avboitState,
        RendererShaderSystem& shaderSystem,
        RendererMaterialSystem& materialSystem,
        RendererCsgSystem& csgSystem
    );

public:
    [[nodiscard]] bool shouldClearTargets(bool hasTransparentRenderers)const noexcept;
    [[nodiscard]] bool captureTargetClearState()const noexcept;
    void restoreTargetClearState(bool targetsNeedClear)noexcept;
    void markFrameTargetUsage(bool hasTransparentRenderers)noexcept;
    void invalidateResources();

public:
    // The graph host owns the shared graph artifact; AVBOIT owns every graph-local identifier required by its
    // transparency stage. Cross-domain users consume the typed stage boundary instead of those local identifiers.
    void resetTaskGraphStage()noexcept;
    [[nodiscard]] RendererAvboitTaskGraphStageState& taskGraphStage()noexcept{ return m_taskGraphStage; }
    [[nodiscard]] const RendererAvboitTaskGraphStageState& taskGraphStage()const noexcept{ return m_taskGraphStage; }
    [[nodiscard]] RendererAvboitTaskGraphValidation validateTaskGraphStage(
        const Core::GpuCompiledGraph& compiledGraph,
        bool clearTargets,
        bool hasTransparentRenderers
    )const;
    [[nodiscard]] bool appendTaskGraphTimingTickets(
        const RendererAvboitTaskGraphValidation& validation,
        RendererAvboitTaskGraphTimingTickets& timingTickets,
        Core::GpuTaskGraphTaskTimingTicket* bindings,
        usize bindingCapacity,
        usize& bindingCount
    )const;


public:
    [[nodiscard]] bool createAvboitResources();
    [[nodiscard]] bool createAvboitPipelines();
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    [[nodiscard]] bool createAvboitFrameTargets(DeferredFrameTargets& createdTargets);
    [[nodiscard]] bool registerAvboitFrameTargetDescriptors(DeferredFrameTargets& createdTargets, AvboitFrameTargets& avboitTargets);
    [[nodiscard]] Core::Sampler& linearSampler()const noexcept;
    [[nodiscard]] bool prepareAvboitPassResources(DeferredFrameTargets& targets, const CsgFrameState& csgFrameState);
    void renderAvboitTransparentCsgIntervals(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
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
        bool occupancyMaterialGeometryStatesGraphOwned = false,
        // The graph can generate alias-free regular emulation vertices in the preceding producer.
        // Shared-output and direct paths leave this false and retain local dispatch/raster work.
        bool occupancyComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr,
        // A distinct frozen CSG-only producer may own the same handoff. It remains separate from the regular flag
        // so mixed CSG streams keep their compatibility interleaving.
        bool occupancyCsgComputeEmulationOutputStatesGraphOwned = false
    );
    void renderAvboitExtinctionPass(
        Core::CommandList& commandList,
        AvboitFrameTargets& targets,
        const MaterialPassDrawItemPartitions* preparedExtinctionDrawItems = nullptr,
        const CsgFrameGpuData* preparedExtinctionCsgFrameData = nullptr,
        usize preparedExtinctionInstanceCount = 0u,
        usize preparedExtinctionMaterialTypedByteCount = 0u,
        bool extinctionCsgIntervalSampleImageStatesGraphOwned = false,
        bool extinctionCsgClipBufferStatesGraphOwned = false,
        bool extinctionMaterialFrameStatesGraphOwned = false,
        bool extinctionMaterialGeometryStatesGraphOwned = false,
        bool extinctionComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* extinctionComputeEmulationTiming = nullptr,
        // A distinct frozen CSG-only producer may own this handoff. Keep it separate from the regular flag so
        // mixed CSG streams retain their local dispatch/raster interleaving.
        bool extinctionCsgComputeEmulationOutputStatesGraphOwned = false
    );
    void renderAvboitAccumulatePass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
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
        bool accumulationMaterialGeometryStatesGraphOwned = false,
        bool accumulationComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr,
        // A distinct frozen CSG-only producer may own this handoff. Keep it separate from the regular flag so
        // mixed CSG streams retain their local dispatch/raster interleaving.
        bool accumulationCsgComputeEmulationOutputStatesGraphOwned = false
    );
    void dispatchAvboitDepthWarp(
        Core::CommandList& commandList,
        AvboitFrameTargets& targets,
        Core::GpuTimingSampleAttribution timingAttribution = Core::s_NoGpuTimingSampleAttribution,
        bool* timingRecorded = nullptr
    );
    void dispatchAvboitIntegration(
        Core::CommandList& commandList,
        AvboitFrameTargets& targets,
        Core::GpuTimingSampleAttribution timingAttribution = Core::s_NoGpuTimingSampleAttribution,
        bool* timingRecorded = nullptr
    );

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


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::Graphics& m_graphics;
    RendererDeferredState& m_deferredState;
    RendererAvboitState& m_avboitState;
    RendererShaderSystem& m_shaderSystem;
    RendererMaterialSystem& m_materialSystem;
    RendererCsgSystem& m_csgSystem;
    RendererAvboitTaskGraphStageState m_taskGraphStage;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

