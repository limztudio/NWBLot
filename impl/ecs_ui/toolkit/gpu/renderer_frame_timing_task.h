// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuRendererTimingScope{
    inline constexpr Core::GpuTimingScopeDefinition s_Frame("render.frame");
};


// Closes the standalone presentation scope after output and any presentation contributor.
struct GpuFrameTimingEndTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::None, true };

    // The transaction owns the timing reservation opened by the standalone prelude. It is a reference
    // (not a nullable pointer) so a missing binding fails at declaration time instead of silently
    // recording `false` inside Record.
    struct Payload{
        Core::GpuTimingFrameTransaction& frameTimingTransaction;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

