// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitComputeEffectChainBuilder;
class AvboitGeneratedGeometryReuse;
class ObjectGeometryCacheGraph;
class RendererAvboitSystem;
class RendererCsgSystem;
class RendererMaterialSystem;
class RendererTaskTimingFeedback;
struct GraphClearTimingRecordState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT stage owns the transparent CSG interval producer, refraction capture, occupancy, depth warp, extinction, integration, and accumulation.
struct FrameGraphAvboitStageInputs{
    DeferredFrameTargets& targets;
    const DeferredGraphResourceImportResult& resources;
    const CsgFrameState& csgFrameState;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources;
    const ECSRenderDetail::MeshViewGpuData& meshViewState;
    Core::GpuTaskId prefixTask;
    bool hasTransparentRenderers = false;
    bool clearTargets = false;
    bool refractionCaptureEnabled = false;
};

struct FrameGraphAvboitStageTiming{
    Core::GpuTimingSubmissionTicket& preTimingTicket;
    Core::GpuTimingSubmissionTicket& depthWarpTimingTicket;
    Core::GpuTimingSubmissionTicket& extinctionTimingTicket;
    Core::GpuTimingSubmissionTicket& integrationTimingTicket;
    Core::GpuTimingSubmissionTicket& accumulationTimingTicket;
    GraphClearTimingRecordState& clearTimingState;
    GraphClearTimingRecordState& transparentCsgIntervalClearTimingState;
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTiming;
    Optional<Core::GpuTimingMeasure>& occupancyComputeEmulationTiming;
    Optional<Core::GpuTimingMeasure>& extinctionComputeEmulationTiming;
    Optional<Core::GpuTimingMeasure>& accumulationComputeEmulationTiming;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphAvboitStage final : NoCopy{
public:
    FrameGraphAvboitStage(
        Core::GpuTaskGraph& graph,
        Core::Alloc::GlobalArena& arena,
        Core::GraphicsRuntime& graphics,
        RendererMaterialSystem& materialSystem,
        RendererCsgSystem& csgSystem,
        RendererAvboitSystem& avboitSystem,
        RendererTaskTimingFeedback& timingFeedback
    )noexcept;


public:
    // Returns the last AVBOIT task that later lighting, reflection, and composite tasks depend on.
    [[nodiscard]] Expected<Core::GpuTaskId> declare(Core::Alloc::ScratchArena& scratchArena, const FrameGraphAvboitStageInputs& inputs, const FrameGraphAvboitStageTiming& timing, ObjectGeometryCacheGraph& objectGeometry);


private:
    [[nodiscard]] bool declareExtinction(
        Core::Alloc::ScratchArena& scratchArena,
        const FrameGraphAvboitStageInputs& inputs,
        const FrameGraphAvboitStageTiming& timing,
        Core::GpuTaskId avboitDepthWarpCompletionTask,
        bool avboitIntervalOutputsGraphOwned,
        ObjectGeometryCacheGraph& objectGeometry,
        AvboitGeneratedGeometryReuse& generatedGeometry,
        AvboitComputeEffectChainBuilder& avboitComputeEffectChainBuilder
    );
    [[nodiscard]] bool declareAccumulation(
        Core::Alloc::ScratchArena& scratchArena,
        const FrameGraphAvboitStageInputs& inputs,
        const FrameGraphAvboitStageTiming& timing,
        bool avboitIntervalOutputsGraphOwned,
        ObjectGeometryCacheGraph& objectGeometry,
        AvboitGeneratedGeometryReuse& generatedGeometry
    );


private:
    Core::GpuTaskGraph& m_graph;
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    RendererMaterialSystem& m_materialSystem;
    RendererCsgSystem& m_csgSystem;
    RendererAvboitSystem& m_avboitSystem;
    RendererTaskTimingFeedback& m_timingFeedback;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

