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

    // All bindings required: G-buffer records the opaque pass against this frame's bindings.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererMaterialSystem& materialSystem;
        RendererCsgSystem& csgSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const b8& meshViewSetupReady;
        const b8& sceneShadingSetupReady;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        CsgGraphResourceSnapshot csgResources;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        b8 materialDrawBuffersUploaded = false;
        b8 csgFrameBuffersUploaded = false;
        b8 materialFrameStatesGraphOwned = false;
        b8 materialGeometryStatesGraphOwned = false;
        b8 regularComputeEmulationOutputStatesGraphOwned = false;
        // G-buffer keeps regular rasterization; terminal shared task finishes the timing range.
        b8 regularSharedComputeEmulationDrawsGraphOwned = false;
        b8 csgReceiverComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* regularSharedComputeEmulationTiming = nullptr;


    public:
        explicit Payload(
            Core::Alloc::GlobalArena& arena,
            Core::GraphicsRuntime& graphicsIn,
            RendererMaterialSystem& materialSystemIn,
            RendererCsgSystem& csgSystemIn,
            DeferredFrameTargets& targetsIn,
            Core::GpuTimingSubmissionTicket*& timingTicketIn,
            const b8& meshViewSetupReadyIn,
            const b8& sceneShadingSetupReadyIn
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
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(payload.regularSharedComputeEmulationTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

