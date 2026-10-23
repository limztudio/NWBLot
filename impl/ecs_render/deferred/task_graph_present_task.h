// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>
#include <core/task/gpu/output_layer_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredPresentGraphTask{
    // Acquired backbuffer recording belongs to the primary presentation timeline.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics, true };

    // The deferred system, runtime, targets, async-final timing slot, ticket, and shadow-visibility task are required: present closes the async-final range when shadow runs on compute.
    // Only the output-layer contributor stays optional.
    struct Payload{
        RendererDeferredSystem& deferredSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        Core::AcquiredPresentationFrame presentationFrame;
        Core::GpuGraphResourceId backBuffer;
        Core::GpuTaskGraphOutputLayer outputLayer;
        Core::IGpuTaskGraphOutputLayerContributor* outputLayerContributor = nullptr;
        Optional<Core::GpuTimingMeasure>& asyncFinalTiming;
        Core::GpuTimingSubmissionTicket& timingTicket;
        const Core::GpuTaskId& shadowVisibilityTask;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token);
    // Narrow output-layer acceptance: the only optional coupling inside Accepted.
    static void AcceptOutputLayer(Core::IGpuTaskGraphOutputLayerContributor* contributor, u64 frameGeneration, const Core::QueueSubmissionToken& token);
};


// Terminal task records the frame-timing endpoint; rejected endpoints stay for recovery.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

