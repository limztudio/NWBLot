// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/transparent_csg_interval_builder.h>

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TransparentCsgIntervalBuilder::TransparentCsgIntervalBuilder(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem,
    RendererAvboitSystem& avboitSystem
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem)
    , m_avboitSystem(avboitSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool TransparentCsgIntervalBuilder::declare(
    const TransparentCsgIntervalProducerInputs& inputs,
    RendererTaskGraphDetail::AvboitPreGraphTask::Payload& avboitPrePayload,
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload,
    GraphClearTimingRecordState& intervalClearTimingState,
    TransparentCsgIntervalProducerResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = TransparentCsgIntervalProducerResult{};
    if(
        !inputs.targets
        || !inputs.csgFrameState
        || !inputs.frameBindings
        || !inputs.csgResources
        || !inputs.meshViewState
        || !inputs.prefixTask.valid()
    )
        return false;

    // Freeze the transparent CSG interval producer before AVBOIT recording; snapshot covers receiver work only.
    outResult.uploadTask = inputs.prefixTask;
    Core::Alloc::ScratchArena transparentCsgMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId transparentCsgMaterialGeometrySet;
    Core::GpuGraphResourceSetId transparentCsgMaterialSampledTextureSet;
    const bool hasTransparentCsgFrameWork = inputs.hasTransparentRenderers
        && (inputs.csgFrameState->hasTransparentStaticWork || inputs.csgFrameState->hasTransparentSkinnedWork)
    ;
    if(hasTransparentCsgFrameWork){
        Core::Alloc::ScratchArena transparentCsgUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions transparentCsgDrawItems{ transparentCsgUploadScratch };
        InstanceGpuDataVector transparentCsgInstanceData{ transparentCsgUploadScratch };
        CsgFrameGpuData transparentCsgFrameData{ transparentCsgUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector transparentCsgMaterialTypedRanges{ transparentCsgUploadScratch };
#endif
        MaterialTypedByteDataVector transparentCsgMaterialTypedBytes{ transparentCsgUploadScratch };
        m_materialSystem.gatherMaterialPassDrawItems(
            inputs.targets->framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            true,
            (*inputs.csgFrameState),
            transparentCsgDrawItems,
            transparentCsgInstanceData,
            transparentCsgFrameData,
#if defined(NWB_DEBUG)
            transparentCsgMaterialTypedRanges,
#endif
            transparentCsgMaterialTypedBytes,
            RendererResourceLookupMode::PreparedOnly,
            inputs.meshViewState
        );

        if(!transparentCsgDrawItems.csgReceiverSurface.empty() && transparentCsgFrameData.hasWork()){
            if(
                !inputs.materialInstances.valid()
                || !inputs.materialTyped.valid()
                || !inputs.csgReceiverRanges.valid()
                || !inputs.csgCutters.valid()
                || !inputs.csgClipContextSlots.valid()
                || !inputs.csgIntervalSampleState.valid()
                || !(*inputs.frameBindings).frameReady(
                    transparentCsgInstanceData.size(),
                    transparentCsgMaterialTypedBytes.size()
                )
                || !(*inputs.csgResources).frameReady(transparentCsgFrameData)
                || !m_materialSystem.materialPassDrawResourcesReady(
                    transparentCsgDrawItems.csgReceiverSurface,
                    (*inputs.frameBindings)
                )
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared transparent CSG interval resources were unavailable during graph declaration"));
                return false;
            }

            const MaterialPassDrawItems* const transparentCsgMaterialGeometryDrawSets[] = {
                &transparentCsgDrawItems.csgReceiverSurface,
            };
            avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned = GatherPreparedMaterialGeometryResourceSet(
                m_graph,
                transparentCsgMaterialGeometryDrawSets,
                LengthOf(transparentCsgMaterialGeometryDrawSets),
                transparentCsgMaterialGeometryScratch,
                Name("render.avboit.intervals.transparent_csg_material_geometry"),
                "Transparent CSG Material Geometry",
                transparentCsgMaterialGeometrySet
            );
            if(!avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material geometry states"));
                return false;
            }
            const bool transparentCsgMaterialSampledTexturesCollected =
                avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned
                && GatherPreparedMaterialSampledTextureResourceSet(
                    m_materialSystem,
                    m_graph,
                    transparentCsgMaterialGeometryDrawSets,
                    LengthOf(transparentCsgMaterialGeometryDrawSets),
                    transparentCsgMaterialGeometryScratch,
                    Name("render.avboit.intervals.transparent_csg_material_sampled_textures"),
                    "Transparent CSG Material Sampled Textures",
                    transparentCsgMaterialSampledTextureSet
                )
            ;
            if(!transparentCsgMaterialSampledTexturesCollected){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material sampled textures"));
                return false;
            }

            m_materialSystem.prepareMaterialPassInstanceUploadData(transparentCsgInstanceData, (*inputs.csgResources));
#if defined(NWB_DEBUG)
            if(
                transparentCsgInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || transparentCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || transparentCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: transparent CSG interval upload size overflows graph blob capacity"));
                return false;
            }
            NWB_ASSERT(transparentCsgInstanceData.size() == transparentCsgMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                transparentCsgMaterialTypedRanges,
                transparentCsgMaterialTypedBytes
            );
#endif

            CsgClipContextSlots transparentCsgClipContextSlotData;
            CsgIntervalSampleStateGpuData transparentCsgIntervalSampleStateData;
            if(
                !m_csgSystem.prepareCsgClipContextSlotData(
                    (*inputs.targets),
                    transparentCsgFrameData,
                    (*inputs.csgResources),
                    (*inputs.frameBindings),
                    transparentCsgClipContextSlotData
                )
                || !m_csgSystem.prepareCsgIntervalSampleStateData(
                    (*inputs.targets),
                    transparentCsgFrameData,
                    (*inputs.csgResources),
                    (*inputs.frameBindings),
                    transparentCsgIntervalSampleStateData
                )
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot transparent CSG interval auxiliary upload data"));
                return false;
            }

            const Core::GpuUploadBlobId transparentCsgInstanceBlob = m_graph.copyUploadData(
                transparentCsgInstanceData.data(),
                transparentCsgInstanceData.size() * sizeof(InstanceGpuData),
                alignof(InstanceGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgMaterialTypedBlob = m_graph.copyUploadData(
                transparentCsgMaterialTypedBytes.data(),
                transparentCsgMaterialTypedBytes.size(),
                alignof(u32)
            );
            const Core::GpuUploadBlobId transparentCsgReceiverRangesBlob = m_graph.copyUploadData(
                transparentCsgFrameData.receiverRanges.data(),
                transparentCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
                alignof(CsgReceiverRangeGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgCuttersBlob = m_graph.copyUploadData(
                transparentCsgFrameData.cutters.data(),
                transparentCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
                alignof(CsgCutterGpuData)
            );
            const Core::GpuUploadBlobId transparentCsgClipContextSlotsBlob = m_graph.copyUploadData(
                &transparentCsgClipContextSlotData,
                sizeof(transparentCsgClipContextSlotData),
                alignof(CsgClipContextSlots)
            );
            const Core::GpuUploadBlobId transparentCsgIntervalSampleStateBlob =
                m_graph.copyUploadData(
                    &transparentCsgIntervalSampleStateData,
                    sizeof(transparentCsgIntervalSampleStateData),
                    alignof(CsgIntervalSampleStateGpuData)
                )
            ;
            if(
                !transparentCsgInstanceBlob.valid()
                || !transparentCsgMaterialTypedBlob.valid()
                || !transparentCsgReceiverRangesBlob.valid()
                || !transparentCsgCuttersBlob.valid()
                || !transparentCsgClipContextSlotsBlob.valid()
                || !transparentCsgIntervalSampleStateBlob.valid()
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable transparent CSG interval upload data"));
                return false;
            }

            Core::GpuTaskSchedulingHint transparentCsgUploadScheduling;
            transparentCsgUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
            transparentCsgUploadScheduling.forceSubmissionBoundary = false;
            transparentCsgUploadScheduling.allowPacketMerge = true;
            transparentCsgUploadScheduling.mergeWithPrevious = true;
            // Caustics and AVBOIT submit independently; start this upload chain in its own packet.
            Core::GpuTaskSchedulingHint transparentCsgFirstUploadScheduling = transparentCsgUploadScheduling;
            transparentCsgFirstUploadScheduling.mergeWithPrevious = false;

            Core::GpuTaskDesc transparentCsgInstanceUploadDesc;
            transparentCsgInstanceUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_instances_upload"))
                .setMarkerLabel("Transparent CSG Material Instances Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgFirstUploadScheduling)
                .setDependencies(&outResult.uploadTask, 1u)
            ;
            outResult.uploadTask = m_graph.addUploadBufferTask(
                transparentCsgInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgInstanceBlob,
                    .destination = inputs.materialInstances,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!outResult.uploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material instance upload"));
                return false;
            }

            Core::GpuTaskDesc transparentCsgMaterialTypedUploadDesc;
            transparentCsgMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_typed_upload"))
                .setMarkerLabel("Transparent CSG Material Typed Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&outResult.uploadTask, 1u)
            ;
            outResult.uploadTask = m_graph.addUploadBufferTask(
                transparentCsgMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgMaterialTypedBlob,
                    .destination = inputs.materialTyped,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!outResult.uploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material typed upload"));
                return false;
            }

            Core::GpuTaskDesc transparentCsgReceiverRangesUploadDesc;
            transparentCsgReceiverRangesUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.receiver_ranges_upload"))
                .setMarkerLabel("Transparent CSG Receiver Ranges Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&outResult.uploadTask, 1u)
            ;
            outResult.uploadTask = m_graph.addUploadBufferTask(
                transparentCsgReceiverRangesUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgReceiverRangesBlob,
                    .destination = inputs.csgReceiverRanges,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!outResult.uploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-range upload"));
                return false;
            }

            Core::GpuTaskDesc transparentCsgCuttersUploadDesc;
            transparentCsgCuttersUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.cutters_upload"))
                .setMarkerLabel("Transparent CSG Cutters Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&outResult.uploadTask, 1u)
            ;
            outResult.uploadTask = m_graph.addUploadBufferTask(
                transparentCsgCuttersUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgCuttersBlob,
                    .destination = inputs.csgCutters,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!outResult.uploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG cutter upload"));
                return false;
            }

            Core::GpuTaskDesc transparentCsgClipContextSlotsUploadDesc;
            transparentCsgClipContextSlotsUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.clip_context_slots_upload"))
                .setMarkerLabel("Transparent CSG Clip Context Slots Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&outResult.uploadTask, 1u)
            ;
            outResult.uploadTask = m_graph.addUploadBufferTask(
                transparentCsgClipContextSlotsUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgClipContextSlotsBlob,
                    .destination = inputs.csgClipContextSlots,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!outResult.uploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG clip-context upload"));
                return false;
            }

            Core::GpuTaskDesc transparentCsgIntervalSampleStateUploadDesc;
            transparentCsgIntervalSampleStateUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.interval_sample_state_upload"))
                .setMarkerLabel("Transparent CSG Interval State Upload")
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&outResult.uploadTask, 1u)
            ;
            outResult.uploadTask = m_graph.addUploadBufferTask(
                transparentCsgIntervalSampleStateUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = transparentCsgIntervalSampleStateBlob,
                    .destination = inputs.csgIntervalSampleState,
                    .finalState = Core::ResourceStates::Common,
                }
            );
            if(!outResult.uploadTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-state upload"));
                return false;
            }

            avboitPrePayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            receiverSpanPayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            receiverSpanPayload.csgFrameBuffersUploaded = true;
            intervalCombinePayload.transparentCsgSnapshot.capture(
                transparentCsgDrawItems.csgReceiverSurface,
                transparentCsgFrameData,
                transparentCsgInstanceData.size(),
                transparentCsgMaterialTypedBytes.size()
            );
            if(
                !avboitPrePayload.transparentCsgSnapshot.captured
                || !receiverSpanPayload.transparentCsgSnapshot.captured
                || !intervalCombinePayload.transparentCsgSnapshot.captured
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not capture transparent CSG interval graph snapshots"));
                return false;
            }
            intervalCombinePayload.csgFrameBuffersUploaded = true;
            avboitPrePayload.transparentCsgStreamsUploaded = true;
        }
    }


    // Clear frozen rect after uploads so the graph owns CopyDest -> UAV ordering.
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        transparentCsgIntervalClearScheduling.forceSubmissionBoundary = false;
        transparentCsgIntervalClearScheduling.allowPacketMerge = true;
        transparentCsgIntervalClearScheduling.mergeWithPrevious = true;
        const auto makeTransparentCsgIntervalClearTaskDesc = [&transparentCsgIntervalClearScheduling](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsUploadQueueRequest())
                .setScheduling(transparentCsgIntervalClearScheduling)
                .setDependencies(&dependency, 1u)
            ;
            return clearDesc;
        };
        const Core::Rect transparentCsgClearRect = avboitPrePayload.transparentCsgSnapshot.csgWorkRegion.resolveRect(
            inputs.targets->width,
            inputs.targets->height
        );
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearBeginHooks{
            .context = &intervalClearTimingState,
            .beforeClear = &BeginGraphClearTimingRecord,
            .discarded = &DiscardGraphClearTimingRecord,
        };
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearEndHooks{
            .context = &intervalClearTimingState,
            .afterClear = &EndGraphClearTimingRecord,
            .discarded = &DiscardGraphClearTimingRecord,
        };
        m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask =
            m_graph.addClearTextureRectUIntTask(
                makeTransparentCsgIntervalClearTaskDesc(
                    Name("render.avboit.transparent_csg.interval_clear"),
                    "Transparent CSG Interval Id Clear",
                    outResult.uploadTask
                ),
                Core::GpuClearTextureRectUIntTaskDesc{
                    .destination = inputs.csgIntervalId,
                    .subresources = inputs.csgPeelSubresources,
                    .rect = transparentCsgClearRect,
                    .uintValue = Core::UIntColor(0u),
                    .recordHooks = transparentCsgIntervalClearBeginHooks,
                }
            );
        if(!m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG interval-id clear"));
            return false;
        }
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearTailScheduling = transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc transparentCsgIntervalClearTailDesc;
        transparentCsgIntervalClearTailDesc
            .setIdentity(Name("render.avboit.transparent_csg.receiver_event_count_clear"))
            .setMarkerLabel("Transparent CSG Receiver Event Count Clear")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(transparentCsgIntervalClearTailScheduling)
            .setDependencies(&m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask, 1u)
        ;
        m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask = m_graph.addClearTextureRectUIntTask(
            transparentCsgIntervalClearTailDesc,
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = inputs.csgReceiverEventCount,
                .subresources = inputs.csgReceiverEventCountSubresources,
                .rect = transparentCsgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = transparentCsgIntervalClearEndHooks,
            }
        );
        if(!m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG receiver-event clear"));
            return false;
        }
        outResult.uploadTask = m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask;
        avboitPrePayload.transparentCsgIntervalTargetsGraphOwned = true;
        avboitPrePayload.transparentCsgIntervalPeelTargetStatesGraphOwned = true;
        avboitPrePayload.transparentCsgReceiverSurfaceImageStatesGraphOwned = true;
        // Span/Combine callbacks own exact UAV handoffs; compat calls keep native fences.
        avboitPrePayload.deferTransparentCsgIntervalCombine = true;
        avboitPrePayload.transparentCsgClipBufferStatesGraphOwned = true;
        avboitPrePayload.transparentCsgMaterialFrameStatesGraphOwned = true;
        NWB_ASSERT(
            avboitPrePayload.transparentCsgStreamsUploaded
            && avboitPrePayload.transparentCsgSnapshot.captured
        );
    }
    outResult.materialGeometrySet = transparentCsgMaterialGeometrySet;
    outResult.materialSampledTextureSet = transparentCsgMaterialSampledTextureSet;
    outResult.produced = avboitPrePayload.transparentCsgStreamsUploaded;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

