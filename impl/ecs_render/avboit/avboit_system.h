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
struct CsgFrameGpuDataView;
class CsgFrameState;
struct MaterialPassDrawItemPartitionsView;
struct MaterialPassDrawItemsView;
namespace ECSRenderDetail{
    struct CsgGraphResourceSnapshot;
    struct MeshFrameBindingSnapshot;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem final : NoCopy{
public:
    RendererAvboitSystem(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, RendererAvboitState& avboitState, RendererShaderSystem& shaderSystem, RendererMaterialSystem& materialSystem, RendererCsgSystem& csgSystem)noexcept;


public:
    [[nodiscard]] b8 shouldClearTargets(b8 hasTransparentRenderers)const noexcept;
    [[nodiscard]] b8 captureTargetClearState()const noexcept;
    void restoreTargetClearState(b8 targetsNeedClear)noexcept;
    void markFrameTargetUsage(b8 hasTransparentRenderers)noexcept;
    void invalidateResources()noexcept;


public:
    // Host owns the graph artifact; AVBOIT owns its stage identifiers; users see the stage boundary.
    void resetTaskGraphStage()noexcept;
    [[nodiscard]] RendererAvboitTaskGraphStageState& taskGraphStage()noexcept{ return m_taskGraphStage; }
    [[nodiscard]] RendererAvboitTaskGraphValidation validateTaskGraphStage(const Core::GpuCompiledGraph::ReadView& compiledGraph, b8 clearTargets, b8 hasTransparentRenderers)const noexcept;
    [[nodiscard]] b8 appendTaskGraphTimingTickets(const RendererAvboitTaskGraphValidation& validation, usize bindingCapacity, RendererAvboitTaskGraphTimingTickets& timingTickets, Core::GpuTaskGraphTaskTimingTicket* bindings, usize& bindingCount)const;


public:
    [[nodiscard]] b8 createAvboitSampler();
    [[nodiscard]] b8 createAvboitResources();
    [[nodiscard]] b8 createAvboitPipelines();
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    [[nodiscard]] b8 createAvboitFrameTargets(DeferredFrameTargets& createdTargets);
    [[nodiscard]] b8 registerAvboitFrameTargetDescriptors(DeferredFrameTargets& createdTargets, AvboitFrameTargets& avboitTargets);
    [[nodiscard]] Core::Sampler& linearSampler()const noexcept;
    [[nodiscard]] b8 prepareAvboitPassResources(Core::Alloc::ScratchArena& scratchArena, const CsgFrameState& csgFrameState, DeferredFrameTargets& targets);
    void renderAvboitTransparentCsgIntervals(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemsView& receiverSurfaceDrawItems,
        const CsgFrameGpuDataView& csgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        usize instanceCount,
        usize materialTypedByteCount,
        b8 materialGeometryStatesGraphOwned,
        Optional<Core::GpuTimingMeasure>& intervalTiming
    );
    void renderAvboitOccupancyPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitionsView& preparedOccupancyDrawItems,
        const CsgFrameGpuDataView& preparedOccupancyCsgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& preparedOccupancyCsgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& preparedOccupancyFrameBindings,
        usize preparedOccupancyInstanceCount,
        usize preparedOccupancyMaterialTypedByteCount,
        b8 occupancyMaterialFrameStatesGraphOwned = false,
        b8 occupancyMaterialGeometryStatesGraphOwned = false,
        // Graph may generate alias-free vertices; shared/direct paths keep local work.
        b8 occupancyComputeEmulationOutputStatesGraphOwned = false,
        // A frozen CSG-only producer may own this handoff; keep it separate.
        b8 occupancyCsgComputeEmulationOutputStatesGraphOwned = false, b8 generatedGeometryReused = false,
        Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr
    );
    void renderAvboitExtinctionPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitionsView& preparedExtinctionDrawItems,
        const CsgFrameGpuDataView& preparedExtinctionCsgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& preparedExtinctionCsgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& preparedExtinctionFrameBindings,
        usize preparedExtinctionInstanceCount,
        usize preparedExtinctionMaterialTypedByteCount,
        b8 extinctionMaterialFrameStatesGraphOwned = false,
        b8 extinctionMaterialGeometryStatesGraphOwned = false,
        b8 extinctionComputeEmulationOutputStatesGraphOwned = false,
        // A frozen CSG-only producer may own this handoff; keep it separate from the regular flag.
        b8 extinctionCsgComputeEmulationOutputStatesGraphOwned = false, b8 generatedGeometryReused = false,
        Optional<Core::GpuTimingMeasure>* extinctionComputeEmulationTiming = nullptr
    );
    void renderAvboitAccumulatePass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitionsView& preparedAccumulationDrawItems,
        const CsgFrameGpuDataView& preparedAccumulationCsgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& preparedAccumulationCsgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& preparedAccumulationFrameBindings,
        usize preparedAccumulationInstanceCount,
        usize preparedAccumulationMaterialTypedByteCount,
        b8 accumulationMaterialFrameStatesGraphOwned = false,
        b8 accumulationMaterialGeometryStatesGraphOwned = false,
        b8 accumulationComputeEmulationOutputStatesGraphOwned = false,
        // A frozen CSG-only producer may own this handoff; keep it separate from the regular flag.
        b8 accumulationCsgComputeEmulationOutputStatesGraphOwned = false, b8 generatedGeometryReused = false,
        Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr
    );
    void dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets, Core::GpuTimingSampleAttribution timingAttribution = Core::s_NoGpuTimingSampleAttribution, b8* timingRecorded = nullptr);
    void dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets, Core::GpuTimingSampleAttribution timingAttribution = Core::s_NoGpuTimingSampleAttribution, b8* timingRecorded = nullptr);


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

