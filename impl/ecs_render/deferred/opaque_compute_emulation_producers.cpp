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
    Core::Alloc::ScratchArena& scratchArena,
    const OpaqueComputeEmulationProducerInputs& inputs,
    const Core::GpuTaskResourceSetUse& outputUavSetUse,
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphTask::Payload& payload,
    const Core::GpuTaskId& dependency
){
    using namespace RendererTaskGraphDetail;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    // G-buffer's semantic timing ticket spans its preparatory compute half and its raster half in one packet.
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;

    constexpr usize s_OpaqueComputeEmulationResourceUseCapacity = 3u;
    resourceUses.reserve(s_OpaqueComputeEmulationResourceUseCapacity);
    resourceUses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange));
    resourceUses.push_back(ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange));
    Core::GpuTaskResourceSetUse opaqueComputeEmulationResourceSetUses[3u] = {};
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
    // This explicit immediate successor must stay with the preceding Graphics prefix work: CSG clear timing and the G-buffer range require one command list under FrontierSafe packetization.
    opaqueComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc opaqueComputeEmulationDesc;
    opaqueComputeEmulationDesc
        .setIdentity(Name("render.graphics_prefix.opaque_compute_emulation"))
        .setMarkerLabel("Opaque Compute Emulation")
        .setScheduling(opaqueComputeEmulationScheduling)
        .setDependencies(&dependency, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
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
    Core::Alloc::ScratchArena& scratchArena,
    const OpaqueComputeEmulationProducerInputs& inputs,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const Core::GpuTaskResourceSetUse& outputUavSetUse,
    ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask::Payload& payload,
    const Core::GpuTaskId& dependency
){
    using namespace RendererTaskGraphDetail;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    payload.csgResources = csgResources;
    // The receiver producer is an early part of G-buffer's one Graphics submission; its dispatch timing shares the existing ticket while the receiver raster keeps its own scope in G-buffer.
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.csgFrameBuffersUploaded = inputs.csgFrameBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;

    constexpr usize s_OpaqueCsgReceiverComputeEmulationCapacity = 8u;
    resourceUses.reserve(s_OpaqueCsgReceiverComputeEmulationCapacity);
    resourceUses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange));
    resourceUses.push_back(ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange));
    // The compute pipeline shares CSG's clip context. It does not write receiver-event images; those stay G-buffer raster outputs absent from this producer's uses.
    resourceUses.push_back(ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange));
    resourceUses.push_back(ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange));
    resourceUses.push_back(ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    Core::GpuTaskResourceSetUse opaqueCsgReceiverComputeEmulationResourceSetUses[3u] = {};
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
    // This successor stays with G-buffer under FrontierSafe so its compiler-owned output boundary, existing prefix timing ticket, and accepted range all remain one primary Graphics packet.
    opaqueCsgReceiverComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc opaqueCsgReceiverComputeEmulationDesc;
    opaqueCsgReceiverComputeEmulationDesc
        .setIdentity(Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation"))
        .setMarkerLabel("Opaque CSG Receiver Compute Emulation")
        .setScheduling(opaqueCsgReceiverComputeEmulationScheduling)
        .setDependencies(&dependency, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(opaqueCsgReceiverComputeEmulationResourceSetUses, opaqueCsgReceiverComputeEmulationResourceSetUseCount)
    ;
    const auto taskResult = m_graph.addTask< ECSRenderDetail::OpaqueCsgReceiverComputeEmulationGraphTask >(
        opaqueCsgReceiverComputeEmulationDesc,
        Move(payload)
    );
    if(!taskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver compute-emulation producer"));
        return MakeUnexpected(Failure{});
    }
    return *taskResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

