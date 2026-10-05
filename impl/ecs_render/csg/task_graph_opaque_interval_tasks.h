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


struct CsgOpaqueIntervalRecordInputs{
    RendererMaterialSystem* materialSystem = nullptr;
    RendererCsgSystem* csgSystem = nullptr;
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
    const bool* meshViewSetupReady = nullptr;
    const bool* sceneShadingSetupReady = nullptr;
    MeshFrameBindingSnapshot frameBindings;
    CsgGraphResourceSnapshot csgResources;
    OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
    bool materialDrawBuffersUploaded = false;
    bool csgFrameBuffersUploaded = false;

    explicit CsgOpaqueIntervalRecordInputs(Core::Alloc::GlobalArena& arena)
        : opaqueDrawSnapshot(arena)
    {}
};

struct CsgReceiverSpanBuildGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload : public CsgOpaqueIntervalRecordInputs{

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : CsgOpaqueIntervalRecordInputs(arena)
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

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : CsgOpaqueIntervalRecordInputs(arena)
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

    struct Payload{
        Core::GraphicsRuntime* graphics = nullptr;
        RendererMaterialSystem* materialSystem = nullptr;
        RendererCsgSystem* csgSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
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

        explicit Payload(Core::Alloc::GlobalArena& arena);
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

