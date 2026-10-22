// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/material/generated_geometry_state.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/raytrace/task_graph_post_gbuffer_normalize_task.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/opaque_upload_chain_builder.h>
#include <impl/ecs_render/deferred/opaque_csg_interval_clear_builder.h>
#include <impl/ecs_render/deferred/opaque_compute_emulation_producers.h>
#include <impl/ecs_render/deferred/opaque_csg_interval_stage.h>
#include <impl/ecs_render/deferred/opaque_shared_compute_emulation_chain.h>
#include <impl/ecs_render/deferred/prefix_scene_upload_builder.h>
#include <impl/ecs_render/deferred/lighting_content_stamp.h>
#include <impl/ecs_render/deferred/task_graph_gbuffer_task.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> RendererFramePipeline::declareDeferredGraphicsPrefixTasks(
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    ObjectGeometryCacheGraph& objectGeometry,
    const Core::GpuTaskId shadowPrepareTask,
    const CsgFrameState& csgFrameState,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const bool hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    const ECSRenderDetail::MeshViewGpuData& meshViewState,
    const bool meshViewUploadRequired,
    const DeferredGraphResourceImportResult& resources,
    const Core::GpuGraphResourceId* const shadowTraceGeometryResources,
    const usize shadowTraceGeometryResourceCount,
    const Core::GpuGraphResourceSetId shadowTraceGeometrySet,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Optional<Core::GpuTimingMeasure>& deferredClearTiming,
    GraphClearTimingRecordState& deferredClearTimingState,
    GraphClearTimingRecordState& csgIntervalClearTimingState,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming,
    Optional<Core::GpuTimingMeasure>& opaqueCsgIntervalSampleComputeEmulationTiming,
    Core::GpuTimingSubmissionTicket** const timingTickets,
    const bool& asyncPrefixTimingSpansOnePacket
){
    using namespace RendererTaskGraphDetail;
    namespace PrefixTimingSlotNs = ECSRenderDetail::DeferredGraphicsPrefixTimingSlot;
    using PrefixTimingSlot = PrefixTimingSlotNs::Enum;

    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixCsgIntervalClearFirstTask = {};
    m_graphicsPrefixCsgIntervalClearTask = {};
    resetGraphicsPrefixTaskState();
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;

    const bool shadowTraceGeometryStatesGraphOwned = shadowTraceGeometrySet.valid();
    const DeferredLightingGraphResources deferredLightingResources = m_deferredSystem.lightingGraphResources();

    if(
        !deferredTargets.valid()
        || !shadowPrepareTask.valid()
        || !deferredLightingResources.valid()
        || !resources.albedo.valid()
        || !resources.normal.valid()
        || !resources.worldPosition.valid()
        || !resources.specularRoughness.valid()
        || !resources.depth.valid()
        || !resources.opaqueColor.valid()
        || !resources.sceneShading.valid()
        || !resources.lights.valid()
        || !resources.meshView.valid()
        || !resources.currentBindlessSlots.valid()
        || !resources.materialContextSlots.valid()
        || (hasOpaqueCsgFrameWork && (
            !resources.csgCapBackNormal.valid()
            || !resources.csgIntervalDepth.valid()
            || !resources.csgIntervalId.valid()
            || !resources.csgReceiverEventData.valid()
            || !resources.csgReceiverEventCount.valid()
            || !resources.csgReceiverSpanData.valid()
            || !resources.csgReceiverSpanCount.valid()
            || !resources.csgRemovedIntervalDepth.valid()
            || !resources.csgRemovedIntervalCapNormal.valid()
            || !resources.csgRemovedIntervalData.valid()
            || !resources.csgRemovedIntervalCount.valid()
        ))
        || !timingTickets
        || !deferredClearTimingState.graphics
        || deferredClearTimingState.timing != &deferredClearTiming
        || !deferredClearTimingState.rebindableTimingTicket
        || (shadowTraceGeometryResourceCount != 0u && !shadowTraceGeometryResources)
    )
        return MakeUnexpected(Failure{});
    for(usize timingSlot = 0u; timingSlot < static_cast<usize>(PrefixTimingSlotNs::kCount); ++timingSlot){
        if(!timingTickets[timingSlot])
            return MakeUnexpected(Failure{});
    }
    const auto timingTicketSlot = [timingTickets](const PrefixTimingSlot slot)noexcept{
        return &timingTickets[static_cast<usize>(slot)];
    };
    const Core::TextureSubresourceSet csgPeelSubresources(0u, 1u, 0u, deferredTargets.csgPeelLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources(0u, 1u, 0u, deferredTargets.csgReceiverEventLayerCount);
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources(0u, 1u, 0u, deferredTargets.csgReceiverSpanLayerCount);
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources(0u, 1u, 0u, 1u);
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources(0u, 1u, 0u, deferredTargets.csgRemovedIntervalLayerCount);
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources(0u, 1u, 0u, 1u);

    const RayTracingLightingClassificationInput rayTracingLightingInput = m_raytracingSystem.snapshotLightingClassificationInput();
    PrefixSceneUploadBuilder prefixSceneUploadBuilder(m_deferredLightingTaskGraph, m_deferredSystem, m_meshSystem, m_graphics);
    auto prefixSceneUploadResult = prefixSceneUploadBuilder.declare(
        PrefixSceneUploadInputs{
            .rayTracingLightingInput = rayTracingLightingInput,
            .meshViewState = meshViewState,
            .meshView = resources.meshView,
            .lights = resources.lights,
            .sceneShading = resources.sceneShading,
            .shadowPrepareTask = shadowPrepareTask,
            .meshViewAspectRatio = meshViewAspectRatio,
            .meshViewUploadRequired = meshViewUploadRequired,
            .asyncPrefixTiming = asyncPrefixTiming,
            .meshViewSetupTimingTicket = *timingTicketSlot(PrefixTimingSlotNs::MeshViewSetup),
            .sceneShadingSetupTimingTicket = *timingTicketSlot(PrefixTimingSlotNs::SceneShadingSetup),
            .asyncPrefixTimingSpansOnePacket = asyncPrefixTimingSpansOnePacket,
            .shadowVisibilityTask = m_deferredShadowVisibilityTask,
            .meshViewSetupReady = m_graphicsPrefixMeshViewSetupReady,
            .sceneShadingSetupReady = m_graphicsPrefixSceneShadingSetupReady,
        }
    );
    if(!prefixSceneUploadResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graphics-prefix scene uploads"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixMeshViewSetupTask = prefixSceneUploadResult->meshViewSetupTask;
    m_graphicsPrefixSceneShadingSetupTask = prefixSceneUploadResult->sceneShadingSetupTask;
    const RayTracingLightingClassification rayTracingLightingClassification = prefixSceneUploadResult->lightingClassification;
    const u32 sceneLightCount = prefixSceneUploadResult->lightCount;
    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;


    // G-buffer load clears stay in attachment storage and G-buffer timing.
    // Deferred-clear timing covers the opaque-color UAV clear before its Compute handoff.
    Core::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    const Core::GpuClearTextureTaskRecordHooks deferredClearHooks{ .context = &deferredClearTimingState, .beforeClear = &BeginGraphClearTimingRecord, .afterClear = &EndGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
    Core::GpuTaskDesc deferredClearDesc;
    deferredClearDesc.setIdentity(Name("render.graphics_prefix.deferred_clear_opaque_color")).setMarkerLabel("Deferred Clear Opaque Color").setScheduling(clearScheduling).setDependencies(&m_graphicsPrefixSceneShadingSetupTask, 1u);
    Core::GpuClearTextureTaskDesc opaqueColorClearDesc;
    opaqueColorClearDesc.destination = resources.opaqueColor;
    opaqueColorClearDesc.subresources = ECSRenderDetail::s_FramebufferSubresources;
    opaqueColorClearDesc.valueType = Core::GpuClearTextureTaskValueType::Float;
    opaqueColorClearDesc.floatValue = ECSRenderDetail::s_ClearColor;
    opaqueColorClearDesc.recordHooks = deferredClearHooks;
    const auto mGraphicsPrefixDeferredClearTaskResult = m_deferredLightingTaskGraph.addClearTextureTask(deferredClearDesc, opaqueColorClearDesc);
    if(!mGraphicsPrefixDeferredClearTaskResult){
        m_graphicsPrefixDeferredClearTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-clear task"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixDeferredClearTask = *mGraphicsPrefixDeferredClearTaskResult;

    // Freeze the opaque packet's draw ordering before recording.
    // The two copied blobs below are the exact source of the heap-selected instance/material streams consumed by these saved draw items.
    MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
    InstanceGpuDataVector instanceData{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{ scratchArena };
#endif
    MaterialTypedByteDataVector materialTypedBytes{ scratchArena };
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
        materialTypedBytes, RendererResourceLookupMode::PreparedOnly, &meshViewState
    );

    ECSRenderDetail::GbufferGraphTask::Payload gbufferPayload{ m_arena, m_graphics, m_materialSystem, m_csgSystem, deferredTargets, *timingTicketSlot(PrefixTimingSlotNs::Gbuffer), m_graphicsPrefixMeshViewSetupReady, m_graphicsPrefixSceneShadingSetupReady, };
    gbufferPayload.frameBindings = frameBindings;
    gbufferPayload.csgResources = csgResources;

    const bool hasOpaqueDrawItems = !opaqueDrawItems.empty();
    // G-buffer and the opaque CSG follow-up both declare the shared material entry batch whenever their stream exists. The selected source-geometry batch is retained and declared below.
    gbufferPayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
    OpaqueUploadChainBuilder opaqueUploadChainBuilder(m_deferredLightingTaskGraph, m_materialSystem, m_csgSystem);
    auto opaqueUploadChainResult = opaqueUploadChainBuilder.declare(
        OpaqueUploadChainInputs{
            .targets = deferredTargets,
            .frameBindings = frameBindings,
            .csgResources = csgResources,
            .drawItems = opaqueDrawItems,
            .instanceData = instanceData,
            .csgFrameData = csgFrameData,
#if defined(NWB_DEBUG)
            .materialTypedRanges = materialTypedRanges,
#endif
            .materialTypedBytes = materialTypedBytes,
            .materialInstances = resources.materialInstances,
            .materialTyped = resources.materialTyped,
            .csgReceiverRanges = resources.csgReceiverRanges,
            .csgCutters = resources.csgCutters,
            .csgClipContextSlots = resources.csgClipContextSlots,
            .csgIntervalSampleState = resources.csgIntervalSampleState,
            .dependencyTask = m_graphicsPrefixDeferredClearTask,
        }
    );
    if(!opaqueUploadChainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque upload chain"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuTaskId csgFrameUploadTask = opaqueUploadChainResult->csgUploadTask;
    const bool hasCsgFrameGpuWork = opaqueUploadChainResult->hasCsgFrameGpuWork;
    gbufferPayload.materialDrawBuffersUploaded = opaqueUploadChainResult->materialDrawBuffersUploaded;
    gbufferPayload.csgFrameBuffersUploaded = opaqueUploadChainResult->csgFrameBuffersUploaded;

    gbufferPayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceData.size(), materialTypedBytes.size());
    OpaqueCsgIntervalStage opaqueCsgIntervalStage(
        m_deferredLightingTaskGraph,
        m_arena,
        m_graphics,
        m_materialSystem,
        m_csgSystem,
        deferredTargets,
        *timingTicketSlot(PrefixTimingSlotNs::CsgReceiverSpanBuild),
        *timingTicketSlot(PrefixTimingSlotNs::CsgIntervalCombine),
        *timingTicketSlot(PrefixTimingSlotNs::CsgIntervalSample),
        m_graphicsPrefixMeshViewSetupReady,
        m_graphicsPrefixSceneShadingSetupReady,
        opaqueCsgIntervalSampleComputeEmulationTiming
    );
    opaqueCsgIntervalStage.capturePayloads(frameBindings, csgResources, opaqueDrawItems, csgFrameData, instanceData.size(), materialTypedBytes.size(), gbufferPayload.materialDrawBuffersUploaded, gbufferPayload.csgFrameBuffersUploaded, hasOpaqueDrawItems, hasOpaqueCsgFrameWork);


    // The G-buffer produces peel/event images, span build consumes its aliases and publishes spans, Combine consumes the aliases and publishes removed-interval images, then Sample consumes them.
    // Their native thunks consume graph-owned StorageImage state without staging target transitions.

    // The semantic opaque-CSG flag admits this clear before native readiness checks.
    // The graph owns both CopyDest writes and their following UAV handoff.
    OpaqueCsgIntervalClearBuilder opaqueCsgIntervalClearBuilder(m_deferredLightingTaskGraph);
    auto opaqueCsgIntervalClearResult = opaqueCsgIntervalClearBuilder.declare(
        OpaqueCsgIntervalClearInputs{
            .targets = deferredTargets,
            .csgFrameData = csgFrameData,
            .csgIntervalId = resources.csgIntervalId,
            .csgReceiverEventCount = resources.csgReceiverEventCount,
            .csgPeelSubresources = csgPeelSubresources,
            .csgReceiverEventCountSubresources = csgReceiverEventCountSubresources,
            .dependencyTask = csgFrameUploadTask,
            .hasOpaqueCsgFrameWork = hasOpaqueCsgFrameWork,
        },
        csgIntervalClearTimingState
    );
    if(!opaqueCsgIntervalClearResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval clears"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixCsgIntervalClearFirstTask = opaqueCsgIntervalClearResult->clearFirstTask;
    m_graphicsPrefixCsgIntervalClearTask = opaqueCsgIntervalClearResult->clearTask;
    const Core::GpuTaskId csgIntervalClearTask = hasOpaqueCsgFrameWork ? opaqueCsgIntervalClearResult->clearTask : csgFrameUploadTask;

    // Capacity grows independently of each frozen draw stream. Synchronize only the bytes uploaded for this pass.
    const Core::BufferRange materialInstanceRange(0u, instanceData.size() * sizeof(InstanceGpuData));
    const Core::BufferRange materialTypedRange(0u, materialTypedBytes.size());
    const Core::BufferRange csgReceiverRange(0u, csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData));
    const Core::BufferRange csgCutterRange(0u, csgFrameData.cutters.size() * sizeof(CsgCutterGpuData));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> gbufferResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgReceiverSpanResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalCombineResourceUses{ scratchArena };
    Core::GpuGraphResourceSetId gbufferMaterialGeometrySet;
    Core::GpuGraphResourceSetId gbufferMaterialSampledTextureSet;
    Core::GpuGraphResourceSetId opaqueComputeEmulationOutputSet;
    Core::GpuGraphResourceId opaqueSharedComputeEmulationOutput;
    Core::GpuGraphResourceSetId opaqueCsgReceiverComputeEmulationOutputSet;
    const MaterialPassDrawItems* const gbufferMaterialGeometryDrawSets[] = { &opaqueDrawItems.regular, &opaqueDrawItems.csgReceiverSurface, };
    const bool gbufferUsesMaterialGeometry = !opaqueDrawItems.regular.empty() || !opaqueDrawItems.csgReceiverSurface.empty();
    gbufferPayload.materialGeometryStatesGraphOwned = false;
    if(gbufferUsesMaterialGeometry){
        const auto gbufferMaterialGeometrySetResult = GatherPreparedMaterialGeometryResourceSet(
            m_deferredLightingTaskGraph,
            gbufferMaterialGeometryDrawSets,
            LengthOf(gbufferMaterialGeometryDrawSets),
            scratchArena,
            Name("render.graphics_prefix.gbuffer.material_geometry"),
            "Opaque Material Geometry"
        );
        gbufferPayload.materialGeometryStatesGraphOwned = gbufferMaterialGeometrySetResult.has_value();
        if(gbufferMaterialGeometrySetResult)
            gbufferMaterialGeometrySet = *gbufferMaterialGeometrySetResult;
    }
    if(gbufferUsesMaterialGeometry && !gbufferPayload.materialGeometryStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material geometry states"));
        // Frozen heap slots require a complete graph declaration before this material callback can record.
        return MakeUnexpected(Failure{});
    }
    bool gbufferMaterialSampledTexturesCollected = false;
    if(gbufferUsesMaterialGeometry){
        const auto gbufferMaterialSampledTextureSetResult = GatherPreparedMaterialSampledTextureResourceSet(
            m_materialSystem,
            m_deferredLightingTaskGraph,
            gbufferMaterialGeometryDrawSets,
            LengthOf(gbufferMaterialGeometryDrawSets),
            scratchArena,
            Name("render.graphics_prefix.gbuffer.material_sampled_textures"),
            "Opaque Material Sampled Textures"
        );
        gbufferMaterialSampledTexturesCollected = gbufferMaterialSampledTextureSetResult.has_value();
        if(gbufferMaterialSampledTextureSetResult)
            gbufferMaterialSampledTextureSet = *gbufferMaterialSampledTextureSetResult;
    }
    if(gbufferUsesMaterialGeometry && !gbufferMaterialSampledTexturesCollected){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material sampled textures"));
        return MakeUnexpected(Failure{});
    }


    // A generated-vertex buffer is persistent per mesh, so pulling every compute dispatch ahead of raster would be wrong when multiple frozen draw items select the same output.
    // The plan deliberately enables only the fully alias-free regular opaque case; all other streams keep their established local interleaved handoff.
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask::Payload opaqueComputeEmulationPayload{
        m_arena,
        m_materialSystem,
        deferredTargets,
        *timingTicketSlot(PrefixTimingSlotNs::Gbuffer),
        m_graphicsPrefixMeshViewSetupReady,
        m_graphicsPrefixSceneShadingSetupReady,
    };
    opaqueComputeEmulationPayload.frameBindings = frameBindings;
    const bool opaqueComputeEmulationPlanCaptured = gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueComputeEmulationPayload.plan.capture(opaqueDrawItems.regular, scratchArena)
    ;
    bool opaqueComputeEmulationOutputStatesGraphOwned = false;
    if(opaqueComputeEmulationPlanCaptured){
        const auto opaqueComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            m_deferredLightingTaskGraph,
            opaqueComputeEmulationPayload.plan,
            scratchArena,
            Name("render.graphics_prefix.opaque_compute_emulation.outputs"),
            "Opaque Compute Emulation Outputs"
        );
        opaqueComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationOutputSetResult.has_value();
        if(opaqueComputeEmulationOutputSetResult)
            opaqueComputeEmulationOutputSet = *opaqueComputeEmulationOutputSetResult;
    }
    if(opaqueComputeEmulationPlanCaptured && !opaqueComputeEmulationOutputStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque compute-emulation output states"));
    }
    gbufferPayload.regularComputeEmulationOutputStatesGraphOwned = opaqueComputeEmulationOutputStatesGraphOwned;

    // A persistent generated-vertex buffer is normally a local dispatch/raster bridge.
    // Handle the small alias classes explicitly: two through five regular opaque compute items sharing one frozen output and heap slot.
    // Opaque CSG remains out of scope so no CSG producer/raster phase can observe this alternation.
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan opaqueSharedComputeEmulationPlan;
    const bool opaqueSharedComputeEmulationPlanCaptured =
        !hasOpaqueCsgFrameWork
        && !opaqueComputeEmulationOutputStatesGraphOwned
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueSharedComputeEmulationPlan.capture(opaqueDrawItems.regular, ECSRenderDetail::s_SharedComputeEmulationMaximumDrawCount)
    ;
    bool opaqueSharedComputeEmulationOutputStatesGraphOwned = false;
    if(opaqueSharedComputeEmulationPlanCaptured){
        const auto opaqueSharedComputeEmulationOutputResult = GatherRegularSharedComputeEmulationResource(m_deferredLightingTaskGraph, opaqueSharedComputeEmulationPlan, "Opaque Shared Compute Emulation Output");
        opaqueSharedComputeEmulationOutputStatesGraphOwned = opaqueSharedComputeEmulationOutputResult.has_value();
        if(opaqueSharedComputeEmulationOutputResult)
            opaqueSharedComputeEmulationOutput = *opaqueSharedComputeEmulationOutputResult;
    }
    if(opaqueSharedComputeEmulationPlanCaptured && !opaqueSharedComputeEmulationOutputStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned shared opaque compute-emulation output state"));
    }
    gbufferPayload.regularSharedComputeEmulationDrawsGraphOwned = opaqueSharedComputeEmulationOutputStatesGraphOwned;
    gbufferPayload.regularSharedComputeEmulationTiming = opaqueSharedComputeEmulationOutputStatesGraphOwned ? &opaqueRegularSharedComputeEmulationTiming : nullptr;

    ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask::Payload opaqueCsgReceiverComputeEmulationPayload{
        m_arena,
        m_materialSystem,
        deferredTargets,
        *timingTicketSlot(PrefixTimingSlotNs::Gbuffer),
        m_graphicsPrefixMeshViewSetupReady,
        m_graphicsPrefixSceneShadingSetupReady,
    };
    opaqueCsgReceiverComputeEmulationPayload.frameBindings = frameBindings;
    const bool opaqueCsgReceiverComputeEmulationPlanCaptured = hasOpaqueCsgFrameWork
        && hasCsgFrameGpuWork
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && opaqueCsgReceiverComputeEmulationPayload.plan.capture(opaqueDrawItems.csgReceiverSurface, opaqueDrawItems.regular, csgFrameData, scratchArena)
    ;
    bool opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned = false;
    if(opaqueCsgReceiverComputeEmulationPlanCaptured){
        const auto opaqueCsgReceiverComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            m_deferredLightingTaskGraph,
            opaqueCsgReceiverComputeEmulationPayload.plan,
            scratchArena,
            Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation.outputs"),
            "Opaque CSG Receiver Compute Emulation Outputs"
        );
        opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned = opaqueCsgReceiverComputeEmulationOutputSetResult.has_value();
        if(opaqueCsgReceiverComputeEmulationOutputSetResult)
            opaqueCsgReceiverComputeEmulationOutputSet = *opaqueCsgReceiverComputeEmulationOutputSetResult;
    }
    if(opaqueCsgReceiverComputeEmulationPlanCaptured && !opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver compute-emulation output states"));
    }
    gbufferPayload.csgReceiverComputeEmulationOutputStatesGraphOwned = opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned;

    constexpr usize s_OpaqueDrawResourceUseCount = 8u;
    constexpr usize s_NoOpaqueDrawResourceUseCount = 6u;
    // Cap back resources.normal, interval resources.depth, interval id, receiver event data, and receiver event count.
    constexpr usize s_OpaqueCsgFrameResourceUseCount = 5u;
    gbufferResourceUses.reserve((hasOpaqueDrawItems ? s_OpaqueDrawResourceUseCount : s_NoOpaqueDrawResourceUseCount) + (hasCsgFrameGpuWork ? s_OpaqueCsgFrameGpuResourceUseCount : 0u) + (hasOpaqueCsgFrameWork ? s_OpaqueCsgFrameResourceUseCount : 0u));
    gbufferResourceUses.push_back(ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer));
    if(hasOpaqueDrawItems){
        gbufferResourceUses.push_back(ReadBufferUse(resources.materialInstances, materialInstanceRange));
        gbufferResourceUses.push_back(ReadBufferUse(resources.materialTyped, materialTypedRange));
    }
    if(hasCsgFrameGpuWork){
        gbufferResourceUses.push_back(ReadBufferUse(resources.csgReceiverRanges, csgReceiverRange));
        gbufferResourceUses.push_back(ReadBufferUse(resources.csgCutters, csgCutterRange));
        gbufferResourceUses.push_back(ReadUse(resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        gbufferResourceUses.push_back(ReadUse(resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // CSG resolves target-specific images through the deferred bindless-slot buffer selected in its context.
        gbufferResourceUses.push_back(ReadUse(resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    }
    if(hasOpaqueCsgFrameWork){
        constexpr usize s_CsgReceiverSpanResourceUseCapacity = 4u;
        // Clip context slots and bindless slots.
        constexpr usize s_CsgSlotResourceUseCount = 2u;
        csgReceiverSpanResourceUses.reserve(s_CsgReceiverSpanResourceUseCapacity + (hasCsgFrameGpuWork ? s_CsgSlotResourceUseCount : 0u));
        constexpr usize s_CsgIntervalCombineResourceUseCapacity = 9u;
        csgIntervalCombineResourceUses.reserve(s_CsgIntervalCombineResourceUseCapacity + (hasCsgFrameGpuWork ? s_CsgSlotResourceUseCount : 0u));
        // Peel payloads and receiver-event entries never load their prior values.
        // Their paired interval ID and event count preserve the sparse-write validity contract after the preceding explicit clears, so only those two resources require ReadWrite access.
        // Keeping the payloads write-only also permits their first graph use to begin at a fresh texture's native Unknown state.
        gbufferResourceUses.push_back(WriteTextureUse(resources.csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        gbufferResourceUses.push_back(WriteTextureUse(resources.csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        gbufferResourceUses.push_back(ReadWriteTextureUse(resources.csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        gbufferResourceUses.push_back(WriteTextureUse(resources.csgReceiverEventData, csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess));
        gbufferResourceUses.push_back(ReadWriteTextureUse(resources.csgReceiverEventCount, csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess));
        csgReceiverSpanResourceUses.push_back(ReadTextureUse(resources.csgReceiverEventData, csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess));
        csgReceiverSpanResourceUses.push_back(ReadTextureUse(resources.csgReceiverEventCount, csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess));
        if(hasCsgFrameGpuWork){
            csgReceiverSpanResourceUses.push_back(ReadUse(resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgReceiverSpanResourceUses.push_back(ReadUse(resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgReceiverSpanResourceUses.push_back(WriteTextureUse(resources.csgReceiverSpanData, csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess));
        csgReceiverSpanResourceUses.push_back(WriteTextureUse(resources.csgReceiverSpanCount, csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(resources.csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(resources.csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(resources.csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(resources.csgReceiverSpanData, csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(ReadTextureUse(resources.csgReceiverSpanCount, csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess));
        if(hasCsgFrameGpuWork){
            csgIntervalCombineResourceUses.push_back(ReadUse(resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            csgIntervalCombineResourceUses.push_back(ReadUse(resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        }
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(resources.csgRemovedIntervalDepth, csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(resources.csgRemovedIntervalCapNormal, csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(resources.csgRemovedIntervalData, csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
        csgIntervalCombineResourceUses.push_back(WriteTextureUse(resources.csgRemovedIntervalCount, csgRemovedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess));
    }
    gbufferResourceUses.push_back(WriteUse(resources.albedo, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(resources.normal, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(resources.worldPosition, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(resources.specularRoughness, Core::ResourceStates::RenderTarget));
    gbufferResourceUses.push_back(WriteUse(resources.depth, Core::ResourceStates::DepthWrite));
    const Core::GpuTaskResourceSetUse gbufferMaterialGeometrySetUse{ .resourceSet = gbufferMaterialGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse gbufferMaterialSampledTextureSetUse{ .resourceSet = gbufferMaterialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputUavSetUse{ .resourceSet = opaqueComputeEmulationOutputSet, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceSetUse opaqueComputeEmulationOutputVertexBufferSetUse{ .resourceSet = opaqueComputeEmulationOutputSet, .range = {}, .requiredState = ECSRenderDetail::s_GeneratedGeometryRasterState, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueCsgReceiverComputeEmulationOutputSet,
        .range = {},
        .requiredState = ECSRenderDetail::s_GeneratedGeometryRasterState,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse gbufferMaterialResourceSetUses[4u] = {};
    usize gbufferMaterialResourceSetUseCount = 0u;
    if(gbufferPayload.materialGeometryStatesGraphOwned)
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialGeometrySetUse;
    if(gbufferMaterialSampledTextureSet.valid())
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = gbufferMaterialSampledTextureSetUse;
    if(opaqueComputeEmulationOutputStatesGraphOwned){
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = opaqueComputeEmulationOutputVertexBufferSetUse;
    }
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        gbufferMaterialResourceSetUses[gbufferMaterialResourceSetUseCount++] = opaqueCsgReceiverComputeEmulationOutputVertexBufferSetUse;
    }

    Core::GpuTaskId gbufferDependency = csgIntervalClearTask;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> objectGeometryReads{scratchArena};
    if(gbufferPayload.materialDrawBuffersUploaded && !objectGeometry.prepare(
        opaqueDrawItems.regular.indexedDrawItems.data(), opaqueDrawItems.regular.indexedDrawItems.size(),
        frameBindings, deferredTargets, gbufferDependency, objectGeometryReads, scratchArena,
        nullptr, timingTicketSlot(PrefixTimingSlotNs::Gbuffer)
    ))
        return MakeUnexpected(Failure{});
    if(gbufferPayload.materialDrawBuffersUploaded && !objectGeometry.prepare(
        opaqueDrawItems.csgReceiverSurface.indexedDrawItems.data(), opaqueDrawItems.csgReceiverSurface.indexedDrawItems.size(),
        frameBindings, deferredTargets, gbufferDependency, objectGeometryReads, scratchArena,
        nullptr, timingTicketSlot(PrefixTimingSlotNs::Gbuffer)
    ))
        return MakeUnexpected(Failure{});
    gbufferResourceUses.insert(gbufferResourceUses.end(), objectGeometryReads.begin(), objectGeometryReads.end());

    const OpaqueComputeEmulationProducerInputs computeEmulationProducerInputs{
        .resources = resources,
        .materialGeometrySetUse = gbufferMaterialGeometrySetUse,
        .materialSampledTextureSetUse = gbufferMaterialSampledTextureSetUse,
        .materialInstanceRange = materialInstanceRange,
        .materialTypedRange = materialTypedRange,
        .csgReceiverRange = csgReceiverRange,
        .csgCutterRange = csgCutterRange,
        .instanceCount = instanceData.size(),
        .materialTypedByteCount = materialTypedBytes.size(),
        .materialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded,
        .csgFrameBuffersUploaded = gbufferPayload.csgFrameBuffersUploaded,
        .materialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned,
        .materialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned,
    };
    OpaqueComputeEmulationProducerBuilder computeEmulationProducerBuilder(m_deferredLightingTaskGraph);
    if(opaqueComputeEmulationOutputStatesGraphOwned){
        const auto producerTask = computeEmulationProducerBuilder.declareRegular(scratchArena, computeEmulationProducerInputs, opaqueComputeEmulationOutputUavSetUse, opaqueComputeEmulationPayload, gbufferDependency);
        if(!producerTask)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixOpaqueComputeEmulationTask = *producerTask;
        gbufferDependency = m_graphicsPrefixOpaqueComputeEmulationTask;
    }
    if(opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned){
        const auto producerTask = computeEmulationProducerBuilder.declareCsgReceiver(scratchArena, computeEmulationProducerInputs, csgResources, opaqueCsgReceiverComputeEmulationOutputUavSetUse, opaqueCsgReceiverComputeEmulationPayload, gbufferDependency);
        if(!producerTask)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask = *producerTask;
        gbufferDependency = m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask;
    }
    Core::GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.cost = Core::GpuTaskCostHint::Medium;
    gbufferScheduling.forceSubmissionBoundary = false;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;


    // The producer is an explicit immediate predecessor.
    // Preserve its compiler-owned UAV-to-vertex/index handoff inside the existing primary-Graphics packet even when FrontierSafe sees an earlier cross-queue consumer.
    gbufferScheduling.allowMergeAcrossConsumerFrontier = opaqueComputeEmulationOutputStatesGraphOwned || opaqueSharedComputeEmulationOutputStatesGraphOwned || opaqueCsgReceiverComputeEmulationOutputStatesGraphOwned;
    Core::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("render.graphics_prefix.gbuffer"))
        .setMarkerLabel("Opaque G-Buffer")
        .setScheduling(gbufferScheduling)
        .setDependencies(&gbufferDependency, 1u)
        .setResourceUses(gbufferResourceUses.data(), gbufferResourceUses.size())
        .setResourceSetUses(gbufferMaterialResourceSetUseCount != 0u ? gbufferMaterialResourceSetUses : nullptr, gbufferMaterialResourceSetUseCount)
    ;
    // The shared-output successors are declared after G-buffer has moved its payload into the graph. Keep their frozen readiness/state bits by value instead of reading a moved payload below.
    const bool opaqueSharedMaterialDrawBuffersUploaded = gbufferPayload.materialDrawBuffersUploaded;
    const bool opaqueSharedMaterialFrameStatesGraphOwned = gbufferPayload.materialFrameStatesGraphOwned;
    const bool opaqueSharedMaterialGeometryStatesGraphOwned = gbufferPayload.materialGeometryStatesGraphOwned;
    const auto mGraphicsPrefixGbufferTaskResult = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::GbufferGraphTask>(gbufferDesc, Move(gbufferPayload));
    if(!mGraphicsPrefixGbufferTaskResult){
        m_graphicsPrefixGbufferTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque G-buffer task"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixGbufferTask = *mGraphicsPrefixGbufferTaskResult;

    Core::GpuTaskId gbufferCompletionTask = m_graphicsPrefixGbufferTask;
    if(opaqueSharedComputeEmulationOutputStatesGraphOwned){
        OpaqueSharedComputeEmulationChainBuilder sharedComputeEmulationChainBuilder(m_deferredLightingTaskGraph, m_materialSystem);
        const auto sharedComputeEmulationChain = sharedComputeEmulationChainBuilder.declare(
            scratchArena,
            OpaqueSharedComputeEmulationChainInputs{
                .targets = deferredTargets,
                .resources = resources,
                .frameBindings = frameBindings,
                .plan = opaqueSharedComputeEmulationPlan,
                .objectGeometryReads = objectGeometryReads,
                .timingTicket = *timingTicketSlot(PrefixTimingSlotNs::Gbuffer),
                .meshViewSetupReady = m_graphicsPrefixMeshViewSetupReady,
                .sceneShadingSetupReady = m_graphicsPrefixSceneShadingSetupReady,
                .sharedComputeEmulationTiming = opaqueRegularSharedComputeEmulationTiming,
                .materialGeometrySetUse = gbufferMaterialGeometrySetUse,
                .materialSampledTextureSetUse = gbufferMaterialSampledTextureSetUse,
                .materialInstanceRange = materialInstanceRange,
                .materialTypedRange = materialTypedRange,
                .sharedOutput = opaqueSharedComputeEmulationOutput,
                .gbufferTask = m_graphicsPrefixGbufferTask,
                .instanceCount = instanceData.size(),
                .materialTypedByteCount = materialTypedBytes.size(),
                .materialDrawBuffersUploaded = opaqueSharedMaterialDrawBuffersUploaded,
                .materialFrameStatesGraphOwned = opaqueSharedMaterialFrameStatesGraphOwned,
                .materialGeometryStatesGraphOwned = opaqueSharedMaterialGeometryStatesGraphOwned,
            },
            m_graphicsPrefixOpaqueSharedComputeEmulationTasks,
            LengthOf(m_graphicsPrefixOpaqueSharedComputeEmulationTasks)
        );
        if(!sharedComputeEmulationChain)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount = sharedComputeEmulationChain->taskCount;
        gbufferCompletionTask = sharedComputeEmulationChain->completionTask;
    }
    if(hasOpaqueCsgFrameWork){
        const auto opaqueCsgTasks = opaqueCsgIntervalStage.declare(
            scratchArena,
            OpaqueCsgIntervalStageInputs{
                .targets = deferredTargets,
                .resources = resources,
                .frameBindings = frameBindings,
                .csgResources = csgResources,
                .opaqueDrawItems = opaqueDrawItems,
                .csgFrameData = csgFrameData,
                .receiverSpanResourceUses = csgReceiverSpanResourceUses,
                .intervalCombineResourceUses = csgIntervalCombineResourceUses,
                .intervalSampleTimingTicket = timingTicketSlot(PrefixTimingSlotNs::CsgIntervalSample),
                .removedIntervalSubresources = csgRemovedIntervalSubresources,
                .removedIntervalCountSubresources = csgRemovedIntervalCountSubresources,
                .materialInstanceRange = materialInstanceRange,
                .materialTypedRange = materialTypedRange,
                .csgReceiverRange = csgReceiverRange,
                .csgCutterRange = csgCutterRange,
                .dependencyTask = gbufferCompletionTask,
                .instanceCount = instanceData.size(),
                .materialTypedByteCount = materialTypedBytes.size(),
                .hasOpaqueDrawItems = hasOpaqueDrawItems,
                .hasCsgFrameGpuWork = hasCsgFrameGpuWork,
            },
            objectGeometry
        );
        if(!opaqueCsgTasks)
            return MakeUnexpected(Failure{});
        m_graphicsPrefixCsgReceiverSpanTask = opaqueCsgTasks->receiverSpanTask;
        m_graphicsPrefixCsgIntervalCombineTask = opaqueCsgTasks->intervalCombineTask;
        m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask = opaqueCsgTasks->intervalSampleComputeEmulationTask;
        m_graphicsPrefixCsgIntervalSampleTask = opaqueCsgTasks->intervalSampleTask;
        gbufferCompletionTask = m_graphicsPrefixCsgIntervalSampleTask;
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> normalizeResourceUses{ scratchArena };
    constexpr usize s_NormalizeResourceUseCapacity = 9u;
    normalizeResourceUses.reserve(s_NormalizeResourceUseCapacity + (shadowTraceGeometryStatesGraphOwned ? 0u : shadowTraceGeometryResourceCount));
    normalizeResourceUses.push_back(ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(resources.normal, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(resources.worldPosition, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(resources.specularRoughness, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(resources.depth, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(resources.sceneShading, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(resources.lights, Core::ResourceStates::ShaderResource));
    normalizeResourceUses.push_back(ReadUse(resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    normalizeResourceUses.push_back(ReadUse(resources.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(usize resourceIndex = 0u; resourceIndex < shadowTraceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = shadowTraceGeometryResources[resourceIndex];
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        if(!shadowTraceGeometryStatesGraphOwned)
            normalizeResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowTraceGeometrySetUse{ .resourceSet = shadowTraceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    Core::GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    normalizeScheduling.forceSubmissionBoundary = false;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("render.graphics_prefix.normalize"))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setScheduling(normalizeScheduling)
        .setDependencies(&gbufferCompletionTask, 1u)
        .setResourceUses(normalizeResourceUses.data(), normalizeResourceUses.size())
        .setResourceSetUses(shadowTraceGeometryStatesGraphOwned ? &shadowTraceGeometrySetUse : nullptr, shadowTraceGeometryStatesGraphOwned ? 1u : 0u)
    ;
    const auto mGraphicsPrefixTaskResult = m_deferredLightingTaskGraph.addTask<PostGbufferNormalizeGraphTask>(
        normalizeDesc,
        PostGbufferNormalizeGraphTask::Payload{ .raytracingSystem = m_raytracingSystem, .asyncPrefixTiming = &asyncPrefixTiming, .timingTicket = *timingTicketSlot(PrefixTimingSlotNs::Normalize), .shadowVisibilityTask = &m_deferredShadowVisibilityTask, }
    );
    if(!mGraphicsPrefixTaskResult){
        m_graphicsPrefixTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare post-G-buffer normalization task"));
        return MakeUnexpected(Failure{});
    }
    m_graphicsPrefixTask = *mGraphicsPrefixTaskResult;
    // Publish only after every prefix task accepts declaration. The caller freezes dependent ray-tracing routes immediately after this successful return.
    m_raytracingSystem.publishPreparedLightingClassification(rayTracingLightingClassification, prefixSceneUploadResult->lightData, sceneLightCount);
    return prefixSceneUploadResult->sceneLightingContentHash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

