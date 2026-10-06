// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
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
class RendererCsgSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The material/CSG systems, the frame targets, the timing-ticket slot, and the setup readiness flags
// are required: every interval record pass works against this frame's bindings. References (not
// nullable pointers) carry those bindings so a missing binding fails at declaration time instead of
// silently returning `false` inside Record.
struct CsgOpaqueIntervalRecordInputs{
    RendererMaterialSystem& materialSystem;
    RendererCsgSystem& csgSystem;
    DeferredFrameTargets& targets;
    Core::GpuTimingSubmissionTicket*& timingTicket;
    const bool& meshViewSetupReady;
    const bool& sceneShadingSetupReady;
    MeshFrameBindingSnapshot frameBindings;
    CsgGraphResourceSnapshot csgResources;
    OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
    bool materialDrawBuffersUploaded = false;
    bool csgFrameBuffersUploaded = false;

    explicit CsgOpaqueIntervalRecordInputs(
        Core::Alloc::GlobalArena& arena,
        RendererMaterialSystem& materialSystemIn,
        RendererCsgSystem& csgSystemIn,
        DeferredFrameTargets& targetsIn,
        Core::GpuTimingSubmissionTicket*& timingTicketIn,
        const bool& meshViewSetupReadyIn,
        const bool& sceneShadingSetupReadyIn
    )
        : materialSystem(materialSystemIn)
        , csgSystem(csgSystemIn)
        , targets(targetsIn)
        , timingTicket(timingTicketIn)
        , meshViewSetupReady(meshViewSetupReadyIn)
        , sceneShadingSetupReady(sceneShadingSetupReadyIn)
        , opaqueDrawSnapshot(arena)
    {}
};

struct CsgReceiverSpanBuildGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload : public CsgOpaqueIntervalRecordInputs{

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            RendererMaterialSystem& materialSystemIn,
            RendererCsgSystem& csgSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket*& timingTicketIn,
            const bool& meshViewSetupReadyIn,
            const bool& sceneShadingSetupReadyIn
        )
            : CsgOpaqueIntervalRecordInputs(
                arena,
                materialSystemIn,
                csgSystemIn,
                targetsIn,
                timingTicketIn,
                meshViewSetupReadyIn,
                sceneShadingSetupReadyIn
            )
        {}
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Interval combine maps peel/span aliases to removed-interval aliases; graph lowers fences.
struct CsgIntervalCombineGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload : public CsgOpaqueIntervalRecordInputs{

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            RendererMaterialSystem& materialSystemIn,
            RendererCsgSystem& csgSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket*& timingTicketIn,
            const bool& meshViewSetupReadyIn,
            const bool& sceneShadingSetupReadyIn
        )
            : CsgOpaqueIntervalRecordInputs(
                arena,
                materialSystemIn,
                csgSystemIn,
                targetsIn,
                timingTicketIn,
                meshViewSetupReadyIn,
                sceneShadingSetupReadyIn
            )
        {}
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Interval sample takes the graph-lowered output fence instead of replaying it.
struct CsgIntervalSampleGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute };

    // The graphics runtime, the material/CSG systems, the frame targets, the timing-ticket slot, and
    // the setup readiness flags are required: interval sample always rasterizes this frame's CSG pass.
    // The compute-emulation timing stays optional: it is bound only when the emulation output is
    // graph-owned. References (not nullable pointers) carry the required bindings so a missing binding
    // fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererMaterialSystem& materialSystem;
        RendererCsgSystem& csgSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const bool& meshViewSetupReady;
        const bool& sceneShadingSetupReady;
        MeshFrameBindingSnapshot frameBindings;
        CsgGraphResourceSnapshot csgResources;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        // Keep CSG compute draws raster-only so the compiler supplies the UAV boundary.
        bool csgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* opaqueCsgComputeEmulationTiming = nullptr;

        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            Core::GraphicsRuntime& graphicsIn,
            RendererMaterialSystem& materialSystemIn,
            RendererCsgSystem& csgSystemIn,
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

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.opaqueCsgComputeEmulationTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

