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
    struct Payload{
        RendererMaterialSystem* materialSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        MeshFrameBindingSnapshot frameBindings;
        CsgGraphResourceSnapshot csgResources;
        OpaqueCsgReceiverComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena);
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Split interval-sample emulation only for distinct outputs; keep timing in one packet.
struct OpaqueCsgIntervalSampleComputeEmulationGraphTask{
    struct Payload{
        Core::GraphicsRuntime* graphics = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        Optional<Core::GpuTimingMeasure>* opaqueCsgTiming = nullptr;
        MeshFrameBindingSnapshot frameBindings;
        CsgGraphResourceSnapshot csgResources;
        OpaqueCsgIntervalSampleComputeEmulationGraphPlan plan;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool intervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena);
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );

    static void discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.opaqueCsgTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

