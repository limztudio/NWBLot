// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem;
class RendererMaterialSystem;
class RendererCsgSystem;
class RendererAvboitSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitAccumulationComputeEmulationGraphTask{
    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererMeshSystem* meshSystem = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        RendererCsgSystem* csgSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* accumulationTiming = nullptr;
        ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan plan;
        ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan csgPlan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool csgIntervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : plan(arena)
            , csgPlan(arena)
        {}
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing);

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload);
};


// Two or three regular AVBOIT Accumulation draws sharing one generated-vertex buffer cannot batch their generators
// ahead of rasterization. Keep the original D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C)] stream as explicit
// primary-Graphics callbacks so the compiler owns every alternating UAV/VertexBuffer boundary.
struct AvboitAccumulationSharedComputeEmulationGraphTask{
    enum class Phase : u8{
        Generate,
        Raster,
    };

    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererMeshSystem* meshSystem = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* accumulationTiming = nullptr;
        ECSRenderDetail::RegularSharedComputeEmulationGraphPlan plan;
        usize drawIndex = 0u;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool beginTiming = false;
        bool finishTiming = false;
        Phase phase = Phase::Generate;
    };

    static void discardTiming(Optional<Core::GpuTimingMeasure>* const timing);

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload);
};


struct AvboitAccumulationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot accumulationSnapshot;
        bool accumulationPhasePrepared = false;
        bool accumulationCsgIntervalSampleImageStatesGraphOwned = false;
        bool accumulationCsgClipBufferStatesGraphOwned = false;
        bool accumulationMaterialFrameStatesGraphOwned = false;
        bool accumulationMaterialGeometryStatesGraphOwned = false;
        bool accumulationComputeEmulationOutputStatesGraphOwned = false;
        bool accumulationCsgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr;
        bool hasTransparentRenderers = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : accumulationSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload);
};


// Accumulation produces attachments that Deferred Composite samples on Compute and leaves the read-only deferred
// depth attachment in DepthRead. Keep all ShaderResource handoffs in a Graphics task immediately after
// rasterization, so no following packet has to name a framebuffer attachment source state. The task intentionally
// records no native work; packet-prologue barriers are the entire contract.
struct AvboitAccumulationFinalizeGraphTask{
    struct Payload{};

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

