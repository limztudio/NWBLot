// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/graph/frame_graph_avboit_occupancy.h>

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


FrameGraphAvboitOccupancyUploadChain::FrameGraphAvboitOccupancyUploadChain(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem){
}


bool FrameGraphAvboitOccupancyUploadChain::declare(
    const FrameGraphAvboitOccupancyUploadInputs& inputs,
    RendererTaskGraphDetail::AvboitOccupancyGraphTask::Payload& occupancyPayload,
    RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask::Payload& computeEmulationPayload,
    AvboitGeneratedGeometryReuse& generatedGeometry,
    FrameGraphAvboitOccupancyUploadResult& outResult
){
    outResult = FrameGraphAvboitOccupancyUploadResult{};
    using namespace RendererTaskGraphDetail;
    RendererTaskGraphDetail::AvboitOccupancyGraphTask::Payload& avboitOccupancyPayload = occupancyPayload;
    RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask::Payload& avboitOccupancyComputeEmulationPayload = computeEmulationPayload;
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
    const bool intervalOutputsGraphOwned = inputs.intervalOutputsGraphOwned;
    Core::GpuTaskId occupancyUploadTask = inputs.uploadTask;
    bool occupancyCsgStreamsUploaded = false;
    bool occupancyRegularComputeEmulationPlanCaptured = false;
    Core::GpuTaskId occupancyReusedGeometryProducer;
    bool occupancyProducesReusableGeometry = false;
    bool occupancyCsgComputeEmulationPlanCaptured = false;
    bool occupancySharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan occupancySharedComputeEmulationPlan;
    usize occupancySharedComputeEmulationInstanceCount = 0u;
    usize occupancySharedComputeEmulationMaterialTypedByteCount = 0u;
    bool occupancyMaterialSampledTexturesCollected = false;
    Core::Alloc::ScratchArena occupancyMaterialGeometryScratch(RendererArenaScope::s_TaskGraphArena);
    Core::GpuGraphResourceSetId occupancyMaterialGeometrySet;
    Core::GpuGraphResourceSetId occupancyMaterialSampledTextureSet;
    outResult.uploadTask = occupancyUploadTask;
    if(inputs.hasTransparentRenderers){
        Core::Alloc::ScratchArena occupancyUploadScratch(RendererArenaScope::s_TaskGraphArena);
        MaterialPassDrawItemPartitions occupancyDrawItems{ occupancyUploadScratch };
        InstanceGpuDataVector occupancyInstanceData{ occupancyUploadScratch };
        CsgFrameGpuData occupancyCsgFrameData{ occupancyUploadScratch };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector occupancyMaterialTypedRanges{ occupancyUploadScratch };
#endif
        MaterialTypedByteDataVector occupancyMaterialTypedBytes{ occupancyUploadScratch };
        AvboitPassUploadHelper occupancyUploadHelper(m_materialSystem);
        AvboitPassUploadResult occupancyUploadResult;
        if(!occupancyUploadHelper.gather(
            AvboitPassUploadInputs{
                .framebuffer = deferredTargets.avboit.lowFramebuffer.get(),
                .pass = MaterialPipelinePass::AvboitOccupancy,
                .csgFrameState = &csgFrameState,
                .frameBindings = &frameBindings,
                .csgResources = &csgResources,
                .meshViewState = &meshViewState,
                .materialInstances = materialInstances,
                .materialTyped = materialTyped,
                .csgReceiverRanges = csgReceiverRanges,
                .csgCutters = csgCutters,
                .csgClipContextSlots = csgClipContextSlots,
                .csgIntervalSampleState = Core::GpuGraphResourceId{},
            },
            occupancyDrawItems,
            occupancyInstanceData,
            occupancyCsgFrameData,
#if defined(NWB_DEBUG)
            occupancyMaterialTypedRanges,
#endif
            occupancyMaterialTypedBytes,
            occupancyUploadResult
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT occupancy resources were unavailable during graph declaration"));
            return false;
        }

        const bool occupancyHasCsgDrawItems = occupancyUploadResult.hasCsgDrawItems;
        if(occupancyUploadResult.hasDrawItems){

            const MaterialPassDrawItems* const occupancyMaterialGeometryDrawSets[] = {
                &occupancyDrawItems.regular,
                &occupancyDrawItems.csg,
            };
            AvboitGeometryPreparationBuilder occupancyGeometryPreparationBuilder(
                m_graph,
                m_materialSystem,
                occupancyMaterialGeometryScratch
            );
            AvboitGeometryPreparationResult occupancyGeometryPreparationResult;
            if(!occupancyGeometryPreparationBuilder.declare(
                AvboitGeometryPreparationInputs{
                    .drawItemSets = occupancyMaterialGeometryDrawSets,
                    .drawItemSetCount = LengthOf(occupancyMaterialGeometryDrawSets),
                    .phase = AvboitGeometryPhase::Occupancy,
                },
                occupancyGeometryPreparationResult
            ))
                return false;
            avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned = occupancyGeometryPreparationResult.geometryOwned;
            occupancyMaterialGeometrySet = occupancyGeometryPreparationResult.materialGeometrySet;
            occupancyMaterialSampledTextureSet = occupancyGeometryPreparationResult.materialSampledTextureSet;
            occupancyMaterialSampledTexturesCollected = occupancyGeometryPreparationResult.sampledTexturesCollected;

            m_materialSystem.prepareMaterialPassInstanceUploadData(occupancyInstanceData, csgResources);
#if defined(NWB_DEBUG)
            if(
                occupancyInstanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
                || occupancyCsgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
                || occupancyCsgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT occupancy upload size overflows graph blob capacity"));
                return false;
            }
            NWB_ASSERT(occupancyInstanceData.size() == occupancyMaterialTypedRanges.size());
            ECSRenderDetail::AssertMaterialTypedUploadRanges(
                occupancyMaterialTypedRanges,
                occupancyMaterialTypedBytes
            );
#endif

            AvboitMaterialUploadBuilder occupancyMaterialUploadBuilder(
                m_graph,
                m_csgSystem
            );
            if(!occupancyMaterialUploadBuilder.declare(
                AvboitMaterialUploadInputs{
                    .targets = &deferredTargets,
                    .csgResources = &csgResources,
                    .frameBindings = &frameBindings,
                    .materialInstances = materialInstances,
                    .materialTyped = materialTyped,
                    .csgReceiverRanges = csgReceiverRanges,
                    .csgCutters = csgCutters,
                    .csgClipContextSlots = csgClipContextSlots,
                    .uploadTask = occupancyUploadTask,
                    .phase = AvboitMaterialUploadPhase::Occupancy,
                },
                occupancyInstanceData,
                occupancyMaterialTypedBytes,
                occupancyCsgFrameData,
                occupancyHasCsgDrawItems,
                occupancyUploadTask,
                occupancyCsgStreamsUploaded
            )){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT occupancy material upload"));
                return false;
            }

            avboitOccupancyPayload.occupancySnapshot.capture(
                occupancyDrawItems,
                occupancyCsgFrameData,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size()
            );
            avboitOccupancyPayload.occupancyPhasePrepared = true;
            avboitOccupancyPayload.occupancyStreamsUploaded = true;
            // A phase owns one alias-free stream; mixed work keeps local interleaving.
            AvboitComputeEmulationCapture occupancyComputeEmulationCapture;
            AvboitComputeEmulationCaptureResult occupancyComputeEmulationCaptureResult;
            if(!occupancyComputeEmulationCapture.capture(
                AvboitComputeEmulationCaptureInputs{
                    .drawItems = &occupancyDrawItems,
                    .csgFrameData = &occupancyCsgFrameData,
                    .geometryOwned = avboitOccupancyPayload.occupancyMaterialGeometryStatesGraphOwned,
                    .sampledTexturesCollected = occupancyMaterialSampledTexturesCollected,
                    .csgStreamsUploaded = occupancyCsgStreamsUploaded,
                    .intervalOutputsGraphOwned = intervalOutputsGraphOwned,
                },
                avboitOccupancyComputeEmulationPayload.plan,
                avboitOccupancyComputeEmulationPayload.csgPlan,
                occupancyUploadScratch,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size(),
                occupancyComputeEmulationCaptureResult
            ))
                return false;
            occupancyRegularComputeEmulationPlanCaptured = occupancyComputeEmulationCaptureResult.regularCaptured;
            occupancyCsgComputeEmulationPlanCaptured = occupancyComputeEmulationCaptureResult.csgCaptured;
            occupancySharedComputeEmulationPlanCaptured = occupancyComputeEmulationCaptureResult.sharedCaptured;
            occupancySharedComputeEmulationPlan = occupancyComputeEmulationCaptureResult.sharedPlan;
            occupancySharedComputeEmulationInstanceCount = occupancyComputeEmulationCaptureResult.sharedInstanceCount;
            occupancySharedComputeEmulationMaterialTypedByteCount = occupancyComputeEmulationCaptureResult.sharedMaterialTypedByteCount;
            if(occupancyRegularComputeEmulationPlanCaptured && generatedGeometry.matches(
                occupancyDrawItems, occupancyInstanceData, frameBindings, meshViewState, MaterialPipelinePass::AvboitOccupancy
            ))
                occupancyReusedGeometryProducer = generatedGeometry.producerTask();
            else{
                generatedGeometry.reset();
                occupancyProducesReusableGeometry = occupancyRegularComputeEmulationPlanCaptured && generatedGeometry.capture(
                    occupancyDrawItems, occupancyInstanceData, frameBindings, meshViewState, MaterialPipelinePass::AvboitOccupancy
                );
            }
        }
        else{
            // Graph phase stays authoritative for empty sets; retain snapshot to skip native re-gather.
            avboitOccupancyPayload.occupancySnapshot.capture(
                occupancyDrawItems,
                occupancyCsgFrameData,
                occupancyInstanceData.size(),
                occupancyMaterialTypedBytes.size()
            );
            avboitOccupancyPayload.occupancyPhasePrepared = true;
        }
    }
    outResult.uploadTask = occupancyUploadTask;
    outResult.materialGeometrySet = occupancyMaterialGeometrySet;
    outResult.materialSampledTextureSet = occupancyMaterialSampledTextureSet;
    outResult.csgStreamsUploaded = occupancyCsgStreamsUploaded;
    outResult.regularComputeEmulationPlanCaptured = occupancyRegularComputeEmulationPlanCaptured;
    outResult.producesReusableGeometry = occupancyProducesReusableGeometry;
    outResult.reusedGeometryProducer = occupancyReusedGeometryProducer;
    outResult.csgComputeEmulationPlanCaptured = occupancyCsgComputeEmulationPlanCaptured;
    outResult.sharedComputeEmulationPlanCaptured = occupancySharedComputeEmulationPlanCaptured;
    outResult.sharedComputeEmulationPlan = occupancySharedComputeEmulationPlan;
    outResult.sharedComputeEmulationInstanceCount = occupancySharedComputeEmulationInstanceCount;
    outResult.sharedComputeEmulationMaterialTypedByteCount = occupancySharedComputeEmulationMaterialTypedByteCount;
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
