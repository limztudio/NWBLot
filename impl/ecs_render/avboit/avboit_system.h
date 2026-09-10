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


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem;
class RendererMaterialSystem;
class RendererShaderSystem;
class RendererAvboitState;
struct CsgFrameGpuData;
struct CsgFrameState;
struct MaterialPassDrawItemPartitions;
struct MaterialPassDrawItems;
namespace ECSRenderDetail{
    struct CsgGraphResourceSnapshot;
    struct MeshFrameBindingSnapshot;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem final : NoCopy{
public:
    RendererAvboitSystem(
        Core::Alloc::GlobalArena& arena,
        Core::GraphicsRuntime& graphics,
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
    // Host owns the graph artifact; AVBOIT owns its stage identifiers; users see the stage boundary.
    void resetTaskGraphStage()noexcept;
    [[nodiscard]] RendererAvboitTaskGraphStageState& taskGraphStage()noexcept{ return m_taskGraphStage; }
    [[nodiscard]] RendererAvboitTaskGraphValidation validateTaskGraphStage(
        const Core::GpuCompiledGraph::ReadView& compiledPlan,
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
        const ECSRenderDetail::CsgGraphResourceSnapshot* preparedTransparentCsgResources = nullptr,
        const ECSRenderDetail::MeshFrameBindingSnapshot* preparedFrameBindings = nullptr,
        usize preparedTransparentCsgInstanceCount = 0u,
        usize preparedTransparentCsgMaterialTypedByteCount = 0u,
        bool preparedTransparentCsgIntervalTargetsGraphOwned = false,
        bool preparedTransparentCsgReceiverSurfaceImageStatesGraphOwned = false,
        bool preparedTransparentCsgIntervalPeelTargetStatesGraphOwned = false,
        bool preparedTransparentCsgReceiverSpanOutputImageStatesGraphOwned = false,
        bool preparedTransparentCsgRemovedIntervalOutputImageStatesGraphOwned = false,
        // Graph interval work declares CSG SRVs/CBVs; unprepared paths keep native setup.
        bool preparedTransparentCsgClipBufferStatesGraphOwned = false,
        // Graph may retain source-buffer SRVs; unprepared work keeps native geometry setup.
        bool preparedTransparentCsgMaterialFrameStatesGraphOwned = false,
        bool preparedTransparentCsgMaterialGeometryStatesGraphOwned = false,
        // Graph may split span/combine dispatches; compat paths keep the native tail.
        bool deferPreparedTransparentCsgIntervalCombine = false,
        // Split callbacks preserve the aggregate timing range; compat keeps local scope.
        Optional<Core::GpuTimingMeasure>* deferredPreparedTransparentCsgIntervalTiming = nullptr
    );
    void renderAvboitOccupancyPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitions* preparedOccupancyDrawItems = nullptr,
        const CsgFrameGpuData* preparedOccupancyCsgFrameData = nullptr,
        const ECSRenderDetail::CsgGraphResourceSnapshot* preparedOccupancyCsgResources = nullptr,
        const ECSRenderDetail::MeshFrameBindingSnapshot* preparedOccupancyFrameBindings = nullptr,
        usize preparedOccupancyInstanceCount = 0u,
        usize preparedOccupancyMaterialTypedByteCount = 0u,
        // Graph declares depth/coverage states first; compat callers keep the bridge.
        bool occupancyStatesGraphOwned = false,
        // Interval producer declares removed-interval outputs first; others keep the UAV handoff.
        bool occupancyCsgIntervalSampleImageStatesGraphOwned = false,
        // Prepared occupancy CSG streams also carry graph-declared clip buffers.
        bool occupancyCsgClipBufferStatesGraphOwned = false,
        bool occupancyMaterialFrameStatesGraphOwned = false,
        bool occupancyMaterialGeometryStatesGraphOwned = false,
        // Graph may generate alias-free vertices; shared/direct paths keep local work.
        bool occupancyComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr,
        // A frozen CSG-only producer may own this handoff; keep it separate.
        bool occupancyCsgComputeEmulationOutputStatesGraphOwned = false
    );
    void renderAvboitExtinctionPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitions* preparedExtinctionDrawItems = nullptr,
        const CsgFrameGpuData* preparedExtinctionCsgFrameData = nullptr,
        const ECSRenderDetail::CsgGraphResourceSnapshot* preparedExtinctionCsgResources = nullptr,
        const ECSRenderDetail::MeshFrameBindingSnapshot* preparedExtinctionFrameBindings = nullptr,
        usize preparedExtinctionInstanceCount = 0u,
        usize preparedExtinctionMaterialTypedByteCount = 0u,
        bool extinctionCsgIntervalSampleImageStatesGraphOwned = false,
        bool extinctionCsgClipBufferStatesGraphOwned = false,
        bool extinctionMaterialFrameStatesGraphOwned = false,
        bool extinctionMaterialGeometryStatesGraphOwned = false,
        bool extinctionComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* extinctionComputeEmulationTiming = nullptr,
        // A frozen CSG-only producer may own this handoff; keep it separate from the regular flag.
        bool extinctionCsgComputeEmulationOutputStatesGraphOwned = false
    );
    void renderAvboitAccumulatePass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitions* preparedAccumulationDrawItems = nullptr,
        const CsgFrameGpuData* preparedAccumulationCsgFrameData = nullptr,
        const ECSRenderDetail::CsgGraphResourceSnapshot* preparedAccumulationCsgResources = nullptr,
        const ECSRenderDetail::MeshFrameBindingSnapshot* preparedAccumulationFrameBindings = nullptr,
        usize preparedAccumulationInstanceCount = 0u,
        usize preparedAccumulationMaterialTypedByteCount = 0u,
        // Graph declares accumulation attachments and depth in a finalizer; compat keeps its bridge.
        bool accumulationFinalStatesGraphOwned = false,
        // Interval producer may hand StorageImage outputs to graph-owned sampling.
        bool accumulationCsgIntervalSampleImageStatesGraphOwned = false,
        bool accumulationCsgClipBufferStatesGraphOwned = false,
        bool accumulationMaterialFrameStatesGraphOwned = false,
        bool accumulationMaterialGeometryStatesGraphOwned = false,
        bool accumulationComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr,
        // A frozen CSG-only producer may own this handoff; keep it separate from the regular flag.
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
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
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
    Core::GraphicsRuntime& m_graphics;
    RendererAvboitState& m_avboitState;
    RendererShaderSystem& m_shaderSystem;
    RendererMaterialSystem& m_materialSystem;
    RendererCsgSystem& m_csgSystem;
    RendererAvboitTaskGraphStageState m_taskGraphStage;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

