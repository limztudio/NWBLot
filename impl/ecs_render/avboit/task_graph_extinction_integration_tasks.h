// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererAvboitSystem;
class RendererTaskTimingFeedback;
struct AvboitFrameTargets;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitExtinctionComputeEmulationGraphTask{
    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* extinctionTiming = nullptr;
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


// Two through five regular Extinction draws targeting one persistent generated-vertex buffer must retain their
// native D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) -> R(D) -> D(E) -> R(E)] order.
// Each phase is graph-visible so the compiler lowers the alternating UAV/VertexBuffer states before the common
// typed Integration tail consumes the packed outputs.
struct AvboitExtinctionSharedComputeEmulationGraphTask{
    enum class Phase : u8{
        Generate,
        Raster,
    };

    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* extinctionTiming = nullptr;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
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


struct AvboitExtinctionGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot extinctionSnapshot;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        bool extinctionPhasePrepared = false;
        bool extinctionCsgIntervalSampleImageStatesGraphOwned = false;
        bool extinctionCsgClipBufferStatesGraphOwned = false;
        bool extinctionMaterialFrameStatesGraphOwned = false;
        bool extinctionMaterialGeometryStatesGraphOwned = false;
        bool extinctionComputeEmulationOutputStatesGraphOwned = false;
        bool extinctionCsgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* extinctionComputeEmulationTiming = nullptr;
        bool hasTransparentRenderers = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : extinctionSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload);
};


struct AvboitIntegrationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        RendererTaskTimingFeedback* timingFeedback = nullptr;
        const Core::GpuTimingScopeDefinition* timingScope = nullptr;
        mutable Core::GpuTimingSampleAttribution timingAttribution = Core::s_NoGpuTimingSampleAttribution;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token);

    static void discarded(Payload& payload);
};


// Accumulation's alias-free compute-emulation stream is independently frozen after Integration and its immutable
// upload chain. The regular and CSG-only variants are mutually exclusive: the existing Accumulation callback
// remains the shared raster endpoint and its following finalizer retains the terminal attachment handoff.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

