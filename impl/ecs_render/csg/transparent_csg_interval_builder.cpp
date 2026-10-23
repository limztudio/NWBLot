// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "transparent_csg_interval_builder.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TransparentCsgIntervalBuilder::TransparentCsgIntervalBuilder(Core::GpuTaskGraph& graph, RendererMaterialSystem& materialSystem, RendererCsgSystem& csgSystem, RendererAvboitSystem& avboitSystem)noexcept
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem)
    , m_avboitSystem(avboitSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<TransparentCsgIntervalProducerResult> TransparentCsgIntervalBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const TransparentCsgIntervalProducerInputs& inputs,
    RendererTaskGraphDetail::AvboitPreGraphTask::Payload& avboitPrePayload,
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload,
    GraphClearTimingRecordState& intervalClearTimingState
){
    using namespace RendererTaskGraphDetail;
    TransparentCsgIntervalProducerResult result{};
    if(!inputs.prefixTask.valid())
        return MakeUnexpected(Failure{});

    // Freeze the transparent CSG interval producer before AVBOIT recording; snapshot covers receiver work only.
    result.uploadTask = inputs.prefixTask;
    Core::GpuGraphResourceSetId transparentCsgMaterialGeometrySet;
    Core::GpuGraphResourceSetId transparentCsgMaterialSampledTextureSet;
    const bool hasTransparentCsgFrameWork = inputs.hasTransparentRenderers && (inputs.csgFrameState.hasTransparentStaticWork || inputs.csgFrameState.hasTransparentSkinnedWork);
    if(hasTransparentCsgFrameWork){
        MaterialPassDrawItemPartitions transparentCsgDrawItems{ scratchArena };
        InstanceGpuDataVector transparentCsgInstanceData{ scratchArena };
        CsgFrameGpuData transparentCsgFrameData{ scratchArena };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector transparentCsgMaterialTypedRanges{ scratchArena };
#endif
        MaterialTypedByteDataVector transparentCsgMaterialTypedBytes{ scratchArena };
        m_materialSystem.gatherMaterialPassDrawItems(
            inputs.targets.framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            true,
            (inputs.csgFrameState),
            RendererResourceLookupMode::PreparedOnly,
            &inputs.meshViewState,
            transparentCsgDrawItems,
            transparentCsgInstanceData,
            transparentCsgFrameData,
#if defined(NWB_DEBUG)
            transparentCsgMaterialTypedRanges,
#endif
            transparentCsgMaterialTypedBytes
        );

        if(!transparentCsgDrawItems.csgReceiverSurface.empty() && transparentCsgFrameData.hasWork()){
            if(
                !inputs.materialInstances.valid()
                || !inputs.materialTyped.valid()
                || !inputs.csgReceiverRanges.valid()
                || !inputs.csgCutters.valid()
                || !inputs.csgClipContextSlots.valid()
                || !inputs.csgIntervalSampleState.valid()
                || !(inputs.frameBindings).frameReady(transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size())
                || !(inputs.csgResources).frameReady(transparentCsgFrameData)
                || !m_materialSystem.materialPassDrawResourcesReady(transparentCsgDrawItems.csgReceiverSurface, (inputs.frameBindings))
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared transparent CSG interval resources were unavailable during graph declaration"));
                return MakeUnexpected(Failure{});
            }

            const MaterialPassDrawItems* const transparentCsgMaterialGeometryDrawSets[] = { &transparentCsgDrawItems.csgReceiverSurface, };
            const auto geometrySetResult = GatherPreparedMaterialGeometryResourceSet(
                m_graph,
                transparentCsgMaterialGeometryDrawSets,
                LengthOf(transparentCsgMaterialGeometryDrawSets),
                scratchArena,
                Name("render.avboit.intervals.transparent_csg_material_geometry"),
                "Transparent CSG Material Geometry"
            );
            avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned = geometrySetResult.has_value();
            if(!geometrySetResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material geometry states"));
                return MakeUnexpected(Failure{});
            }
            transparentCsgMaterialGeometrySet = *geometrySetResult;
            const auto sampledTextureSetResult = GatherPreparedMaterialSampledTextureResourceSet(
                m_materialSystem,
                m_graph,
                transparentCsgMaterialGeometryDrawSets,
                LengthOf(transparentCsgMaterialGeometryDrawSets),
                scratchArena,
                Name("render.avboit.intervals.transparent_csg_material_sampled_textures"),
                "Transparent CSG Material Sampled Textures"
            );
            if(!sampledTextureSetResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material sampled textures"));
                return MakeUnexpected(Failure{});
            }
            transparentCsgMaterialSampledTextureSet = *sampledTextureSetResult;

            m_materialSystem.prepareMaterialPassInstanceUploadData(transparentCsgInstanceData, (inputs.csgResources));
#if defined(NWB_DEBUG)
            if(
                transparentCsgInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || transparentCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || transparentCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: transparent CSG interval upload size overflows graph blob capacity"));
                return MakeUnexpected(Failure{});
            }
            NWB_ASSERT(transparentCsgInstanceData.size() == transparentCsgMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(transparentCsgMaterialTypedRanges, transparentCsgMaterialTypedBytes);
#endif

            const auto clipContextSlotResult = m_csgSystem.prepareCsgClipContextSlotData(inputs.targets, transparentCsgFrameData, inputs.csgResources, inputs.frameBindings);
            if(!clipContextSlotResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot transparent CSG interval auxiliary upload data"));
                return MakeUnexpected(Failure{});
            }
            const auto intervalSampleStateResult = m_csgSystem.prepareCsgIntervalSampleStateData(inputs.targets, transparentCsgFrameData, inputs.csgResources, inputs.frameBindings);
            if(!intervalSampleStateResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot transparent CSG interval auxiliary upload data"));
                return MakeUnexpected(Failure{});
            }
            const CsgClipContextSlots& transparentCsgClipContextSlotData = *clipContextSlotResult;
            const CsgIntervalSampleStateGpuData& transparentCsgIntervalSampleStateData = *intervalSampleStateResult;

            const auto transparentCsgInstanceBlobResult = m_graph.copyUploadData(transparentCsgInstanceData.data(), transparentCsgInstanceData.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData));
            const auto transparentCsgMaterialTypedBlobResult = m_graph.copyUploadData(transparentCsgMaterialTypedBytes.data(), transparentCsgMaterialTypedBytes.size(), alignof(u32));
            const auto transparentCsgReceiverRangesBlobResult = m_graph.copyUploadData(transparentCsgFrameData.receiverRanges.data(), transparentCsgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData));
            const auto transparentCsgCuttersBlobResult = m_graph.copyUploadData(transparentCsgFrameData.cutters.data(), transparentCsgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData));
            const auto transparentCsgClipContextSlotsBlobResult = m_graph.copyUploadData(&transparentCsgClipContextSlotData, sizeof(transparentCsgClipContextSlotData), alignof(CsgClipContextSlots));
            const auto transparentCsgIntervalSampleStateBlobResult = m_graph.copyUploadData(&transparentCsgIntervalSampleStateData, sizeof(transparentCsgIntervalSampleStateData), alignof(CsgIntervalSampleStateGpuData));
            if(
                !transparentCsgInstanceBlobResult
                || !transparentCsgMaterialTypedBlobResult
                || !transparentCsgReceiverRangesBlobResult
                || !transparentCsgCuttersBlobResult
                || !transparentCsgClipContextSlotsBlobResult
                || !transparentCsgIntervalSampleStateBlobResult
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable transparent CSG interval upload data"));
                return MakeUnexpected(Failure{});
            }
            const Core::GpuUploadBlobId transparentCsgInstanceBlob = *transparentCsgInstanceBlobResult;
            const Core::GpuUploadBlobId transparentCsgMaterialTypedBlob = *transparentCsgMaterialTypedBlobResult;
            const Core::GpuUploadBlobId transparentCsgReceiverRangesBlob = *transparentCsgReceiverRangesBlobResult;
            const Core::GpuUploadBlobId transparentCsgCuttersBlob = *transparentCsgCuttersBlobResult;
            const Core::GpuUploadBlobId transparentCsgClipContextSlotsBlob = *transparentCsgClipContextSlotsBlobResult;
            const Core::GpuUploadBlobId transparentCsgIntervalSampleStateBlob = *transparentCsgIntervalSampleStateBlobResult;

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
                .setScheduling(transparentCsgFirstUploadScheduling)
                .setDependencies(&result.uploadTask, 1u)
            ;
            const auto resultUploadTaskResult = m_graph.addUploadBufferTask(
                transparentCsgInstanceUploadDesc,
                Core::GpuUploadBufferTaskDesc{ .source = transparentCsgInstanceBlob, .destination = inputs.materialInstances, .finalState = Core::ResourceStates::Common, }
            );
            if(!resultUploadTaskResult){
                result.uploadTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material instance upload"));
                return MakeUnexpected(Failure{});
            }
            result.uploadTask = *resultUploadTaskResult;

            Core::GpuTaskDesc transparentCsgMaterialTypedUploadDesc;
            transparentCsgMaterialTypedUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.material_typed_upload"))
                .setMarkerLabel("Transparent CSG Material Typed Upload")
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&result.uploadTask, 1u)
            ;
            const auto resultUploadTaskResult2 = m_graph.addUploadBufferTask(
                transparentCsgMaterialTypedUploadDesc,
                Core::GpuUploadBufferTaskDesc{ .source = transparentCsgMaterialTypedBlob, .destination = inputs.materialTyped, .finalState = Core::ResourceStates::Common, }
            );
            if(!resultUploadTaskResult2){
                result.uploadTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG material typed upload"));
                return MakeUnexpected(Failure{});
            }
            result.uploadTask = *resultUploadTaskResult2;

            Core::GpuTaskDesc transparentCsgReceiverRangesUploadDesc;
            transparentCsgReceiverRangesUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.receiver_ranges_upload"))
                .setMarkerLabel("Transparent CSG Receiver Ranges Upload")
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&result.uploadTask, 1u)
            ;
            const auto resultUploadTaskResult3 = m_graph.addUploadBufferTask(
                transparentCsgReceiverRangesUploadDesc,
                Core::GpuUploadBufferTaskDesc{ .source = transparentCsgReceiverRangesBlob, .destination = inputs.csgReceiverRanges, .finalState = Core::ResourceStates::Common, }
            );
            if(!resultUploadTaskResult3){
                result.uploadTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-range upload"));
                return MakeUnexpected(Failure{});
            }
            result.uploadTask = *resultUploadTaskResult3;

            Core::GpuTaskDesc transparentCsgCuttersUploadDesc;
            transparentCsgCuttersUploadDesc.setIdentity(Name("render.avboit.transparent_csg.cutters_upload")).setMarkerLabel("Transparent CSG Cutters Upload").setScheduling(transparentCsgUploadScheduling).setDependencies(&result.uploadTask, 1u);
            const auto resultUploadTaskResult4 = m_graph.addUploadBufferTask(transparentCsgCuttersUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = transparentCsgCuttersBlob, .destination = inputs.csgCutters, .finalState = Core::ResourceStates::Common, });
            if(!resultUploadTaskResult4){
                result.uploadTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG cutter upload"));
                return MakeUnexpected(Failure{});
            }
            result.uploadTask = *resultUploadTaskResult4;

            Core::GpuTaskDesc transparentCsgClipContextSlotsUploadDesc;
            transparentCsgClipContextSlotsUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.clip_context_slots_upload"))
                .setMarkerLabel("Transparent CSG Clip Context Slots Upload")
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&result.uploadTask, 1u)
            ;
            const auto resultUploadTaskResult5 = m_graph.addUploadBufferTask(
                transparentCsgClipContextSlotsUploadDesc,
                Core::GpuUploadBufferTaskDesc{ .source = transparentCsgClipContextSlotsBlob, .destination = inputs.csgClipContextSlots, .finalState = Core::ResourceStates::Common, }
            );
            if(!resultUploadTaskResult5){
                result.uploadTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG clip-context upload"));
                return MakeUnexpected(Failure{});
            }
            result.uploadTask = *resultUploadTaskResult5;

            Core::GpuTaskDesc transparentCsgIntervalSampleStateUploadDesc;
            transparentCsgIntervalSampleStateUploadDesc
                .setIdentity(Name("render.avboit.transparent_csg.interval_sample_state_upload"))
                .setMarkerLabel("Transparent CSG Interval State Upload")
                .setScheduling(transparentCsgUploadScheduling)
                .setDependencies(&result.uploadTask, 1u)
            ;
            const auto resultUploadTaskResult6 = m_graph.addUploadBufferTask(
                transparentCsgIntervalSampleStateUploadDesc,
                Core::GpuUploadBufferTaskDesc{ .source = transparentCsgIntervalSampleStateBlob, .destination = inputs.csgIntervalSampleState, .finalState = Core::ResourceStates::Common, }
            );
            if(!resultUploadTaskResult6){
                result.uploadTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-state upload"));
                return MakeUnexpected(Failure{});
            }
            result.uploadTask = *resultUploadTaskResult6;

            avboitPrePayload.transparentCsgSnapshot.capture(transparentCsgDrawItems.csgReceiverSurface, transparentCsgFrameData, transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size());
            receiverSpanPayload.transparentCsgSnapshot.capture(transparentCsgDrawItems.csgReceiverSurface, transparentCsgFrameData, transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size());
            receiverSpanPayload.csgFrameBuffersUploaded = true;
            intervalCombinePayload.transparentCsgSnapshot.capture(transparentCsgDrawItems.csgReceiverSurface, transparentCsgFrameData, transparentCsgInstanceData.size(), transparentCsgMaterialTypedBytes.size());
            if(!avboitPrePayload.transparentCsgSnapshot.captured || !receiverSpanPayload.transparentCsgSnapshot.captured || !intervalCombinePayload.transparentCsgSnapshot.captured){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not capture transparent CSG interval graph snapshots"));
                return MakeUnexpected(Failure{});
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
        const auto makeTransparentCsgIntervalClearTaskDesc = [&transparentCsgIntervalClearScheduling](const Name identity, const AStringView markerLabel, const Core::GpuTaskId& dependency)noexcept{
            Core::GpuTaskDesc clearDesc;
            clearDesc.setIdentity(identity).setMarkerLabel(markerLabel).setScheduling(transparentCsgIntervalClearScheduling).setDependencies(&dependency, 1u);
            return clearDesc;
        };
        const Core::Rect transparentCsgClearRect = avboitPrePayload.transparentCsgSnapshot.csgWorkRegion.resolveRect(inputs.targets.width, inputs.targets.height);
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearBeginHooks{ .context = &intervalClearTimingState, .beforeClear = &BeginGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
        const Core::GpuClearTextureTaskRecordHooks transparentCsgIntervalClearEndHooks{ .context = &intervalClearTimingState, .afterClear = &EndGraphClearTimingRecord, .discarded = &DiscardGraphClearTimingRecord, };
        const auto mAvboitSystemTaskGraphStageMTransparentCsgIntervalClearFirstTaskResult = m_graph.addClearTextureRectUIntTask(
            makeTransparentCsgIntervalClearTaskDesc(Name("render.avboit.transparent_csg.interval_clear"), "Transparent CSG Interval Id Clear", result.uploadTask),
            Core::GpuClearTextureRectUIntTaskDesc{ .destination = inputs.csgIntervalId, .subresources = inputs.csgPeelSubresources, .rect = transparentCsgClearRect, .uintValue = Core::UIntColor(0u), .recordHooks = transparentCsgIntervalClearBeginHooks, }
        );
        if(!mAvboitSystemTaskGraphStageMTransparentCsgIntervalClearFirstTaskResult){
            m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG interval-id clear"));
            return MakeUnexpected(Failure{});
        }
        m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask = *mAvboitSystemTaskGraphStageMTransparentCsgIntervalClearFirstTaskResult;
        Core::GpuTaskSchedulingHint transparentCsgIntervalClearTailScheduling = transparentCsgIntervalClearScheduling;
        transparentCsgIntervalClearTailScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc transparentCsgIntervalClearTailDesc;
        transparentCsgIntervalClearTailDesc
            .setIdentity(Name("render.avboit.transparent_csg.receiver_event_count_clear"))
            .setMarkerLabel("Transparent CSG Receiver Event Count Clear")
            .setScheduling(transparentCsgIntervalClearTailScheduling)
            .setDependencies(&m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask, 1u)
        ;
        const auto mAvboitSystemTaskGraphStageMTransparentCsgIntervalClearTaskResult = m_graph.addClearTextureRectUIntTask(
            transparentCsgIntervalClearTailDesc,
            Core::GpuClearTextureRectUIntTaskDesc{
                .destination = inputs.csgReceiverEventCount,
                .subresources = inputs.csgReceiverEventCountSubresources,
                .rect = transparentCsgClearRect,
                .uintValue = Core::UIntColor(0u),
                .recordHooks = transparentCsgIntervalClearEndHooks,
            }
        );
        if(!mAvboitSystemTaskGraphStageMTransparentCsgIntervalClearTaskResult){
            m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG receiver-event clear"));
            return MakeUnexpected(Failure{});
        }
        m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask = *mAvboitSystemTaskGraphStageMTransparentCsgIntervalClearTaskResult;
        result.uploadTask = m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask;
        NWB_ASSERT(avboitPrePayload.transparentCsgStreamsUploaded && avboitPrePayload.transparentCsgSnapshot.captured);
    }
    result.materialGeometrySet = transparentCsgMaterialGeometrySet;
    result.materialSampledTextureSet = transparentCsgMaterialSampledTextureSet;
    result.produced = avboitPrePayload.transparentCsgStreamsUploaded;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

