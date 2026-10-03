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
        const MaterialPassDrawItems& receiverSurfaceDrawItems,
        const CsgFrameGpuData& csgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        usize instanceCount,
        usize materialTypedByteCount,
        bool materialGeometryStatesGraphOwned,
        Optional<Core::GpuTimingMeasure>& intervalTiming
    );
    void renderAvboitOccupancyPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitions& preparedOccupancyDrawItems,
        const CsgFrameGpuData& preparedOccupancyCsgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& preparedOccupancyCsgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& preparedOccupancyFrameBindings,
        usize preparedOccupancyInstanceCount,
        usize preparedOccupancyMaterialTypedByteCount,
        bool occupancyMaterialFrameStatesGraphOwned = false,
        bool occupancyMaterialGeometryStatesGraphOwned = false,
        // Graph may generate alias-free vertices; shared/direct paths keep local work.
        bool occupancyComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr,
        // A frozen CSG-only producer may own this handoff; keep it separate.
        bool occupancyCsgComputeEmulationOutputStatesGraphOwned = false,
        bool generatedGeometryReused = false
    );
    void renderAvboitExtinctionPass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitions& preparedExtinctionDrawItems,
        const CsgFrameGpuData& preparedExtinctionCsgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& preparedExtinctionCsgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& preparedExtinctionFrameBindings,
        usize preparedExtinctionInstanceCount,
        usize preparedExtinctionMaterialTypedByteCount,
        bool extinctionMaterialFrameStatesGraphOwned = false,
        bool extinctionMaterialGeometryStatesGraphOwned = false,
        bool extinctionComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* extinctionComputeEmulationTiming = nullptr,
        // A frozen CSG-only producer may own this handoff; keep it separate from the regular flag.
        bool extinctionCsgComputeEmulationOutputStatesGraphOwned = false,
        bool generatedGeometryReused = false
    );
    void renderAvboitAccumulatePass(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const MaterialPassDrawItemPartitions& preparedAccumulationDrawItems,
        const CsgFrameGpuData& preparedAccumulationCsgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& preparedAccumulationCsgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& preparedAccumulationFrameBindings,
        usize preparedAccumulationInstanceCount,
        usize preparedAccumulationMaterialTypedByteCount,
        bool accumulationMaterialFrameStatesGraphOwned = false,
        bool accumulationMaterialGeometryStatesGraphOwned = false,
        bool accumulationComputeEmulationOutputStatesGraphOwned = false,
        Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr,
        // A frozen CSG-only producer may own this handoff; keep it separate from the regular flag.
        bool accumulationCsgComputeEmulationOutputStatesGraphOwned = false,
        bool generatedGeometryReused = false
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

