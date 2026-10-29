// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueCsgReceiverComputeEmulationGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings required: the receiver emulation always generates against this frame's bindings.
    struct Payload{
        RendererMaterialSystem& materialSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const b8& meshViewSetupReady;
        const b8& sceneShadingSetupReady;
        MeshFrameBindingSnapshot frameBindings;
        CsgGraphResourceSnapshot csgResources;
        OpaqueCsgReceiverComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        b8 materialDrawBuffersUploaded = false;
        b8 csgFrameBuffersUploaded = false;
        b8 materialFrameStatesGraphOwned = false;
        b8 materialGeometryStatesGraphOwned = false;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, RendererMaterialSystem& materialSystemIn, DeferredFrameTargets& targetsIn, Core::GpuTimingSubmissionTicket*& timingTicketIn, const b8& meshViewSetupReadyIn, const b8& sceneShadingSetupReadyIn)noexcept;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};


// Split interval-sample emulation only for distinct outputs; keep timing in one packet.
struct OpaqueCsgIntervalSampleComputeEmulationGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // All bindings required: emulation always brackets its dispatch with the frame timing.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererMaterialSystem& materialSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const b8& meshViewSetupReady;
        const b8& sceneShadingSetupReady;
        Optional<Core::GpuTimingMeasure>& opaqueCsgTiming;
        MeshFrameBindingSnapshot frameBindings;
        CsgGraphResourceSnapshot csgResources;
        OpaqueCsgIntervalSampleComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        b8 materialDrawBuffersUploaded = false;
        b8 csgFrameBuffersUploaded = false;
        b8 materialFrameStatesGraphOwned = false;
        b8 materialGeometryStatesGraphOwned = false;


    public:
        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            Core::GraphicsRuntime& graphicsIn,
            RendererMaterialSystem& materialSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket*& timingTicketIn,
            const b8& meshViewSetupReadyIn,
            const b8& sceneShadingSetupReadyIn,
            Optional<Core::GpuTimingMeasure>& opaqueCsgTimingIn
        )noexcept;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.opaqueCsgTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

