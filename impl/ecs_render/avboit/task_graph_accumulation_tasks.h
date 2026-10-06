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
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitAccumulationComputeEmulationGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The graphics runtime, the material system, the frame targets, the timing ticket, and the timing slot are required: compute emulation always generates against this frame's bindings. References (not nullable pointers) carry those bindings so a missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererMaterialSystem& materialSystem;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& accumulationTiming;
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
            Optional<Core::GpuTimingMeasure>& accumulationTimingIn
        )
            : graphics(graphicsIn)
            , materialSystem(materialSystemIn)
            , targets(targetsIn)
            , timingTicket(timingTicketIn)
            , accumulationTiming(accumulationTimingIn)
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
        Core::DiscardGpuTimingMeasure(&payload.accumulationTiming);
    }
};


// Shared-buffer accumulation draws cannot batch generators; keep D/R streams explicit.
struct AvboitAccumulationSharedComputeEmulationGraphTask{
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
        Optional<Core::GpuTimingMeasure>& accumulationTiming;
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
        Core::DiscardGpuTimingMeasure(&payload.accumulationTiming);
    }
};


struct AvboitAccumulationGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute };

    // The avboit system, the frame targets, and the timing ticket are required: accumulation always records against this frame's bindings. References (not nullable pointers) carry those bindings so a missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererAvboitSystem& avboitSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::TransparentMaterialPassGraphSnapshot accumulationSnapshot;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        bool accumulationPhasePrepared = false;
        bool accumulationMaterialFrameStatesGraphOwned = false;
        bool accumulationMaterialGeometryStatesGraphOwned = false;
        bool accumulationComputeEmulationOutputStatesGraphOwned = false;
        bool accumulationCsgComputeEmulationOutputStatesGraphOwned = false;
        bool generatedGeometryReused = false;
        Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr;
        bool hasTransparentRenderers = false;

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            RendererAvboitSystem& avboitSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket& timingTicketIn
        )
            : avboitSystem(avboitSystemIn)
            , targets(targetsIn)
            , timingTicket(timingTicketIn)
            , accumulationSnapshot(arena)
        {}
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.accumulationComputeEmulationTiming);
    }
};


// Keep ShaderResource handoffs in a Graphics task right after rasterization; barriers are the contract.
struct AvboitAccumulationFinalizeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    struct Payload{};

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    )noexcept;

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

