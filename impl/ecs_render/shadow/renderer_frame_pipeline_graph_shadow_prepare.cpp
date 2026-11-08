// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/shadow_prepare_graph_coordinator.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/raytrace/software_scene_refit.h>
#include <impl/ecs_render/raytrace/prepared_software_bvh_graph_resources.h>
#include <impl/ecs_render/shadow/shadow_prepare_geometry_resources.h>
#include <impl/ecs_render/shadow/shadow_prepare_upload_chain.h>
#include <impl/ecs_render/shadow/shadow_prepare_resource_planner.h>
#include <impl/ecs_render/shadow/shadow_software_bvh_build_chain.h>
#include <impl/ecs_render/shadow/shadow_acceleration_finalize_builder.h>
#include <impl/ecs_render/shadow/task_graph_shadow_prepare_tasks.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareGraphCoordinator::declareDeferredShadowPrepareTask(
    const ShadowPrepareGraphContext& context,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Span<const Core::GpuGraphResourceId> shadowTraceGeometryResources,
    const Span<const Core::GpuGraphResourceId> softwareBvhBuildStateResources,
    const b8 softwareTraceResourcesPrepared,
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace RendererTaskGraphDetail;

    m_deferredShadowPrepareSceneRefitTask = {};
    m_sceneBvhRefitInputsUploadTask = {};
    resetShadowPrepareTaskState();
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !context.rayTracing.shadowVisibilityResourcesPreflighted()
        || !currentBindlessSlots.valid()
        || !materialContextSlots.valid()
    )
        return false;

    ShadowPrepareUploadChain uploads;
    ShadowPrepareUploadChainBuilder uploadChainBuilder(context.graph, context.graphics, context.rayTracing);
    const b8 uploadsDeclared = uploadChainBuilder.declare(
        ShadowPrepareUploadInputs{
            .targets = deferredTargets,
            .rayTracingResources = rayTracingResources,
            .currentBindlessSlots = currentBindlessSlots,
            .materialContextSlots = materialContextSlots,
        },
        scratchArena,
        uploads
    );
    m_deferredBindlessSlotsUploadTask = uploads.tasks.bindlessSlots;
    m_rayTraceMaterialContextSlotsUploadTask = uploads.tasks.rayTraceMaterialContextSlots;
    m_csgTraceContextUploadTask = uploads.tasks.csgTraceContext;
    m_causticEmissionTargetsUploadTask = uploads.tasks.causticEmissionTargets;
    m_surfelFrameConstantsUploadTask = uploads.tasks.surfelFrameConstants;
    m_shadowInstanceMaterialUploadTask = uploads.tasks.shadowInstanceMaterial;
    m_shadowInstanceUploadTask = uploads.tasks.shadowInstance;
    m_shadowMaterialTypedUploadTask = uploads.tasks.shadowMaterialTyped;
    m_sceneBvhNodesUploadTask = uploads.tasks.sceneBvhNodes;
    m_sceneBvhInstancesUploadTask = uploads.tasks.sceneBvhInstances;
    if(!uploadsDeclared)
        return false;
    Core::GpuTaskId shadowPrepareDependency = uploads.completionTask;

    // Hardware TLAS builds retain their preflight instance stream; the first Graphics packet owns the native build.
    const b8 sceneTlasBuildGraphOwned = context.rayTracing.preparedSceneTlasBuildReady();
    if(sceneTlasBuildGraphOwned && !rayTracingShadowResources.sceneTlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build has no imported acceleration structure"));
        return false;
    }
    const b8 meshBlasBuildsGraphOwned = context.rayTracing.preparedMeshBlasBuildsReady();
    const Span<const PreparedMeshBlasBuild> preparedMeshBlasBuilds = context.rayTracing.preparedMeshBlasBuilds();
    if(meshBlasBuildsGraphOwned && preparedMeshBlasBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no operations"));
        return false;
    }
    if(meshBlasBuildsGraphOwned && !rayTracingShadowResources.sceneTlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no scene TLAS"));
        return false;
    }
    const b8 meshSwBvhBuildsGraphOwned = context.rayTracing.preparedMeshSwBvhBuildsReady();
    const Span<const PreparedMeshSwBvhBuild> preparedMeshSwBvhBuilds = context.rayTracing.preparedMeshSwBvhBuilds();
    if(meshSwBvhBuildsGraphOwned && preparedMeshSwBvhBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build plan has no operations"));
        return false;
    }
    const b8 meshBlasGeometryBuildInputStatesGraphOwned = meshBlasBuildsGraphOwned && !softwareTraceResourcesPrepared && !meshSwBvhBuildsGraphOwned;
    // Software preparation uses typed sentinel clears before each mesh build.
    const b8 pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate = meshSwBvhBuildsGraphOwned && !context.rayTracing.shadowVisibilityHardwareSupported();

    PreparedMeshSwBvhGraphResourceVector pureSoftwareMeshSwBvhGraphResources{ scratchArena };
    ShadowPrepareResourcePlan shadowResourcePlan(scratchArena);
    const ShadowPrepareResourceInputs shadowResourceInputs{
        rayTracingShadowResources, uploads, currentBindlessSlots, materialContextSlots,
        shadowTraceGeometryResources, softwareBvhBuildStateResources, preparedMeshBlasBuilds,
        sceneTlasBuildGraphOwned, meshBlasBuildsGraphOwned, meshBlasGeometryBuildInputStatesGraphOwned
    };
    ShadowPrepareResourcePlanner shadowResourcePlanner(context.graph, context.rayTracing, context.mesh);
    if(!shadowResourcePlanner.prepare(shadowResourceInputs, scratchArena, shadowResourcePlan))
        return false;
    const auto& resourceUses = shadowResourcePlan.resourceUses;
    const auto& resourceSetUses = shadowResourcePlan.resourceSetUses;
    const auto& resourceSetUseCount = shadowResourcePlan.resourceSetUseCount;
    const auto& accelStructFinalizeResourceUses = shadowResourcePlan.accelStructFinalizeResourceUses;
    const auto& sceneTlas = shadowResourcePlan.sceneTlas;

    const b8 pureSoftwareMeshSwBvhBuildsGraphOwned = pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate;
    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        auto resources = ResolvePreparedSoftwareBvhGraphResources(context.graph, preparedMeshSwBvhBuilds, scratchArena);
        if(!resources){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build is missing graph resources"));
            return false;
        }
        pureSoftwareMeshSwBvhGraphResources = Move(*resources);
    }

    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        ShadowSoftwareBvhBuildChain buildChain(context.graph, context.rayTracing);
        if(!buildChain.declare(pureSoftwareMeshSwBvhGraphResources, timingTicket, shadowPrepareDependency, m_deferredShadowPrepareSoftwareBvhBuildFirstTask, m_deferredShadowPrepareSoftwareBvhBuildLastTask))
            return false;
    }

    if(const auto& sceneRefit = context.rayTracing.preparedSceneSwBvhRefit(); sceneRefit){
        // A scene refit requires every frozen mesh build to be declared before it.
        if(!context.rayTracing.preparedMeshSwBvhBuildPlanFrozen() || (!preparedMeshSwBvhBuilds.empty() && !pureSoftwareMeshSwBvhBuildsGraphOwned))
            return false;
        const auto refitTasks = DeclareSoftwareSceneRefit(sceneRefit, uploads.sceneBvhNodes, shadowPrepareDependency, context.graph, scratchArena);
        if(!refitTasks){
            m_deferredShadowPrepareSceneRefitTask = {};
            m_sceneBvhRefitInputsUploadTask = {};
            return false;
        }
        m_deferredShadowPrepareSceneRefitTask = refitTasks->refit;
        m_sceneBvhRefitInputsUploadTask = refitTasks->inputUpload;
        if(!m_deferredShadowPrepareSceneRefitTask.valid() || !m_sceneBvhRefitInputsUploadTask.valid())
            return false;
        shadowPrepareDependency = m_deferredShadowPrepareSceneRefitTask;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    // Software per-mesh chains precede this endpoint. Keep the whole packet even when later consumers form a frontier.
    scheduling.allowMergeAcrossConsumerFrontier = pureSoftwareMeshSwBvhBuildsGraphOwned || m_deferredShadowPrepareSceneRefitTask.valid();
    const Core::GpuTaskExternalStateSource shadowPrepareStateSource{ .states = context.persistentState.source(), };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setScheduling(scheduling)
        .setDependencies({ &shadowPrepareDependency, s_ShadowPrepareSingleDependencyCount })
        .setExternalStateSources({ shadowPrepareStateSource.states ? &shadowPrepareStateSource : nullptr, shadowPrepareStateSource.states ? 1u : 0u })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ resourceSetUses.data(), resourceSetUseCount })
    ;
    const auto deferredShadowPrepareTaskResult = context.graph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>(
        desc,
        ECSRenderDetail::ShadowPrepareGraphTask::Payload{
            .raytracingSystem = context.rayTracing,
            .outcome = m_shadowPreparationOutcome,
            .targets = deferredTargets,
            .timingTicket = timingTicket,
            .deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded,
            .currentBindlessSlotsGraphOwned = uploads.currentBindlessSlotsGraphOwned,
            .shadowMaterialContextBatchGraphOwned = uploads.shadowMaterialContextBatchGraphOwned,
            .sceneBvhBatchGraphOwned = uploads.sceneBvhBatchGraphOwned,
            .sceneTlasBuildGraphOwned = sceneTlasBuildGraphOwned,
            .meshBlasBuildsGraphOwned = meshBlasBuildsGraphOwned,
            .meshBlasGeometryBuildInputStatesGraphOwned = meshBlasGeometryBuildInputStatesGraphOwned,
            .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
            .preparedMeshSwBvhBuildsRecordedByGraph = pureSoftwareMeshSwBvhBuildsGraphOwned,
        }
    );
    if(!deferredShadowPrepareTaskResult){
        m_deferredShadowPrepareTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation task"));
        return false;
    }
    m_deferredShadowPrepareTask = *deferredShadowPrepareTaskResult;
    const b8 accelStructBuildStatesGraphOwned = sceneTlasBuildGraphOwned || meshBlasBuildsGraphOwned;
    if(accelStructBuildStatesGraphOwned){
        ShadowAccelerationFinalizeBuilder finalizeBuilder(context.graph);
        if(!finalizeBuilder.declare(scratchArena, sceneTlasBuildGraphOwned, sceneTlas, preparedMeshBlasBuilds.size(), accelStructFinalizeResourceUses, m_deferredShadowPrepareTask, m_deferredShadowPrepareAccelStructFinalizeTask))
            return false;
    }
    return true;
}


void ShadowPrepareGraphCoordinator::resetShadowPrepareUploadTaskState()noexcept{
    m_deferredBindlessSlotsUploadTask = {};
    m_rayTraceMaterialContextSlotsUploadTask = {};
    m_csgTraceContextUploadTask = {};
    m_causticEmissionTargetsUploadTask = {};
    m_surfelFrameConstantsUploadTask = {};
    m_shadowInstanceMaterialUploadTask = {};
    m_shadowInstanceUploadTask = {};
    m_shadowMaterialTypedUploadTask = {};
    m_sceneBvhNodesUploadTask = {};
    m_sceneBvhInstancesUploadTask = {};
}

void ShadowPrepareGraphCoordinator::resetShadowPrepareTaskState()noexcept{
    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
    resetShadowPrepareUploadTaskState();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

