// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_acceleration_finalize_builder.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


struct ShadowPrepareAccelStructFinalizeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    struct Payload{};


public:
    [[nodiscard]] static b8 Record([[maybe_unused]] const Payload& payload, [[maybe_unused]] Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context)noexcept{
        return true;
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowAccelerationFinalizeBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const b8 sceneTlasBuildGraphOwned,
    const Core::GpuGraphResourceId sceneTlas,
    const usize preparedMeshBlasBuildCount,
    const Span<const Core::GpuTaskResourceUse> accelStructFinalizeResourceUses,
    const Core::GpuTaskId& prepareTask,
    Core::GpuTaskId& outTask
){
    const usize expectedFinalizeResourceUseCount = (sceneTlasBuildGraphOwned ? 1u : 0u) + preparedMeshBlasBuildCount;
    if((sceneTlasBuildGraphOwned && !sceneTlas.valid()) || accelStructFinalizeResourceUses.size() != expectedFinalizeResourceUseCount){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen acceleration-structure build has no final-state graph resources"));
        return false;
    }
    Core::GpuGraphResourceSetId accelStructFinalizeSet;
    if(!accelStructFinalizeResourceUses.empty()){
        Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> accelStructFinalizeResources{ scratchArena };
        accelStructFinalizeResources.reserve(accelStructFinalizeResourceUses.size());
        for(const Core::GpuTaskResourceUse& use : accelStructFinalizeResourceUses)
            accelStructFinalizeResources.push_back(use.resource);
        const auto accelStructFinalizeSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.accel_struct_finalize_resources"))
                .setMarkerLabel("Shadow Prepare Accel-Struct Finalize Resources")
                .setMembers(accelStructFinalizeResources)
        );
        if(accelStructFinalizeSetResult)
            accelStructFinalizeSet = *accelStructFinalizeSetResult;
    }
    const b8 accelStructFinalizeSetGraphOwned = accelStructFinalizeSet.valid();
    const Core::GpuTaskResourceSetUse accelStructFinalizeSetUse{ .resourceSet = accelStructFinalizeSet, .range = {}, .requiredState = Core::ResourceStates::AccelStructRead, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskSchedulingHint accelStructFinalizeScheduling;
    accelStructFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    accelStructFinalizeScheduling.forceSubmissionBoundary = false;
    accelStructFinalizeScheduling.allowPacketMerge = true;
    accelStructFinalizeScheduling.mergeWithPrevious = true;
    // Compute consumers follow Shadow Preparation. Keep Read finalizers in the same packet so they wait on every build.
    accelStructFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc accelStructFinalizeDesc;
    accelStructFinalizeDesc
        .setIdentity(Name("render.shadow_prepare.accel_struct_finalize"))
        .setMarkerLabel("Shadow Preparation Accel-Struct Finalize")
        .setScheduling(accelStructFinalizeScheduling)
        .setDependencies({ &prepareTask, 1u })
        // The typed final-state collection expands to the same inputs. Keep individuals when members form no unique set.
        .setResourceUses({ accelStructFinalizeSetGraphOwned ? nullptr : accelStructFinalizeResourceUses.data(), accelStructFinalizeSetGraphOwned ? 0u : accelStructFinalizeResourceUses.size() })
        .setResourceSetUses({ accelStructFinalizeSetGraphOwned ? &accelStructFinalizeSetUse : nullptr, accelStructFinalizeSetGraphOwned ? 1u : 0u })
    ;
    const auto deferredShadowPrepareAccelStructFinalizeTaskResult = m_graph.addTask< ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask >(
        accelStructFinalizeDesc,
        ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask::Payload{}
    );
    if(!deferredShadowPrepareAccelStructFinalizeTaskResult){
        outTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare acceleration-structure final-state task"));
        return false;
    }
    outTask = *deferredShadowPrepareAccelStructFinalizeTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

