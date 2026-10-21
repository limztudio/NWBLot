// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ObjectGeometryCacheGraph;
class RendererCsgSystem;
class RendererMaterialSystem;
struct CsgFrameGpuData;
struct DeferredGraphResourceImportResult;
struct MaterialPassDrawItemPartitions;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Receiver ranges, cutters, clip context slots, interval sample state, and bindless slots.
inline constexpr usize s_OpaqueCsgFrameGpuResourceUseCount = 5u;

struct OpaqueCsgIntervalStageInputs{
    DeferredFrameTargets& targets;
    const DeferredGraphResourceImportResult& resources;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources;
    const MaterialPassDrawItemPartitions& opaqueDrawItems;
    const CsgFrameGpuData& csgFrameData;
    const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& receiverSpanResourceUses;
    const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& intervalCombineResourceUses;
    Core::GpuTimingSubmissionTicket** intervalSampleTimingTicket = nullptr;
    Core::TextureSubresourceSet removedIntervalSubresources{};
    Core::TextureSubresourceSet removedIntervalCountSubresources{};
    Core::BufferRange materialInstanceRange{};
    Core::BufferRange materialTypedRange{};
    Core::BufferRange csgReceiverRange{};
    Core::BufferRange csgCutterRange{};
    Core::GpuTaskId dependencyTask;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool hasOpaqueDrawItems = false;
    bool hasCsgFrameGpuWork = false;
};

struct OpaqueCsgIntervalStageTasks{
    Core::GpuTaskId receiverSpanTask;
    Core::GpuTaskId intervalCombineTask;
    Core::GpuTaskId intervalSampleComputeEmulationTask;
    Core::GpuTaskId intervalSampleTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Opaque CSG work after the G-buffer: receiver spans, interval combine, the optional interval-sample compute producer, and interval sample.
// The payloads are built before the G-buffer task freezes its upload state, then move into the graph in declare().
class OpaqueCsgIntervalStage final : NoCopy{
public:
    OpaqueCsgIntervalStage(
        Core::GpuTaskGraph& graph,
        Core::Alloc::GlobalArena& arena,
        Core::GraphicsRuntime& graphics,
        RendererMaterialSystem& materialSystem,
        RendererCsgSystem& csgSystem,
        DeferredFrameTargets& targets,
        Core::GpuTimingSubmissionTicket*& receiverSpanTimingTicket,
        Core::GpuTimingSubmissionTicket*& intervalCombineTimingTicket,
        Core::GpuTimingSubmissionTicket*& intervalSampleTimingTicket,
        const bool& meshViewSetupReady,
        const bool& sceneShadingSetupReady,
        Optional<Core::GpuTimingMeasure>& intervalSampleComputeEmulationTiming
    )noexcept;


public:
    // Copies the frozen opaque draw stream and the G-buffer upload state into the CSG payloads.
    void capturePayloads(
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        const MaterialPassDrawItemPartitions& opaqueDrawItems,
        const CsgFrameGpuData& csgFrameData,
        usize instanceCount,
        usize materialTypedByteCount,
        bool materialDrawBuffersUploaded,
        bool csgFrameBuffersUploaded,
        bool hasOpaqueDrawItems,
        bool hasOpaqueCsgFrameWork
    );
    [[nodiscard]] Expected<OpaqueCsgIntervalStageTasks> declare(Core::Alloc::ScratchArena& scratchArena, const OpaqueCsgIntervalStageInputs& inputs, ObjectGeometryCacheGraph& objectGeometry);


private:
    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
    Optional<Core::GpuTimingMeasure>& m_intervalSampleComputeEmulationTiming;
    ECSRenderDetail::CsgReceiverSpanBuildGraphTask::Payload m_receiverSpanPayload;
    ECSRenderDetail::CsgIntervalCombineGraphTask::Payload m_intervalCombinePayload;
    ECSRenderDetail::CsgIntervalSampleGraphTask::Payload m_intervalSamplePayload;
    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask::Payload m_intervalSampleComputeEmulationPayload;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

