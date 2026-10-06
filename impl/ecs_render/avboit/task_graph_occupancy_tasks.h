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
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>


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
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute };

    // The avboit system, the frame targets, the timing ticket, and the intervals timing slot are
    // required: the pre-pass always records against this frame's ticket. References (not nullable
    // pointers) carry those bindings so a missing binding fails at declaration time instead of
    // silently returning `false` inside Record.
    struct Payload{
        RendererAvboitSystem& avboitSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTiming;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        ECSRenderDetail::TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
        bool hasTransparentRenderers = false;
        bool transparentCsgStreamsUploaded = false;
        bool transparentCsgMaterialGeometryStatesGraphOwned = false;

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            RendererAvboitSystem& avboitSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket& timingTicketIn,
            Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTimingIn
        )
            : avboitSystem(avboitSystemIn)
            , targets(targetsIn)
            , timingTicket(timingTicketIn)
            , transparentCsgIntervalsTiming(transparentCsgIntervalsTimingIn)
            , transparentCsgSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.transparentCsgIntervalsTiming);
    }
};


// Occupancy emulation streams freeze after target clear; regular and CSG stay exclusive.
struct AvboitOccupancyComputeEmulationGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The graphics runtime, the material system, the frame targets, the timing ticket, and the timing slot are required: compute emulation always generates against this frame's bindings. References (not nullable pointers) carry those bindings so a missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererMaterialSystem& materialSystem;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& occupancyTiming;
        ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan plan;
        ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan csgPlan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool conservativeGeometryScissor = false;

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            Core::GraphicsRuntime& graphicsIn,
            RendererMaterialSystem& materialSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket& timingTicketIn,
            Optional<Core::GpuTimingMeasure>& occupancyTimingIn
        )
            : graphics(graphicsIn)
            , materialSystem(materialSystemIn)
            , targets(targetsIn)
            , timingTicket(timingTicketIn)
            , occupancyTiming(occupancyTimingIn)
            , plan(arena)
            , csgPlan(arena)
        {}
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.occupancyTiming);
    }
};


// Shared-buffer occupancy draws cannot batch generators; keep D/R streams as explicit callbacks.
struct AvboitOccupancySharedComputeEmulationGraphTask{
    struct Phase{
        enum Enum : u8{
            Generate,
            Raster,
        };
    };

    // The graphics runtime, the material system, the frame targets, the timing ticket, and the timing slot are required: shared emulation always generates against this frame's bindings. References (not nullable pointers) carry those bindings so a missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererMaterialSystem& materialSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& occupancyTiming;
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
        Phase::Enum phase = Phase::Generate;
    };

    [[nodiscard]] static constexpr Core::GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept{
        return { payload.phase == Phase::Generate ? Core::GpuQueueCapability::Compute : Core::GpuQueueCapability::Graphics };
    }

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.occupancyTiming);
    }
};


// Occupancy follows the interval producer with its own immutable stream.
struct AvboitOccupancyGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute };

    // The avboit system, the frame targets, and the timing ticket are required: occupancy always records against this frame's bindings. References (not nullable pointers) carry those bindings so a missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        bool hasTransparentRenderers = false;
        bool occupancyPhasePrepared = false;
        bool occupancyStreamsUploaded = false;
        bool occupancyMaterialFrameStatesGraphOwned = false;
        bool occupancyMaterialGeometryStatesGraphOwned = false;
        bool occupancyComputeEmulationOutputStatesGraphOwned = false;
        bool occupancyCsgComputeEmulationOutputStatesGraphOwned = false;
        bool generatedGeometryReused = false;
        RendererAvboitSystem& avboitSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot occupancySnapshot;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr;

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            RendererAvboitSystem& avboitSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket& timingTicketIn
        )
            : avboitSystem(avboitSystemIn)
            , targets(targetsIn)
            , timingTicket(timingTicketIn)
            , occupancySnapshot(arena)
        {}
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.occupancyComputeEmulationTiming);
    }
};


struct AvboitDepthWarpGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The avboit system, the frame targets, and the timing ticket are required: depth warp always
    // dispatches against this frame's targets. The timing feedback and scope stay optional: tasks
    // without feedback skip sample attribution. References (not nullable pointers) carry the required
    // bindings so a missing binding fails at declaration time instead of silently returning `false`
    // inside Record.
    struct Payload{
        RendererAvboitSystem& avboitSystem;
        AvboitFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        RendererTaskTimingFeedback* timingFeedback = nullptr;
        const Core::GpuTimingScopeDefinition* timingScope = nullptr;
        mutable Core::GpuTimingSampleAttribution timingAttribution = Core::s_NoGpuTimingSampleAttribution;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;

    static void Discarded(Payload& payload);
};


// Extinction emulation streams freeze after prior uploads; regular and CSG stay exclusive.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

