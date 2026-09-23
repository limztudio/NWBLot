// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/graph/frame_graph_avboit_accumulation.h>

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


FrameGraphAvboitAccumulationUploadChain::FrameGraphAvboitAccumulationUploadChain(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem){
}


bool FrameGraphAvboitAccumulationUploadChain::declare(
    const FrameGraphAvboitAccumulationUploadInputs& inputs,
    RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& accumulationPayload,
    RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& computeEmulationPayload,
    AvboitGeneratedGeometryReuse& generatedGeometry,
    FrameGraphAvboitAccumulationUploadResult& outResult
){
    outResult = FrameGraphAvboitAccumulationUploadResult{};
    using namespace RendererTaskGraphDetail;
    RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& avboitAccumulationPayload = accumulationPayload;
    RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& avboitAccumulationComputeEmulationPayload = computeEmulationPayload;
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
    Core::GpuTaskId accumulationUploadTask = inputs.uploadTask;
    bool accumulationStreamsUploaded = false;
    bool accumulationCsgStreamsUploaded = false;
    bool accumulationRegularComputeEmulationPlanCaptured = false;
    Core::GpuTaskId accumulationReusedGeometryProducer;
    bool accumulationProducesReusableGeometry = false;
    bool accumulationCsgComputeEmulationPlanCaptured = false;
    bool accumulationSharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan accumulationSharedComputeEmulationPlan;
    usize accumulationSharedComputeEmulationInstanceCount = 0u;
    usize accumulationSharedComputeEmulationMaterialTypedByteCount = 0u;
    bool accumulationMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena accumulationMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId accumulationMaterialGeometrySet;
    Core::GpuGraphResourceSetId accumulationMaterialSampledTextureSet;
    {
        Core::Alloc::ScratchArena accumulationUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions accumulationDrawItems{ accumulationUploadScratch };
        InstanceGpuDataVector accumulationInstanceData{ accumulationUploadScratch };
        CsgFrameGpuData accumulationCsgFrameData{ accumulationUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector accumulationMaterialTypedRanges{ accumulationUploadScratch };
#endif
        MaterialTypedByteDataVector accumulationMaterialTypedBytes{ accumulationUploadScratch };
        AvboitPassUploadHelper accumulationUploadHelper(m_materialSystem);
        AvboitPassUploadResult accumulationUploadResult;
        if(!accumulationUploadHelper.gather(
            AvboitPassUploadInputs{
                .framebuffer = deferredTargets.avboit.accumulationFramebuffer.get(),
                .pass = MaterialPipelinePass::AvboitAccumulate,
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
            accumulationDrawItems,
            accumulationInstanceData,
            accumulationCsgFrameData,
#if defined(NWB_DEBUG)
            accumulationMaterialTypedRanges,
#endif
            accumulationMaterialTypedBytes,
            accumulationUploadResult
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT accumulation resources were unavailable during graph declaration"));
            return false;
        }

        const bool accumulationHasCsgDrawItems = accumulationUploadResult.hasCsgDrawItems;
        if(accumulationUploadResult.hasDrawItems){

            const MaterialPassDrawItems* const accumulationMaterialGeometryDrawSets[] = {
                &accumulationDrawItems.regular,
                &accumulationDrawItems.csg,
            };
            AvboitGeometryPreparationBuilder accumulationGeometryPreparationBuilder(
                m_graph,
                m_materialSystem,
                accumulationMaterialGeometryScratch
            );
            AvboitGeometryPreparationResult accumulationGeometryPreparationResult;
            if(!accumulationGeometryPreparationBuilder.declare(
                AvboitGeometryPreparationInputs{
                    .drawItemSets = accumulationMaterialGeometryDrawSets,
                    .drawItemSetCount = LengthOf(accumulationMaterialGeometryDrawSets),
                    .phase = AvboitGeometryPhase::Accumulation,
                },
                accumulationGeometryPreparationResult
            ))
                return false;
            avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned = accumulationGeometryPreparationResult.geometryOwned;
            accumulationMaterialGeometrySet = accumulationGeometryPreparationResult.materialGeometrySet;
            accumulationMaterialSampledTextureSet = accumulationGeometryPreparationResult.materialSampledTextureSet;
            accumulationMaterialSampledTexturesCollected = accumulationGeometryPreparationResult.sampledTexturesCollected;

            m_materialSystem.prepareMaterialPassInstanceUploadData(accumulationInstanceData, csgResources);
#if defined(NWB_DEBUG)
            if(
                accumulationInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || accumulationCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || accumulationCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT accumulation upload size overflows graph blob capacity"));
                return false;
            }
            NWB_ASSERT(accumulationInstanceData.size() == accumulationMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                accumulationMaterialTypedRanges,
                accumulationMaterialTypedBytes
            );
#endif

            AvboitMaterialUploadBuilder accumulationMaterialUploadBuilder(
                m_graph,
                m_csgSystem
            );
            if(!accumulationMaterialUploadBuilder.declare(
                AvboitMaterialUploadInputs{
                    .targets = &deferredTargets,
                    .csgResources = &csgResources,
                    .frameBindings = &frameBindings,
                    .materialInstances = materialInstances,
                    .materialTyped = materialTyped,
                    .csgReceiverRanges = csgReceiverRanges,
                    .csgCutters = csgCutters,
                    .csgClipContextSlots = csgClipContextSlots,
                    .uploadTask = accumulationUploadTask,
                    .phase = AvboitMaterialUploadPhase::Accumulation,
                },
                accumulationInstanceData,
                accumulationMaterialTypedBytes,
                accumulationCsgFrameData,
                accumulationHasCsgDrawItems,
                accumulationUploadTask,
                accumulationCsgStreamsUploaded
            )){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation material upload"));
                return false;
            }

            avboitAccumulationPayload.accumulationSnapshot.capture(
                accumulationDrawItems,
                accumulationCsgFrameData,
                accumulationInstanceData.size(),
                accumulationMaterialTypedBytes.size()
            );
            avboitAccumulationPayload.accumulationPhasePrepared = true;
            accumulationStreamsUploaded = true;
            // A phase owns one alias-free stream; mixed work keeps local interleaving.
            AvboitComputeEmulationCapture accumulationComputeEmulationCapture;
            AvboitComputeEmulationCaptureResult accumulationComputeEmulationCaptureResult;
            if(!accumulationComputeEmulationCapture.capture(
                AvboitComputeEmulationCaptureInputs{
                    .drawItems = &accumulationDrawItems,
                    .csgFrameData = &accumulationCsgFrameData,
                    .geometryOwned = avboitAccumulationPayload.accumulationMaterialGeometryStatesGraphOwned,
                    .sampledTexturesCollected = accumulationMaterialSampledTexturesCollected,
                    .csgStreamsUploaded = accumulationCsgStreamsUploaded,
                    .intervalOutputsGraphOwned = intervalOutputsGraphOwned,
                },
                avboitAccumulationComputeEmulationPayload.plan,
                avboitAccumulationComputeEmulationPayload.csgPlan,
                accumulationUploadScratch,
                accumulationInstanceData.size(),
                accumulationMaterialTypedBytes.size(),
                accumulationComputeEmulationCaptureResult
            ))
                return false;
            accumulationRegularComputeEmulationPlanCaptured = accumulationComputeEmulationCaptureResult.regularCaptured;
            accumulationCsgComputeEmulationPlanCaptured = accumulationComputeEmulationCaptureResult.csgCaptured;
            accumulationSharedComputeEmulationPlanCaptured = accumulationComputeEmulationCaptureResult.sharedCaptured;
            accumulationSharedComputeEmulationPlan = accumulationComputeEmulationCaptureResult.sharedPlan;
            accumulationSharedComputeEmulationInstanceCount = accumulationComputeEmulationCaptureResult.sharedInstanceCount;
            accumulationSharedComputeEmulationMaterialTypedByteCount = accumulationComputeEmulationCaptureResult.sharedMaterialTypedByteCount;
            if(accumulationRegularComputeEmulationPlanCaptured && generatedGeometry.matches(
                accumulationDrawItems, accumulationInstanceData, frameBindings, meshViewState, MaterialPipelinePass::AvboitAccumulate
            ))
                accumulationReusedGeometryProducer = generatedGeometry.producerTask();
            else{
                generatedGeometry.reset();
                accumulationProducesReusableGeometry = accumulationRegularComputeEmulationPlanCaptured && generatedGeometry.capture(
                    accumulationDrawItems, accumulationInstanceData, frameBindings, meshViewState, MaterialPipelinePass::AvboitAccumulate
                );
            }
            if(
                accumulationRegularComputeEmulationPlanCaptured
                || accumulationCsgComputeEmulationPlanCaptured
            ){
                avboitAccumulationComputeEmulationPayload.instanceCount = accumulationInstanceData.size();
                avboitAccumulationComputeEmulationPayload.materialTypedByteCount = accumulationMaterialTypedBytes.size();
            }
        }
        else{
            // An empty captured phase stays authoritative; recording must not re-gather state.
            avboitAccumulationPayload.accumulationSnapshot.capture(
                accumulationDrawItems,
                accumulationCsgFrameData,
                accumulationInstanceData.size(),
                accumulationMaterialTypedBytes.size()
            );
            avboitAccumulationPayload.accumulationPhasePrepared = true;
        }
    }
    outResult.uploadTask = accumulationUploadTask;
    outResult.materialGeometrySet = accumulationMaterialGeometrySet;
    outResult.materialSampledTextureSet = accumulationMaterialSampledTextureSet;
    outResult.streamsUploaded = accumulationStreamsUploaded;
    outResult.csgStreamsUploaded = accumulationCsgStreamsUploaded;
    outResult.regularComputeEmulationPlanCaptured = accumulationRegularComputeEmulationPlanCaptured;
    outResult.producesReusableGeometry = accumulationProducesReusableGeometry;
    outResult.reusedGeometryProducer = accumulationReusedGeometryProducer;
    outResult.csgComputeEmulationPlanCaptured = accumulationCsgComputeEmulationPlanCaptured;
    outResult.sharedComputeEmulationPlanCaptured = accumulationSharedComputeEmulationPlanCaptured;
    outResult.sharedComputeEmulationPlan = accumulationSharedComputeEmulationPlan;
    outResult.sharedComputeEmulationInstanceCount = accumulationSharedComputeEmulationInstanceCount;
    outResult.sharedComputeEmulationMaterialTypedByteCount = accumulationSharedComputeEmulationMaterialTypedByteCount;
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
