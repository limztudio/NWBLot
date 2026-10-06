// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph.h"

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Opens a frame timing transaction in the graph's isolated primary-Graphics prelude packet.
// Both bindings are required: the transaction owns the timing reservation and the device is the live
// GraphicsRuntime device the prelude records against. They are references (not nullable pointers) so a
// missing binding fails at declaration time instead of silently recording `false` inside Record.
struct FrameTimingBeginGraphTask{
    static constexpr GpuTaskCommandRequirements s_CommandRequirements = { GpuQueueCapability::Graphics, true };

    struct Payload{
        GpuTimingFrameTransaction& frameTimingTransaction;
        Device& device;
        GpuTimingScopeDefinition scopeDefinition;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    );
    static void Accepted(Payload& payload, const QueueSubmissionToken& token);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

