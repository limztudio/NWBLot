// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/graph/frame_graph_avboit_extinction.h>

#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/avboit/avboit_pass_upload_helper.h>
#include <impl/ecs_render/avboit/material_upload_builder.h>
#include <impl/ecs_render/avboit/geometry_preparation_builder.h>
#include <impl/ecs_render/avboit/compute_emulation_capture.h>
#include <impl/ecs_render/avboit/generated_geometry_reuse.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/material/material_typed_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphAvboitExtinctionUploadChain::FrameGraphAvboitExtinctionUploadChain(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem){
}


Expected<FrameGraphAvboitExtinctionUploadResult> FrameGraphAvboitExtinctionUploadChain::declare(
    const FrameGraphAvboitExtinctionUploadInputs& inputs,
    RendererTaskGraphDetail::AvboitExtinctionGraphTask::Payload& extinctionPayload,
    RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask::Payload& computeEmulationPayload,
    AvboitGeneratedGeometryReuse& generatedGeometry
){
    FrameGraphAvboitExtinctionUploadResult result{};
    RendererTaskGraphDetail::AvboitExtinctionGraphTask::Payload& avboitExtinctionPayload = extinctionPayload;
    RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask::Payload& avboitExtinctionComputeEmulationPayload = computeEmulationPayload;
    DeferredFrameTargets& deferredTargets = *inputs.targets;
    const CsgFrameState& csgFrameState = *inputs.csgFrameState;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings = *inputs.frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources = *inputs.csgResources;
    const ECSRenderDetail::MeshViewGpuData& meshViewState = *inputs.meshViewState;
    const Core::GpuGraphResourceId materialInstances = inputs.materialInstances;
    const Core::GpuGraphResourceId materialTyped = inputs.materialTyped;
    const Core::GpuGraphResourceId csgReceiverRanges = inputs.csgReceiverRanges;
    const Core::GpuGraphResourceId csgCutters = inputs.csgCutters;
    const Core::GpuGraphResourceId csgClipContextSlots = inputs.csgClipContextSlots;
    const Core::GpuGraphResourceId csgIntervalSampleState = inputs.csgIntervalSampleState;
    const bool intervalOutputsGraphOwned = inputs.intervalOutputsGraphOwned;
    Core::GpuTaskId extinctionUploadTask = inputs.uploadTask;
    bool extinctionStreamsUploaded = false;
    bool extinctionCsgStreamsUploaded = false;
    bool extinctionRegularComputeEmulationPlanCaptured = false;
    Core::GpuTaskId extinctionReusedGeometryProducer;
    bool extinctionProducesReusableGeometry = false;
    bool extinctionCsgComputeEmulationPlanCaptured = false;
    bool extinctionSharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan extinctionSharedComputeEmulationPlan;
    usize extinctionSharedComputeEmulationInstanceCount = 0u;
    usize extinctionSharedComputeEmulationMaterialTypedByteCount = 0u;
    bool extinctionMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena extinctionMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId extinctionMaterialGeometrySet;
    Core::GpuGraphResourceSetId extinctionMaterialSampledTextureSet;
    result.uploadTask = extinctionUploadTask;
    if(inputs.hasTransparentRenderers){
        Core::Alloc::ScratchArena extinctionUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions extinctionDrawItems{ extinctionUploadScratch };
        InstanceGpuDataVector extinctionInstanceData{ extinctionUploadScratch };
        CsgFrameGpuData extinctionCsgFrameData{ extinctionUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector extinctionMaterialTypedRanges{ extinctionUploadScratch };
#endif
        MaterialTypedByteDataVector extinctionMaterialTypedBytes{ extinctionUploadScratch };
        AvboitPassUploadHelper extinctionUploadHelper(m_materialSystem);
        auto extinctionUploadResult = extinctionUploadHelper.gather(
            AvboitPassUploadInputs{
                .framebuffer = deferredTargets.avboit.lowFramebuffer.get(),
                .pass = MaterialPipelinePass::AvboitExtinction,
                .csgFrameState = &csgFrameState,
                .frameBindings = &frameBindings,
                .csgResources = &csgResources,
                .meshViewState = &meshViewState,
                .materialInstances = materialInstances,
                .materialTyped = materialTyped,
                .csgReceiverRanges = csgReceiverRanges,
                .csgCutters = csgCutters,
                .csgClipContextSlots = csgClipContextSlots,
                .csgIntervalSampleState = csgIntervalSampleState,
            },
            extinctionDrawItems,
            extinctionInstanceData,
            extinctionCsgFrameData,
#if defined(NWB_DEBUG)
            extinctionMaterialTypedRanges,
#endif
            extinctionMaterialTypedBytes
        );
        if(!extinctionUploadResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT extinction resources were unavailable during graph declaration"));
            return MakeUnexpected(Failure{});
        }

        const bool extinctionHasCsgDrawItems = extinctionUploadResult->hasCsgDrawItems;
        if(extinctionHasCsgDrawItems && !intervalOutputsGraphOwned){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: clipped AVBOIT extinction draws require prepared interval outputs"));
            return MakeUnexpected(Failure{});
        }
        if(extinctionUploadResult->hasDrawItems){

            const MaterialPassDrawItems* const extinctionMaterialGeometryDrawSets[] = {
                &extinctionDrawItems.regular,
                &extinctionDrawItems.csg,
            };
            AvboitGeometryPreparationBuilder extinctionGeometryPreparationBuilder(
                m_graph,
                m_materialSystem,
                extinctionMaterialGeometryScratch
            );
            auto extinctionGeometryPreparationResult = extinctionGeometryPreparationBuilder.declare(
                AvboitGeometryPreparationInputs{
                    .drawItemSets = extinctionMaterialGeometryDrawSets,
                    .drawItemSetCount = LengthOf(extinctionMaterialGeometryDrawSets),
                    .phase = AvboitGeometryPhase::Extinction,
                }
            );
            if(!extinctionGeometryPreparationResult)
                return MakeUnexpected(Failure{});
            avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned = extinctionGeometryPreparationResult->geometryOwned;
            extinctionMaterialGeometrySet = extinctionGeometryPreparationResult->materialGeometrySet;
            extinctionMaterialSampledTextureSet = extinctionGeometryPreparationResult->materialSampledTextureSet;
            extinctionMaterialSampledTexturesCollected = extinctionGeometryPreparationResult->sampledTexturesCollected;

            m_materialSystem.prepareMaterialPassInstanceUploadData(extinctionInstanceData, csgResources);
#if defined(NWB_DEBUG)
            if(
                extinctionInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || extinctionCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || extinctionCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT extinction upload size overflows graph blob capacity"));
                return MakeUnexpected(Failure{});
            }
            NWB_ASSERT(extinctionInstanceData.size() == extinctionMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                extinctionMaterialTypedRanges,
                extinctionMaterialTypedBytes
            );
#endif

            AvboitMaterialUploadBuilder extinctionMaterialUploadBuilder(
                m_graph,
                m_csgSystem
            );
            auto extinctionCsgStreamsUploadedResult = extinctionMaterialUploadBuilder.declare(
                AvboitMaterialUploadInputs{
                    .targets = &deferredTargets,
                    .csgResources = &csgResources,
                    .frameBindings = &frameBindings,
                    .materialInstances = materialInstances,
                    .materialTyped = materialTyped,
                    .csgReceiverRanges = csgReceiverRanges,
                    .csgCutters = csgCutters,
                    .csgClipContextSlots = csgClipContextSlots,
                    .uploadTask = extinctionUploadTask,
                    .phase = AvboitMaterialUploadPhase::Extinction,
                },
                extinctionInstanceData,
                extinctionMaterialTypedBytes,
                extinctionCsgFrameData,
                extinctionHasCsgDrawItems,
                extinctionUploadTask
            );
            if(!extinctionCsgStreamsUploadedResult){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction material upload"));
                return MakeUnexpected(Failure{});
            }
            extinctionCsgStreamsUploaded = *extinctionCsgStreamsUploadedResult;

            avboitExtinctionPayload.extinctionSnapshot.capture(
                extinctionDrawItems,
                extinctionCsgFrameData,
                extinctionInstanceData.size(),
                extinctionMaterialTypedBytes.size()
            );
            avboitExtinctionPayload.extinctionPhasePrepared = true;
            extinctionStreamsUploaded = true;
            // Mixed work keeps local interleaving; one handoff cannot preserve per-draw order.
            AvboitComputeEmulationCapture extinctionComputeEmulationCapture;
            auto extinctionComputeEmulationCaptureResult = extinctionComputeEmulationCapture.capture(
                AvboitComputeEmulationCaptureInputs{
                    .drawItems = &extinctionDrawItems,
                    .csgFrameData = &extinctionCsgFrameData,
                    .geometryOwned = avboitExtinctionPayload.extinctionMaterialGeometryStatesGraphOwned,
                    .sampledTexturesCollected = extinctionMaterialSampledTexturesCollected,
                    .csgStreamsUploaded = extinctionCsgStreamsUploaded,
                    .intervalOutputsGraphOwned = intervalOutputsGraphOwned,
                },
                avboitExtinctionComputeEmulationPayload.plan,
                avboitExtinctionComputeEmulationPayload.csgPlan,
                extinctionUploadScratch,
                extinctionInstanceData.size(),
                extinctionMaterialTypedBytes.size()
            );
            if(!extinctionComputeEmulationCaptureResult)
                return MakeUnexpected(Failure{});
            extinctionRegularComputeEmulationPlanCaptured = extinctionComputeEmulationCaptureResult->regularCaptured;
            extinctionCsgComputeEmulationPlanCaptured = extinctionComputeEmulationCaptureResult->csgCaptured;
            extinctionSharedComputeEmulationPlanCaptured = extinctionComputeEmulationCaptureResult->sharedCaptured;
            extinctionSharedComputeEmulationPlan = extinctionComputeEmulationCaptureResult->sharedPlan;
            extinctionSharedComputeEmulationInstanceCount = extinctionComputeEmulationCaptureResult->sharedInstanceCount;
            extinctionSharedComputeEmulationMaterialTypedByteCount = extinctionComputeEmulationCaptureResult->sharedMaterialTypedByteCount;
            if(extinctionRegularComputeEmulationPlanCaptured && generatedGeometry.matches(
                extinctionDrawItems, extinctionInstanceData, frameBindings, meshViewState, MaterialPipelinePass::AvboitExtinction
            ))
                extinctionReusedGeometryProducer = generatedGeometry.producerTask();
            else{
                generatedGeometry.reset();
                extinctionProducesReusableGeometry = extinctionRegularComputeEmulationPlanCaptured && generatedGeometry.capture(
                    extinctionDrawItems, extinctionInstanceData, frameBindings, meshViewState, MaterialPipelinePass::AvboitExtinction
                );
            }
        }
        else{
            // Keep graph ownership for empty phases; skip native re-gather of mutable state.
            avboitExtinctionPayload.extinctionSnapshot.capture(
                extinctionDrawItems,
                extinctionCsgFrameData,
                extinctionInstanceData.size(),
                extinctionMaterialTypedBytes.size()
            );
            avboitExtinctionPayload.extinctionPhasePrepared = true;
        }
    }
    result.uploadTask = extinctionUploadTask;
    result.materialGeometrySet = extinctionMaterialGeometrySet;
    result.materialSampledTextureSet = extinctionMaterialSampledTextureSet;
    result.streamsUploaded = extinctionStreamsUploaded;
    result.csgStreamsUploaded = extinctionCsgStreamsUploaded;
    result.regularComputeEmulationPlanCaptured = extinctionRegularComputeEmulationPlanCaptured;
    result.producesReusableGeometry = extinctionProducesReusableGeometry;
    result.reusedGeometryProducer = extinctionReusedGeometryProducer;
    result.csgComputeEmulationPlanCaptured = extinctionCsgComputeEmulationPlanCaptured;
    result.sharedComputeEmulationPlanCaptured = extinctionSharedComputeEmulationPlanCaptured;
    result.sharedComputeEmulationPlan = extinctionSharedComputeEmulationPlan;
    result.sharedComputeEmulationInstanceCount = extinctionSharedComputeEmulationInstanceCount;
    result.sharedComputeEmulationMaterialTypedByteCount = extinctionSharedComputeEmulationMaterialTypedByteCount;
    result.declared = true;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

