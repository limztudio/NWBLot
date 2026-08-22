// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_finalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::declareDeferredShadowPrepareTask(
    DeferredFrameTargets& deferredTargets,
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
                // This selector persists its descriptor-visible state across packet closes. The built-in upload
                // performs its intrinsic CopyDest transition, then publishes ConstantBuffer to Shadow Preparation.
                .finalState = Core::ResourceStates::ConstantBuffer,
            }
        );
        if(!m_deferredBindlessSlotsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred bindless selector upload"));
            return false;
        }
    }

    // All trace backing buffers and their heap entries are finalized by preflight. Retain the resolved descriptor
    // slots now, before the graph is compiled, so recording never rereads mutable heap handles.
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
            // Automatic-state selector buffers publish Common. Shadow Preparation owns the following
            // ConstantBuffer transition and becomes the cross-queue producer for later trace consumers.
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
    const Name causticEmissionTargetsIdentity = graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
            ? Name("render.hardware_caustics.emission_targets")
            : Name("render.software_caustics.emission_targets")
    ;
    const Core::GpuGraphResourceId causticEmissionTargets = m_rayTracingState.m_causticEmissionTargetBuffer
        ? importBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            causticEmissionTargetsIdentity,
            "Caustic Emission Targets"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(m_rayTracingState.m_causticEmissionTargetBuffer && !causticEmissionTargets.valid()){
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
                // This automatic-state buffer publishes Common. Shadow Preparation owns the following SRV handoff
                // and thereby becomes the single producer observed by later software or hardware caustic packets.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_causticEmissionTargetsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare caustic emission-target upload"));
            return false;
        }
        shadowPrepareDependency = m_causticEmissionTargetsUploadTask;
    }

    const Core::GpuGraphResourceId surfelFrameConstants = m_rayTracingState.m_surfelConstants
        ? importBuffer(
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(m_rayTracingState.m_surfelConstants && !surfelFrameConstants.valid()){
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
                // This automatic-state constant buffer publishes Common. Shadow Preparation owns the following CB
                // handoff and therefore becomes the accepted cross-queue producer for Surfel GI.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!m_surfelFrameConstantsUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare surfel-frame constants upload"));
            return false;
        }
        shadowPrepareDependency = m_surfelFrameConstantsUploadTask;
    }

    const Core::GpuGraphResourceId shadowInstanceMaterials = m_rayTracingState.m_shadowInstanceMaterialBuffer
        ? importBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId shadowInstances = m_rayTracingState.m_shadowInstanceBuffer
        ? importBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId shadowMaterialTyped = m_rayTracingState.m_shadowMaterialTypedBuffer
        ? importBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(
        (m_rayTracingState.m_shadowInstanceMaterialBuffer && !shadowInstanceMaterials.valid())
        || (m_rayTracingState.m_shadowInstanceBuffer && !shadowInstances.valid())
        || (m_rayTracingState.m_shadowMaterialTypedBuffer && !shadowMaterialTyped.valid())
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

    const Core::GpuGraphResourceId sceneBvhNodes = m_rayTracingState.m_sceneBvhNodeBuffer
        ? importBuffer(
            m_rayTracingState.m_sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId sceneBvhInstances = m_rayTracingState.m_sceneInstanceBuffer
        ? importBuffer(
            m_rayTracingState.m_sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances"
        )
        : Core::GpuGraphResourceId{}
    ;
    if(
        (m_rayTracingState.m_sceneBvhNodeBuffer && !sceneBvhNodes.valid())
        || (m_rayTracingState.m_sceneInstanceBuffer && !sceneBvhInstances.valid())
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
                // Automatic-state software scene-BVH storage publishes Common. Shadow Preparation becomes the SRV
                // handoff producer observed by the later Compute shadow, caustic, and surfel consumers.
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
    // Preparation itself. They do not add an upload packet: the existing first Graphics packet owns the native
    // build and its acceptance cache.
    const bool sceneTlasBuildGraphOwned = m_raytracingSystem.preparedSceneTlasBuildReady();
    if(sceneTlasBuildGraphOwned && !m_rayTracingState.m_tlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build has no imported acceleration structure"));
        return false;
    }
    const bool meshBlasBuildsGraphOwned = m_raytracingSystem.preparedMeshBlasBuildsReady();
    const PreparedMeshBlasBuildVector& preparedMeshBlasBuilds = m_raytracingSystem.preparedMeshBlasBuilds();
    if(meshBlasBuildsGraphOwned && preparedMeshBlasBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no operations"));
        return false;
    }
    if(meshBlasBuildsGraphOwned && !m_rayTracingState.m_tlas){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build plan has no scene TLAS"));
        return false;
    }
    const bool meshSwBvhBuildsGraphOwned = m_raytracingSystem.preparedMeshSwBvhBuildsReady();
    const PreparedMeshSwBvhBuildVector& preparedMeshSwBvhBuilds = m_raytracingSystem.preparedMeshSwBvhBuilds();
    if(meshSwBvhBuildsGraphOwned && preparedMeshSwBvhBuilds.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build plan has no operations"));
        return false;
    }
    // Keep the hybrid HW-to-SW continuation inside the first accepting packet, but make its recording boundary
    // explicit. The tail remains absent for pure-HW and pure-SW routes, whose established aggregate callbacks stay
    // untouched.
    const bool hybridSoftwareTailGraphOwned =
        m_raytracingSystem.hybridShadowVisibilityResourcesPreflighted();
    // A healthy hybrid preflight retains the opaque-HW material context before it publishes the software context
    // consumed by the tail. If that tail later declines to record, it can restore the frozen HW bytes from these
    // graph-owned blobs without re-gathering material descriptors. A missing/stale capture deliberately keeps the
    // existing direct retry boundary below.
    Core::GpuUploadBlobId hybridHardwareFallbackInstanceMaterialBlob;
    Core::GpuUploadBlobId hybridHardwareFallbackInstanceBlob;
    Core::GpuUploadBlobId hybridHardwareFallbackMaterialTypedBlob;
    bool hybridHardwareFallbackUploadsGraphOwned = false;
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
        const bool hybridHardwareFallbackBlobBatchEmpty =
            !hybridHardwareFallbackInstanceMaterialBlob.valid()
            && !hybridHardwareFallbackInstanceBlob.valid()
            && !hybridHardwareFallbackMaterialTypedBlob.valid()
        ;
        if(
            retainedHybridHardwareFallback
            && hybridHardwareFallbackBlobBatchComplete
            && shadowInstanceMaterials.valid()
            && shadowInstances.valid()
            && shadowMaterialTyped.valid()
        ){
            hybridHardwareFallbackUploadsGraphOwned = true;
        }
        else if(!retainedHybridHardwareFallback || !hybridHardwareFallbackBlobBatchEmpty){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: frozen hybrid hardware material fallback cannot use graph-owned upload blobs; retaining direct retry"
            ));
        }
    }
    // A fully frozen hybrid packet has a separate software-tail callback, so the graph can now lower its exact
    // BLAS AccelStructBuildInput -> SW-BVH ShaderResource handoff at that boundary. Keep the direct/retry fallback
    // native unless both frozen plans and every required shared trace-geometry import are available.
    const bool hybridSoftwareTailInputStatesCandidate =
        hybridSoftwareTailGraphOwned
        && meshBlasBuildsGraphOwned
        && meshSwBvhBuildsGraphOwned
    ;
    // A prepared hardware route with no software tail has no later consumer, so its frozen geometry can enter
    // graph-owned directly as before. The hybrid candidate below extends that only to its verified tail boundary.
    bool meshBlasGeometryBuildInputStatesGraphOwned =
        meshBlasBuildsGraphOwned
        && (
            hybridSoftwareTailInputStatesCandidate
            || (!softwareTraceResourcesPrepared && !meshSwBvhBuildsGraphOwned)
        )
    ;
    bool meshSwBvhInputStatesGraphOwned = hybridSoftwareTailInputStatesCandidate;
    // The pure-software route has no accepted hardware fallback. It can therefore move each frozen operation's
    // private sentinel setup into graph built-ins, while the hybrid route deliberately retains its conditional
    // native transaction until its fallback boundary is separately modeled.
    const bool pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate =
        meshSwBvhBuildsGraphOwned
        && !m_raytracingSystem.shadowVisibilityHardwareSupported()
    ;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    struct PreparedMeshSwBvhGraphResources{
        PreparedMeshSwBvhBuild build;
        Core::GpuGraphResourceId position;
        Core::GpuGraphResourceId triangleIndex;
        Core::GpuGraphResourceId node;
        Core::GpuGraphResourceId parent;
        Core::GpuGraphResourceId sortKeys;
        Core::GpuGraphResourceId sortPayload;
        Core::GpuGraphResourceId visitCounter;
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceSetUse, Core::Alloc::ScratchArena> resourceSetUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accelStructFinalizeResourceUses{ scratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> meshBlasGeometryBuildInputResources{ scratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> hybridSoftwareTailInputResources{ scratchArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> shadowPrepareTraceGeometryResources{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hybridSoftwareTailResourceUses{ scratchArena };
    Vector<PreparedMeshSwBvhGraphResources, Core::Alloc::ScratchArena> pureSoftwareMeshSwBvhGraphResources{
        scratchArena
    };
    resourceUses.reserve(
        19u
        + shadowTraceGeometryResourceCount
        + softwareBvhBuildStateResourceCount
        + meshState().m_meshes.size()
        + preparedMeshBlasBuilds.size() * 2u
    );
    accelStructFinalizeResourceUses.reserve(
        (sceneTlasBuildGraphOwned ? 1u : 0u)
        + preparedMeshBlasBuilds.size()
    );
    resourceSetUses.reserve(3u);
    meshBlasGeometryBuildInputResources.reserve(preparedMeshBlasBuilds.size() * 2u);
    hybridSoftwareTailInputResources.reserve(preparedMeshSwBvhBuilds.size() * 2u);
    shadowPrepareTraceGeometryResources.reserve(shadowTraceGeometryResourceCount);
    hybridSoftwareTailResourceUses.reserve(preparedMeshSwBvhBuilds.size() * 2u + 3u);
    pureSoftwareMeshSwBvhGraphResources.reserve(preparedMeshSwBvhBuilds.size());
    // Shadow Preparation owns each preflight input's post-transition packet boundary. This deliberately supersedes
    // preceding immutable uploads as graph producers, so later Compute readers wait on this first Graphics packet
    // rather than forcing FrontierSafe packetization to split an upload away from its accepting consumer.
    resourceUses.push_back(ReadWriteUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Retain Shadow Preparation as the graph producer for this selector. This WAW handoff retires the immutable
    // upload before later Compute reads without a record-time native state transition.
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

    if(hybridHardwareFallbackUploadsGraphOwned){
        // The tail may conditionally restore these immutable HW bytes after the optional SW traversal fails. Its
        // final shader-visible state is declared here; the graph-owned recorder performs the internal CopyDest
        // writes only on that fallback arm.
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
    const auto isShadowTraceGeometryResource = [&](const Core::GpuGraphResourceId resource){
        for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
            if(shadowTraceGeometryResources[resourceIndex] == resource)
                return true;
        }
        return false;
    };
    const auto appendTraceGeometryResource = [&](
        Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& resources,
        const Core::BufferHandle& buffer
    ){
        const Core::GpuGraphResourceId resource = m_deferredLightingTaskGraph.findImportedBuffer(buffer);
        if(!resource.valid() || !isShadowTraceGeometryResource(resource))
            return false;
        for(const Core::GpuGraphResourceId existing : resources){
            if(existing == resource)
                return true;
        }
        resources.push_back(resource);
        return true;
    };
    if(meshBlasGeometryBuildInputStatesGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            if(
                !appendTraceGeometryResource(meshBlasGeometryBuildInputResources, build.positionBuffer)
                || !appendTraceGeometryResource(meshBlasGeometryBuildInputResources, build.triangleIndexBuffer)
            ){
                // Do not reject an otherwise valid packet merely because a future preflight change omitted one
                // frozen stream from the graph-owned trace set. Retain the complete native BLAS/SW bridge instead.
                meshBlasGeometryBuildInputStatesGraphOwned = false;
                meshSwBvhInputStatesGraphOwned = false;
                meshBlasGeometryBuildInputResources.clear();
                break;
            }
        }
    }
    if(meshSwBvhInputStatesGraphOwned){
        for(const PreparedMeshSwBvhBuild& build : preparedMeshSwBvhBuilds){
            if(
                !appendTraceGeometryResource(hybridSoftwareTailInputResources, build.positionBuffer)
                || !appendTraceGeometryResource(hybridSoftwareTailInputResources, build.triangleIndexBuffer)
            ){
                // The prepared SW recorder accepts one all-or-native input-state policy. If any frozen input cannot
                // be declared at the tail boundary, preserve the established native bridge for both callbacks.
                meshBlasGeometryBuildInputStatesGraphOwned = false;
                meshSwBvhInputStatesGraphOwned = false;
                meshBlasGeometryBuildInputResources.clear();
                hybridSoftwareTailInputResources.clear();
                break;
            }
        }
    }
    const auto isMeshBlasGeometryBuildInput = [&](const Core::GpuGraphResourceId resource){
        for(const Core::GpuGraphResourceId buildInput : meshBlasGeometryBuildInputResources){
            if(buildInput == resource)
                return true;
        }
        return false;
    };
    const auto isPreparedMeshBlasBuild = [&](const Name meshName){
        if(!meshBlasBuildsGraphOwned)
            return false;
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            if(build.meshName == meshName)
                return true;
        }
        return false;
    };
    if(meshBlasBuildsGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            const Name blasIdentity = DeriveName(build.meshName, AStringView(":blas"));
            // Only this backing generation's first graph build knows native Common. Retained BLAS storage must
            // import the accepted Shadow Preparation binding; Unknown deliberately rejects a missing handoff.
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
                // The typed graph resource lowers its state and ownership through its retained backing allocation.
                // A prepared no-tail route and a fully verified hybrid tail own the matching geometry boundary;
                // direct and incomplete compatibility routes retain their native bridge.
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
    for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = shadowTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        if(isMeshBlasGeometryBuildInput(resource))
            continue;
        shadowPrepareTraceGeometryResources.push_back(resource);
    }


// The frozen trace list can carry BLAS position/index resources that need AccelStructBuildInput in this task.
    // Keep those exact members separate, but declare the remaining SRV subset as one immutable graph collection.
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
            // Parent links and global build scratch retain their native UAV close state. They are state-only graph
            // resources: later traversal keeps its narrower node/geometry declarations.
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
    if(m_rayTracingState.m_tlas){
        // Every import observes the current backing generation. Fresh direct and frozen paths know native Common;
        // retained storage remains Unknown until the accepted packet-state binding supplies its final native state.
        const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();
        sceneTlas = m_deferredLightingTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS").setInitialState(sceneTlasInitialState)
        );
        resourcesImported = resourcesImported && sceneTlas.valid();
        if(sceneTlas.valid()){
            if(sceneTlasBuildGraphOwned){
                // The frozen native recorder only builds. The graph lowers its required Write entry state here and
                // the state-only successor below lowers the final Read handoff through the same retained backing.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // Direct compatibility builders still publish their native Write -> Read sequence inside Shadow
                // Preparation. Keep the graph-visible final handoff unchanged for those routes.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        // A frozen plan owns its retained handles even if a record-time replacement later sends it through the
        // native compatibility fallback. Do not collide a replacement with the frozen graph identity here.
        if(isPreparedMeshBlasBuild(mesh.meshName))
            continue;
        if(!mesh.blas)
            continue;

        const Name blasIdentity = DeriveName(mesh.meshName, AStringView(":blas"));
        const Core::ResourceStates::Mask blasInitialState = mesh.blasBackingFresh
            ? Core::ResourceStates::Common
            : Core::ResourceStates::Unknown
        ;
        const Core::GpuGraphResourceId blas = m_deferredLightingTaskGraph.importAccelStruct(
            mesh.blas,
            AccelStructResourceDesc(blasIdentity, "Mesh BLAS").setInitialState(blasInitialState)
        );
        resourcesImported = resourcesImported && blas.valid();
        if(blas.valid()){
            const bool nativeBuildsBlas = mesh.runtimeMesh || mesh.blasBuildPending;
            if(nativeBuildsBlas){
                // Direct hybrid compatibility and frozen opaque plans both record the native write/read sequence
                // here. The typed graph resource seeds its retained backing state on the next declaration.
                resourceUses.push_back(ReadWriteUse(blas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // State-only import: a later rejected preparation can re-pend this static BLAS, and its next build
                // must seed the true accepted AccelStructRead state instead of BufferDesc::initialState.
                resourceUses.push_back(ReadUse(blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!resourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-preparation resources"));
        return false;
    }

    bool pureSoftwareMeshSwBvhBuildsGraphOwned = pureSoftwareMeshSwBvhBuildsGraphOwnedCandidate;
    if(pureSoftwareMeshSwBvhBuildsGraphOwned){
        for(const PreparedMeshSwBvhBuild& build : preparedMeshSwBvhBuilds){
            const PreparedMeshSwBvhGraphResources resources{
                .build = build,
                .position = m_deferredLightingTaskGraph.findImportedBuffer(build.positionBuffer),
                .triangleIndex = m_deferredLightingTaskGraph.findImportedBuffer(build.triangleIndexBuffer),
                .node = m_deferredLightingTaskGraph.findImportedBuffer(build.nodeBuffer),
                .parent = m_deferredLightingTaskGraph.findImportedBuffer(build.parentBuffer),
                .sortKeys = m_deferredLightingTaskGraph.findImportedBuffer(build.sortKeysBuffer),
                .sortPayload = m_deferredLightingTaskGraph.findImportedBuffer(build.sortPayloadBuffer),
                .visitCounter = m_deferredLightingTaskGraph.findImportedBuffer(build.visitCounterBuffer),
            };
            if(
                !resources.position.valid()
                || !resources.triangleIndex.valid()
                || !resources.node.valid()
                || !resources.parent.valid()
                || !resources.sortKeys.valid()
                || !resources.sortPayload.valid()
                || !resources.visitCounter.valid()
            ){
                // Keep the established aggregate direct path if a future preflight leaves any frozen operation
                // without an exact graph identity. Never mix a partial typed-clear chain with native sentinels.
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: pure software BVH build is missing graph resources; retaining aggregate compatibility recorder"));
                pureSoftwareMeshSwBvhBuildsGraphOwned = false;
                pureSoftwareMeshSwBvhGraphResources.clear();
                break;
            }
            pureSoftwareMeshSwBvhGraphResources.push_back(resources);
        }
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
                .setQueue(GraphicsComputeQueueRequest())
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
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(dependencies, dependencyCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(resourceSetUses.data(), resourceSetUses.size())
    ;
    m_deferredShadowPrepareTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>(
        desc,
        ECSRenderDetail::ShadowPrepareGraphTask::Payload{
            .renderer = this,
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
                .hardwarePreparationReady = &m_preparedShadowVisibilityReady,
                .timingTicket = &timingTicket,
                .shadowMaterialContextBatchGraphOwned = shadowMaterialContextBatchGraphOwned,
                .sceneBvhBatchGraphOwned = sceneBvhBatchGraphOwned,
                .meshSwBvhBuildsGraphOwned = meshSwBvhBuildsGraphOwned,
                .meshSwBvhInputStatesGraphOwned = meshSwBvhInputStatesGraphOwned,
                .hybridHardwareFallbackUploadsGraphOwned = hybridHardwareFallbackUploadsGraphOwned,
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

