// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/raytrace/task_graph_software_scene_refit.h>
#include <impl/ecs_render/raytrace/prepared_software_bvh_graph_resources.h>
#include <impl/ecs_render/raytrace/shadow_prepare_geometry_resources.h>
#include <impl/ecs_render/raytrace/shadow_prepare_upload_chain.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_finalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererFramePipeline::declareDeferredShadowPrepareTask(
    Core::Alloc::ScratchArena& scratchArena,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
    const Core::GpuGraphResourceId* const softwareBvhBuildStateResources,
    const usize softwareBvhBuildStateResourceCount,
    const bool softwareTraceResourcesPrepared,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace RendererTaskGraphDetail;

    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareSceneRefitTask = {};
    m_sceneBvhRefitInputsUploadTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
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
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !m_raytracingSystem.shadowVisibilityResourcesPreflighted()
        || !currentBindlessSlots.valid()
        || !materialContextSlots.valid()
        || (shadowTraceGeometryResourceCount != 0u && !shadowTraceGeometryResources)
        || (softwareBvhBuildStateResourceCount != 0u && !softwareBvhBuildStateResources)
    )
        return false;

    ShadowPrepareUploadChain uploads;
    ShadowPrepareUploadChainBuilder uploadChainBuilder(m_deferredLightingTaskGraph, m_graphics, m_raytracingSystem);
    const bool uploadsDeclared = uploadChainBuilder.declare(scratchArena, ShadowPrepareUploadInputs{ .targets = deferredTargets, .rayTracingResources = rayTracingResources, .currentBindlessSlots = currentBindlessSlots, .materialContextSlots = materialContextSlots, }, uploads);
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
    const bool sceneTlasBuildGraphOwned = m_raytracingSystem.preparedSceneTlasBuildReady();
    if(sceneTlasBuildGraphOwned && !rayTracingShadowResources.sceneTlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build has no imported acceleration structure"));
        return false;
    }
    const bool meshBlasBuildsGraphOwned = m_raytracingSystem.preparedMeshBlasBuildsReady();
    const PreparedMeshBlasBuildVector& preparedMeshBlasBuilds = m_raytracingSystem.preparedMeshBlasBuilds();
    if(meshBlasBuildsGraphOwned && preparedMeshBlasBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no operations"));
        return false;
    }
    if(meshBlasBuildsGraphOwned && !rayTracingShadowResources.sceneTlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no scene TLAS"));
        return false;
    }
    const bool meshSwBvhBuildsGraphOwned = m_raytracingSystem.preparedMeshSwBvhBuildsReady();
    const PreparedMeshSwBvhBuildVector& preparedMeshSwBvhBuilds = m_raytracingSystem.preparedMeshSwBvhBuilds();
    if(meshSwBvhBuildsGraphOwned && preparedMeshSwBvhBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build plan has no operations"));
        return false;
    }
    const bool meshBlasGeometryBuildInputStatesGraphOwned = meshBlasBuildsGraphOwned && !softwareTraceResourcesPrepared && !meshSwBvhBuildsGraphOwned;
    // Software preparation uses typed sentinel clears before each mesh build.
    const bool pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate = meshSwBvhBuildsGraphOwned && !m_raytracingSystem.shadowVisibilityHardwareSupported();

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceSetUse, Core::Alloc::ScratchArena> resourceSetUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accelStructFinalizeResourceUses{ scratchArena };
    const ShadowPrepareGeometryInputs geometryInputs{ .blasBuilds = preparedMeshBlasBuilds, .traceResources = shadowTraceGeometryResources, .traceResourceCount = shadowTraceGeometryResourceCount, .blasBuildsGraphOwned = meshBlasBuildsGraphOwned, };
    ShadowPrepareGeometryResources geometryResources(geometryInputs, scratchArena);
    const auto& meshBlasGeometryBuildInputResources = geometryResources.m_blasBuildInputs;
    const auto& shadowPrepareTraceGeometryResources = geometryResources.m_remainingTraceGeometry;
    PreparedMeshSwBvhGraphResourceVector pureSoftwareMeshSwBvhGraphResources{ scratchArena };
    ECSRenderDetail::MeshBlasGraphStateVector liveMeshBlasGraphStates{ scratchArena };
    m_meshSystem.collectBlasGraphStates(liveMeshBlasGraphStates);
    constexpr usize s_ShadowPrepareResourceUseCapacity = 19u;
    resourceUses.reserve(s_ShadowPrepareResourceUseCapacity + (uploads.csgTraceContext.valid() ? 1u : 0u) + shadowTraceGeometryResourceCount + softwareBvhBuildStateResourceCount + liveMeshBlasGraphStates.size() + preparedMeshBlasBuilds.size() * 2u);
    accelStructFinalizeResourceUses.reserve((sceneTlasBuildGraphOwned ? 1u : 0u) + preparedMeshBlasBuilds.size());
    constexpr usize s_ShadowPrepareResourceSetUseCapacity = 3u;
    resourceSetUses.reserve(s_ShadowPrepareResourceSetUseCapacity);
    geometryResources.prepareStorage(meshBlasGeometryBuildInputStatesGraphOwned);
    // Shadow Preparation owns post-transition boundaries; Compute readers wait on this packet.
    resourceUses.push_back(ReadWriteUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Retain Shadow Preparation as producer; WAW handoff retires the immutable upload.
    resourceUses.push_back(WriteUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(uploads.csgTraceContext.valid())
        resourceUses.push_back(WriteUse(uploads.csgTraceContext, Core::ResourceStates::ShaderResource));
    if(uploads.causticEmissionTargets.valid())
        resourceUses.push_back(WriteUse(uploads.causticEmissionTargets, Core::ResourceStates::ShaderResource));
    if(uploads.surfelFrameConstants.valid())
        resourceUses.push_back(WriteUse(uploads.surfelFrameConstants, Core::ResourceStates::ConstantBuffer));
    if(uploads.shadowInstanceMaterials.valid())
        resourceUses.push_back(WriteUse(uploads.shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
    if(uploads.shadowInstances.valid())
        resourceUses.push_back(WriteUse(uploads.shadowInstances, Core::ResourceStates::ShaderResource));
    if(uploads.shadowMaterialTyped.valid())
        resourceUses.push_back(WriteUse(uploads.shadowMaterialTyped, Core::ResourceStates::ShaderResource));
    if(uploads.sceneBvhNodes.valid())
        resourceUses.push_back(WriteUse(uploads.sceneBvhNodes, Core::ResourceStates::ShaderResource));
    if(uploads.sceneBvhInstances.valid())
        resourceUses.push_back(WriteUse(uploads.sceneBvhInstances, Core::ResourceStates::ShaderResource));

    bool resourcesImported = true;
    if(!geometryResources.gatherBuildInputs(m_deferredLightingTaskGraph)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS geometry inputs are missing graph resources"));
        return false;
    }
    if(meshBlasBuildsGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            const auto blasIdentity = DeriveName(build.meshName, RendererFramePipelineDetail::s_BlasNameSuffix);
            if(!blasIdentity){
                resourcesImported = false;
                continue;
            }
            // First build knows native Common; retained storage imports the accepted binding.
            const Core::ResourceStates::Mask blasInitialState = build.backingFresh ? Core::ResourceStates::Common : Core::ResourceStates::Unknown;
            const auto blas = m_deferredLightingTaskGraph.importAccelStruct(build.blas, AccelStructResourceDesc(*blasIdentity, "Prepared Mesh BLAS").setInitialState(blasInitialState));
            resourcesImported = resourcesImported && blas.has_value();
            if(blas.has_value()){
                // Typed acceleration-structure uses lower through their backing allocation.
                resourceUses.push_back(ReadWriteUse(*blas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(*blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    Core::GpuGraphResourceSetId meshBlasGeometryBuildInputSet;
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputResources.empty()){
        const auto meshBlasGeometryBuildInputSetResult = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.blas_geometry_build_inputs"))
                .setMarkerLabel("Shadow Prepare BLAS Geometry Build Inputs")
                .setMembers(meshBlasGeometryBuildInputResources.data(), meshBlasGeometryBuildInputResources.size())
        );
        if(meshBlasGeometryBuildInputSetResult)
            meshBlasGeometryBuildInputSet = *meshBlasGeometryBuildInputSetResult;
    }
    const bool meshBlasGeometryBuildInputSetGraphOwned = meshBlasGeometryBuildInputSet.valid();
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputSetGraphOwned){
        for(const Core::GpuGraphResourceId resource : meshBlasGeometryBuildInputResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::AccelStructBuildInput));
    }
    const Core::GpuTaskResourceSetUse meshBlasGeometryBuildInputSetUse{ .resourceSet = meshBlasGeometryBuildInputSet, .range = {}, .requiredState = Core::ResourceStates::AccelStructBuildInput, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(meshBlasGeometryBuildInputSetGraphOwned)
        resourceSetUses.push_back(meshBlasGeometryBuildInputSetUse);
    if(!geometryResources.gatherRemainingTraceResources())
        return false;


    // Frozen trace list may carry build inputs; keep them separate from the SRV subset.
    Core::GpuGraphResourceSetId shadowPrepareTraceGeometrySet;
    if(!shadowPrepareTraceGeometryResources.empty()){
        const auto shadowPrepareTraceGeometrySetResult = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare_trace_geometry"))
                .setMarkerLabel("Shadow Prepare Trace Geometry")
                .setMembers(shadowPrepareTraceGeometryResources.data(), shadowPrepareTraceGeometryResources.size())
        );
        if(shadowPrepareTraceGeometrySetResult)
            shadowPrepareTraceGeometrySet = *shadowPrepareTraceGeometrySetResult;
    }
    const bool shadowPrepareTraceGeometryStatesGraphOwned = shadowPrepareTraceGeometrySet.valid();
    if(!shadowPrepareTraceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : shadowPrepareTraceGeometryResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowPrepareTraceGeometrySetUse{ .resourceSet = shadowPrepareTraceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(shadowPrepareTraceGeometryStatesGraphOwned)
        resourceSetUses.push_back(shadowPrepareTraceGeometrySetUse);
    for(usize resourceIndex = 0u; resourceIndex < softwareBvhBuildStateResourceCount; ++resourceIndex){
        if(!softwareBvhBuildStateResources[resourceIndex].valid())
            return false;
    }
    Core::GpuGraphResourceSetId softwareBvhBuildStateSet;
    if(softwareBvhBuildStateResourceCount != 0u){
        const auto softwareBvhBuildStateSetResult = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.software_bvh_build_state"))
                .setMarkerLabel("Shadow Prepare Software BVH Build State")
                .setMembers(softwareBvhBuildStateResources, softwareBvhBuildStateResourceCount)
        );
        if(softwareBvhBuildStateSetResult)
            softwareBvhBuildStateSet = *softwareBvhBuildStateSetResult;
    }
    const bool softwareBvhBuildStateStatesGraphOwned = softwareBvhBuildStateSet.valid();
    if(!softwareBvhBuildStateStatesGraphOwned){
        for(usize resourceIndex = 0u; resourceIndex < softwareBvhBuildStateResourceCount; ++resourceIndex){
            // Parent links and scratch keep native UAV state; they are state-only resources.
            resourceUses.push_back(ReadWriteUse(softwareBvhBuildStateResources[resourceIndex], Core::ResourceStates::UnorderedAccess));
        }
    }
    const Core::GpuTaskResourceSetUse softwareBvhBuildStateSetUse{ .resourceSet = softwareBvhBuildStateSet, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(softwareBvhBuildStateStatesGraphOwned)
        resourceSetUses.push_back(softwareBvhBuildStateSetUse);

    Core::GpuGraphResourceId sceneTlas;
    if(rayTracingShadowResources.sceneTlas){
        // Imports observe the backing generation; retained storage stays Unknown until accepted.
        const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();
        const auto sceneTlasResult = m_deferredLightingTaskGraph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name(RendererFramePipelineDetail::s_SceneTlasResourceName), RendererFramePipelineDetail::s_SceneTlasResourceLabel).setInitialState(sceneTlasInitialState)
        );
        if(sceneTlasResult)
            sceneTlas = *sceneTlasResult;
        resourcesImported = resourcesImported && sceneTlasResult.has_value();
        if(sceneTlas.valid()){
            if(sceneTlasBuildGraphOwned){
                // Frozen recorder only builds; graph lowers Write entry, successor lowers Read handoff.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // Cached scene TLAS remains read-ready; retain its accepted-state handoff.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    for(const ECSRenderDetail::MeshBlasGraphState& state : liveMeshBlasGraphStates){
        // Frozen plans own retained handles; never collide replacements with frozen identities.
        if(geometryResources.isPreparedMeshBlasBuild(state.meshName))
            continue;

        const auto blasIdentity = DeriveName(state.meshName, RendererFramePipelineDetail::s_BlasNameSuffix);
        if(!blasIdentity){
            resourcesImported = false;
            continue;
        }
        const Core::ResourceStates::Mask blasInitialState = state.backingFresh ? Core::ResourceStates::Common : Core::ResourceStates::Unknown;
        const auto blas = m_deferredLightingTaskGraph.importAccelStruct(state.blas, AccelStructResourceDesc(*blasIdentity, "Mesh BLAS").setInitialState(blasInitialState));
        resourcesImported = resourcesImported && blas.has_value();
        if(blas.has_value()){
            if(state.nativeBuildsBlas){
                // Retained backing keeps its accepted read state until the selected build is published.
                resourceUses.push_back(ReadWriteUse(*blas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // State-only import; rejected preparation re-pends and seeds accepted state.
                resourceUses.push_back(ReadUse(*blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!resourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-preparation resources"));
        return false;
    }

    const bool pureSoftwareMeshSwBvhBuildsGraphOwned = pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate;
    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        auto resources = ResolvePreparedSoftwareBvhGraphResources(scratchArena, m_deferredLightingTaskGraph, preparedMeshSwBvhBuilds);
        if(!resources){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build is missing graph resources"));
            return false;
        }
        pureSoftwareMeshSwBvhGraphResources = Move(*resources);
    }

    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
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
            clearDesc.setIdentity(*identity).setMarkerLabel(label).setScheduling(clearScheduling).setDependencies(&buildDependency, 1u);
            return m_deferredLightingTaskGraph.addClearBufferTask(clearDesc, Core::GpuClearBufferTaskDesc{ .destination = destination, .clearValue = clearValue, });
        };
        const auto appendPureSoftwareBvhClear = [&](const Expected<Core::GpuTaskId>& clearTaskResult)noexcept{
            if(!clearTaskResult)
                return false;
            const Core::GpuTaskId clearTask = *clearTaskResult;
            if(!m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid())
                m_deferredShadowPrepareSoftwareBvhBuildFirstTask = clearTask;
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

            const Core::GpuTaskResourceUse buildResourceUses[]{
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
            buildDesc.setIdentity(*buildIdentity).setMarkerLabel("Shadow Prepare SW-BVH Build").setScheduling(buildScheduling).setDependencies(&buildDependency, 1u).setResourceUses(buildResourceUses, LengthOf(buildResourceUses));
            const auto buildTaskResult = m_deferredLightingTaskGraph.addTask<
                ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask
            >(buildDesc, ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask::Payload{ .raytracingSystem = m_raytracingSystem, .timingTicket = timingTicket, .build = build, });
            if(!buildTaskResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH build callback"));
                return false;
            }
            const Core::GpuTaskId buildTask = *buildTaskResult;
            buildDependency = buildTask;
            m_deferredShadowPrepareSoftwareBvhBuildLastTask = buildTask;
        }
        if(!m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid() || !m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH graph chain has no task bounds"));
            return false;
        }
        shadowPrepareDependency = buildDependency;
    }

    if(const auto& sceneRefit = m_raytracingSystem.preparedSceneSwBvhRefit(); sceneRefit){
        // A scene refit requires every frozen mesh build to be declared before it.
        if(!m_raytracingSystem.preparedMeshSwBvhBuildPlanFrozen() || (!preparedMeshSwBvhBuilds.empty() && !pureSoftwareMeshSwBvhBuildsGraphOwned))
            return false;
        const auto refitTasks = DeclareSoftwareSceneRefit(m_deferredLightingTaskGraph, sceneRefit, uploads.sceneBvhNodes, shadowPrepareDependency, scratchArena);
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
    // A pure-software per-mesh chain is an explicit immediate predecessor of this endpoint. Keep the complete packet even when later trace consumers form a FrontierSafe frontier.
    scheduling.allowMergeAcrossConsumerFrontier = pureSoftwareMeshSwBvhBuildsGraphOwned || m_deferredShadowPrepareSceneRefitTask.valid();
    constexpr usize s_DependencyCount = 1u;
    const Core::GpuTaskExternalStateSource shadowPrepareStateSource{ .states = m_shadowPreparePersistentState.source(), };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setScheduling(scheduling)
        .setDependencies(&shadowPrepareDependency, s_DependencyCount)
        .setExternalStateSources(shadowPrepareStateSource.states ? &shadowPrepareStateSource : nullptr, shadowPrepareStateSource.states ? 1u : 0u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(resourceSetUses.data(), resourceSetUses.size())
    ;
    const auto mDeferredShadowPrepareTaskResult = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>(
        desc,
        ECSRenderDetail::ShadowPrepareGraphTask::Payload{
            .raytracingSystem = m_raytracingSystem,
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
    if(!mDeferredShadowPrepareTaskResult){
        m_deferredShadowPrepareTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation task"));
        return false;
    }
    m_deferredShadowPrepareTask = *mDeferredShadowPrepareTaskResult;
    const bool accelStructBuildStatesGraphOwned = sceneTlasBuildGraphOwned || meshBlasBuildsGraphOwned;
    if(accelStructBuildStatesGraphOwned){
        const usize expectedFinalizeResourceUseCount = (sceneTlasBuildGraphOwned ? 1u : 0u) + preparedMeshBlasBuilds.size();
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
            const auto accelStructFinalizeSetResult = m_deferredLightingTaskGraph.importResourceSet(
                Core::GpuGraphResourceSetDesc{}
                    .setIdentity(Name("render.shadow_prepare.accel_struct_finalize_resources"))
                    .setMarkerLabel("Shadow Prepare Accel-Struct Finalize Resources")
                    .setMembers(accelStructFinalizeResources.data(), accelStructFinalizeResources.size())
            );
            if(accelStructFinalizeSetResult)
                accelStructFinalizeSet = *accelStructFinalizeSetResult;
        }
        const bool accelStructFinalizeSetGraphOwned = accelStructFinalizeSet.valid();
        const Core::GpuTaskResourceSetUse accelStructFinalizeSetUse{ .resourceSet = accelStructFinalizeSet, .range = {}, .requiredState = Core::ResourceStates::AccelStructRead, .access = Core::GpuTaskResourceAccess::Read, };
        Core::GpuTaskSchedulingHint accelStructFinalizeScheduling;
        accelStructFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accelStructFinalizeScheduling.forceSubmissionBoundary = false;
        accelStructFinalizeScheduling.allowPacketMerge = true;
        accelStructFinalizeScheduling.mergeWithPrevious = true;
        // Shadow Preparation has direct later Compute consumers. Keep these Read finalizers in the same packet so those consumers wait on every completed build and its typed backing state together.
        accelStructFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc accelStructFinalizeDesc;
        accelStructFinalizeDesc
            .setIdentity(Name("render.shadow_prepare.accel_struct_finalize"))
            .setMarkerLabel("Shadow Preparation Accel-Struct Finalize")
            .setScheduling(accelStructFinalizeScheduling)
            .setDependencies(&m_deferredShadowPrepareTask, 1u)
            // The immutable typed final-state collection expands to the same compiler inputs. Retain the individual declarations when members cannot form a complete unique set.
            .setResourceUses(accelStructFinalizeSetGraphOwned ? nullptr : accelStructFinalizeResourceUses.data(), accelStructFinalizeSetGraphOwned ? 0u : accelStructFinalizeResourceUses.size())
            .setResourceSetUses(accelStructFinalizeSetGraphOwned ? &accelStructFinalizeSetUse : nullptr, accelStructFinalizeSetGraphOwned ? 1u : 0u)
        ;
        const auto mDeferredShadowPrepareAccelStructFinalizeTaskResult = m_deferredLightingTaskGraph.addTask< ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask >(
            accelStructFinalizeDesc,
            ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask::Payload{}
        );
        if(!mDeferredShadowPrepareAccelStructFinalizeTaskResult){
            m_deferredShadowPrepareAccelStructFinalizeTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare acceleration-structure final-state task"));
            return false;
        }
        m_deferredShadowPrepareAccelStructFinalizeTask = *mDeferredShadowPrepareAccelStructFinalizeTaskResult;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

