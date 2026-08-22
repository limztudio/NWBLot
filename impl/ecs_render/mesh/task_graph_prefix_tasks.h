// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/mesh/mesh_view_private.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshViewSetupGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncPrefixTiming = nullptr;
        Core::GpuTimingSubmissionTicket** timingTicket = nullptr;
        const bool* asyncPrefixTimingSpansOnePacket = nullptr;
        const Core::GpuTaskId* shadowVisibilityTask = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// The CPU mirror is updated only after the built-in upload packet accepts. This keeps a rejected recording from
// suppressing the retry's immutable blob declaration.
struct MeshViewUploadCommitGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        ECSRenderDetail::MeshViewGpuData viewState;
        bool uploadRequired = false;
        bool* ready = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token);
    static void discarded(Payload& payload);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

