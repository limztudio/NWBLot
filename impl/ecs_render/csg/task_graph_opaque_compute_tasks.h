// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem;
class RendererMaterialSystem;
class RendererCsgSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpaqueCsgReceiverComputeEmulationGraphTask{
    struct Payload{
        RendererMeshSystem* meshSystem = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        RendererCsgSystem* csgSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
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


// Interval-sample CSG compute emulation is split only for pairwise-distinct generated outputs. It follows
// interval combine and precedes the existing CSG material/cap raster callback, keeping the output handoff and the
// original Opaque CSG timing range inside that one semantic Graphics packet.
struct OpaqueCsgIntervalSampleComputeEmulationGraphTask{
    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererMeshSystem* meshSystem = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        RendererCsgSystem* csgSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        Optional<Core::GpuTimingMeasure>* opaqueCsgTiming = nullptr;
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

    static void discardTiming(Optional<Core::GpuTimingMeasure>* timing);
    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    static void discarded(Payload& payload);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

