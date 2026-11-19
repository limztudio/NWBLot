// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph.h"

#include <core/common/log.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameTimingBeginGraphTask{
    static constexpr GpuTaskCommandRequirements s_CommandRequirements = { GpuQueueCapability::Graphics, true };

    struct Payload{
        GpuTimingFrameTransaction& frameTimingTransaction;
        Device& device;
        GpuTimingScopeDefinition scopeDefinition;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, CommandList& commandList, [[maybe_unused]] const GpuTaskRecordContext& context){
        return payload.frameTimingTransaction.begin(payload.scopeDefinition, payload.device, commandList);
    }

    static void Accepted(Payload& payload, const QueueSubmissionToken& token){
        if(!payload.frameTimingTransaction.confirmBeginSubmission(token)){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("GPU task graph: failed to confirm accepted frame timing begin; discarding timing reservation"));
            payload.frameTimingTransaction.discard();
        }
    }
};


inline Expected<GpuTaskId> AddFrameTimingBeginTask(
    GpuTaskGraph& graph, const GpuTaskDesc& desc, GpuTimingFrameTransaction& transaction, Device& device, const GpuTimingScopeDefinition& scope
){
    return graph.addTask<FrameTimingBeginGraphTask>(desc, { transaction, device, scope });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

