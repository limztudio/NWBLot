// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GbufferGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool csgIntervalPeelTargetStatesGraphOwned = false;
        bool csgReceiverSurfaceImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        bool regularComputeEmulationOutputStatesGraphOwned = false;
        // Two or three shared-output regular compute draws are recorded by serial successor tasks. G-buffer
        // retains only regular mesh rasterization, starts the original timing range, and leaves it open for the
        // terminal shared raster task to finish.
        bool regularSharedComputeEmulationDrawsGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* regularSharedComputeEmulationTiming = nullptr;
        bool csgReceiverComputeEmulationOutputStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena)
            : opaqueDrawSnapshot(arena)
        {}
    };

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

