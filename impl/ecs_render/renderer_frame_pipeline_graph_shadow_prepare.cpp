// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <impl/ecs_render/raytrace/prepared_software_bvh_graph_resources.h>
#include <impl/ecs_render/raytrace/shadow_prepare_geometry_resources.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_finalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererFramePipeline::declareDeferredShadowPrepareTask(
    DeferredFrameTargets& deferredTargets,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
    const Core::GpuGraphResourceId* const softwareBvhBuildStateResources,
    const usize softwareBvhBuildStateResourceCount,
    const bool softwareTraceResourcesPrepared,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace RendererTaskGraphDetail;

    m_deferredShadowPrepareTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildFirstTask = {};
    m_deferredShadowPrepareSoftwareBvhBuildLastTask = {};
    m_deferredShadowPrepareHybridSoftwareTailTask = {};
    m_deferredShadowPrepareAccelStructFinalizeTask = {};
    m_deferredBindlessSlotsUploadTask = {};
    m_rayTraceMaterialContextSlotsUploadTask = {};
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

    const bool currentBindlessSlotsGraphOwned = !deferredTargets.bindless.slotsUploaded;
    if(currentBindlessSlotsGraphOwned){
        const Core::GpuUploadBlobId bindlessSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
            &deferredTargets.bindless.slots,
            sizeof(deferredTargets.bindless.slots),
            alignof(DeferredBindlessResourceSlots)
        );
        if(!bindlessSlotsBlob.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain deferred bindless selector upload data"));
            return false;
        }

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(Name("render.deferred.bindless_slots_upload"))
            .setMarkerLabel("Deferred Bindless Slots Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(uploadScheduling)
        ;
        m_deferredBindlessSlotsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = bindlessSlotsBlob,
                .destination = currentBindlessSlots,
                // Selector persists across packet closes; upload publishes ConstantBuffer.
                .finalState = Core::ResourceStates::ConstantBuffer,
            }
        );
        if(!m_deferredBindlessSlotsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred bindless selector upload"));
            return false;
        }
    }

    // Trace buffers finalized by preflight; retain slots before compile.
    RayTraceMaterialContextSlots rayTraceMaterialContextSlots;
    if(!m_raytracingSystem.snapshotRayTraceMaterialContextSlots(rayTraceMaterialContextSlots)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot ray-trace material-context selector"));
        return false;
    }
    const Core::GpuUploadBlobId rayTraceMaterialContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
        &rayTraceMaterialContextSlots,
        sizeof(rayTraceMaterialContextSlots),
        alignof(RayTraceMaterialContextSlots)
    );
    if(!rayTraceMaterialContextSlotsBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain ray-trace material-context selector upload data"));
        return false;
    }

    const Core::GpuTaskId* const materialContextUploadDependencies = currentBindlessSlotsGraphOwned
        ? &m_deferredBindlessSlotsUploadTask
        : nullptr
    ;
    const usize materialContextUploadDependencyCount = currentBindlessSlotsGraphOwned ? 1u : 0u;
    Core::GpuTaskSchedulingHint materialContextUploadScheduling;
    materialContextUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    materialContextUploadScheduling.forceSubmissionBoundary = false;
    materialContextUploadScheduling.allowPacketMerge = true;
    materialContextUploadScheduling.mergeWithPrevious = currentBindlessSlotsGraphOwned;
    Core::GpuTaskDesc materialContextUploadDesc;
    materialContextUploadDesc
        .setIdentity(Name("render.raytrace.material_context_slots_upload"))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(materialContextUploadScheduling)
        .setDependencies(materialContextUploadDependencies, materialContextUploadDependencyCount)
    ;
    m_rayTraceMaterialContextSlotsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
        materialContextUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = rayTraceMaterialContextSlotsBlob,
            .destination = materialContextSlots,
            // Selector buffers publish Common; Shadow Preparation owns the next transition.
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!m_rayTraceMaterialContextSlotsUploadTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare ray-trace material-context selector upload"));
        return false;
    }

    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Name causticEmissionTargetsIdentity = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
            ? Name("render.hardware_caustics.emission_targets")
            : Name("render.software_caustics.emission_targets")
    ;
    const Core::GpuGraphResourceId causticEmissionTargets = rayTracingResources.causticEmissionTargetBuffer
        ? importBuffer(
            rayTracingResources.causticEmissionTargetBuffer,
            causticEmissionTargetsIdentity,
            "Caustic Emission Targets"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(rayTracingResources.causticEmissionTargetBuffer && !causticEmissionTargets.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted caustic emission targets"));
        return false;
    }

    Core::GpuUploadBlobId causticEmissionTargetsBlob;
    if(!m_raytracingSystem.retainPreparedCausticEmissionTargetUpload(
        m_deferredLightingTaskGraph,
        causticEmissionTargetsBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted caustic emission-target upload data"));
        return false;
    }

    Core::GpuTaskId shadowPrepareDependency = m_rayTraceMaterialContextSlotsUploadTask;
    if(causticEmissionTargetsBlob.valid()){
        if(!causticEmissionTargets.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target upload has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint causticEmissionTargetsUploadScheduling;
        causticEmissionTargetsUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        causticEmissionTargetsUploadScheduling.forceSubmissionBoundary = false;
        causticEmissionTargetsUploadScheduling.allowPacketMerge = true;
        causticEmissionTargetsUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc causticEmissionTargetsUploadDesc;
        causticEmissionTargetsUploadDesc
            .setIdentity(Name("render.raytrace.caustic_emission_targets_upload"))
            .setMarkerLabel("Caustic Emission Targets Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(causticEmissionTargetsUploadScheduling)
            .setDependencies(&m_rayTraceMaterialContextSlotsUploadTask, 1u)
        ;
        m_causticEmissionTargetsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            causticEmissionTargetsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = causticEmissionTargetsBlob,
                .destination = causticEmissionTargets,
                // Buffer publishes Common; Shadow Preparation owns the SRV handoff.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_causticEmissionTargetsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare caustic emission-target upload"));
            return false;
        }
        shadowPrepareDependency = m_causticEmissionTargetsUploadTask;
    }

    const Core::GpuGraphResourceId surfelFrameConstants = rayTracingResources.surfelFrameConstantsBuffer
        ? importBuffer(
            rayTracingResources.surfelFrameConstantsBuffer,
            Name("render.surfel_gi.constants"),
            "Surfel Constants"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(rayTracingResources.surfelFrameConstantsBuffer && !surfelFrameConstants.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted surfel constants"));
        return false;
    }

    Core::GpuUploadBlobId surfelFrameConstantsBlob;
    if(!m_raytracingSystem.retainPreparedSurfelFrameConstantsUpload(
        m_deferredLightingTaskGraph,
        deferredTargets,
        surfelFrameConstantsBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted surfel-frame constants upload data"));
        return false;
    }
    if(surfelFrameConstantsBlob.valid()){
        if(!surfelFrameConstants.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel-frame constants upload has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint surfelFrameConstantsUploadScheduling;
        surfelFrameConstantsUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        surfelFrameConstantsUploadScheduling.forceSubmissionBoundary = false;
        surfelFrameConstantsUploadScheduling.allowPacketMerge = true;
        surfelFrameConstantsUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc surfelFrameConstantsUploadDesc;
        surfelFrameConstantsUploadDesc
            .setIdentity(Name("render.surfel_gi.constants_upload"))
            .setMarkerLabel("Surfel Frame Constants Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(surfelFrameConstantsUploadScheduling)
            .setDependencies(&shadowPrepareDependency, 1u)
        ;
        m_surfelFrameConstantsUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            surfelFrameConstantsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = surfelFrameConstantsBlob,
                .destination = surfelFrameConstants,
                // Buffer publishes Common; Shadow Preparation owns the CB handoff.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_surfelFrameConstantsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare surfel-frame constants upload"));
            return false;
        }
        shadowPrepareDependency = m_surfelFrameConstantsUploadTask;
    }

    const Core::GpuGraphResourceId shadowInstanceMaterials = rayTracingResources.shadowInstanceMaterialBuffer
        ? importBuffer(
            rayTracingResources.shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId shadowInstances = rayTracingResources.shadowInstanceBuffer
        ? importBuffer(
            rayTracingResources.shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId shadowMaterialTyped = rayTracingResources.shadowMaterialTypedBuffer
        ? importBuffer(
            rayTracingResources.shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(
        (rayTracingResources.shadowInstanceMaterialBuffer && !shadowInstanceMaterials.valid())
        || (rayTracingResources.shadowInstanceBuffer && !shadowInstances.valid())
        || (rayTracingResources.shadowMaterialTypedBuffer && !shadowMaterialTyped.valid())
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow material-context buffers"));
        return false;
    }

    Core::GpuUploadBlobId shadowInstanceMaterialsBlob;
    Core::GpuUploadBlobId shadowInstancesBlob;
    Core::GpuUploadBlobId shadowMaterialTypedBlob;
    if(!m_raytracingSystem.retainPreparedShadowMaterialContextUploads(
        m_deferredLightingTaskGraph,
        shadowInstanceMaterialsBlob,
        shadowInstancesBlob,
        shadowMaterialTypedBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow material-context upload data"));
        return false;
    }
    const bool shadowMaterialContextBatchGraphOwned = shadowInstanceMaterialsBlob.valid();
    if(
        shadowMaterialContextBatchGraphOwned != shadowInstancesBlob.valid()
        || shadowMaterialContextBatchGraphOwned != shadowMaterialTypedBlob.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen shadow material-context upload batch"));
        return false;
    }
    if(shadowMaterialContextBatchGraphOwned){
        if(!shadowInstanceMaterials.valid() || !shadowInstances.valid() || !shadowMaterialTyped.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen shadow material-context batch has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint shadowMaterialContextUploadScheduling;
        shadowMaterialContextUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        shadowMaterialContextUploadScheduling.forceSubmissionBoundary = false;
        shadowMaterialContextUploadScheduling.allowPacketMerge = true;
        shadowMaterialContextUploadScheduling.mergeWithPrevious = true;

        Core::GpuTaskDesc shadowInstanceMaterialsUploadDesc;
        shadowInstanceMaterialsUploadDesc
            .setIdentity(Name("render.deferred_effects.instance_material_upload"))
            .setMarkerLabel("Shadow Instance Materials Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(shadowMaterialContextUploadScheduling)
            .setDependencies(&shadowPrepareDependency, 1u)
        ;
        m_shadowInstanceMaterialUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            shadowInstanceMaterialsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = shadowInstanceMaterialsBlob,
                .destination = shadowInstanceMaterials,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_shadowInstanceMaterialUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow instance-material upload"));
            return false;
        }

        Core::GpuTaskDesc shadowInstancesUploadDesc;
        shadowInstancesUploadDesc
            .setIdentity(Name("render.deferred_effects.shadow_instances_upload"))
            .setMarkerLabel("Shadow Instances Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(shadowMaterialContextUploadScheduling)
            .setDependencies(&m_shadowInstanceMaterialUploadTask, 1u)
        ;
        m_shadowInstanceUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            shadowInstancesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = shadowInstancesBlob,
                .destination = shadowInstances,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_shadowInstanceUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow instance upload"));
            return false;
        }

        Core::GpuTaskDesc shadowMaterialTypedUploadDesc;
        shadowMaterialTypedUploadDesc
            .setIdentity(Name("render.deferred_effects.material_typed_upload"))
            .setMarkerLabel("Shadow Typed Materials Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(shadowMaterialContextUploadScheduling)
            .setDependencies(&m_shadowInstanceUploadTask, 1u)
        ;
        m_shadowMaterialTypedUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            shadowMaterialTypedUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = shadowMaterialTypedBlob,
                .destination = shadowMaterialTyped,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_shadowMaterialTypedUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow typed-material upload"));
            return false;
        }
        shadowPrepareDependency = m_shadowMaterialTypedUploadTask;
    }

    const Core::GpuGraphResourceId sceneBvhNodes = rayTracingResources.sceneBvhNodeBuffer
        ? importBuffer(
            rayTracingResources.sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId sceneBvhInstances = rayTracingResources.sceneInstanceBuffer
        ? importBuffer(
            rayTracingResources.sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(
        (rayTracingResources.sceneBvhNodeBuffer && !sceneBvhNodes.valid())
        || (rayTracingResources.sceneInstanceBuffer && !sceneBvhInstances.valid())
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted software scene-BVH buffers"));
        return false;
    }

    Core::GpuUploadBlobId sceneBvhNodesBlob;
    Core::GpuUploadBlobId sceneBvhInstancesBlob;
    if(!m_raytracingSystem.retainPreparedSceneBvhUploads(
        m_deferredLightingTaskGraph,
        sceneBvhNodesBlob,
        sceneBvhInstancesBlob
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted software scene-BVH upload data"));
        return false;
    }
    const bool sceneBvhBatchGraphOwned = sceneBvhNodesBlob.valid();
    if(sceneBvhBatchGraphOwned != sceneBvhInstancesBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen software scene-BVH upload pair"));
        return false;
    }
    if(sceneBvhBatchGraphOwned){
        if(!sceneBvhNodes.valid() || !sceneBvhInstances.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene-BVH pair has no imported destination"));
            return false;
        }

        Core::GpuTaskSchedulingHint sceneBvhUploadScheduling;
        sceneBvhUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        sceneBvhUploadScheduling.forceSubmissionBoundary = false;
        sceneBvhUploadScheduling.allowPacketMerge = true;
        sceneBvhUploadScheduling.mergeWithPrevious = true;

        Core::GpuTaskDesc sceneBvhNodesUploadDesc;
        sceneBvhNodesUploadDesc
            .setIdentity(Name("render.shadow_visibility.scene_bvh_nodes_upload"))
            .setMarkerLabel("Scene BVH Nodes Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(sceneBvhUploadScheduling)
            .setDependencies(&shadowPrepareDependency, 1u)
        ;
        m_sceneBvhNodesUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            sceneBvhNodesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneBvhNodesBlob,
                .destination = sceneBvhNodes,
                // Scene-BVH storage publishes Common; Shadow Preparation owns the SRV handoff.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_sceneBvhNodesUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software scene-BVH node upload"));
            return false;
        }

        Core::GpuTaskDesc sceneBvhInstancesUploadDesc;
        sceneBvhInstancesUploadDesc
            .setIdentity(Name("render.shadow_visibility.scene_instances_upload"))
            .setMarkerLabel("Scene BVH Instances Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(sceneBvhUploadScheduling)
            .setDependencies(&m_sceneBvhNodesUploadTask, 1u)
        ;
        m_sceneBvhInstancesUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            sceneBvhInstancesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneBvhInstancesBlob,
                .destination = sceneBvhInstances,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_sceneBvhInstancesUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software scene-BVH instance upload"));
            return false;
        }
        shadowPrepareDependency = m_sceneBvhInstancesUploadTask;
    }


// Opaque and healthy hybrid hardware TLAS builds retain their preflight instance stream inside Shadow
    // Preparation itself; first Graphics packet owns the native build.
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
    // Keep hybrid HW-to-SW tail in first packet with explicit boundary; pure routes untouched.
    const bool hybridSoftwareTailGraphOwned =
        m_raytracingSystem.hybridShadowVisibilityResourcesPreflighted()
        && m_raytracingSystem.preparedMeshSwBvhBuildPlanFrozen()
    ;
    // Healthy hybrid retains HW context; declining tail restores frozen bytes from blobs.
    Core::GpuUploadBlobId hybridHardwareFallbackInstanceMaterialBlob;
    Core::GpuUploadBlobId hybridHardwareFallbackInstanceBlob;
    Core::GpuUploadBlobId hybridHardwareFallbackMaterialTypedBlob;
    if(hybridSoftwareTailGraphOwned){
        const bool retainedHybridHardwareFallback =
            m_raytracingSystem.retainPreparedHybridHardwareMaterialContextFallbackUploads(
                m_deferredLightingTaskGraph,
                hybridHardwareFallbackInstanceMaterialBlob,
                hybridHardwareFallbackInstanceBlob,
                hybridHardwareFallbackMaterialTypedBlob
            )
        ;
        const bool hybridHardwareFallbackBlobBatchComplete =
            hybridHardwareFallbackInstanceMaterialBlob.valid()
            && hybridHardwareFallbackInstanceBlob.valid()
            && hybridHardwareFallbackMaterialTypedBlob.valid()
        ;
        if(
            !retainedHybridHardwareFallback
            || !hybridHardwareFallbackBlobBatchComplete
            || !shadowInstanceMaterials.valid()
            || !shadowInstances.valid()
            || !shadowMaterialTyped.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: healthy hybrid tail requires a complete graph-owned hardware material fallback"));
            return false;
        }
    }
    // Frozen hybrid packet lowers the BLAS -> SW-BVH handoff at its tail boundary.
    const bool hybridSoftwareTailInputStatesCandidate =
        hybridSoftwareTailGraphOwned
        && meshBlasBuildsGraphOwned
        && meshSwBvhBuildsGraphOwned
    ;
    // Tail-less hardware geometry enters graph-owned; hybrid only to its tail boundary.
    bool meshBlasGeometryBuildInputStatesGraphOwned =
        meshBlasBuildsGraphOwned
        && (
            hybridSoftwareTailInputStatesCandidate
            || (!softwareTraceResourcesPrepared && !meshSwBvhBuildsGraphOwned)
        )
    ;
    bool meshSwBvhInputStatesGraphOwned = hybridSoftwareTailInputStatesCandidate;
    // Pure-software moves sentinel setup into built-ins; hybrid keeps conditional native path.
    const bool pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate =
        meshSwBvhBuildsGraphOwned
        && !m_raytracingSystem.shadowVisibilityHardwareSupported()
    ;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceSetUse, Core::Alloc::ScratchArena> resourceSetUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accelStructFinalizeResourceUses{ scratchArena };
    const ShadowPrepareGeometryInputs geometryInputs{
        .blasBuilds = preparedMeshBlasBuilds,
        .softwareBuilds = preparedMeshSwBvhBuilds,
        .traceResources = shadowTraceGeometryResources,
        .traceResourceCount = shadowTraceGeometryResourceCount,
        .blasBuildsGraphOwned = meshBlasBuildsGraphOwned,
    };
    ShadowPrepareGeometryResources geometryResources(geometryInputs, scratchArena);
    const auto& meshBlasGeometryBuildInputResources = geometryResources.m_blasBuildInputs;
    const auto& hybridSoftwareTailInputResources = geometryResources.m_softwareTailInputs;
    const auto& shadowPrepareTraceGeometryResources = geometryResources.m_remainingTraceGeometry;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hybridSoftwareTailResourceUses{ scratchArena };
    PreparedMeshSwBvhGraphResourceVector pureSoftwareMeshSwBvhGraphResources{ scratchArena };
    ECSRenderDetail::MeshBlasGraphStateVector liveMeshBlasGraphStates{ scratchArena };
    m_meshSystem.collectBlasGraphStates(liveMeshBlasGraphStates);
    resourceUses.reserve(
        19u
        + shadowTraceGeometryResourceCount
        + softwareBvhBuildStateResourceCount
        + liveMeshBlasGraphStates.size()
        + preparedMeshBlasBuilds.size() * 2u
    );
    accelStructFinalizeResourceUses.reserve(
        (sceneTlasBuildGraphOwned ? 1u : 0u)
        + preparedMeshBlasBuilds.size()
    );
    resourceSetUses.reserve(3u);
    geometryResources.prepareStorage(meshBlasGeometryBuildInputStatesGraphOwned, meshSwBvhInputStatesGraphOwned);
    hybridSoftwareTailResourceUses.reserve(preparedMeshSwBvhBuilds.size() * 2u + 3u);
    pureSoftwareMeshSwBvhGraphResources.reserve(preparedMeshSwBvhBuilds.size());
    // Shadow Preparation owns post-transition boundaries; Compute readers wait on this packet.
    resourceUses.push_back(ReadWriteUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Retain Shadow Preparation as producer; WAW handoff retires the immutable upload.
    resourceUses.push_back(WriteUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(causticEmissionTargets.valid())
        resourceUses.push_back(WriteUse(causticEmissionTargets, Core::ResourceStates::ShaderResource));
    if(surfelFrameConstants.valid())
        resourceUses.push_back(WriteUse(surfelFrameConstants, Core::ResourceStates::ConstantBuffer));
    if(shadowInstanceMaterials.valid())
        resourceUses.push_back(WriteUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
    if(shadowInstances.valid())
        resourceUses.push_back(WriteUse(shadowInstances, Core::ResourceStates::ShaderResource));
    if(shadowMaterialTyped.valid())
        resourceUses.push_back(WriteUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource));
    if(sceneBvhNodes.valid())
        resourceUses.push_back(WriteUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
    if(sceneBvhInstances.valid())
        resourceUses.push_back(WriteUse(sceneBvhInstances, Core::ResourceStates::ShaderResource));

    if(hybridSoftwareTailGraphOwned){
        // Tail may restore HW bytes on SW failure; final state declared here.
        hybridSoftwareTailResourceUses.push_back(
            WriteUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource)
        );
        hybridSoftwareTailResourceUses.push_back(
            WriteUse(shadowInstances, Core::ResourceStates::ShaderResource)
        );
        hybridSoftwareTailResourceUses.push_back(
            WriteUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource)
        );
    }

    bool resourcesImported = true;
    geometryResources.gatherBuildInputs(
        m_deferredLightingTaskGraph,
        meshBlasGeometryBuildInputStatesGraphOwned,
        meshSwBvhInputStatesGraphOwned
    );
    if(meshBlasBuildsGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            const Name blasIdentity = DeriveName(build.meshName, AStringView(":blas"));
            // First build knows native Common; retained storage imports the accepted binding.
            const Core::ResourceStates::Mask blasInitialState = build.backingFresh
                ? Core::ResourceStates::Common
                : Core::ResourceStates::Unknown
            ;
            const Core::GpuGraphResourceId blas = m_deferredLightingTaskGraph.importAccelStruct(
                build.blas,
                AccelStructResourceDesc(blasIdentity, "Prepared Mesh BLAS").setInitialState(blasInitialState)
            );
            resourcesImported = resourcesImported && blas.valid();
            if(blas.valid()){
                // Typed resource lowers state via backing allocation; compat routes keep native bridge.
                resourceUses.push_back(ReadWriteUse(blas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    Core::GpuGraphResourceSetId meshBlasGeometryBuildInputSet;
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputResources.empty()){
        meshBlasGeometryBuildInputSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.blas_geometry_build_inputs"))
                .setMarkerLabel("Shadow Prepare BLAS Geometry Build Inputs")
                .setMembers(
                    meshBlasGeometryBuildInputResources.data(),
                    meshBlasGeometryBuildInputResources.size()
                )
        );
    }
    const bool meshBlasGeometryBuildInputSetGraphOwned = meshBlasGeometryBuildInputSet.valid();
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputSetGraphOwned){
        for(const Core::GpuGraphResourceId resource : meshBlasGeometryBuildInputResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::AccelStructBuildInput));
    }
    const Core::GpuTaskResourceSetUse meshBlasGeometryBuildInputSetUse{
        .resourceSet = meshBlasGeometryBuildInputSet,
        .range = {},
        .requiredState = Core::ResourceStates::AccelStructBuildInput,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    if(meshBlasGeometryBuildInputSetGraphOwned)
        resourceSetUses.push_back(meshBlasGeometryBuildInputSetUse);
    Core::GpuGraphResourceSetId hybridSoftwareTailInputSet;
    if(meshSwBvhInputStatesGraphOwned && !hybridSoftwareTailInputResources.empty()){
        hybridSoftwareTailInputSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.hybrid_software_tail_inputs"))
                .setMarkerLabel("Shadow Prepare Hybrid Software Tail Inputs")
                .setMembers(hybridSoftwareTailInputResources.data(), hybridSoftwareTailInputResources.size())
        );
    }
    const bool hybridSoftwareTailInputSetGraphOwned = hybridSoftwareTailInputSet.valid();
    if(meshSwBvhInputStatesGraphOwned && !hybridSoftwareTailInputSetGraphOwned){
        for(const Core::GpuGraphResourceId resource : hybridSoftwareTailInputResources)
            hybridSoftwareTailResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse hybridSoftwareTailInputSetUse{
        .resourceSet = hybridSoftwareTailInputSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    if(!geometryResources.gatherRemainingTraceResources())
        return false;


    // Frozen trace list may carry build inputs; keep them separate from the SRV subset.
    Core::GpuGraphResourceSetId shadowPrepareTraceGeometrySet;
    if(!shadowPrepareTraceGeometryResources.empty()){
        shadowPrepareTraceGeometrySet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare_trace_geometry"))
                .setMarkerLabel("Shadow Prepare Trace Geometry")
                .setMembers(
                    shadowPrepareTraceGeometryResources.data(),
                    shadowPrepareTraceGeometryResources.size()
                )
        );
    }
    const bool shadowPrepareTraceGeometryStatesGraphOwned = shadowPrepareTraceGeometrySet.valid();
    if(!shadowPrepareTraceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : shadowPrepareTraceGeometryResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowPrepareTraceGeometrySetUse{
        .resourceSet = shadowPrepareTraceGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    if(shadowPrepareTraceGeometryStatesGraphOwned)
        resourceSetUses.push_back(shadowPrepareTraceGeometrySetUse);
    for(usize resourceIndex = 0u; resourceIndex < softwareBvhBuildStateResourceCount; ++resourceIndex){
        if(!softwareBvhBuildStateResources[resourceIndex].valid())
            return false;
    }
    Core::GpuGraphResourceSetId softwareBvhBuildStateSet;
    if(softwareBvhBuildStateResourceCount != 0u){
        softwareBvhBuildStateSet = m_deferredLightingTaskGraph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name("render.shadow_prepare.software_bvh_build_state"))
                .setMarkerLabel("Shadow Prepare Software BVH Build State")
                .setMembers(softwareBvhBuildStateResources, softwareBvhBuildStateResourceCount)
        );
    }
    const bool softwareBvhBuildStateStatesGraphOwned = softwareBvhBuildStateSet.valid();
    if(!softwareBvhBuildStateStatesGraphOwned){
        for(usize resourceIndex = 0u; resourceIndex < softwareBvhBuildStateResourceCount; ++resourceIndex){
            // Parent links and scratch keep native UAV state; they are state-only resources.
            resourceUses.push_back(ReadWriteUse(
                softwareBvhBuildStateResources[resourceIndex],
                Core::ResourceStates::UnorderedAccess
            ));
        }
    }
    const Core::GpuTaskResourceSetUse softwareBvhBuildStateSetUse{
        .resourceSet = softwareBvhBuildStateSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    if(softwareBvhBuildStateStatesGraphOwned)
        resourceSetUses.push_back(softwareBvhBuildStateSetUse);

    Core::GpuGraphResourceId sceneTlas;
    if(rayTracingShadowResources.sceneTlas){
        // Imports observe the backing generation; retained storage stays Unknown until accepted.
        const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();
        sceneTlas = m_deferredLightingTaskGraph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS").setInitialState(sceneTlasInitialState)
        );
        resourcesImported = resourcesImported && sceneTlas.valid();
        if(sceneTlas.valid()){
            if(sceneTlasBuildGraphOwned){
                // Frozen recorder only builds; graph lowers Write entry, successor lowers Read handoff.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // Compat builders keep their native sequence; final handoff stays unchanged.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    for(const ECSRenderDetail::MeshBlasGraphState& state : liveMeshBlasGraphStates){
        // Frozen plans own retained handles; never collide replacements with frozen identities.
        if(geometryResources.isPreparedMeshBlasBuild(state.meshName))
            continue;

        const Name blasIdentity = DeriveName(state.meshName, AStringView(":blas"));
        const Core::ResourceStates::Mask blasInitialState = state.backingFresh
            ? Core::ResourceStates::Common
            : Core::ResourceStates::Unknown
        ;
        const Core::GpuGraphResourceId blas = m_deferredLightingTaskGraph.importAccelStruct(
            state.blas,
            AccelStructResourceDesc(blasIdentity, "Mesh BLAS").setInitialState(blasInitialState)
        );
        resourcesImported = resourcesImported && blas.valid();
        if(blas.valid()){
            if(state.nativeBuildsBlas){
                // Compat and frozen plans record native sequences; backing seeds on next declaration.
                resourceUses.push_back(ReadWriteUse(blas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // State-only import; rejected preparation re-pends and seeds accepted state.
                resourceUses.push_back(ReadUse(blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!resourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-preparation resources"));
        return false;
    }

    bool pureSoftwareMeshSwBvhBuildsGraphOwned = pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate;
    if(pureSoftwareMeshSwBvhBuildsGraphOwned && !ResolvePreparedSoftwareBvhGraphResources(
        m_deferredLightingTaskGraph, preparedMeshSwBvhBuilds, pureSoftwareMeshSwBvhGraphResources
    )){
        // Keep the established aggregate direct path if a future preflight leaves any frozen operation
        // without an exact graph identity. Never mix a partial typed-clear chain with native sentinels.
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH build is missing graph resources; retaining aggregate compatibility recorder"));
        pureSoftwareMeshSwBvhBuildsGraphOwned = false;
    }

    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        // Every operation shares sort keys, payload, and visit-counter scratch. Its built-in clears must therefore
        // remain immediately adjacent to its compute callback, and the entire chain must remain in Shadow
        // Preparation's accepting Graphics packet despite later Compute consumers of the final traversal state.
        Core::GpuTaskSchedulingHint clearScheduling;
        clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        clearScheduling.forceSubmissionBoundary = false;
        clearScheduling.allowPacketMerge = true;
        clearScheduling.mergeWithPrevious = true;
        clearScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskSchedulingHint buildScheduling = clearScheduling;
        buildScheduling.cost = Core::GpuTaskCostHint::Large;

        Core::GpuTaskId buildDependency = shadowPrepareDependency;
        const auto addPureSoftwareBvhClear = [&](
            const Name identity,
            const AStringView label,
            const Core::GpuGraphResourceId destination,
            const u32 clearValue
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(clearScheduling)
                .setDependencies(&buildDependency, 1u)
            ;
            return m_deferredLightingTaskGraph.addClearBufferTask(
                clearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = destination,
                    .clearValue = clearValue,
                }
            );
        };
        const auto appendPureSoftwareBvhClear = [&](const Core::GpuTaskId clearTask){
            if(!clearTask.valid())
                return false;
            if(!m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid())
                m_deferredShadowPrepareSoftwareBvhBuildFirstTask = clearTask;
            buildDependency = clearTask;
            return true;
        };

        for(const PreparedMeshSwBvhGraphResources& resources : pureSoftwareMeshSwBvhGraphResources){
            const PreparedMeshSwBvhBuild& build = resources.build;
            if(!build.performRefit){
                if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(
                    DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_keys_clear")),
                    "Shadow Prepare SW-BVH Sort-Key Clear",
                    resources.sortKeys,
                    BvhNodeIndex::Invalid
                ))
                    || !appendPureSoftwareBvhClear(addPureSoftwareBvhClear(
                        DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_parent_clear")),
                        "Shadow Prepare SW-BVH Parent Clear",
                        resources.parent,
                        BvhNodeIndex::Invalid
                    ))
                ){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH sentinel clears"));
                    return false;
                }
            }
            if(!appendPureSoftwareBvhClear(addPureSoftwareBvhClear(
                DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_counter_clear")),
                "Shadow Prepare SW-BVH Counter Clear",
                resources.visitCounter,
                0u
            ))){
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
            Core::GpuTaskDesc buildDesc;
            buildDesc
                .setIdentity(DeriveName(build.meshName, AStringView(":shadow_prepare_sw_bvh_build")))
                .setMarkerLabel("Shadow Prepare SW-BVH Build")
                .setQueue(GraphicsPreferredComputeQueueRequest())
                .setScheduling(buildScheduling)
                .setDependencies(&buildDependency, 1u)
                .setResourceUses(buildResourceUses, LengthOf(buildResourceUses))
            ;
            const Core::GpuTaskId buildTask = m_deferredLightingTaskGraph.addTask<
                ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask
            >(
                buildDesc,
                ECSRenderDetail::ShadowPrepareSoftwareBvhBuildGraphTask::Payload{
                    .raytracingSystem = &m_raytracingSystem,
                    .build = build,
                    .timingTicket = &timingTicket,
                }
            );
            if(!buildTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare pure software BVH build callback"));
                return false;
            }
            buildDependency = buildTask;
            m_deferredShadowPrepareSoftwareBvhBuildLastTask = buildTask;
        }
        if(
            !m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid()
            || !m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH graph chain has no task bounds"));
            return false;
        }
        shadowPrepareDependency = buildDependency;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    // A pure-software per-mesh chain is an explicit immediate predecessor of this semantic endpoint. Retain the
    // complete accepting packet even when later trace consumers form a FrontierSafe consumer frontier.
    scheduling.allowMergeAcrossConsumerFrontier = pureSoftwareMeshSwBvhBuildsGraphOwned;
    const Core::GpuTaskId* const dependencies = &shadowPrepareDependency;
    constexpr usize dependencyCount = 1u;
    const Core::GpuTaskExternalStateSource shadowPrepareStateSource{
        .states = m_shadowPreparePersistentState.source(),
    };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(GraphicsComputeUploadQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(dependencies, dependencyCount)
        .setExternalStateSources(
            shadowPrepareStateSource.states ? &shadowPrepareStateSource : nullptr,
            shadowPrepareStateSource.states ? 1u : 0u
        )
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(resourceSetUses.data(), resourceSetUses.size())
    ;
    m_deferredShadowPrepareTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>(
        desc,
        ECSRenderDetail::ShadowPrepareGraphTask::Payload{
            .graphics = &m_graphics,
            .raytracingSystem = &m_raytracingSystem,
            .outcome = &m_shadowPreparationOutcome,
            .targets = &deferredTargets,
            .frameTimingTransaction = &frameTimingTransaction,
            .timingTicket = &timingTicket,
            .deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded,
            .currentBindlessSlotsGraphOwned = currentBindlessSlotsGraphOwned,
            .shadowMaterialContextBatchGraphOwned = shadowMaterialContextBatchGraphOwned,
            .sceneBvhBatchGraphOwned = sceneBvhBatchGraphOwned,
            .sceneTlasBuildGraphOwned = sceneTlasBuildGraphOwned,
            .meshBlasBuildsGraphOwned = meshBlasBuildsGraphOwned,
            .meshBlasGeometryBuildInputStatesGraphOwned = meshBlasGeometryBuildInputStatesGraphOwned,
            .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
            .preparedMeshSwBvhBuildsRecordedByGraph = pureSoftwareMeshSwBvhBuildsGraphOwned,
            .deferHybridSoftwareTail = hybridSoftwareTailGraphOwned,
        }
    );
    if(!m_deferredShadowPrepareTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared shadow-preparation task"));
        return false;
    }
    Core::GpuTaskId shadowPrepareFinalizeDependency = m_deferredShadowPrepareTask;
    if(hybridSoftwareTailGraphOwned){
        Core::GpuTaskSchedulingHint hybridSoftwareTailScheduling;
        hybridSoftwareTailScheduling.cost = Core::GpuTaskCostHint::Large;
        hybridSoftwareTailScheduling.forceSubmissionBoundary = false;
        hybridSoftwareTailScheduling.allowPacketMerge = true;
        hybridSoftwareTailScheduling.mergeWithPrevious = true;
        // Shadow Preparation already has direct later Compute consumers. The explicit immediate tail restores the
        // monolithic packet's ordering: those consumers wait for the complete HW-to-SW fallback boundary.
        hybridSoftwareTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc hybridSoftwareTailDesc;
        hybridSoftwareTailDesc
            .setIdentity(Name("render.shadow_prepare.hybrid_software_tail"))
            .setMarkerLabel("Shadow Preparation Hybrid Software Tail")
            .setQueue(GraphicsComputeUploadQueueRequest())
            .setScheduling(hybridSoftwareTailScheduling)
            .setDependencies(&m_deferredShadowPrepareTask, 1u)
            .setResourceUses(hybridSoftwareTailResourceUses.data(), hybridSoftwareTailResourceUses.size())
            .setResourceSetUses(
                hybridSoftwareTailInputSetGraphOwned ? &hybridSoftwareTailInputSetUse : nullptr,
                hybridSoftwareTailInputSetGraphOwned ? 1u : 0u
            )
        ;
        m_deferredShadowPrepareHybridSoftwareTailTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::ShadowPrepareHybridSoftwareTailGraphTask
        >(
            hybridSoftwareTailDesc,
            ECSRenderDetail::ShadowPrepareHybridSoftwareTailGraphTask::Payload{
                .raytracingSystem = &m_raytracingSystem,
                .targets = &deferredTargets,
                .hardwarePreparationReady = &m_shadowPreparationOutcome.ready,
                .timingTicket = &timingTicket,
                .shadowMaterialContextBatchGraphOwned = shadowMaterialContextBatchGraphOwned,
                .sceneBvhBatchGraphOwned = sceneBvhBatchGraphOwned,
                .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
                .meshSwBvhInputStatesGraphOwned = meshSwBvhInputStatesGraphOwned,
                .hybridHardwareFallbackInstanceMaterialBlob = hybridHardwareFallbackInstanceMaterialBlob,
                .hybridHardwareFallbackInstanceBlob = hybridHardwareFallbackInstanceBlob,
                .hybridHardwareFallbackMaterialTypedBlob = hybridHardwareFallbackMaterialTypedBlob,
            }
        );
        if(!m_deferredShadowPrepareHybridSoftwareTailTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hybrid software shadow-preparation tail"));
            return false;
        }
        shadowPrepareFinalizeDependency = m_deferredShadowPrepareHybridSoftwareTailTask;
    }

    const bool accelStructBuildStatesGraphOwned = sceneTlasBuildGraphOwned || meshBlasBuildsGraphOwned;
    if(accelStructBuildStatesGraphOwned){
        const usize expectedFinalizeResourceUseCount =
            (sceneTlasBuildGraphOwned ? 1u : 0u)
            + preparedMeshBlasBuilds.size()
        ;
        if(
            (sceneTlasBuildGraphOwned && !sceneTlas.valid())
            || accelStructFinalizeResourceUses.size() != expectedFinalizeResourceUseCount
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen acceleration-structure build has no final-state graph resources"));
            return false;
        }
        Core::GpuGraphResourceSetId accelStructFinalizeSet;
        if(!accelStructFinalizeResourceUses.empty()){
            Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> accelStructFinalizeResources{ scratchArena };
            accelStructFinalizeResources.reserve(accelStructFinalizeResourceUses.size());
            for(const Core::GpuTaskResourceUse& use : accelStructFinalizeResourceUses)
                accelStructFinalizeResources.push_back(use.resource);
            accelStructFinalizeSet = m_deferredLightingTaskGraph.importResourceSet(
                Core::GpuGraphResourceSetDesc{}
                    .setIdentity(Name("render.shadow_prepare.accel_struct_finalize_resources"))
                    .setMarkerLabel("Shadow Prepare Accel-Struct Finalize Resources")
                    .setMembers(accelStructFinalizeResources.data(), accelStructFinalizeResources.size())
            );
        }
        const bool accelStructFinalizeSetGraphOwned = accelStructFinalizeSet.valid();
        const Core::GpuTaskResourceSetUse accelStructFinalizeSetUse{
            .resourceSet = accelStructFinalizeSet,
            .range = {},
            .requiredState = Core::ResourceStates::AccelStructRead,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        Core::GpuTaskSchedulingHint accelStructFinalizeScheduling;
        accelStructFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
        accelStructFinalizeScheduling.forceSubmissionBoundary = false;
        accelStructFinalizeScheduling.allowPacketMerge = true;
        accelStructFinalizeScheduling.mergeWithPrevious = true;
        // Shadow Preparation has direct later Compute consumers. Keep these Read finalizers in the same accepting
        // packet so those consumers wait on every completed build and its typed backing state together.
        accelStructFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc accelStructFinalizeDesc;
        accelStructFinalizeDesc
            .setIdentity(Name("render.shadow_prepare.accel_struct_finalize"))
            .setMarkerLabel("Shadow Preparation Accel-Struct Finalize")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(accelStructFinalizeScheduling)
            .setDependencies(&shadowPrepareFinalizeDependency, 1u)
            // The immutable typed final-state collection expands to the same compiler inputs. Retain the
            // individual declarations if a future compatibility route cannot form a complete unique set.
            .setResourceUses(
                accelStructFinalizeSetGraphOwned ? nullptr : accelStructFinalizeResourceUses.data(),
                accelStructFinalizeSetGraphOwned ? 0u : accelStructFinalizeResourceUses.size()
            )
            .setResourceSetUses(
                accelStructFinalizeSetGraphOwned ? &accelStructFinalizeSetUse : nullptr,
                accelStructFinalizeSetGraphOwned ? 1u : 0u
            )
        ;
        m_deferredShadowPrepareAccelStructFinalizeTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask
        >(
            accelStructFinalizeDesc,
            ECSRenderDetail::ShadowPrepareAccelStructFinalizeGraphTask::Payload{}
        );
        if(!m_deferredShadowPrepareAccelStructFinalizeTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare acceleration-structure final-state task"));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

