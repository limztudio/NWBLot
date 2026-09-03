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


struct AvboitPreGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* transparentCsgIntervalsTiming = nullptr;
        bool hasTransparentRenderers = false;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        ECSRenderDetail::TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
        bool transparentCsgStreamsUploaded = false;
        bool transparentCsgIntervalTargetsGraphOwned = false;
        bool transparentCsgIntervalPeelTargetStatesGraphOwned = false;
        bool transparentCsgReceiverSurfaceImageStatesGraphOwned = false;
        bool transparentCsgReceiverSpanOutputImageStatesGraphOwned = false;
        bool transparentCsgRemovedIntervalOutputImageStatesGraphOwned = false;
        bool deferTransparentCsgIntervalCombine = false;
        bool transparentCsgClipBufferStatesGraphOwned = false;
        bool transparentCsgMaterialFrameStatesGraphOwned = false;
        bool transparentCsgMaterialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : transparentCsgSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.transparentCsgIntervalsTiming);
    }
};


// Occupancy's alias-free compute-emulation stream is independently frozen after the phase's final target clear.
// The regular and CSG-only variants are mutually exclusive: their existing Occupancy callback remains the shared
// raster endpoint, while mixed or shared-output streams retain the established local bridge.
struct AvboitOccupancyComputeEmulationGraphTask{
    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* occupancyTiming = nullptr;
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

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.occupancyTiming);
    }
};


// Two through five regular AVBOIT Occupancy draws sharing one generated-vertex buffer cannot batch their
// generators ahead of rasterization. Keep the original D(A) -> R(A) -> D(B) -> R(B) [-> D(C) -> R(C) -> D(D) ->
// R(D) -> D(E) -> R(E)] stream as explicit primary-Graphics callbacks so the compiler owns every alternating UAV/VertexBuffer
// boundary before the existing Depth-Warp successor.
struct AvboitOccupancySharedComputeEmulationGraphTask{
    enum class Phase : u8{
        Generate,
        Raster,
    };

    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<Core::GpuTimingMeasure>* occupancyTiming = nullptr;
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

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.occupancyTiming);
    }
};


// Occupancy follows the interval producer in the same AVBOIT packet, but has an independent immutable stream:
// each transparent raster phase overwrites the shared material and CSG buffers with phase-local instance indices.
struct AvboitOccupancyGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool hasTransparentRenderers = false;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot occupancySnapshot;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        bool occupancyPhasePrepared = false;
        bool occupancyStreamsUploaded = false;
        bool occupancyCsgIntervalSampleImageStatesGraphOwned = false;
        bool occupancyCsgClipBufferStatesGraphOwned = false;
        bool occupancyMaterialFrameStatesGraphOwned = false;
        bool occupancyMaterialGeometryStatesGraphOwned = false;
        bool occupancyComputeEmulationOutputStatesGraphOwned = false;
        bool occupancyCsgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : occupancySnapshot(arena)
        {}
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.occupancyComputeEmulationTiming);
    }
};


struct AvboitDepthWarpGraphTask{
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

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;

    static void discarded(Payload& payload);
};


// Extinction's alias-free compute-emulation stream is independently frozen after the prior AVBOIT phase uploads.
// The regular and CSG-only variants are mutually exclusive: the existing Extinction callback remains the shared
// raster endpoint, while mixed or shared-output streams retain the established local bridge.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

