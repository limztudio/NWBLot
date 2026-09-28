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

    struct Payload{
        RendererDeferredSystem* deferredSystem = nullptr;
        Core::GraphicsRuntime* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::AcquiredPresentationFrame presentationFrame;
        Core::GpuGraphResourceId backBuffer;
        Core::GpuTaskGraphOutputLayer outputLayer;
        Core::IGpuTaskGraphOutputLayerContributor* outputLayerContributor = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncFinalTiming = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        const Core::GpuTaskId* shadowVisibilityTask = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token);
};


// Terminal task records the frame-timing endpoint; rejected endpoints stay for recovery.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

