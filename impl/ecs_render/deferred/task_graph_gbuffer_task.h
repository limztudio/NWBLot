// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererCsgSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GbufferGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute };

    // The runtime, material/CSG systems, frame targets, timing-ticket slot, and setup flags are required: G-buffer always records the opaque pass against this frame's bindings.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererMaterialSystem& materialSystem;
        RendererCsgSystem& csgSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const bool& meshViewSetupReady;
        const bool& sceneShadingSetupReady;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        CsgGraphResourceSnapshot csgResources;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool regularComputeEmulationOutputStatesGraphOwned = false;
        // G-buffer keeps regular rasterization; terminal shared task finishes the timing range.
        bool regularSharedComputeEmulationDrawsGraphOwned = false;
        bool csgReceiverComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* regularSharedComputeEmulationTiming = nullptr;


    public:
        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            Core::GraphicsRuntime& graphicsIn,
            RendererMaterialSystem& materialSystemIn,
            RendererCsgSystem& csgSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket*& timingTicketIn,
            const bool& meshViewSetupReadyIn,
            const bool& sceneShadingSetupReadyIn
        )noexcept
            : graphics(graphicsIn)
            , materialSystem(materialSystemIn)
            , csgSystem(csgSystemIn)
            , targets(targetsIn)
            , timingTicket(timingTicketIn)
            , meshViewSetupReady(meshViewSetupReadyIn)
            , sceneShadingSetupReady(sceneShadingSetupReadyIn)
            , opaqueDrawSnapshot(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.regularSharedComputeEmulationTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

