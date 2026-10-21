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
    inline constexpr Name s_SerialMergeDomain("mesh_skinning.serial");

    [[nodiscard]] inline Core::GpuTaskSchedulingHint JointPaletteUploadScheduling()noexcept{
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Core::GpuTaskCostHint::Tiny;
        scheduling.overlapPreferred = false;
        scheduling.avoidQueueCrossing = true;
        scheduling.forceSubmissionBoundary = false;
        scheduling.allowPacketMerge = true;
        // The external renderer consumes one accepted skinning state on this serial timeline.
        scheduling.mergeWithPrevious = true;
        scheduling.frontierScoredMergeDomain = s_SerialMergeDomain;
        return scheduling;
    }

    [[nodiscard]] inline Core::GpuTaskSchedulingHint SkinningDispatchScheduling()noexcept{
        Core::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Core::GpuTaskCostHint::Small;
        scheduling.overlapPreferred = false;
        scheduling.avoidQueueCrossing = true;
        scheduling.forceSubmissionBoundary = false;
        scheduling.allowPacketMerge = true;
        // The external renderer consumes one accepted skinning state on this serial timeline.
        scheduling.mergeWithPrevious = true;
        scheduling.frontierScoredMergeDomain = s_SerialMergeDomain;
        return scheduling;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deformation and post-dispatch share the same plan loop; only the per-plan recorder differs.
using SkinningPlanRecorder = bool (MeshSkinningSystem::*)(const MeshSkinningGraphDispatchPlan&, Core::CommandList&, const Core::GpuTaskRecordContext&);

template<SkinningPlanRecorder recorder>
[[nodiscard]] inline bool RecordSkinningPlans(
    MeshSkinningSystem& system,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& plans,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_assert(recorder != nullptr);
    if(plans.empty())
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
    for(const MeshSkinningGraphDispatchPlan& plan : plans){
        if(!(system.*recorder)(plan, commandList, context))
            return false;
    }
    return true;
}


// Deformation produces UAV streams; compiler owns the UAV-to-SRV handoff.
struct MeshSkinningSystem::TaskGraphSkinningDeformationTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The skinning system and the timing ticket are required: deformation always records against this frame's ticket.
    struct Payload{
        MeshSkinningSystem& system;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> plans;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, MeshSkinningSystem& systemIn, Core::GpuTimingSubmissionTicket& timingTicketIn)noexcept
            : system(systemIn)
            , timingTicket(timingTicketIn)
            , plans(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSkinningPlans<&MeshSkinningSystem::recordGraphOwnedSkinningDeformation>(payload.system, payload.timingTicket, payload.plans, commandList, context);
    }
};


// Bounds/repack consume deformation and publish UAV outputs for the local-bounds reduction.
struct MeshSkinningSystem::TaskGraphSkinningPostDispatchTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The skinning system and the timing ticket are required: bounds/repack always records against this frame's ticket.
    struct Payload{
        MeshSkinningSystem& system;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> plans;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, MeshSkinningSystem& systemIn, Core::GpuTimingSubmissionTicket& timingTicketIn)noexcept
            : system(systemIn)
            , timingTicket(timingTicketIn)
            , plans(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RecordSkinningPlans<&MeshSkinningSystem::recordGraphOwnedSkinningPostDispatch>(payload.system, payload.timingTicket, payload.plans, commandList, context);
    }
};


// Outputs end in ShaderResource state; only complete accepted bounds/repack/reduction publish a geometry generation.
struct MeshSkinningSystem::TaskGraphSkinningFinalizerTask{
    // Native-state publication feeds the renderer without a separate cross-graph readiness edge.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::None, true };

    struct Payload{
        MeshSkinningSystem& system;
        Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> plans;


    public:
        Payload(Core::Alloc::GlobalArena& arena, MeshSkinningSystem& system)noexcept
            : system(system)
            , plans(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context)noexcept{
        if(payload.plans.empty())
            return false;

        for(const MeshSkinningGraphDispatchPlan& plan : payload.plans){
            if(plan.hasActiveSkin || plan.copiedRestStreams){
                Core::Buffer* const skinnedPosition = context.declarations.bufferForResource(plan.skinnedPositionResource);
                Core::Buffer* const skinnedNormal = context.declarations.bufferForResource(plan.skinnedNormalResource);
                Core::Buffer* const skinnedTangent = context.declarations.bufferForResource(plan.skinnedTangentResource);
                if(
                    !skinnedPosition
                    || !skinnedNormal
                    || !skinnedTangent
                    || commandList.getBufferState(skinnedPosition) != Core::ResourceStates::ShaderResource
                    || commandList.getBufferState(skinnedNormal) != Core::ResourceStates::ShaderResource
                    || commandList.getBufferState(skinnedTangent) != Core::ResourceStates::ShaderResource
                )
                    return false;
            }
            if(plan.updatesMeshletBounds){
                Core::Buffer* const meshletBounds = context.declarations.bufferForResource(plan.meshletBoundsResource);
                Core::Buffer* const localBounds = context.declarations.bufferForResource(plan.localBoundsResource);
                if(!meshletBounds || !localBounds || commandList.getBufferState(meshletBounds) != Core::ResourceStates::ShaderResource || commandList.getBufferState(localBounds) != Core::ResourceStates::ShaderResource)
                    return false;
            }
            if(plan.repacksNormals){
                Core::Buffer* const attributes = context.declarations.bufferForResource(plan.attributeResource);
                if(!attributes || commandList.getBufferState(attributes) != Core::ResourceStates::ShaderResource)
                    return false;
            }
        }
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        if(!token.valid())
            return;
        for(const MeshSkinningGraphDispatchPlan& plan : payload.plans)
            payload.system.confirmGraphOwnedSkinningDispatch(plan);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

