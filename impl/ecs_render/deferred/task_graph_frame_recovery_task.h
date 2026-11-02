// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/common/log.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameTimingEndGraphTask{
    // Frame timing must end on the same primary physical queue that recorded its beginning.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::None, true };


public:
    // The transaction owns the timing reservation opened by the frame-timing prelude.
    struct Payload{
        Core::GpuTimingFrameTransaction& frameTimingTransaction;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        return payload.frameTimingTransaction.recordEnd(commandList);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameRecoveryGraphTask{
    // Frame timing must end on the same primary physical queue that recorded its beginning.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::None, true };


public:
    // The transaction owns the timing reservation this recovery tail may retire.
    // The armed/retire flags are caller-owned outputs the tail clears.
    struct Payload{
        Core::GpuTimingFrameTransaction& frameTimingTransaction;
        b8& armed;
        b8& retiresFrameTiming;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        return payload.armed && (!payload.retiresFrameTiming || payload.frameTimingTransaction.recordEnd(commandList));
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.armed && payload.retiresFrameTiming && !payload.frameTimingTransaction.confirmEndSubmission(token, false)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire frame recovery timing query"));
            payload.frameTimingTransaction.discard();
        }
        payload.armed = false;
        payload.retiresFrameTiming = false;
    }

    static void Discarded(Payload& payload)noexcept{
        if(payload.armed)
            payload.frameTimingTransaction.discard();
        payload.armed = false;
        payload.retiresFrameTiming = false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

