// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgReceiverSpanBuildGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool receiverSpanInputImageStatesGraphOwned = false;
        bool receiverSpanOutputImageStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena);
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Interval combine consumes the five StorageImage aliases produced by peel/span build, then writes the four
// removed-interval aliases consumed by the following material/cap draws. Keep both boundaries in the established
// Graphics submission when safe, but let the graph lower their exact same-UAV fences.
struct CsgIntervalCombineGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool intervalCombineInputImageStatesGraphOwned = false;
        bool removedIntervalOutputImageStatesGraphOwned = false;

        explicit Payload(Core::Alloc::GlobalArena& arena);
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Interval combine writes StorageImage-backed removed-interval outputs, while the following opaque material and cap
// draws load those same aliases. This task receives the graph-lowered output fence rather than replaying it from a
// renderer thunk.
struct CsgIntervalSampleGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
        OpaqueMaterialPassGraphSnapshot opaqueDrawSnapshot;
        bool materialDrawBuffersUploaded = false;
        bool csgFrameBuffersUploaded = false;
        bool intervalSampleImageStatesGraphOwned = false;
        bool csgClipBufferStatesGraphOwned = false;
        bool materialFrameStatesGraphOwned = false;
        bool materialGeometryStatesGraphOwned = false;
        // When the preceding interval-sample producer owns generated-vertex UAV output, this callback keeps the
        // frozen CSG compute draws raster-only so the compiler supplies the one UAV-to-VertexBuffer boundary.
        bool csgComputeEmulationOutputStatesGraphOwned = false;
        Optional<Core::GpuTimingMeasure>* opaqueCsgComputeEmulationTiming = nullptr;

        explicit Payload(Core::Alloc::GlobalArena& arena);
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

