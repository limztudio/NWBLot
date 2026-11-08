// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_software_bvh_build_chain.h"

#include <impl/ecs_render/shadow/task_graph_shadow_prepare_tasks.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowSoftwareBvhBuildChain::declare(
    const Span<const PreparedMeshSwBvhGraphResources> pureSoftwareMeshSwBvhGraphResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Core::GpuTaskId& shadowPrepareDependency,
    Core::GpuTaskId& firstTask,
    Core::GpuTaskId& lastTask
){
    using namespace RendererTaskGraphDetail;

    // Operations share scratch; keep clears adjacent in the accepting packet.
    Core::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    clearScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskSchedulingHint buildScheduling = clearScheduling;
    buildScheduling.cost = Core::GpuTaskCostHint::Large;

    Core::GpuTaskId buildDependency = shadowPrepareDependency;
    const auto addPureSoftwareBvhClear = [&](const Expected<Name>& identity, const AStringView label, const Core::GpuGraphResourceId destination, const u32 clearValue)->Expected<Core::GpuTaskId>{
        if(!identity)
            return MakeUnexpected(Failure{});
        Core::GpuTaskDesc clearDesc;
        clearDesc.setIdentity(*identity).setMarkerLabel(label).setScheduling(clearScheduling).setDependencies({ &buildDependency, 1u });
        return m_graph.addClearBufferTask(clearDesc, Core::GpuClearBufferTaskDesc{ .destination = destination, .clearValue = clearValue, });
    };
    const auto appendPureSoftwareBvhClear = [&](const Expected<Core::GpuTaskId>& clearTaskResult)noexcept{
        if(!clearTaskResult)
            return false;
        const Core::GpuTaskId clearTask = *clearTaskResult;
        if(!firstTask.valid())
            firstTask = clearTask;
        buildDependency = clearTask;
        return true;
    };

    for(const PreparedMeshSwBvhGraphResources& resources : pureSoftwareMeshSwBvhGraphResources){
        const PreparedMeshSwBvhBuild& build = resources.build;
        if(!build.performRefit){
            if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_keys_clear")), "Shadow Prepare SW-BVH Sort-Key Clear", resources.sortKeys, BvhNodeIndex::Invalid))
                || !appendPureSoftwareBvhClear(addPureSoftwareBvhClear(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_parent_clear")), "Shadow Prepare SW-BVH Parent Clear", resources.parent, BvhNodeIndex::Invalid))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH sentinel clears"));
                return false;
            }
        }
        if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_counter_clear")), "Shadow Prepare SW-BVH Counter Clear", resources.visitCounter, 0u))){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH counter clear"));
            return false;
        }

        const Array<Core::GpuTaskResourceUse, 7u> buildResourceUses{
            ReadUse(resources.position, Core::ResourceStates::ShaderResource),
            ReadUse(resources.triangleIndex, Core::ResourceStates::ShaderResource),
            ReadWriteUse(resources.node, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.parent, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.sortKeys, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.sortPayload, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(resources.visitCounter, Core::ResourceStates::UnorderedAccess),
        };
        const auto buildIdentity = DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_build"));
        if(!buildIdentity){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH build callback"));
            return false;
        }
        Core::GpuTaskDesc buildDesc;
        buildDesc.setIdentity(*buildIdentity).setMarkerLabel("Shadow Prepare SW-BVH Build").setScheduling(buildScheduling).setDependencies({ &buildDependency, 1u }).setResourceUses(buildResourceUses);
        const auto buildTaskResult = m_graph.addTask<
            ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask
        >(buildDesc, ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask::Payload{ .raytracingSystem = m_raytracingSystem, .timingTicket = timingTicket, .build = build, });
        if(!buildTaskResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH build callback"));
            return false;
        }
        const Core::GpuTaskId buildTask = *buildTaskResult;
        buildDependency = buildTask;
        lastTask = buildTask;
    }
    if(!firstTask.valid() || !lastTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH graph chain has no task bounds"));
        return false;
    }
    shadowPrepareDependency = buildDependency;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

