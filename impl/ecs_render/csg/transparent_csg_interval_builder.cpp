// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "transparent_csg_interval_builder.h"
#include "opaque_csg_interval_clear_builder.h"

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


Expected<TransparentCsgIntervalProducerResult> TransparentCsgIntervalBuilder::declare(
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
    const b8 hasTransparentCsgFrameWork = inputs.hasTransparentRenderers && (inputs.csgFrameState.hasTransparentStaticWork || inputs.csgFrameState.hasTransparentSkinnedWork);
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

            const Array<MaterialPassDrawItemsView, 1u> transparentCsgMaterialGeometryDrawSets = { transparentCsgDrawItems.csgReceiverSurface, };
            const auto geometrySetResult = GatherPreparedMaterialGeometryResourceSet(transparentCsgMaterialGeometryDrawSets, Name("render.avboit.intervals.transparent_csg_material_geometry"), "Transparent CSG Material Geometry", m_graph, scratchArena);
            avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned = geometrySetResult.has_value();
            if(!geometrySetResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material geometry states"));
                return MakeUnexpected(Failure{});
            }
            transparentCsgMaterialGeometrySet = *geometrySetResult;
            const auto sampledTextureSetResult = GatherPreparedMaterialSampledTextureResourceSet(
                m_materialSystem,
                transparentCsgMaterialGeometryDrawSets,
                Name("render.avboit.intervals.transparent_csg_material_sampled_textures"),
                "Transparent CSG Material Sampled Textures",
                m_graph,
                scratchArena
            );
            if(!sampledTextureSetResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared transparent CSG material sampled textures"));
                return MakeUnexpected(Failure{});
            }
            transparentCsgMaterialSampledTextureSet = *sampledTextureSetResult;

            m_materialSystem.prepareMaterialPassInstanceUploadData(inputs.csgResources, transparentCsgInstanceData);
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
            ValidateMaterialTypedUploadRanges(transparentCsgMaterialTypedRanges, transparentCsgMaterialTypedBytes);
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

            const auto appendUploadTask = [&](
                const Name identity,
                const AStringView markerLabel,
                const Core::GpuTaskSchedulingHint& scheduling,
                const Core::GpuUploadBlobId source,
                const Core::GpuGraphResourceId destination,
                const tchar* const warningMessage
            ){
                Core::GpuTaskDesc uploadDesc;
                uploadDesc.setIdentity(identity).setMarkerLabel(markerLabel).setScheduling(scheduling).setDependencies({ &result.uploadTask, 1u });
                const auto uploadTaskResult = m_graph.addUploadBufferTask(
                    uploadDesc,
                    Core::GpuUploadBufferTaskDesc{ .source = source, .destination = destination, .finalState = Core::ResourceStates::Common, }
                );
                if(!uploadTaskResult){
                    result.uploadTask = {};
                    NWB_LOGGER_WARNING(warningMessage);
                    return false;
                }
                result.uploadTask = *uploadTaskResult;
                return true;
            };
            if(
                !appendUploadTask(
                    Name("render.avboit.transparent_csg.material_instances_upload"), "Transparent CSG Material Instances Upload", transparentCsgFirstUploadScheduling,
                    transparentCsgInstanceBlob, inputs.materialInstances, NWB_TEXT("RendererSystem: could not declare transparent CSG material instance upload")
                )
                || !appendUploadTask(
                    Name("render.avboit.transparent_csg.material_typed_upload"), "Transparent CSG Material Typed Upload", transparentCsgUploadScheduling,
                    transparentCsgMaterialTypedBlob, inputs.materialTyped, NWB_TEXT("RendererSystem: could not declare transparent CSG material typed upload")
                )
                || !appendUploadTask(
                    Name("render.avboit.transparent_csg.receiver_ranges_upload"), "Transparent CSG Receiver Ranges Upload", transparentCsgUploadScheduling,
                    transparentCsgReceiverRangesBlob, inputs.csgReceiverRanges, NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-range upload")
                )
                || !appendUploadTask(
                    Name("render.avboit.transparent_csg.cutters_upload"), "Transparent CSG Cutters Upload", transparentCsgUploadScheduling,
                    transparentCsgCuttersBlob, inputs.csgCutters, NWB_TEXT("RendererSystem: could not declare transparent CSG cutter upload")
                )
                || !appendUploadTask(
                    Name("render.avboit.transparent_csg.clip_context_slots_upload"), "Transparent CSG Clip Context Slots Upload", transparentCsgUploadScheduling,
                    transparentCsgClipContextSlotsBlob, inputs.csgClipContextSlots, NWB_TEXT("RendererSystem: could not declare transparent CSG clip-context upload")
                )
                || !appendUploadTask(
                    Name("render.avboit.transparent_csg.interval_sample_state_upload"), "Transparent CSG Interval State Upload", transparentCsgUploadScheduling,
                    transparentCsgIntervalSampleStateBlob, inputs.csgIntervalSampleState, NWB_TEXT("RendererSystem: could not declare transparent CSG interval-state upload")
                )
            )
                return MakeUnexpected(Failure{});

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
        const CsgIntervalRectClearDesc clearDesc{
            .intervalClearIdentity = Name("render.avboit.transparent_csg.interval_clear"),
            .intervalClearMarkerLabel = "Transparent CSG Interval Id Clear",
            .intervalClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG interval-id clear"),
            .receiverEventClearIdentity = Name("render.avboit.transparent_csg.receiver_event_count_clear"),
            .receiverEventClearMarkerLabel = "Transparent CSG Receiver Event Count Clear",
            .receiverEventClearWarningMessage = NWB_TEXT("RendererSystem: could not declare graph-owned transparent CSG receiver-event clear"),
            .csgIntervalId = inputs.csgIntervalId,
            .csgReceiverEventCount = inputs.csgReceiverEventCount,
            .csgPeelSubresources = inputs.csgPeelSubresources,
            .csgReceiverEventCountSubresources = inputs.csgReceiverEventCountSubresources,
            .clearRect = ToCoreRect(avboitPrePayload.transparentCsgSnapshot.csgWorkRegion.resolveRect(inputs.targets.width, inputs.targets.height)),
            .dependencyTask = result.uploadTask,
        };
        if(!DeclareCsgIntervalRectClearTasks(
            clearDesc,
            m_graph,
            intervalClearTimingState,
            m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearFirstTask,
            m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask
        ))
            return MakeUnexpected(Failure{});
        result.uploadTask = m_avboitSystem.taskGraphStage().m_transparentCsgIntervalClearTask;
        NWB_ASSERT(avboitPrePayload.transparentCsgStreamsUploaded && avboitPrePayload.transparentCsgSnapshot.captured);
    }
    result.materialGeometrySet = transparentCsgMaterialGeometrySet;
    result.materialSampledTextureSet = transparentCsgMaterialSampledTextureSet;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
