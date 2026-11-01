// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshSkinningGraphDetail{
    [[nodiscard]] Core::GpuTaskSchedulingHint MakeSerialMergeScheduling(Core::GpuTaskCostHint::Enum cost)noexcept;

    [[nodiscard]] inline Core::GpuTaskSchedulingHint JointPaletteUploadScheduling()noexcept{
        return MakeSerialMergeScheduling(Core::GpuTaskCostHint::Tiny);
    }

    [[nodiscard]] inline Core::GpuTaskSchedulingHint SkinningDispatchScheduling()noexcept{
        return MakeSerialMergeScheduling(Core::GpuTaskCostHint::Small);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deformation and post-dispatch share the same plan loop; only the per-plan recorder differs.
using SkinningPlanRecorder = b8 (MeshSkinningSystem::*)(const MeshSkinningGraphDispatchPlan&, const Core::GpuTaskRecordContext&, Core::CommandList&);

template<SkinningPlanRecorder Recorder>
[[nodiscard]] inline b8 RecordSkinningPlans(
    MeshSkinningSystem& system,
    const Span<const MeshSkinningGraphDispatchPlan> plans,
    const Core::GpuTaskRecordContext& context,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Core::CommandList& commandList
){
    static_assert(Recorder != nullptr);
    if(plans.empty())
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
    for(const MeshSkinningGraphDispatchPlan& plan : plans){
        if(!(system.*Recorder)(plan, context, commandList))
            return false;
    }
    return true;
}


struct TaskGraphSkinningPlansPayload{
    MeshSkinningSystem& system;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Span<const MeshSkinningGraphDispatchPlan> plans;


public:
    TaskGraphSkinningPlansPayload(MeshSkinningSystem& systemIn, Core::GpuTimingSubmissionTicket& timingTicketIn, const Span<const MeshSkinningGraphDispatchPlan> plansIn)noexcept
        : system(systemIn)
        , timingTicket(timingTicketIn)
        , plans(plansIn)
    {}
};


// Deformation produces UAV streams; compiler owns the UAV-to-SRV handoff.
struct MeshSkinningSystem::TaskGraphSkinningDeformationTask{
    using Payload = TaskGraphSkinningPlansPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSkinningPlans<&MeshSkinningSystem::recordGraphOwnedSkinningDeformation>(payload.system, payload.plans, context, payload.timingTicket, commandList);
    }
};


// Bounds/repack consume deformation and publish UAV outputs for the local-bounds reduction.
struct MeshSkinningSystem::TaskGraphSkinningPostDispatchTask{
    using Payload = TaskGraphSkinningPlansPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSkinningPlans<&MeshSkinningSystem::recordGraphOwnedSkinningPostDispatch>(payload.system, payload.plans, context, payload.timingTicket, commandList);
    }
};


// Outputs end in ShaderResource state; only complete accepted bounds/repack/reduction publish a geometry generation.
struct MeshSkinningSystem::TaskGraphSkinningFinalizerTask{
    struct Payload{
        MeshSkinningSystem& system;
        Span<const MeshSkinningGraphDispatchPlan> plans;


    public:
        Payload(MeshSkinningSystem& system, const Span<const MeshSkinningGraphDispatchPlan> plans)noexcept
            : system(system)
            , plans(plans)
        {}
    };


public:
    // Native-state publication feeds the renderer without a separate cross-graph readiness edge.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::None, true };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context)noexcept;
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

