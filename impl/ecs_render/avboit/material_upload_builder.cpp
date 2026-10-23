// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_upload_builder.h"

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitMaterialUploadBuilder::AvboitMaterialUploadBuilder(Core::GpuTaskGraph& graph, RendererCsgSystem& csgSystem)noexcept
    : m_graph(graph)
    , m_csgSystem(csgSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_material_upload_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UploadIdentities{
    NameText<char> instanceIdentity;
    AStringView instanceLabel;
    NameText<char> typedIdentity;
    AStringView typedLabel;
    NameText<char> receiverIdentity;
    AStringView receiverLabel;
    NameText<char> cutterIdentity;
    AStringView cutterLabel;
    NameText<char> clipIdentity;
    AStringView clipLabel;
};


[[nodiscard]] UploadIdentities IdentitiesForPhase(AvboitMaterialUploadPhase::Enum phase)noexcept{
    switch(phase){
    case AvboitMaterialUploadPhase::Extinction:
        return UploadIdentities{
            .instanceIdentity = "render.avboit.extinction.material_instances_upload",
            .instanceLabel = "AVBOIT Extinction Material Instances Upload",
            .typedIdentity = "render.avboit.extinction.material_typed_upload",
            .typedLabel = "AVBOIT Extinction Material Typed Upload",
            .receiverIdentity = "render.avboit.extinction.csg_receiver_ranges_upload",
            .receiverLabel = "AVBOIT Extinction CSG Receiver Ranges Upload",
            .cutterIdentity = "render.avboit.extinction.csg_cutters_upload",
            .cutterLabel = "AVBOIT Extinction CSG Cutters Upload",
            .clipIdentity = "render.avboit.extinction.csg_clip_context_slots_upload",
            .clipLabel = "AVBOIT Extinction CSG Clip Context Slots Upload",
        };
    case AvboitMaterialUploadPhase::Accumulation:
        return UploadIdentities{
            .instanceIdentity = "render.avboit.accumulation.material_instances_upload",
            .instanceLabel = "AVBOIT Accumulation Material Instances Upload",
            .typedIdentity = "render.avboit.accumulation.material_typed_upload",
            .typedLabel = "AVBOIT Accumulation Material Typed Upload",
            .receiverIdentity = "render.avboit.accumulation.csg_receiver_ranges_upload",
            .receiverLabel = "AVBOIT Accumulation CSG Receiver Ranges Upload",
            .cutterIdentity = "render.avboit.accumulation.csg_cutters_upload",
            .cutterLabel = "AVBOIT Accumulation CSG Cutters Upload",
            .clipIdentity = "render.avboit.accumulation.csg_clip_context_slots_upload",
            .clipLabel = "AVBOIT Accumulation CSG Clip Context Slots Upload",
        };
    case AvboitMaterialUploadPhase::Occupancy:
    default:
        return UploadIdentities{
            .instanceIdentity = "render.avboit.occupancy.material_instances_upload",
            .instanceLabel = "AVBOIT Occupancy Material Instances Upload",
            .typedIdentity = "render.avboit.occupancy.material_typed_upload",
            .typedLabel = "AVBOIT Occupancy Material Typed Upload",
            .receiverIdentity = "render.avboit.occupancy.csg_receiver_ranges_upload",
            .receiverLabel = "AVBOIT Occupancy CSG Receiver Ranges Upload",
            .cutterIdentity = "render.avboit.occupancy.csg_cutters_upload",
            .cutterLabel = "AVBOIT Occupancy CSG Cutters Upload",
            .clipIdentity = "render.avboit.occupancy.csg_clip_context_slots_upload",
            .clipLabel = "AVBOIT Occupancy CSG Clip Context Slots Upload",
        };
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<bool> AvboitMaterialUploadBuilder::declare(
    const AvboitMaterialUploadInputs& inputs,
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes,
    const CsgFrameGpuData& csgFrameData,
    bool hasCsgDrawItems,
    Core::GpuTaskId& inOutUploadTask
){
    if(!inOutUploadTask.valid())
        return MakeUnexpected(Failure{});
    if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData) || csgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData) || csgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT upload size overflows graph blob capacity"));
        return MakeUnexpected(Failure{});
    }

    const __hidden_avboit_material_upload_builder::UploadIdentities identities = __hidden_avboit_material_upload_builder::IdentitiesForPhase(inputs.phase);
    const auto instanceBlobResult = m_graph.copyUploadData(instanceData.data(), instanceData.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData));
    const auto materialTypedBlobResult = m_graph.copyUploadData(materialTypedBytes.data(), materialTypedBytes.size(), alignof(u32));
    if(!instanceBlobResult || !materialTypedBlobResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT material upload data"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuUploadBlobId instanceBlob = *instanceBlobResult;
    const Core::GpuUploadBlobId materialTypedBlob = *materialTypedBlobResult;

    Core::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    uploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskDesc instanceUploadDesc;
    instanceUploadDesc.setIdentity(Name(identities.instanceIdentity)).setMarkerLabel(identities.instanceLabel).setScheduling(uploadScheduling).setDependencies(&inOutUploadTask, 1u);
    const auto inOutUploadTaskResult = m_graph.addUploadBufferTask(instanceUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = instanceBlob, .destination = inputs.materialInstances, .finalState = Core::ResourceStates::Common, });
    if(!inOutUploadTaskResult){
        inOutUploadTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT material instance upload"));
        return MakeUnexpected(Failure{});
    }
    inOutUploadTask = *inOutUploadTaskResult;

    Core::GpuTaskDesc materialTypedUploadDesc;
    materialTypedUploadDesc.setIdentity(Name(identities.typedIdentity)).setMarkerLabel(identities.typedLabel).setScheduling(uploadScheduling).setDependencies(&inOutUploadTask, 1u);
    const auto inOutUploadTaskResult2 = m_graph.addUploadBufferTask(materialTypedUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = materialTypedBlob, .destination = inputs.materialTyped, .finalState = Core::ResourceStates::Common, });
    if(!inOutUploadTaskResult2){
        inOutUploadTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT material typed upload"));
        return MakeUnexpected(Failure{});
    }
    inOutUploadTask = *inOutUploadTaskResult2;

    if(!hasCsgDrawItems)
        return false;

    const auto clipContextSlotData = m_csgSystem.prepareCsgClipContextSlotData(inputs.targets, csgFrameData, inputs.csgResources, inputs.frameBindings);
    if(!clipContextSlotData){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT CSG context data"));
        return MakeUnexpected(Failure{});
    }
    const auto receiverRangesBlobResult = m_graph.copyUploadData(csgFrameData.receiverRanges.data(), csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData));
    const auto cuttersBlobResult = m_graph.copyUploadData(csgFrameData.cutters.data(), csgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData));
    const auto clipContextSlotsBlobResult = m_graph.copyUploadData(&*clipContextSlotData, sizeof(*clipContextSlotData), alignof(CsgClipContextSlots));
    if(!receiverRangesBlobResult || !cuttersBlobResult || !clipContextSlotsBlobResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT CSG upload data"));
        return MakeUnexpected(Failure{});
    }
    const Core::GpuUploadBlobId receiverRangesBlob = *receiverRangesBlobResult;
    const Core::GpuUploadBlobId cuttersBlob = *cuttersBlobResult;
    const Core::GpuUploadBlobId clipContextSlotsBlob = *clipContextSlotsBlobResult;

    Core::GpuTaskDesc receiverRangesUploadDesc;
    receiverRangesUploadDesc.setIdentity(Name(identities.receiverIdentity)).setMarkerLabel(identities.receiverLabel).setScheduling(uploadScheduling).setDependencies(&inOutUploadTask, 1u);
    const auto inOutUploadTaskResult3 = m_graph.addUploadBufferTask(receiverRangesUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = receiverRangesBlob, .destination = inputs.csgReceiverRanges, .finalState = Core::ResourceStates::Common, });
    if(!inOutUploadTaskResult3){
        inOutUploadTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT CSG receiver-range upload"));
        return MakeUnexpected(Failure{});
    }
    inOutUploadTask = *inOutUploadTaskResult3;

    Core::GpuTaskDesc cuttersUploadDesc;
    cuttersUploadDesc.setIdentity(Name(identities.cutterIdentity)).setMarkerLabel(identities.cutterLabel).setScheduling(uploadScheduling).setDependencies(&inOutUploadTask, 1u);
    const auto inOutUploadTaskResult4 = m_graph.addUploadBufferTask(cuttersUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = cuttersBlob, .destination = inputs.csgCutters, .finalState = Core::ResourceStates::Common, });
    if(!inOutUploadTaskResult4){
        inOutUploadTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT CSG cutter upload"));
        return MakeUnexpected(Failure{});
    }
    inOutUploadTask = *inOutUploadTaskResult4;

    Core::GpuTaskDesc clipContextSlotsUploadDesc;
    clipContextSlotsUploadDesc.setIdentity(Name(identities.clipIdentity)).setMarkerLabel(identities.clipLabel).setScheduling(uploadScheduling).setDependencies(&inOutUploadTask, 1u);
    const auto inOutUploadTaskResult5 = m_graph.addUploadBufferTask(clipContextSlotsUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = clipContextSlotsBlob, .destination = inputs.csgClipContextSlots, .finalState = Core::ResourceStates::Common, });
    if(!inOutUploadTaskResult5){
        inOutUploadTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT CSG clip-context upload"));
        return MakeUnexpected(Failure{});
    }
    inOutUploadTask = *inOutUploadTaskResult5;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

