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
    // The transaction owns the timing reservation opened by the standalone prelude.
    struct Payload{
        Core::GpuTimingFrameTransaction& frameTimingTransaction;
    };


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::None, true };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        return payload.frameTimingTransaction.recordEnd(commandList);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

