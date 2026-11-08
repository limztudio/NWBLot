// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_pass_upload_helper.h"

#include <impl/ecs_render/avboit/compute_emulation_capture.h>
#include <impl/ecs_render/avboit/generated_geometry_reuse.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_system.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AvboitPassUploadResult> AvboitPassUploadHelper::gather(
    const AvboitPassUploadInputs& inputs,
    MaterialPassDrawItemPartitions& drawItems,
    InstanceGpuDataVector& instanceData,
    CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
    MaterialTypedByteDataVector& materialTypedBytes
){
    AvboitPassUploadResult result{};
    m_materialSystem.gatherMaterialPassDrawItems(
        &inputs.framebuffer,
        inputs.pass,
        true,
        inputs.csgFrameState,
        RendererResourceLookupMode::PreparedOnly,
        &inputs.meshViewState,
        drawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    );

    result.hasDrawItems = !drawItems.empty();
    result.hasCsgDrawItems = !drawItems.csg.empty();
    if(!result.hasDrawItems)
        return result;
    const b8 csgReady = !result.hasCsgDrawItems || (
        csgFrameData.hasWork()
        && inputs.csgReceiverRanges.valid()
        && inputs.csgCutters.valid()
        && inputs.csgClipContextSlots.valid()
        && (inputs.pass != MaterialPipelinePass::AvboitExtinction || inputs.csgIntervalSampleState.valid())
        && inputs.csgResources.frameReady(csgFrameData)
        && m_materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems.csg, inputs.frameBindings)
    );
    if(
        !inputs.materialInstances.valid()
        || !inputs.materialTyped.valid()
        || !inputs.frameBindings.frameReady(instanceData.size(), materialTypedBytes.size())
        || !m_materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems.regular, inputs.frameBindings)
        || !csgReady
    )
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphAvboitMaterialUploadResult> FrameGraphAvboitPassUploadChain::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const FrameGraphAvboitPassPhaseDesc& phaseDesc,
    const FrameGraphAvboitPassUploadInputs& inputs,
    AvboitRasterPassTaskPayload& rasterPayload,
    AvboitComputeEmulationTaskPayload& computeEmulationPayload,
    AvboitGeneratedGeometryReuse& generatedGeometry
){
    FrameGraphAvboitMaterialUploadResult result{};
    result.uploadTask = inputs.uploadTask;
    if(!inputs.hasTransparentRenderers)
        return result;

    MaterialPassDrawItemPartitions drawItems{ scratchArena };
    InstanceGpuDataVector instanceData{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{ scratchArena };
#endif
    MaterialTypedByteDataVector materialTypedBytes{ scratchArena };
    AvboitPassUploadHelper uploadHelper(m_materialSystem);
    const auto uploadResult = uploadHelper.gather(
        AvboitPassUploadInputs{
            .framebuffer = phaseDesc.framebuffer,
            .pass = phaseDesc.pass,
            .csgFrameState = inputs.csgFrameState,
            .frameBindings = inputs.frameBindings,
            .csgResources = inputs.csgResources,
            .meshViewState = inputs.meshViewState,
            .materialInstances = inputs.materialInstances,
            .materialTyped = inputs.materialTyped,
            .csgReceiverRanges = inputs.csgReceiverRanges,
            .csgCutters = inputs.csgCutters,
            .csgClipContextSlots = inputs.csgClipContextSlots,
            .csgIntervalSampleState = inputs.csgIntervalSampleState,
        },
        drawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    );
    if(!uploadResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: prepared AVBOIT pass resources were unavailable during graph declaration"));
        return MakeUnexpected(Failure{});
    }

    const b8 hasCsgDrawItems = uploadResult->hasCsgDrawItems;
    if(hasCsgDrawItems && !inputs.intervalOutputsGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: clipped AVBOIT draws require prepared interval outputs"));
        return MakeUnexpected(Failure{});
    }
    if(!uploadResult->hasDrawItems){
        rasterPayload.snapshot.capture(drawItems, csgFrameData, instanceData.size(), materialTypedBytes.size());
        rasterPayload.phasePrepared = true;
        return result;
    }

    const Array<MaterialPassDrawItemsView, 2u> materialGeometryDrawSets = { drawItems.regular, drawItems.csg, };
    AvboitGeometryPreparationBuilder geometryPreparationBuilder(m_graph, m_materialSystem, scratchArena);
    const auto geometryPreparationResult = geometryPreparationBuilder.declare(AvboitGeometryPreparationInputs{
        .drawItemSets = materialGeometryDrawSets,
        .phase = phaseDesc.phase,
    });
    if(!geometryPreparationResult)
        return MakeUnexpected(Failure{});
    rasterPayload.materialGeometryStatesGraphOwned = geometryPreparationResult->geometryOwned;
    result.materialGeometrySet = geometryPreparationResult->materialGeometrySet;
    result.materialSampledTextureSet = geometryPreparationResult->materialSampledTextureSet;
    const b8 sampledTexturesCollected = geometryPreparationResult->sampledTexturesCollected;

    m_materialSystem.prepareMaterialPassInstanceUploadData(inputs.csgResources, instanceData);
#if defined(NWB_DEBUG)
    if(
        instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
        || csgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
        || csgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT upload size overflows graph blob capacity"));
        return MakeUnexpected(Failure{});
    }
    NWB_ASSERT(instanceData.size() == materialTypedRanges.size());
    ValidateMaterialTypedUploadRanges(materialTypedRanges, materialTypedBytes);
#endif

    AvboitMaterialUploadBuilder materialUploadBuilder(m_graph, m_csgSystem);
    const auto csgStreamsUploadedResult = materialUploadBuilder.declare(
        AvboitMaterialUploadInputs{
            .targets = inputs.targets,
            .csgResources = inputs.csgResources,
            .frameBindings = inputs.frameBindings,
            .materialInstances = inputs.materialInstances,
            .materialTyped = inputs.materialTyped,
            .csgReceiverRanges = inputs.csgReceiverRanges,
            .csgCutters = inputs.csgCutters,
            .csgClipContextSlots = inputs.csgClipContextSlots,
            .uploadTask = result.uploadTask,
            .phase = phaseDesc.phase,
        },
        instanceData,
        materialTypedBytes,
        csgFrameData,
        hasCsgDrawItems,
        result.uploadTask
    );
    if(!csgStreamsUploadedResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT material upload"));
        return MakeUnexpected(Failure{});
    }
    result.csgStreamsUploaded = *csgStreamsUploadedResult;

    rasterPayload.snapshot.capture(drawItems, csgFrameData, instanceData.size(), materialTypedBytes.size());
    rasterPayload.phasePrepared = true;
    rasterPayload.streamsUploaded = true;
    result.streamsUploaded = true;

    AvboitComputeEmulationCapture computeEmulationCapture;
    const auto computeEmulationCaptureResult = computeEmulationCapture.capture(
        AvboitComputeEmulationCaptureInputs{
            .drawItems = drawItems,
            .csgFrameData = csgFrameData,
            .geometryOwned = rasterPayload.materialGeometryStatesGraphOwned,
            .sampledTexturesCollected = sampledTexturesCollected,
            .csgStreamsUploaded = result.csgStreamsUploaded,
            .intervalOutputsGraphOwned = inputs.intervalOutputsGraphOwned,
        },
        instanceData.size(),
        materialTypedBytes.size(),
        scratchArena,
        computeEmulationPayload.plan,
        computeEmulationPayload.csgPlan
    );
    if(!computeEmulationCaptureResult)
        return MakeUnexpected(Failure{});
    result.regularComputeEmulationPlanCaptured = computeEmulationCaptureResult->regularCaptured;
    result.csgComputeEmulationPlanCaptured = computeEmulationCaptureResult->csgCaptured;
    result.sharedComputeEmulationPlanCaptured = computeEmulationCaptureResult->sharedCaptured;
    result.sharedComputeEmulationPlan = computeEmulationCaptureResult->sharedPlan;
    result.sharedComputeEmulationInstanceCount = computeEmulationCaptureResult->sharedInstanceCount;
    result.sharedComputeEmulationMaterialTypedByteCount = computeEmulationCaptureResult->sharedMaterialTypedByteCount;
    if(result.regularComputeEmulationPlanCaptured && generatedGeometry.matches(drawItems, instanceData, inputs.frameBindings, inputs.meshViewState, phaseDesc.pass))
        result.reusedGeometryProducer = generatedGeometry.producerTask();
    else{
        generatedGeometry.reset();
        result.producesReusableGeometry = result.regularComputeEmulationPlanCaptured && generatedGeometry.capture(drawItems, instanceData, inputs.frameBindings, inputs.meshViewState, phaseDesc.pass);
    }
    if(result.regularComputeEmulationPlanCaptured || result.csgComputeEmulationPlanCaptured){
        computeEmulationPayload.instanceCount = instanceData.size();
        computeEmulationPayload.materialTypedByteCount = materialTypedBytes.size();
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
