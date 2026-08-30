// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <impl/ecs_render/raytrace/task_graph_post_gbuffer_normalize_task.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/deferred/task_graph_clear_timing.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/deferred/task_graph_gbuffer_task.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/csg/task_graph_clear_timing.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/csg/task_graph_resource_sets.h>

#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererFramePipeline::declareDeferredGraphicsPrefixTasks(
    DeferredFrameTargets& deferredTargets,
    const Core::GpuTaskId shadowPrepareTask,
    const CsgFrameState& csgFrameState,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const bool hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    const ECSRenderDetail::MeshViewGpuData& meshViewState,
    const bool meshViewUploadRequired,
    const Core::GpuGraphResourceId albedo,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId opaqueColor,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId meshView,
    const Core::GpuGraphResourceId materialInstances,
    const Core::GpuGraphResourceId materialTyped,
    const Core::GpuGraphResourceId csgReceiverRanges,
    const Core::GpuGraphResourceId csgCutters,
    const Core::GpuGraphResourceId csgClipContextSlots,
    const Core::GpuGraphResourceId csgIntervalSampleState,
    const Core::GpuGraphResourceId csgCapBackNormal,
    const Core::GpuGraphResourceId csgIntervalDepth,
    const Core::GpuGraphResourceId csgIntervalId,
    const Core::GpuGraphResourceId csgReceiverEventData,
    const Core::GpuGraphResourceId csgReceiverEventCount,
    const Core::GpuGraphResourceId csgReceiverSpanData,
    const Core::GpuGraphResourceId csgReceiverSpanCount,
    const Core::GpuGraphResourceId csgRemovedIntervalDepth,
    const Core::GpuGraphResourceId csgRemovedIntervalCapNormal,
    const Core::GpuGraphResourceId csgRemovedIntervalData,
    const Core::GpuGraphResourceId csgRemovedIntervalCount,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
    const Core::GpuGraphResourceSetId shadowTraceGeometrySet,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    ECSRenderDetail::DeferredClearTimingRecordState& deferredClearTimingState,
    ECSRenderDetail::CsgIntervalClearTimingRecordState& csgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket** const timingTickets,
    const bool* const asyncPrefixTimingSpansOnePacket
){
    using namespace RendererTaskGraphDetail;
    using PrefixTimingSlot = ECSRenderDetail::DeferredGraphicsPrefixTimingSlot;

    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearFirstTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixCsgIntervalClearTask = {};
    m_graphicsPrefixOpaqueComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_graphicsPrefixOpaqueSharedComputeEmulationTasks)
        task = {};
    m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = 0u;
    m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = {};
    m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixCsgReceiverSpanTask = {};
    m_graphicsPrefixCsgIntervalCombineTask = {};
    m_graphicsPrefixCsgIntervalSampleTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;

    const bool shadowTraceGeometryStatesGraphOwned = shadowTraceGeometrySet.valid();
    const DeferredLightingGraphResources deferredLightingResources = m_deferredSystem.lightingGraphResources();

    if(
        !deferredTargets.valid()
        || !shadowPrepareTask.valid()
        || !deferredLightingResources.valid()
        || !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !opaqueColor.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !meshView.valid()
        || !currentBindlessSlots.valid()
        || !materialContextSlots.valid()
        || (hasOpaqueCsgFrameWork && (
            !csgCapBackNormal.valid()
            || !csgIntervalDepth.valid()
            || !csgIntervalId.valid()
            || !csgReceiverEventData.valid()
            || !csgReceiverEventCount.valid()
            || !csgReceiverSpanData.valid()
            || !csgReceiverSpanCount.valid()
            || !csgRemovedIntervalDepth.valid()
            || !csgRemovedIntervalCapNormal.valid()
            || !csgRemovedIntervalData.valid()
            || !csgRemovedIntervalCount.valid()
        ))
        || !timingTickets
        || !asyncPrefixTimingSpansOnePacket
        || !deferredClearTimingState.graphics
        || deferredClearTimingState.timing != &deferredClearTiming
        || !deferredClearTimingState.timingTicket
        || (shadowTraceGeometryResourceCount != 0u && !shadowTraceGeometryResources)
    )
        return false;
    for(usize timingSlot = 0u; timingSlot < static_cast<usize>(PrefixTimingSlot::kCount); ++timingSlot){
        if(!timingTickets[timingSlot])
            return false;
    }
    const auto timingTicketSlot = [timingTickets](const PrefixTimingSlot slot){
        return &timingTickets[static_cast<usize>(slot)];
    };
    const Core::TextureSubresourceSet csgPeelSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgPeelLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverEventLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgReceiverSpanLayerCount
    );
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(
        0u,
        1u,
        0u,
        deferredTargets.csgRemovedIntervalLayerCount
    );
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources(0u, 1u, 0u, 1u);

    ECSRenderDetail::SceneLightGpuData sceneLightData[NWB_SCENE_MAX_LIGHTS] = {};
    ECSRenderDetail::SceneShadingGpuData sceneShadingState;
    u32 sceneLightCount = 0u;
    const RayTracingLightingClassificationInput rayTracingLightingInput = m_raytracingSystem.snapshotLightingClassificationInput();
    RayTracingLightingClassification rayTracingLightingClassification;
    bool sceneLightUploadRequired = false;
    bool sceneShadingUploadRequired = false;
    if(!m_deferredSystem.prepareSceneShadingBufferUploads(
        meshViewAspectRatio,
        rayTracingLightingInput,
        sceneLightData,
        LengthOf(sceneLightData),
        sceneLightCount,
        rayTracingLightingClassification,
        sceneLightUploadRequired,
        sceneShadingState,
        sceneShadingUploadRequired
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare immutable scene-shading upload data"));
        return false;
    }

    Core::GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.cost = Core::GpuTaskCostHint::Medium;
    meshViewSetupScheduling.forceSubmissionBoundary = false;
    meshViewSetupScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc
        .setIdentity(Name("render.graphics_prefix.mesh_view_setup"))
        .setMarkerLabel("Mesh View Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(meshViewSetupScheduling)
        .setDependencies(&shadowPrepareTask, 1u)
    ;
    m_graphicsPrefixMeshViewSetupTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::MeshViewSetupGraphTask>(
        meshViewSetupDesc,
        ECSRenderDetail::MeshViewSetupGraphTask::Payload{
            .graphics = &m_graphics,
            .asyncPrefixTiming = &asyncPrefixTiming,
            .timingTicket = timingTicketSlot(PrefixTimingSlot::MeshViewSetup),
            .asyncPrefixTimingSpansOnePacket = asyncPrefixTimingSpansOnePacket,
            .shadowVisibilityTask = &m_deferredShadowVisibilityTask,
        }
    );
    if(!m_graphicsPrefixMeshViewSetupTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare mesh-view setup task"));
        return false;
    }

    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskId meshViewUploadTask = m_graphicsPrefixMeshViewSetupTask;
    if(meshViewUploadRequired){
        const Core::GpuUploadBlobId meshViewBlob = m_deferredLightingTaskGraph.copyUploadData(
            &meshViewState,
            sizeof(meshViewState),
            alignof(ECSRenderDetail::MeshViewGpuData)
        );
        Core::GpuTaskDesc meshViewUploadDesc;
        meshViewUploadDesc
            .setIdentity(Name("render.graphics_prefix.mesh_view_upload"))
            .setMarkerLabel("Mesh View Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&m_graphicsPrefixMeshViewSetupTask, 1u)
        ;
        meshViewUploadTask = meshViewBlob.valid()
            ? m_deferredLightingTaskGraph.addUploadBufferTask(
                meshViewUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = meshViewBlob,
                    .destination = meshView,
                    // The backing buffer deliberately restores Common when a native packet closes.  Declare that
                    // exact graph-visible boundary here; the G-buffer consumer below owns the Common ->
                    // ConstantBuffer transition.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!meshViewUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned mesh-view upload"));
            return false;
        }
    }

    Core::GpuTaskSchedulingHint meshViewCommitScheduling = immutableUploadScheduling;
    Core::GpuTaskDesc meshViewCommitDesc;
    meshViewCommitDesc
        .setIdentity(Name("render.graphics_prefix.mesh_view_upload_commit"))
        .setMarkerLabel("Mesh View Upload Commit")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(meshViewCommitScheduling)
        .setDependencies(&meshViewUploadTask, 1u)
    ;
    const Core::GpuTaskId meshViewCommitTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::MeshViewUploadCommitGraphTask>(
        meshViewCommitDesc,
        ECSRenderDetail::MeshViewUploadCommitGraphTask::Payload{
            .meshSystem = &m_meshSystem,
            .viewState = meshViewState,
            .uploadRequired = meshViewUploadRequired,
            .ready = &m_graphicsPrefixMeshViewSetupReady,
        }
    );
    if(!meshViewCommitTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare mesh-view upload commit"));
        return false;
    }

    Core::GpuTaskId sceneUploadTask = meshViewCommitTask;
    if(sceneLightUploadRequired){
        const usize sceneLightByteCount = static_cast<usize>(sceneLightCount) * sizeof(sceneLightData[0u]);
        const Core::GpuUploadBlobId sceneLightBlob = m_deferredLightingTaskGraph.copyUploadData(
            sceneLightData,
            sceneLightByteCount,
            alignof(ECSRenderDetail::SceneLightGpuData)
        );
        Core::GpuTaskDesc sceneLightUploadDesc;
        sceneLightUploadDesc
            .setIdentity(Name("render.graphics_prefix.scene_lights_upload"))
            .setMarkerLabel("Scene Lights Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&sceneUploadTask, 1u)
        ;
        sceneUploadTask = sceneLightBlob.valid()
            ? m_deferredLightingTaskGraph.addUploadBufferTask(
                sceneLightUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = sceneLightBlob,
                    .destination = lights,
                    // These shared frame buffers retain Common between native packets.  The first declared reader
                    // owns the transition to ShaderResource.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!sceneUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned scene-light upload"));
            return false;
        }
    }

    if(sceneShadingUploadRequired){
        const Core::GpuUploadBlobId sceneShadingBlob = m_deferredLightingTaskGraph.copyUploadData(
            &sceneShadingState,
            sizeof(sceneShadingState),
            alignof(ECSRenderDetail::SceneShadingGpuData)
        );
        Core::GpuTaskDesc sceneShadingUploadDesc;
        sceneShadingUploadDesc
            .setIdentity(Name("render.graphics_prefix.scene_shading_upload"))
            .setMarkerLabel("Scene Shading Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&sceneUploadTask, 1u)
        ;
        sceneUploadTask = sceneShadingBlob.valid()
            ? m_deferredLightingTaskGraph.addUploadBufferTask(
                sceneShadingUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = sceneShadingBlob,
                    .destination = sceneShading,
                    // See the light upload above: preserve the resource's automatic Common boundary and let the
                    // first declared reader lower its ConstantBuffer transition.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!sceneUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned scene-shading upload"));
            return false;
        }
    }

    Core::GpuTaskSchedulingHint sceneShadingSetupScheduling;
    sceneShadingSetupScheduling.cost = Core::GpuTaskCostHint::Tiny;
    sceneShadingSetupScheduling.forceSubmissionBoundary = false;
    sceneShadingSetupScheduling.allowPacketMerge = true;
    sceneShadingSetupScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc sceneShadingSetupDesc;
    sceneShadingSetupDesc
        .setIdentity(Name("render.graphics_prefix.scene_shading_setup"))
        .setMarkerLabel("Scene Shading Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(sceneShadingSetupScheduling)
        .setDependencies(&sceneUploadTask, 1u)
    ;
    ECSRenderDetail::SceneShadingSetupGraphTask::Payload sceneShadingSetupPayload;
    sceneShadingSetupPayload.deferredSystem = &m_deferredSystem;
    sceneShadingSetupPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::SceneShadingSetup);
    sceneShadingSetupPayload.ready = &m_graphicsPrefixSceneShadingSetupReady;
    NWB_MEMCPY(
        sceneShadingSetupPayload.lightData,
        sizeof(sceneShadingSetupPayload.lightData),
        sceneLightData,
        sizeof(sceneLightData)
    );
    sceneShadingSetupPayload.sceneShadingState = sceneShadingState;
    sceneShadingSetupPayload.lightCount = sceneLightCount;
    sceneShadingSetupPayload.lightUploadRequired = sceneLightUploadRequired;
    sceneShadingSetupPayload.sceneShadingUploadRequired = sceneShadingUploadRequired;
    m_graphicsPrefixSceneShadingSetupTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::SceneShadingSetupGraphTask>(
        sceneShadingSetupDesc,
        Move(sceneShadingSetupPayload)
    );
    if(!m_graphicsPrefixSceneShadingSetupTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare scene-shading setup task"));
        return false;
    }


// Built-in clears retain one declaration per target and record their exact native operation through the graph.
    // Opaque color deliberately remains last: its post-clear timing endpoint stays inside the operation that owns the
    // later Graphics-to-Compute handoff, so FrontierSafe packetization cannot split timing from the final clear.
    Core::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    const auto makeClearDesc = [&clearScheduling](
        const Name identity,
        const AStringView markerLabel,
        const Core::GpuTaskId* const dependency
    ){
        Core::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(identity)
            .setMarkerLabel(markerLabel)
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(clearScheduling)
            .setDependencies(dependency, 1u)
        ;
        return clearDesc;
    };
    const Core::GpuClearTextureTaskRecordHooks deferredClearBeginHooks{
        .context = &deferredClearTimingState,
        .beforeClear = &ECSRenderDetail::BeginDeferredClearTiming,
        .discarded = &ECSRenderDetail::DiscardDeferredClearTiming,
    };
    const Core::GpuClearTextureTaskRecordHooks deferredClearEndHooks{
        .context = &deferredClearTimingState,
        .afterClear = &ECSRenderDetail::EndDeferredClearTiming,
        .discarded = &ECSRenderDetail::DiscardDeferredClearTiming,
    };
    const Core::GpuClearTextureTaskRecordHooks noDeferredClearHooks;
    const auto makeFloatClearDesc = [](
        const Core::GpuGraphResourceId destination,
        const Core::Color& clearValue,
        const Core::GpuClearTextureTaskRecordHooks& recordHooks
    ){
        Core::GpuClearTextureTaskDesc clearDesc;
        clearDesc.destination = destination;
        clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
        clearDesc.floatValue = clearValue;
        clearDesc.recordHooks = recordHooks;
        return clearDesc;
    };
    const auto makeDepthClearDesc = [](const Core::GpuGraphResourceId destination){
        Core::GpuClearTextureTaskDesc clearDesc;
        clearDesc.destination = destination;
        clearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clearDesc.valueType = Core::GpuClearTextureTaskValueType::DepthStencil;
        clearDesc.depthValue = Core::s_DepthClearValue;
        clearDesc.clearDepth = true;
        return clearDesc;
    };
    Core::GpuTaskId deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_albedo"),
            "Deferred Clear Albedo",
            &m_graphicsPrefixSceneShadingSetupTask
        ),
        makeFloatClearDesc(albedo, ECSRenderDetail::s_ClearColor, deferredClearBeginHooks)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred albedo clear"));
        return false;
    }
    m_graphicsPrefixDeferredClearFirstTask = deferredClearTask;
    deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_normal"),
            "Deferred Clear Normal",
            &deferredClearTask
        ),
        makeFloatClearDesc(normal, ECSRenderDetail::s_GBufferNormalClearColor, noDeferredClearHooks)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred normal clear"));
        return false;
    }
    deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_world_position"),
            "Deferred Clear World Position",
            &deferredClearTask
        ),
        makeFloatClearDesc(worldPosition, ECSRenderDetail::s_GBufferWorldPositionClearColor, noDeferredClearHooks)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred world-position clear"));
        return false;
    }
    deferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_depth"),
            "Deferred Clear Depth",
            &deferredClearTask
        ),
        makeDepthClearDesc(depth)
    );
    if(!deferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred depth clear"));
        return false;
    }
    m_graphicsPrefixDeferredClearTask = m_deferredLightingTaskGraph.addClearTextureTask(
        makeClearDesc(
            Name("render.graphics_prefix.deferred_clear_opaque_color"),
            "Deferred Clear Opaque Color",
            &deferredClearTask
        ),
        makeFloatClearDesc(opaqueColor, ECSRenderDetail::s_ClearColor, deferredClearEndHooks)
    );
    if(!m_graphicsPrefixDeferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-clear task"));
        return false;
    }

    // Freeze the opaque packet's draw ordering before recording.  The two copied blobs below are the exact source
    // of the heap-selected instance/material streams consumed by these saved draw items.
    Core::Alloc::ScratchArena materialUploadScratch(RendererArenaScope::s_TaskGraphArena);
    MaterialPassDrawItemPartitions opaqueDrawItems{ materialUploadScratch };
    InstanceGpuDataVector instanceData{ materialUploadScratch };
    CsgFrameGpuData csgFrameData{ materialUploadScratch };
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{ materialUploadScratch };
#endif
    MaterialTypedByteDataVector materialTypedBytes{ materialUploadScratch };
    m_materialSystem.gatherMaterialPassDrawItems(
        deferredTargets.framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        csgFrameState,
        opaqueDrawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes,
        RendererResourceLookupMode::PreparedOnly,
        &meshViewState
    );

    ECSRenderDetail::GbufferGraphTask::Payload gbufferPayload{ m_arena };
    gbufferPayload.graphics = &m_graphics;
    gbufferPayload.materialSystem = &m_materialSystem;
    gbufferPayload.csgSystem = &m_csgSystem;
    gbufferPayload.targets = &deferredTargets;
    gbufferPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
    gbufferPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
    gbufferPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
    gbufferPayload.frameBindings = frameBindings;
    gbufferPayload.csgResources = csgResources;

    const bool hasOpaqueDrawItems = !opaqueDrawItems.empty();
    // G-buffer and the optional opaque CSG follow-up both declare the shared material entry batch whenever their
    // immutable draw stream exists. The selected source-geometry batch is retained and declared separately below.
    gbufferPayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
    Core::GpuTaskId materialDrawUploadTask = m_graphicsPrefixDeferredClearTask;
    if(hasOpaqueDrawItems){
        if(
            !materialInstances.valid()
            || !materialTyped.valid()
            || !frameBindings.frameReady(instanceData.size(), materialTypedBytes.size())
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared opaque material draw buffers were unavailable during graph declaration"));
            return false;
        }
        m_materialSystem.prepareMaterialPassInstanceUploadData(instanceData, csgResources);
#if defined(NWB_DEBUG)
        if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: opaque material instance upload size overflows graph blob capacity"));
            return false;
        }
        NWB_ASSERT(instanceData.size() == materialTypedRanges.size());
        ECSRenderDetail::AssertMaterialTypedUploadRanges(materialTypedRanges, materialTypedBytes);
#endif

        const Core::GpuUploadBlobId instanceBlob = m_deferredLightingTaskGraph.copyUploadData(
            instanceData.data(),
            instanceData.size() * sizeof(InstanceGpuData),
            alignof(InstanceGpuData)
        );
        const Core::GpuUploadBlobId materialTypedBlob = m_deferredLightingTaskGraph.copyUploadData(
            materialTypedBytes.data(),
            materialTypedBytes.size(),
            alignof(u32)
        );
        if(!instanceBlob.valid() || !materialTypedBlob.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable opaque material upload data"));
            return false;
        }

        Core::GpuTaskDesc instanceUploadDesc;
        instanceUploadDesc
            .setIdentity(Name("render.graphics_prefix.material_instances_upload"))
            .setMarkerLabel("Material Instances Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&materialDrawUploadTask, 1u)
        ;
        materialDrawUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            instanceUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = instanceBlob,
                .destination = materialInstances,
                // Both draw buffers use automatic Common restoration when the native packet closes.  Keep that
                // graph-visible boundary exact; the G-buffer read below owns the transient SRV transition.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!materialDrawUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned material instance upload"));
            return false;
        }

        Core::GpuTaskDesc materialTypedUploadDesc;
        materialTypedUploadDesc
            .setIdentity(Name("render.graphics_prefix.material_typed_upload"))
            .setMarkerLabel("Material Typed Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&materialDrawUploadTask, 1u)
        ;
        materialDrawUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            materialTypedUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = materialTypedBlob,
                .destination = materialTyped,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!materialDrawUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned material typed upload"));
            return false;
        }
        gbufferPayload.materialDrawBuffersUploaded = true;
    }

    // Freeze every opaque CSG upload byte after preflight fixed the buffer, descriptor, and target generations.
    // Native G-buffer recording consumes these values without rebuilding either CSG uniform payload from live state.
    const bool hasCsgFrameGpuWork = csgFrameData.hasWork();
    Core::GpuTaskId csgFrameUploadTask = materialDrawUploadTask;
    if(hasCsgFrameGpuWork){
        if(
            !csgReceiverRanges.valid()
            || !csgCutters.valid()
            || !csgClipContextSlots.valid()
            || !csgIntervalSampleState.valid()
            || !csgResources.frameReady(csgFrameData)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared CSG frame buffers were unavailable during graph declaration"));
            return false;
        }
#if defined(NWB_DEBUG)
        if(
            csgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
            || csgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: CSG frame upload size overflows graph blob capacity"));
            return false;
        }
#endif

        CsgClipContextSlots csgClipContextSlotData;
        CsgIntervalSampleStateGpuData csgIntervalSampleStateData;
        if(
            !m_csgSystem.prepareCsgClipContextSlotData(
                deferredTargets,
                csgFrameData,
                csgResources,
                frameBindings,
                csgClipContextSlotData
            )
            || !m_csgSystem.prepareCsgIntervalSampleStateData(
                deferredTargets,
                csgFrameData,
                csgResources,
                frameBindings,
                csgIntervalSampleStateData
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot opaque CSG auxiliary upload data"));
            return false;
        }

        const Core::GpuUploadBlobId receiverRangesBlob = m_deferredLightingTaskGraph.copyUploadData(
            csgFrameData.receiverRanges.data(),
            csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
            alignof(CsgReceiverRangeGpuData)
        );
        const Core::GpuUploadBlobId cuttersBlob = m_deferredLightingTaskGraph.copyUploadData(
            csgFrameData.cutters.data(),
            csgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
            alignof(CsgCutterGpuData)
        );
        const Core::GpuUploadBlobId clipContextSlotsBlob = m_deferredLightingTaskGraph.copyUploadData(
            &csgClipContextSlotData,
            sizeof(csgClipContextSlotData),
            alignof(CsgClipContextSlots)
        );
        const Core::GpuUploadBlobId intervalSampleStateBlob = m_deferredLightingTaskGraph.copyUploadData(
            &csgIntervalSampleStateData,
            sizeof(csgIntervalSampleStateData),
            alignof(CsgIntervalSampleStateGpuData)
        );
        if(
            !receiverRangesBlob.valid()
            || !cuttersBlob.valid()
            || !clipContextSlotsBlob.valid()
            || !intervalSampleStateBlob.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable CSG frame upload data"));
            return false;
        }

        Core::GpuTaskDesc receiverRangesUploadDesc;
        receiverRangesUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_receiver_ranges_upload"))
            .setMarkerLabel("CSG Receiver Ranges Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            receiverRangesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = receiverRangesBlob,
                .destination = csgReceiverRanges,
                // CSG structured buffers restore Common at native packet close; G-buffer owns their transient SRV
                // state exactly like the graph-owned material streams above.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG receiver-range upload"));
            return false;
        }

        Core::GpuTaskDesc cuttersUploadDesc;
        cuttersUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_cutters_upload"))
            .setMarkerLabel("CSG Cutters Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            cuttersUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = cuttersBlob,
                .destination = csgCutters,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG cutter upload"));
            return false;
        }

        Core::GpuTaskDesc clipContextSlotsUploadDesc;
        clipContextSlotsUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_clip_context_slots_upload"))
            .setMarkerLabel("CSG Clip Context Slots Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            clipContextSlotsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = clipContextSlotsBlob,
                .destination = csgClipContextSlots,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG clip-context upload"));
            return false;
        }

        Core::GpuTaskDesc intervalSampleStateUploadDesc;
        intervalSampleStateUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_sample_state_upload"))
            .setMarkerLabel("CSG Interval Sample State Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_deferredLightingTaskGraph.addUploadBufferTask(
            intervalSampleStateUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = intervalSampleStateBlob,
                .destination = csgIntervalSampleState,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned CSG interval-state upload"));
            return false;
        }
        gbufferPayload.csgFrameBuffersUploaded = true;
    }
    gbufferPayload.opaqueDrawSnapshot.capture(
        opaqueDrawItems,
        csgFrameData,
        instanceData.size(),
        materialTypedBytes.size()
    );
    ECSRenderDetail::CsgReceiverSpanBuildGraphTask::Payload csgReceiverSpanPayload{ m_arena };
    csgReceiverSpanPayload.frameBindings = frameBindings;
    if(hasOpaqueCsgFrameWork){
        csgReceiverSpanPayload.materialSystem = &m_materialSystem;
        csgReceiverSpanPayload.csgSystem = &m_csgSystem;
        csgReceiverSpanPayload.targets = &deferredTargets;
        // The graph owns the receiver-surface producer fence. Normal prefix compilation merges this callback with
        // G-buffer, while a FrontierSafe boundary retains its own submission-local timing ticket.
        csgReceiverSpanPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::CsgReceiverSpanBuild);
        csgReceiverSpanPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        csgReceiverSpanPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        csgReceiverSpanPayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        csgReceiverSpanPayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        csgReceiverSpanPayload.csgResources = csgResources;
        csgReceiverSpanPayload.receiverSpanInputImageStatesGraphOwned = true;
        csgReceiverSpanPayload.receiverSpanOutputImageStatesGraphOwned = true;
        csgReceiverSpanPayload.opaqueDrawSnapshot.capture(
            opaqueDrawItems,
            csgFrameData,
            instanceData.size(),
            materialTypedBytes.size()
        );
    }
    ECSRenderDetail::CsgIntervalCombineGraphTask::Payload csgIntervalCombinePayload{ m_arena };
    csgIntervalCombinePayload.frameBindings = frameBindings;
    if(hasOpaqueCsgFrameWork){
        csgIntervalCombinePayload.materialSystem = &m_materialSystem;
        csgIntervalCombinePayload.csgSystem = &m_csgSystem;
        csgIntervalCombinePayload.targets = &deferredTargets;
        // The graph owns the preceding producer fence. Normal prefix compilation merges this callback with
        // G-buffer, while a FrontierSafe boundary retains its own submission-local timing ticket.
        csgIntervalCombinePayload.timingTicket = timingTicketSlot(PrefixTimingSlot::CsgIntervalCombine);
        csgIntervalCombinePayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        csgIntervalCombinePayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        csgIntervalCombinePayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        csgIntervalCombinePayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        csgIntervalCombinePayload.csgResources = csgResources;
        csgIntervalCombinePayload.intervalCombineInputImageStatesGraphOwned = true;
        csgIntervalCombinePayload.removedIntervalOutputImageStatesGraphOwned = true;
        csgIntervalCombinePayload.opaqueDrawSnapshot.capture(
            opaqueDrawItems,
            csgFrameData,
            instanceData.size(),
            materialTypedBytes.size()
        );
    }
    ECSRenderDetail::CsgIntervalSampleGraphTask::Payload csgIntervalSamplePayload{ m_arena };
    csgIntervalSamplePayload.frameBindings = frameBindings;
    csgIntervalSamplePayload.csgResources = csgResources;
    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask::Payload
        opaqueCsgIntervalSampleComputeEmulationPayload{ m_arena };
    opaqueCsgIntervalSampleComputeEmulationPayload.frameBindings = frameBindings;
    if(hasOpaqueCsgFrameWork){
        csgIntervalSamplePayload.graphics = &m_graphics;
        csgIntervalSamplePayload.materialSystem = &m_materialSystem;
        csgIntervalSamplePayload.csgSystem = &m_csgSystem;
        csgIntervalSamplePayload.targets = &deferredTargets;
        csgIntervalSamplePayload.timingTicket = timingTicketSlot(PrefixTimingSlot::CsgIntervalSample);
        csgIntervalSamplePayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        csgIntervalSamplePayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        csgIntervalSamplePayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        csgIntervalSamplePayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        csgIntervalSamplePayload.intervalSampleImageStatesGraphOwned = true;
        csgIntervalSamplePayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
        // The interval-sample task is scheduled for semantic CSG work, but only the gathered GPU work
        // declares these heap-selected clip buffers. Retain the native bridge for an empty gathered frame.
        csgIntervalSamplePayload.csgClipBufferStatesGraphOwned = hasCsgFrameGpuWork;
        csgIntervalSamplePayload.opaqueDrawSnapshot.capture(
            opaqueDrawItems,
            csgFrameData,
            instanceData.size(),
            materialTypedBytes.size()
        );
    }


// The G-buffer produces peel/event images, span build consumes its event aliases and publishes spans, Combine
    // consumes the five prior-stage aliases and publishes removed-interval images, then Sample consumes those
    // outputs. Their native thunks consume graph-owned StorageImage state without staging target transitions.
    gbufferPayload.csgIntervalPeelTargetStatesGraphOwned = hasOpaqueCsgFrameWork;
    gbufferPayload.csgReceiverSurfaceImageStatesGraphOwned = hasOpaqueCsgFrameWork;
    // Match the actual graph declarations above; semantic CSG work may have no gathered GPU frame data.
    gbufferPayload.csgClipBufferStatesGraphOwned = hasCsgFrameGpuWork;

    // The clear is intentionally keyed to the semantic opaque-CSG frame flag, rather than the later native
    // readiness checks. This preserves the old defensive clear timing while making its two actual CopyDest writes
    // and the following UAV handoff visible to the graph.
    Core::GpuTaskId csgIntervalClearTask = csgFrameUploadTask;
    if(hasOpaqueCsgFrameWork){
        Core::GpuTaskSchedulingHint csgIntervalClearScheduling;
        csgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        csgIntervalClearScheduling.forceSubmissionBoundary = false;
        csgIntervalClearScheduling.allowPacketMerge = true;
        csgIntervalClearScheduling.mergeWithPrevious = true;
        const auto makeCsgIntervalClearTaskDesc = [&csgIntervalClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(csgIntervalClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const Core::Rect csgClearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height);
        const Core::GpuClearTextureTaskRecordHooks csgIntervalClearBeginHooks{
            .context = &csgIntervalClearTimingState,
            .beforeClear = &ECSRenderDetail::BeginCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        const Core::GpuClearTextureTaskRecordHooks csgIntervalClearEndHooks{
            .context = &csgIntervalClearTimingState,
            .afterClear = &ECSRenderDetail::EndCsgIntervalClearTiming,
            .discarded = &ECSRenderDetail::DiscardCsgIntervalClearTiming,
        };
        m_graphicsPrefixCsgIntervalClearFirstTask = m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
            makeCsgIntervalClearTaskDesc(
                Name("render.graphics_prefix.csg_interval_clear"),
                "CSG Interval Id Clear",
                csgFrameUploadTask
            ),
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = csgIntervalId,
                .subresources = csgPeelSubresources,
                .rect = csgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = csgIntervalClearBeginHooks,
            }
        );
        if(!m_graphicsPrefixCsgIntervalClearFirstTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval-id clear"));
            return false;
        }
        Core::GpuTaskSchedulingHint csgIntervalClearTailScheduling = csgIntervalClearScheduling;
        // The timing endpoint must remain in the first clear's Graphics packet even when another queue observes the
        // interval id. The explicit immediate dependency satisfies FrontierSafe's consumer-frontier override.
        csgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc csgIntervalClearTailDesc;
        csgIntervalClearTailDesc
            .setIdentity(Name("render.graphics_prefix.csg_receiver_event_count_clear"))
            .setMarkerLabel("CSG Receiver Event Count Clear")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(csgIntervalClearTailScheduling)
            .setDependencies(&m_graphicsPrefixCsgIntervalClearFirstTask, 1u)
        ;
        m_graphicsPrefixCsgIntervalClearTask = m_deferredLightingTaskGraph.addClearTextureRectUIntTask(
            csgIntervalClearTailDesc,
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = csgReceiverEventCount,
                .subresources = csgReceiverEventCountSubresources,
                .rect = csgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = csgIntervalClearEndHooks,
            }
        );
        if(!m_graphicsPrefixCsgIntervalClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver-event clear"));
            return false;
        }
        csgIntervalClearTask = m_graphicsPrefixCsgIntervalClearTask;
    }

    Core::Alloc::ScratchArena gbufferResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> gbufferResourceUses{ gbufferResourceScratch };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueComputeEmulationResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueSharedComputeEmulationGenerateResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueSharedComputeEmulationRasterResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueCsgReceiverComputeEmulationResourceUses{
        gbufferResourceScratch
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgReceiverSpanResourceUses{ gbufferResourceScratch };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalCombineResourceUses{ gbufferResourceScratch };
    Core::GpuGraphResourceSetId gbufferMaterialGeometrySet;
    Core::GpuGraphResourceSetId gbufferMaterialSampledTextureSet;
    Core::GpuGraphResourceSetId opaqueComputeEmulationOutputSet;
    Core::GpuGraphResourceId opaqueSharedComputeEmulationOutput;
    Core::GpuGraphResourceSetId opaqueCsgReceiverComputeEmulationOutputSet;
    Core::GpuGraphResourceSetId opaqueCsgIntervalSampleComputeEmulationOutputSet;
    const MaterialPassDrawItems* const gbufferMaterialGeometryDrawSets[] = {
        &opaqueDrawItems.regular,
        &opaqueDrawItems.csgReceiverSurface,
    };
    const bool gbufferUsesMaterialGeometry =
        !opaqueDrawItems.regular.empty()
        || !opaqueDrawItems.csgReceiverSurface.empty()
    ;
    gbufferPayload.materialGeometryStatesGraphOwned = gbufferUsesMaterialGeometry
        && GatherPreparedMaterialGeometryResourceSet(
            m_deferredLightingTaskGraph,
            gbufferMaterialGeometryDrawSets,
            LengthOf(gbufferMaterialGeometryDrawSets),
            gbufferResourceScratch,
            Name("render.graphics_prefix.gbuffer.material_geometry"),
            "Opaque Material Geometry",
            gbufferMaterialGeometrySet
        )
    ;
    if(gbufferUsesMaterialGeometry && !gbufferPayload.materialGeometryStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material geometry states"));
        // A prepared material callback selects these mesh buffers through frozen heap slots.  Do not let the
        // graph record it with an undeclared dynamic resource set; the caller will retain the direct compatibility
        // path for this frame instead.
        return false;
    }
    const bool gbufferMaterialSampledTexturesCollected = gbufferUsesMaterialGeometry
        && GatherPreparedMaterialSampledTextureResourceSet(
            m_materialSystem,
            m_deferredLightingTaskGraph,
            gbufferMaterialGeometryDrawSets,
            LengthOf(gbufferMaterialGeometryDrawSets),
            gbufferResourceScratch,
            Name("render.graphics_prefix.gbuffer.material_sampled_textures"),
            "Opaque Material Sampled Textures",
            gbufferMaterialSampledTextureSet
        )
    ;
    if(gbufferUsesMaterialGeometry && !gbufferMaterialSampledTexturesCollected){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material sampled textures"));
        return false;
    }


// A generated-vertex buffer is persistent per mesh, so pulling every compute dispatch ahead of raster would be
    // wrong when multiple frozen draw items select the same output. The plan deliberately enables only the fully
    // alias-free regular opaque case; all other streams keep their established local interleaved handoff.
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask::Payload opaqueComputeEmulationPayload{ m_arena };
    opaqueComputeEmulationPayload.frameBindings = frameBindings;
    const bool opaqueComputeEmulationPlanCaptured = gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueComputeEmulationPayload.plan.capture(opaqueDrawItems.regular)
    ;
    const bool opaqueComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationPlanCaptured
        && GatherOpaqueRegularComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            opaqueComputeEmulationPayload.plan,
            gbufferResourceScratch,
            Name("render.graphics_prefix.opaque_compute_emulation.outputs"),
            "Opaque Compute Emulation Outputs",
            opaqueComputeEmulationOutputSet
        )
    ;
    if(opaqueComputeEmulationPlanCaptured && !opaqueComputeEmulationOutputStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned opaque compute-emulation output states"
        ));
    }
    gbufferPayload.regularComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationOutputStatesGraphOwned;

    // A persistent generated-vertex buffer is normally a local dispatch/raster bridge. Handle the small alias
    // classes explicitly: two through five regular opaque compute items sharing one frozen output and heap slot.
    // Opaque CSG remains out of scope so no CSG producer/raster phase can observe this alternation.
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan opaqueSharedComputeEmulationPlan;
    const bool opaqueSharedComputeEmulationPlanCaptured =
        !hasOpaqueCsgFrameWork
        && !opaqueComputeEmulationOutputStatesGraphOwned
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueSharedComputeEmulationPlan.capture(
            opaqueDrawItems.regular,
            ECSRenderDetail::s_SharedComputeEmulationMaximumDrawCount
        )
    ;
    const bool opaqueSharedComputeEmulationOutputStatesGraphOwned =
        opaqueSharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_deferredLightingTaskGraph,
            opaqueSharedComputeEmulationPlan,
            "Opaque Shared Compute Emulation Output",
            opaqueSharedComputeEmulationOutput
        )
    ;
    if(
        opaqueSharedComputeEmulationPlanCaptured
        && !opaqueSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned shared opaque compute-emulation output state"
        ));
    }
    gbufferPayload.regularSharedComputeEmulationDrawsGraphOwned =
        opaqueSharedComputeEmulationOutputStatesGraphOwned;
    gbufferPayload.regularSharedComputeEmulationTiming =
        opaqueSharedComputeEmulationOutputStatesGraphOwned
            ? &opaqueRegularSharedComputeEmulationTiming
            : nullptr
    ;

    ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask::Payload opaqueCsgReceiverComputeEmulationPayload{
        m_arena
    };
    opaqueCsgReceiverComputeEmulationPayload.frameBindings = frameBindings;
    const bool opaqueCsgReceiverComputeEmulationPlanCaptured = hasOpaqueCsgFrameWork
        && hasCsgFrameGpuWork
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueCsgReceiverComputeEmulationPayload.plan.capture(
            opaqueDrawItems.csgReceiverSurface,
            opaqueDrawItems.regular,
            csgFrameData
        )
    ;
    const bool opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned =
        opaqueCsgReceiverComputeEmulationPlanCaptured
        && GatherOpaqueCsgReceiverComputeEmulationResourceSet(
            m_deferredLightingTaskGraph,
            opaqueCsgReceiverComputeEmulationPayload.plan,
            gbufferResourceScratch,
            Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation.outputs"),
            "Opaque CSG Receiver Compute Emulation Outputs",
            opaqueCsgReceiverComputeEmulationOutputSet
        )
    ;
    if(
        opaqueCsgReceiverComputeEmulationPlanCaptured
        && !opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned opaque CSG receiver compute-emulation output states"
        ));
    }
    gbufferPayload.csgReceiverComputeEmulationOutputStatesGraphOwned =
        opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned;

    gbufferResourceUses.reserve(
        (hasOpaqueDrawItems ? 7u : 5u)
        + (hasCsgFrameGpuWork ? 5u : 0u)
        + (hasOpaqueCsgFrameWork ? 5u : 0u)
    );
    gbufferResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    if(hasOpaqueDrawItems){
        gbufferResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        gbufferResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
    }
    if(hasCsgFrameGpuWork){
        gbufferResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
        gbufferResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
        gbufferResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        gbufferResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // CSG resolves target-specific images through the deferred bindless-slot buffer selected in its context.
        gbufferResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    }
    if(hasOpaqueCsgFrameWork){
        csgReceiverSpanResourceUses.reserve(4u + (hasCsgFrameGpuWork ? 2u : 0u));
        csgIntervalCombineResourceUses.reserve(9u + (hasCsgFrameGpuWork ? 2u : 0u));
        // Peel payloads and receiver-event entries never load their prior values. Their paired interval ID and
        // event count preserve the sparse-write validity contract after the preceding explicit clears, so only those
        // two resources require ReadWrite access. Keeping the payloads write-only also permits their first graph
        // use to begin at a fresh texture's native Unknown state.
        gbufferResourceUses.push_back(
            WriteTextureUse(csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        gbufferResourceUses.push_back(
            WriteTextureUse(csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        gbufferResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        gbufferResourceUses.push_back(WriteTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        gbufferResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgReceiverSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgReceiverSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        if(hasCsgFrameGpuWork){
            csgReceiverSpanResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgReceiverSpanResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgReceiverSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgReceiverSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgCapBackNormal,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalDepth,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalId,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        if(hasCsgFrameGpuWork){
            csgIntervalCombineResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgIntervalCombineResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    gbufferResourceUses.push_back(WriteUse(albedo, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(normal, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(worldPosition, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(depth, Core::ResourceStates::DepthWrite));
    const Core::GpuTaskResourceSetUse gbufferMaterialGeometrySetUse{
        .resourceSet = gbufferMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse gbufferMaterialSampledTextureSetUse{
        .resourceSet = gbufferMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse gbufferMaterialResourceSetUses[4u] = {};
    usize gbufferMaterialResourceSetUseCount = 0u;
    if(gbufferPayload.materialGeometryStatesGraphOwned)
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialGeometrySetUse;
    if(gbufferMaterialSampledTextureSet.valid())
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialSampledTextureSetUse;
    if(opaqueComputeEmulationOutputStatesGraphOwned){
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] =
            opaqueComputeEmulationOutputVertexBufferSetUse;
    }
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] =
            opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse;
    }

    Core::GpuTaskId gbufferDependency = csgIntervalClearTask;
    if(opaqueComputeEmulationOutputStatesGraphOwned){
        opaqueComputeEmulationPayload.materialSystem = &m_materialSystem;
        opaqueComputeEmulationPayload.targets = &deferredTargets;
        // G-buffer's semantic timing ticket spans its preparatory compute half and its raster half in one packet.
        opaqueComputeEmulationPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
        opaqueComputeEmulationPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        opaqueComputeEmulationPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        opaqueComputeEmulationPayload.instanceCount = instanceData.size();
        opaqueComputeEmulationPayload.materialTypedByteCount = materialTypedBytes.size();
        opaqueComputeEmulationPayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        opaqueComputeEmulationPayload.materialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned;
        opaqueComputeEmulationPayload.materialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned;

        opaqueComputeEmulationResourceUses.reserve(3u);
        opaqueComputeEmulationResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        opaqueComputeEmulationResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
        opaqueComputeEmulationResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        Core::GpuTaskResourceSetUse opaqueComputeEmulationResourceSetUses[3u] = {};
        usize opaqueComputeEmulationResourceSetUseCount = 0u;
        opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = gbufferMaterialGeometrySetUse;
        if(gbufferMaterialSampledTextureSet.valid()){
            opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] =
                gbufferMaterialSampledTextureSetUse;
        }
        opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] =
            opaqueComputeEmulationOutputUavSetUse;
        Core::GpuTaskSchedulingHint opaqueComputeEmulationScheduling;
        opaqueComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueComputeEmulationScheduling.allowPacketMerge = true;
        opaqueComputeEmulationScheduling.mergeWithPrevious = true;
        // This explicit immediate successor must remain with the preceding Graphics prefix work: CSG clear timing
        // and the G-buffer semantic range both require one accepting command list under FrontierSafe packetization.
        opaqueComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc opaqueComputeEmulationDesc;
        opaqueComputeEmulationDesc
            .setIdentity(Name("render.graphics_prefix.opaque_compute_emulation"))
            .setMarkerLabel("Opaque Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(opaqueComputeEmulationScheduling)
            .setDependencies(&gbufferDependency, 1u)
            .setResourceUses(
                opaqueComputeEmulationResourceUses.data(),
                opaqueComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                opaqueComputeEmulationResourceSetUses,
                opaqueComputeEmulationResourceSetUseCount
            )
        ;
        m_graphicsPrefixOpaqueComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask
        >(
            opaqueComputeEmulationDesc,
            Move(opaqueComputeEmulationPayload)
        );
        if(!m_graphicsPrefixOpaqueComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque compute-emulation producer"));
            return false;
        }
        gbufferDependency = m_graphicsPrefixOpaqueComputeEmulationTask;
    }
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        opaqueCsgReceiverComputeEmulationPayload.materialSystem = &m_materialSystem;
        opaqueCsgReceiverComputeEmulationPayload.csgResources = csgResources;
        opaqueCsgReceiverComputeEmulationPayload.targets = &deferredTargets;
        // The receiver producer is an early part of G-buffer's one semantic Graphics submission; its dispatch
        // timing shares the existing ticket while the receiver raster retains its own scope in G-buffer.
        opaqueCsgReceiverComputeEmulationPayload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
        opaqueCsgReceiverComputeEmulationPayload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
        opaqueCsgReceiverComputeEmulationPayload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
        opaqueCsgReceiverComputeEmulationPayload.instanceCount = instanceData.size();
        opaqueCsgReceiverComputeEmulationPayload.materialTypedByteCount = materialTypedBytes.size();
        opaqueCsgReceiverComputeEmulationPayload.materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
        opaqueCsgReceiverComputeEmulationPayload.csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded;
        opaqueCsgReceiverComputeEmulationPayload.materialFrameStatesGraphOwned =
            gbufferPayload.materialFrameStatesGraphOwned;
        opaqueCsgReceiverComputeEmulationPayload.materialGeometryStatesGraphOwned =
            gbufferPayload.materialGeometryStatesGraphOwned;

        opaqueCsgReceiverComputeEmulationResourceUses.reserve(8u);
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        // The compute pipeline shares CSG's descriptor-visible clip context. It does not write receiver-event
        // images; those remain G-buffer raster outputs and are intentionally absent from this producer's uses.
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
        );
        opaqueCsgReceiverComputeEmulationResourceUses.push_back(
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationResourceSetUses[3u] = {};
        usize opaqueCsgReceiverComputeEmulationResourceSetUseCount = 0u;
        opaqueCsgReceiverComputeEmulationResourceSetUses[
            opaqueCsgReceiverComputeEmulationResourceSetUseCount++
        ] = gbufferMaterialGeometrySetUse;
        if(gbufferMaterialSampledTextureSet.valid()){
            opaqueCsgReceiverComputeEmulationResourceSetUses[
                opaqueCsgReceiverComputeEmulationResourceSetUseCount++
            ] = gbufferMaterialSampledTextureSetUse;
        }
        opaqueCsgReceiverComputeEmulationResourceSetUses[
            opaqueCsgReceiverComputeEmulationResourceSetUseCount++
        ] = opaqueCsgReceiverComputeEmulationOutputUavSetUse;
        Core::GpuTaskSchedulingHint opaqueCsgReceiverComputeEmulationScheduling;
        opaqueCsgReceiverComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueCsgReceiverComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueCsgReceiverComputeEmulationScheduling.allowPacketMerge = true;
        opaqueCsgReceiverComputeEmulationScheduling.mergeWithPrevious = true;
        // This immediate successor stays with G-buffer under FrontierSafe so its compiler-owned output boundary,
        // existing prefix timing ticket, and accepted semantic range all remain one primary Graphics packet.
        opaqueCsgReceiverComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc opaqueCsgReceiverComputeEmulationDesc;
        opaqueCsgReceiverComputeEmulationDesc
            .setIdentity(Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation"))
            .setMarkerLabel("Opaque CSG Receiver Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(opaqueCsgReceiverComputeEmulationScheduling)
            .setDependencies(&gbufferDependency, 1u)
            .setResourceUses(
                opaqueCsgReceiverComputeEmulationResourceUses.data(),
                opaqueCsgReceiverComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                opaqueCsgReceiverComputeEmulationResourceSetUses,
                opaqueCsgReceiverComputeEmulationResourceSetUseCount
            )
        ;
        m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask
        >(
            opaqueCsgReceiverComputeEmulationDesc,
            Move(opaqueCsgReceiverComputeEmulationPayload)
        );
        if(!m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver compute-emulation producer"));
            return false;
        }
        gbufferDependency = m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask;
    }
    Core::GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.cost = Core::GpuTaskCostHint::Medium;
    gbufferScheduling.forceSubmissionBoundary = false;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;


// The producer is an explicit immediate predecessor. Preserve its compiler-owned UAV-to-VertexBuffer handoff
    // inside the existing primary-Graphics packet even when FrontierSafe sees an earlier cross-queue consumer.
    gbufferScheduling.allowMergeAcrossConsumerFrontier =
        opaqueComputeEmulationOutputStatesGraphOwned
        || opaqueSharedComputeEmulationOutputStatesGraphOwned
        || opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned
    ;
    Core::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("render.graphics_prefix.gbuffer"))
        .setMarkerLabel("Opaque G-Buffer")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(gbufferScheduling)
        .setDependencies(&gbufferDependency, 1u)
        .setResourceUses(gbufferResourceUses.data(), gbufferResourceUses.size())
        .setResourceSetUses(
            gbufferMaterialResourceSetUseCount != 0u ? gbufferMaterialResourceSetUses : nullptr,
            gbufferMaterialResourceSetUseCount
        )
    ;
    // The shared-output successors are declared after G-buffer has moved its payload into the graph. Keep
    // their frozen readiness/state bits by value instead of reading a moved-from payload below.
    const bool opaqueSharedMaterialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
    const bool opaqueSharedMaterialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned;
    const bool opaqueSharedMaterialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned;
    m_graphicsPrefixGbufferTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::GbufferGraphTask>(
        gbufferDesc,
        Move(gbufferPayload)
    );
    if(!m_graphicsPrefixGbufferTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque G-buffer task"));
        return false;
    }

    Core::GpuTaskId gbufferCompletionTask = m_graphicsPrefixGbufferTask;
    if(opaqueSharedComputeEmulationOutputStatesGraphOwned){
        // These two through five draw items target exactly one imported buffer. Keep every state phase explicit
        // so the compiler, not the material callback, owns Common -> UAV -> VertexBuffer -> ... -> VertexBuffer.
        // The immediate chain is also the semantic ordering contract for the retained persistent output alias.
        opaqueSharedComputeEmulationGenerateResourceUses.reserve(4u);
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
        );
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationGenerateResourceUses.push_back(
            WriteUse(opaqueSharedComputeEmulationOutput, Core::ResourceStates::UnorderedAccess)
        );

        opaqueSharedComputeEmulationRasterResourceUses.reserve(8u);
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            ReadUse(opaqueSharedComputeEmulationOutput, Core::ResourceStates::VertexBuffer)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(albedo, Core::ResourceStates::RenderTarget)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(normal, Core::ResourceStates::RenderTarget)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(worldPosition, Core::ResourceStates::RenderTarget)
        );
        opaqueSharedComputeEmulationRasterResourceUses.push_back(
            WriteUse(depth, Core::ResourceStates::DepthWrite)
        );

        Core::GpuTaskResourceSetUse opaqueSharedComputeEmulationResourceSetUses[2u] = {};
        usize opaqueSharedComputeEmulationResourceSetUseCount = 0u;
        opaqueSharedComputeEmulationResourceSetUses[opaqueSharedComputeEmulationResourceSetUseCount++] =
            gbufferMaterialGeometrySetUse;
        if(gbufferMaterialSampledTextureSet.valid()){
            opaqueSharedComputeEmulationResourceSetUses[opaqueSharedComputeEmulationResourceSetUseCount++] =
                gbufferMaterialSampledTextureSetUse;
        }
        Core::GpuTaskSchedulingHint opaqueSharedComputeEmulationScheduling;
        opaqueSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueSharedComputeEmulationScheduling.allowPacketMerge = true;
        opaqueSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Each phase has an explicit immediate predecessor. Allow that serial chain to remain in the existing
        // primary-Graphics packet when FrontierSafe detects the normal downstream Compute consumer frontier.
        opaqueSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addOpaqueSharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &opaqueSharedComputeEmulationPlan,
            &opaqueRegularSharedComputeEmulationTiming,
            &instanceData,
            &materialTypedBytes,
            &frameBindings,
            opaqueSharedMaterialDrawBuffersUploaded,
            opaqueSharedMaterialFrameStatesGraphOwned,
            opaqueSharedMaterialGeometryStatesGraphOwned,
            &opaqueSharedComputeEmulationScheduling,
            timingTicketSlot
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(opaqueSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Payload payload;
            payload.frameBindings = frameBindings;
            payload.materialSystem = &m_materialSystem;
            payload.targets = &deferredTargets;
            payload.timingTicket = timingTicketSlot(PrefixTimingSlot::Gbuffer);
            payload.meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady;
            payload.sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady;
            payload.opaqueRegularTiming = &opaqueRegularSharedComputeEmulationTiming;
            payload.plan = opaqueSharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = instanceData.size();
            payload.materialTypedByteCount = materialTypedBytes.size();
            payload.materialDrawBuffersUploaded = opaqueSharedMaterialDrawBuffersUploaded;
            payload.materialFrameStatesGraphOwned = opaqueSharedMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = opaqueSharedMaterialGeometryStatesGraphOwned;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_deferredLightingTaskGraph.addTask<
                ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask
            >(desc, Move(payload));
        };
        using OpaqueSharedPhase = ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Phase;
        const Name opaqueSharedComputeEmulationPhaseIdentities[] = {
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_a"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_a"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_b"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_b"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_c"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_c"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_d"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_d"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_generate_e"),
            Name("render.graphics_prefix.opaque_shared_compute_emulation_raster_e"),
        };
        const AStringView opaqueSharedComputeEmulationPhaseMarkers[] = {
            "Opaque Shared Compute Emulation Generate A",
            "Opaque Shared Compute Emulation Raster A",
            "Opaque Shared Compute Emulation Generate B",
            "Opaque Shared Compute Emulation Raster B",
            "Opaque Shared Compute Emulation Generate C",
            "Opaque Shared Compute Emulation Raster C",
            "Opaque Shared Compute Emulation Generate D",
            "Opaque Shared Compute Emulation Raster D",
            "Opaque Shared Compute Emulation Generate E",
            "Opaque Shared Compute Emulation Raster E",
        };
        const usize opaqueSharedComputeEmulationPhaseCount =
            ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(opaqueSharedComputeEmulationPlan.drawCount);
        if(!ECSRenderDetail::IsSupportedSharedComputeEmulationPhaseCount(opaqueSharedComputeEmulationPhaseCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid shared opaque compute-emulation phase count"));
            return false;
        }
        Core::GpuTaskId opaqueSharedComputeEmulationDependency = m_graphicsPrefixGbufferTask;
        for(usize phaseIndex = 0u;
            phaseIndex < opaqueSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase =
                phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex] =
                addOpaqueSharedComputeEmulationPhase(
                    opaqueSharedComputeEmulationPhaseIdentities[phaseIndex],
                    opaqueSharedComputeEmulationPhaseMarkers[phaseIndex],
                    opaqueSharedComputeEmulationDependency,
                    isRasterPhase ? OpaqueSharedPhase::Raster : OpaqueSharedPhase::Generate,
                    phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
                    phaseIndex + 1u == opaqueSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? opaqueSharedComputeEmulationRasterResourceUses
                        : opaqueSharedComputeEmulationGenerateResourceUses,
                    opaqueSharedComputeEmulationResourceSetUses,
                    opaqueSharedComputeEmulationResourceSetUseCount
                )
            ;
            if(!m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared opaque compute-emulation phase"));
                return false;
            }
            opaqueSharedComputeEmulationDependency = m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex];
        }
        m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = opaqueSharedComputeEmulationPhaseCount;
        gbufferCompletionTask = opaqueSharedComputeEmulationDependency;
    }
    if(hasOpaqueCsgFrameWork){
        Core::GpuTaskSchedulingHint csgReceiverSpanScheduling;
        csgReceiverSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgReceiverSpanScheduling.forceSubmissionBoundary = false;
        csgReceiverSpanScheduling.allowPacketMerge = true;
        csgReceiverSpanScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgReceiverSpanDesc;
        csgReceiverSpanDesc
            .setIdentity(Name("render.graphics_prefix.csg_receiver_span"))
            .setMarkerLabel("Opaque CSG Receiver Span")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(csgReceiverSpanScheduling)
            .setDependencies(&gbufferCompletionTask, 1u)
            .setResourceUses(csgReceiverSpanResourceUses.data(), csgReceiverSpanResourceUses.size())
        ;
        m_graphicsPrefixCsgReceiverSpanTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::CsgReceiverSpanBuildGraphTask
        >(
            csgReceiverSpanDesc,
            Move(csgReceiverSpanPayload)
        );
        if(!m_graphicsPrefixCsgReceiverSpanTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver-span task"));
            return false;
        }

        Core::GpuTaskSchedulingHint csgIntervalCombineScheduling;
        csgIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgIntervalCombineScheduling.forceSubmissionBoundary = false;
        csgIntervalCombineScheduling.allowPacketMerge = true;
        csgIntervalCombineScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgIntervalCombineDesc;
        csgIntervalCombineDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_combine"))
            .setMarkerLabel("Opaque CSG Interval Combine")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(csgIntervalCombineScheduling)
            .setDependencies(&m_graphicsPrefixCsgReceiverSpanTask, 1u)
            .setResourceUses(csgIntervalCombineResourceUses.data(), csgIntervalCombineResourceUses.size())
        ;
        m_graphicsPrefixCsgIntervalCombineTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::CsgIntervalCombineGraphTask
        >(
            csgIntervalCombineDesc,
            Move(csgIntervalCombinePayload)
        );
        if(!m_graphicsPrefixCsgIntervalCombineTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-combine task"));
            return false;
        }

        Core::Alloc::ScratchArena csgIntervalSampleResourceScratch(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalSampleResourceUses{
            csgIntervalSampleResourceScratch
        };
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>
            opaqueCsgIntervalSampleComputeEmulationResourceUses{ csgIntervalSampleResourceScratch };
        Core::GpuGraphResourceSetId csgIntervalSampleMaterialGeometrySet;
        Core::GpuGraphResourceSetId csgIntervalSampleMaterialSampledTextureSet;
        const MaterialPassDrawItems* const csgIntervalSampleMaterialGeometryDrawSets[] = { &opaqueDrawItems.csg };
        const bool csgIntervalSampleUsesMaterialGeometry = !opaqueDrawItems.csg.empty();
        csgIntervalSamplePayload.materialGeometryStatesGraphOwned = csgIntervalSampleUsesMaterialGeometry
            && GatherPreparedMaterialGeometryResourceSet(
                m_deferredLightingTaskGraph,
                csgIntervalSampleMaterialGeometryDrawSets,
                LengthOf(csgIntervalSampleMaterialGeometryDrawSets),
                csgIntervalSampleResourceScratch,
                Name("render.graphics_prefix.csg_interval_sample.material_geometry"),
                "Opaque CSG Material Geometry",
                csgIntervalSampleMaterialGeometrySet
            )
        ;
        if(csgIntervalSampleUsesMaterialGeometry && !csgIntervalSamplePayload.materialGeometryStatesGraphOwned){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material geometry states"));
            return false;
        }
        const bool csgIntervalSampleMaterialSampledTexturesCollected = csgIntervalSampleUsesMaterialGeometry
            && GatherPreparedMaterialSampledTextureResourceSet(
                m_materialSystem,
                m_deferredLightingTaskGraph,
                csgIntervalSampleMaterialGeometryDrawSets,
                LengthOf(csgIntervalSampleMaterialGeometryDrawSets),
                csgIntervalSampleResourceScratch,
                Name("render.graphics_prefix.csg_interval_sample.material_sampled_textures"),
                "Opaque CSG Material Sampled Textures",
                csgIntervalSampleMaterialSampledTextureSet
            )
        ;
        if(csgIntervalSampleUsesMaterialGeometry && !csgIntervalSampleMaterialSampledTexturesCollected){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material sampled textures"));
            return false;
        }

        // The CSG interval-sample material stream has a different placement from both regular opaque and receiver
        // CSG work: Combine has already completed, and the following Sample callback rasterizes immediately. That
        // makes pairwise-distinct outputs graph-ownable without excluding earlier regular/receiver aliases.
        const bool opaqueCsgIntervalSampleComputeEmulationPlanCaptured = hasCsgFrameGpuWork
            && csgIntervalSamplePayload.materialFrameStatesGraphOwned
            && csgIntervalSamplePayload.materialGeometryStatesGraphOwned
            && csgIntervalSampleMaterialSampledTexturesCollected
            && opaqueCsgIntervalSampleComputeEmulationPayload.plan.capture(
                opaqueDrawItems.csg,
                csgFrameData
            )
        ;
        const bool opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned =
            opaqueCsgIntervalSampleComputeEmulationPlanCaptured
            && GatherOpaqueCsgIntervalSampleComputeEmulationResourceSet(
                m_deferredLightingTaskGraph,
                opaqueCsgIntervalSampleComputeEmulationPayload.plan,
                csgIntervalSampleResourceScratch,
                Name("render.graphics_prefix.opaque_csg_interval_sample_compute_emulation.outputs"),
                "Opaque CSG Interval-Sample Compute Emulation Outputs",
                opaqueCsgIntervalSampleComputeEmulationOutputSet
            )
        ;
        if(
            opaqueCsgIntervalSampleComputeEmulationPlanCaptured
            && !opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned
        ){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare graph-owned opaque CSG interval-sample compute-emulation outputs"
            ));
        }
        csgIntervalSamplePayload.csgComputeEmulationOutputStatesGraphOwned =
            opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
        csgIntervalSamplePayload.opaqueCsgComputeEmulationTiming =
            opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned
                ? &opaqueCsgIntervalSampleComputeEmulationTiming
                : nullptr
        ;
        csgIntervalSampleResourceUses.reserve(
            1u
            + (hasOpaqueDrawItems ? 2u : 0u)
            + (hasCsgFrameGpuWork ? 5u : 0u)
            + 4u
            + 4u
        );
        csgIntervalSampleResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        if(hasOpaqueDrawItems){
            csgIntervalSampleResourceUses.push_back(ReadUse(materialInstances, Core::ResourceStates::ShaderResource));
            csgIntervalSampleResourceUses.push_back(ReadUse(materialTyped, Core::ResourceStates::ShaderResource));
        }
        if(hasCsgFrameGpuWork){
            csgIntervalSampleResourceUses.push_back(ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource));
            csgIntervalSampleResourceUses.push_back(ReadUse(csgCutters, Core::ResourceStates::ShaderResource));
            csgIntervalSampleResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgIntervalSampleResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            csgIntervalSampleResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(ReadTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        csgIntervalSampleResourceUses.push_back(WriteUse(albedo, Core::ResourceStates::RenderTarget));
        csgIntervalSampleResourceUses.push_back(WriteUse(normal, Core::ResourceStates::RenderTarget));
        csgIntervalSampleResourceUses.push_back(WriteUse(worldPosition, Core::ResourceStates::RenderTarget));
        csgIntervalSampleResourceUses.push_back(WriteUse(depth, Core::ResourceStates::DepthWrite));
        const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialGeometrySetUse{
            .resourceSet = csgIntervalSampleMaterialGeometrySet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialSampledTextureSetUse{
            .resourceSet = csgIntervalSampleMaterialSampledTextureSet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse{
            .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
            .range = {},
            .requiredState = Core::ResourceStates::UnorderedAccess,
            .access = Core::GpuTaskResourceAccess::Write,
        };
        const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse{
            .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
            .range = {},
            .requiredState = Core::ResourceStates::VertexBuffer,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        Core::GpuTaskResourceSetUse csgIntervalSampleMaterialResourceSetUses[3u] = {};
        usize csgIntervalSampleMaterialResourceSetUseCount = 0u;
        if(csgIntervalSamplePayload.materialGeometryStatesGraphOwned){
            csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] =
                csgIntervalSampleMaterialGeometrySetUse;
        }
        if(csgIntervalSampleMaterialSampledTextureSet.valid()){
            csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] =
                csgIntervalSampleMaterialSampledTextureSetUse;
        }
        if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
            csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] =
                opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse;
        }

        Core::GpuTaskId csgIntervalSampleDependency = m_graphicsPrefixCsgIntervalCombineTask;
        if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
            opaqueCsgIntervalSampleComputeEmulationPayload.graphics = &m_graphics;
            opaqueCsgIntervalSampleComputeEmulationPayload.materialSystem = &m_materialSystem;
            opaqueCsgIntervalSampleComputeEmulationPayload.csgResources = csgResources;
            opaqueCsgIntervalSampleComputeEmulationPayload.targets = &deferredTargets;
            // Producer and sample share the semantic CSG interval-sample submission/ticket; the timer opens here
            // and closes after the raster half, exactly preserving the former local material timing scope.
            opaqueCsgIntervalSampleComputeEmulationPayload.timingTicket =
                timingTicketSlot(PrefixTimingSlot::CsgIntervalSample);
            opaqueCsgIntervalSampleComputeEmulationPayload.meshViewSetupReady =
                &m_graphicsPrefixMeshViewSetupReady;
            opaqueCsgIntervalSampleComputeEmulationPayload.sceneShadingSetupReady =
                &m_graphicsPrefixSceneShadingSetupReady;
            opaqueCsgIntervalSampleComputeEmulationPayload.opaqueCsgTiming =
                &opaqueCsgIntervalSampleComputeEmulationTiming;
            opaqueCsgIntervalSampleComputeEmulationPayload.instanceCount = instanceData.size();
            opaqueCsgIntervalSampleComputeEmulationPayload.materialTypedByteCount = materialTypedBytes.size();
            opaqueCsgIntervalSampleComputeEmulationPayload.materialDrawBuffersUploaded =
                csgIntervalSamplePayload.materialDrawBuffersUploaded;
            opaqueCsgIntervalSampleComputeEmulationPayload.csgFrameBuffersUploaded =
                csgIntervalSamplePayload.csgFrameBuffersUploaded;
            opaqueCsgIntervalSampleComputeEmulationPayload.intervalSampleImageStatesGraphOwned =
                csgIntervalSamplePayload.intervalSampleImageStatesGraphOwned;
            opaqueCsgIntervalSampleComputeEmulationPayload.csgClipBufferStatesGraphOwned =
                csgIntervalSamplePayload.csgClipBufferStatesGraphOwned;
            opaqueCsgIntervalSampleComputeEmulationPayload.materialFrameStatesGraphOwned =
                csgIntervalSamplePayload.materialFrameStatesGraphOwned;
            opaqueCsgIntervalSampleComputeEmulationPayload.materialGeometryStatesGraphOwned =
                csgIntervalSamplePayload.materialGeometryStatesGraphOwned;

            opaqueCsgIntervalSampleComputeEmulationResourceUses.reserve(12u);
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(meshView, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(materialInstances, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(materialTyped, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgReceiverRanges, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgCutters, Core::ResourceStates::ShaderResource)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(
                ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
            );
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalDepth,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCapNormal,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalData,
                csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(
                csgRemovedIntervalCount,
                csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationResourceSetUses[3u] = {};
            usize opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount = 0u;
            opaqueCsgIntervalSampleComputeEmulationResourceSetUses[
                opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++
            ] = csgIntervalSampleMaterialGeometrySetUse;
            if(csgIntervalSampleMaterialSampledTextureSet.valid()){
                opaqueCsgIntervalSampleComputeEmulationResourceSetUses[
                    opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++
                ] = csgIntervalSampleMaterialSampledTextureSetUse;
            }
            opaqueCsgIntervalSampleComputeEmulationResourceSetUses[
                opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++
            ] = opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse;
            Core::GpuTaskSchedulingHint opaqueCsgIntervalSampleComputeEmulationScheduling;
            opaqueCsgIntervalSampleComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
            opaqueCsgIntervalSampleComputeEmulationScheduling.forceSubmissionBoundary = false;
            opaqueCsgIntervalSampleComputeEmulationScheduling.allowPacketMerge = true;
            opaqueCsgIntervalSampleComputeEmulationScheduling.mergeWithPrevious = true;
            // Combine is the explicit immediate predecessor. The graph preserves its interval-image UAV handoff
            // across any FrontierSafe split; Sample merges with this producer so its generated-vertex handoff and
            // timing scope remain in one accepted Graphics packet.
            opaqueCsgIntervalSampleComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
            Core::GpuTaskDesc opaqueCsgIntervalSampleComputeEmulationDesc;
            opaqueCsgIntervalSampleComputeEmulationDesc
                .setIdentity(Name("render.graphics_prefix.opaque_csg_interval_sample_compute_emulation"))
                .setMarkerLabel("Opaque CSG Interval-Sample Compute Emulation")
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(opaqueCsgIntervalSampleComputeEmulationScheduling)
                .setDependencies(&m_graphicsPrefixCsgIntervalCombineTask, 1u)
                .setResourceUses(
                    opaqueCsgIntervalSampleComputeEmulationResourceUses.data(),
                    opaqueCsgIntervalSampleComputeEmulationResourceUses.size()
                )
                .setResourceSetUses(
                    opaqueCsgIntervalSampleComputeEmulationResourceSetUses,
                    opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount
                )
            ;
            m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask =
                m_deferredLightingTaskGraph.addTask<
                    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask
                >(
                    opaqueCsgIntervalSampleComputeEmulationDesc,
                    Move(opaqueCsgIntervalSampleComputeEmulationPayload)
                )
            ;
            if(!m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare opaque CSG interval-sample compute-emulation producer"
                ));
                return false;
            }
            csgIntervalSampleDependency = m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask;
        }

        Core::GpuTaskSchedulingHint csgIntervalSampleScheduling;
        csgIntervalSampleScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgIntervalSampleScheduling.forceSubmissionBoundary = false;
        csgIntervalSampleScheduling.allowPacketMerge = true;
        csgIntervalSampleScheduling.mergeWithPrevious = true;
        csgIntervalSampleScheduling.allowMergeAcrossConsumerFrontier =
            opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
        Core::GpuTaskDesc csgIntervalSampleDesc;
        csgIntervalSampleDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_sample"))
            .setMarkerLabel("Opaque CSG Interval Sample")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(csgIntervalSampleScheduling)
            .setDependencies(&csgIntervalSampleDependency, 1u)
            .setResourceUses(csgIntervalSampleResourceUses.data(), csgIntervalSampleResourceUses.size())
            .setResourceSetUses(
                csgIntervalSampleMaterialResourceSetUseCount != 0u
                    ? csgIntervalSampleMaterialResourceSetUses
                    : nullptr,
                csgIntervalSampleMaterialResourceSetUseCount
            )
        ;
        m_graphicsPrefixCsgIntervalSampleTask = m_deferredLightingTaskGraph.addTask<
            ECSRenderDetail::CsgIntervalSampleGraphTask
        >(
            csgIntervalSampleDesc,
            Move(csgIntervalSamplePayload)
        );
        if(!m_graphicsPrefixCsgIntervalSampleTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-sample task"));
            return false;
        }
        gbufferCompletionTask = m_graphicsPrefixCsgIntervalSampleTask;
    }

    Core::Alloc::ScratchArena normalizeScratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> normalizeResourceUses{ normalizeScratchArena };
    normalizeResourceUses.reserve(8u + (shadowTraceGeometryStatesGraphOwned ? 0u : shadowTraceGeometryResourceCount));
    normalizeResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(normal, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(worldPosition, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = shadowTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        // This task actually restores the state after G-buffer, so it owns an outgoing Prefix state seed instead of
        // looking like an optional same-state reader.
        if(!shadowTraceGeometryStatesGraphOwned)
            normalizeResourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowTraceGeometrySetUse{
        .resourceSet = shadowTraceGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
    Core::GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    normalizeScheduling.forceSubmissionBoundary = false;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("render.graphics_prefix.normalize"))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(normalizeScheduling)
        .setDependencies(&gbufferCompletionTask, 1u)
        .setResourceUses(normalizeResourceUses.data(), normalizeResourceUses.size())
        .setResourceSetUses(
            shadowTraceGeometryStatesGraphOwned ? &shadowTraceGeometrySetUse : nullptr,
            shadowTraceGeometryStatesGraphOwned ? 1u : 0u
        )
    ;
    m_graphicsPrefixTask = m_deferredLightingTaskGraph.addTask<PostGbufferNormalizeGraphTask>(
        normalizeDesc,
        PostGbufferNormalizeGraphTask::Payload{
            .raytracingSystem = &m_raytracingSystem,
            .asyncPrefixTiming = &asyncPrefixTiming,
            .timingTicket = timingTicketSlot(PrefixTimingSlot::Normalize),
            .shadowVisibilityTask = &m_deferredShadowVisibilityTask,
        }
    );
    if(!m_graphicsPrefixTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare post-G-buffer normalization task"));
        return false;
    }
    // Publish only after every prefix task accepts declaration. The caller freezes dependent ray-tracing routes
    // immediately after this successful return.
    m_raytracingSystem.publishPreparedLightingClassification(
        rayTracingLightingClassification,
        sceneLightData,
        sceneLightCount
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

