// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueRegularComputeEmulationGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The material system, the frame targets, the timing-ticket slot, and the setup readiness flags
    // are required: compute emulation always generates against this frame's bindings. References (not
    // nullable pointers) carry those bindings so a missing binding fails at declaration time instead
    // of silently returning `false` inside Record.
    struct Payload{
        RendererMaterialSystem& materialSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const bool& meshViewSetupReady;
        const bool& sceneShadingSetupReady;
        MeshFrameBindingSnapshot frameBindings;
        OpaqueRegularComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            RendererMaterialSystem& materialSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket*& timingTicketIn,
            const bool& meshViewSetupReadyIn,
            const bool& sceneShadingSetupReadyIn
        );
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Shared outputs interleave generation and rasterization; each instance records one phase.
struct OpaqueRegularSharedComputeEmulationGraphTask{
    struct Phase{
        enum Enum : u8{
            Generate,
            Raster,
        };
    };

    // The material system, the frame targets, the timing-ticket slot, the setup readiness flags, and
    // the opaque timing slot are required: shared emulation always brackets its phase with the frame
    // timing. References (not nullable pointers) carry those bindings so a missing binding fails at
    // declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererMaterialSystem& materialSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const bool& meshViewSetupReady;
        const bool& sceneShadingSetupReady;
        Optional<Core::GpuTimingMeasure>& opaqueRegularTiming;
        MeshFrameBindingSnapshot frameBindings;
        RegularSharedComputeEmulationGraphPlan plan;
        usize drawIndex = 0u;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
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
        Core::DiscardGpuTimingMeasure(&payload.opaqueRegularTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

