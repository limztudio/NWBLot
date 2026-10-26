// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_compute_emulation_producers.h"

#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> OpaqueComputeEmulationProducerBuilder::declareRegular(
    const OpaqueComputeEmulationProducerInputs& inputs,
    const Core::GpuTaskResourceSetUse& outputUavSetUse,
    const Core::GpuTaskId& dependency,
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask::Payload& payload
){
    using namespace RendererTaskGraphDetail;

    // G-buffer's semantic timing ticket spans its preparatory compute half and its raster half in one packet.
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer),
        ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange),
        ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange),
    };
    constexpr usize s_ProducerResourceSetUseCapacity = 3u;
    Core::GpuTaskResourceSetUse opaqueComputeEmulationResourceSetUses[s_ProducerResourceSetUseCapacity] = {};
    usize opaqueComputeEmulationResourceSetUseCount = 0u;
    opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = inputs.materialGeometrySetUse;
    if(inputs.materialSampledTextureSetUse.resourceSet.valid()){
        opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = inputs.materialSampledTextureSetUse;
    }
    opaqueComputeEmulationResourceSetUses[opaqueComputeEmulationResourceSetUseCount++] = outputUavSetUse;
    Core::GpuTaskSchedulingHint opaqueComputeEmulationScheduling;
    opaqueComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
    opaqueComputeEmulationScheduling.forceSubmissionBoundary = false;
    opaqueComputeEmulationScheduling.allowPacketMerge = true;
    opaqueComputeEmulationScheduling.mergeWithPrevious = true;
    // Stays with the Graphics prefix: CSG-clear timing and the G-buffer range need one list under FrontierSafe.
    opaqueComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc opaqueComputeEmulationDesc;
    opaqueComputeEmulationDesc
        .setIdentity(Name("render.graphics_prefix.opaque_compute_emulation"))
        .setMarkerLabel("Opaque Compute Emulation")
        .setScheduling(opaqueComputeEmulationScheduling)
        .setDependencies(&dependency, 1u)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
        .setResourceSetUses(opaqueComputeEmulationResourceSetUses, opaqueComputeEmulationResourceSetUseCount)
    ;
    const auto taskResult = m_graph.addTask< ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask >(opaqueComputeEmulationDesc, Move(payload));
    if(!taskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque compute-emulation producer"));
        return MakeUnexpected(Failure{});
    }
    return *taskResult;
}

Expected<Core::GpuTaskId> OpaqueComputeEmulationProducerBuilder::declareCsgReceiver(
    const OpaqueComputeEmulationProducerInputs& inputs,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const Core::GpuTaskResourceSetUse& outputUavSetUse,
    const Core::GpuTaskId& dependency,
    ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask::Payload& payload
){
    using namespace RendererTaskGraphDetail;

    payload.csgResources = csgResources;
    // This producer is early G-buffer submission; dispatch shares its ticket while receiver raster keeps its own scope.
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.csgFrameBuffersUploaded = inputs.csgFrameBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;

    // The compute pipeline shares CSG's clip context. Receiver-event images stay G-buffer raster outputs, absent here.
    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer),
        ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange),
        ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange),
        ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange),
        ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange),
        ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer),
        ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer),
        ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    constexpr usize s_ProducerResourceSetUseCapacity = 3u;
    Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationResourceSetUses[s_ProducerResourceSetUseCapacity] = {};
    usize opaqueCsgReceiverComputeEmulationResourceSetUseCount = 0u;
    opaqueCsgReceiverComputeEmulationResourceSetUses[opaqueCsgReceiverComputeEmulationResourceSetUseCount++] = inputs.materialGeometrySetUse;
    if(inputs.materialSampledTextureSetUse.resourceSet.valid()){
        opaqueCsgReceiverComputeEmulationResourceSetUses[opaqueCsgReceiverComputeEmulationResourceSetUseCount++] = inputs.materialSampledTextureSetUse;
    }
    opaqueCsgReceiverComputeEmulationResourceSetUses[opaqueCsgReceiverComputeEmulationResourceSetUseCount++] = outputUavSetUse;
    Core::GpuTaskSchedulingHint opaqueCsgReceiverComputeEmulationScheduling;
    opaqueCsgReceiverComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
    opaqueCsgReceiverComputeEmulationScheduling.forceSubmissionBoundary = false;
    opaqueCsgReceiverComputeEmulationScheduling.allowPacketMerge = true;
    opaqueCsgReceiverComputeEmulationScheduling.mergeWithPrevious = true;
    // Stays with G-buffer under FrontierSafe: output boundary, timing ticket, and range stay one Graphics packet.
    opaqueCsgReceiverComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc opaqueCsgReceiverComputeEmulationDesc;
    opaqueCsgReceiverComputeEmulationDesc
        .setIdentity(Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation"))
        .setMarkerLabel("Opaque CSG Receiver Compute Emulation")
        .setScheduling(opaqueCsgReceiverComputeEmulationScheduling)
        .setDependencies(&dependency, 1u)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
        .setResourceSetUses(opaqueCsgReceiverComputeEmulationResourceSetUses, opaqueCsgReceiverComputeEmulationResourceSetUseCount)
    ;
    const auto taskResult = m_graph.addTask< ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask >(opaqueCsgReceiverComputeEmulationDesc, Move(payload));
    if(!taskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver compute-emulation producer"));
        return MakeUnexpected(Failure{});
    }
    return *taskResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

