// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/material_upload_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitMaterialUploadBuilder::AvboitMaterialUploadBuilder(
    Core::GpuTaskGraph& graph,
    RendererCsgSystem& csgSystem
)
    : m_graph(graph)
    , m_csgSystem(csgSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_upload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UploadIdentities{
    const char* instanceIdentity;
    const char* instanceLabel;
    const char* typedIdentity;
    const char* typedLabel;
    const char* receiverIdentity;
    const char* receiverLabel;
    const char* cutterIdentity;
    const char* cutterLabel;
    const char* clipIdentity;
    const char* clipLabel;
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


[[nodiscard]] bool AvboitMaterialUploadBuilder::declare(
    const AvboitMaterialUploadInputs& inputs,
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes,
    const CsgFrameGpuData& csgFrameData,
    bool hasCsgDrawItems,
    Core::GpuTaskId& inOutUploadTask,
    bool& outCsgStreamsUploaded
){
    using namespace RendererTaskGraphDetail;
    outCsgStreamsUploaded = false;
    if(
        !inputs.targets
        || !inputs.csgResources
        || !inputs.frameBindings
        || !inOutUploadTask.valid()
    )
        return false;
    if(
        instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)
        || csgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
        || csgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: AVBOIT upload size overflows graph blob capacity"));
        return false;
    }

    const UploadIdentities identities = __hidden_material_upload::IdentitiesForPhase(inputs.phase);
    const Core::GpuUploadBlobId instanceBlob = m_graph.copyUploadData(
        instanceData.data(),
        instanceData.size() * sizeof(InstanceGpuData),
        alignof(InstanceGpuData)
    );
    const Core::GpuUploadBlobId materialTypedBlob = m_graph.copyUploadData(
        materialTypedBytes.data(),
        materialTypedBytes.size(),
        alignof(u32)
    );
    if(!instanceBlob.valid() || !materialTypedBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT material upload data"));
        return false;
    }

    Core::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    uploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskDesc instanceUploadDesc;
    instanceUploadDesc
        .setIdentity(Name(identities.instanceIdentity))
        .setMarkerLabel(identities.instanceLabel)
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(uploadScheduling)
        .setDependencies(&inOutUploadTask, 1u)
    ;
    inOutUploadTask = m_graph.addUploadBufferTask(
        instanceUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = instanceBlob,
            .destination = inputs.materialInstances,
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!inOutUploadTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT material instance upload"));
        return false;
    }

    Core::GpuTaskDesc materialTypedUploadDesc;
    materialTypedUploadDesc
        .setIdentity(Name(identities.typedIdentity))
        .setMarkerLabel(identities.typedLabel)
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(uploadScheduling)
        .setDependencies(&inOutUploadTask, 1u)
    ;
    inOutUploadTask = m_graph.addUploadBufferTask(
        materialTypedUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = materialTypedBlob,
            .destination = inputs.materialTyped,
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!inOutUploadTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT material typed upload"));
        return false;
    }

    if(!hasCsgDrawItems)
        return true;

    CsgClipContextSlots clipContextSlotData;
    if(!m_csgSystem.prepareCsgClipContextSlotData(
        *inputs.targets,
        csgFrameData,
        *inputs.csgResources,
        *inputs.frameBindings,
        clipContextSlotData
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot AVBOIT CSG context data"));
        return false;
    }
    const Core::GpuUploadBlobId receiverRangesBlob = m_graph.copyUploadData(
        csgFrameData.receiverRanges.data(),
        csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
        alignof(CsgReceiverRangeGpuData)
    );
    const Core::GpuUploadBlobId cuttersBlob = m_graph.copyUploadData(
        csgFrameData.cutters.data(),
        csgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
        alignof(CsgCutterGpuData)
    );
    const Core::GpuUploadBlobId clipContextSlotsBlob = m_graph.copyUploadData(
        &clipContextSlotData,
        sizeof(clipContextSlotData),
        alignof(CsgClipContextSlots)
    );
    if(
        !receiverRangesBlob.valid()
        || !cuttersBlob.valid()
        || !clipContextSlotsBlob.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain immutable AVBOIT CSG upload data"));
        return false;
    }

    Core::GpuTaskDesc receiverRangesUploadDesc;
    receiverRangesUploadDesc
        .setIdentity(Name(identities.receiverIdentity))
        .setMarkerLabel(identities.receiverLabel)
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(uploadScheduling)
        .setDependencies(&inOutUploadTask, 1u)
    ;
    inOutUploadTask = m_graph.addUploadBufferTask(
        receiverRangesUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = receiverRangesBlob,
            .destination = inputs.csgReceiverRanges,
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!inOutUploadTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT CSG receiver-range upload"));
        return false;
    }

    Core::GpuTaskDesc cuttersUploadDesc;
    cuttersUploadDesc
        .setIdentity(Name(identities.cutterIdentity))
        .setMarkerLabel(identities.cutterLabel)
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(uploadScheduling)
        .setDependencies(&inOutUploadTask, 1u)
    ;
    inOutUploadTask = m_graph.addUploadBufferTask(
        cuttersUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = cuttersBlob,
            .destination = inputs.csgCutters,
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!inOutUploadTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT CSG cutter upload"));
        return false;
    }

    Core::GpuTaskDesc clipContextSlotsUploadDesc;
    clipContextSlotsUploadDesc
        .setIdentity(Name(identities.clipIdentity))
        .setMarkerLabel(identities.clipLabel)
        .setQueue(GraphicsUploadQueueRequest())
        .setScheduling(uploadScheduling)
        .setDependencies(&inOutUploadTask, 1u)
    ;
    inOutUploadTask = m_graph.addUploadBufferTask(
        clipContextSlotsUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = clipContextSlotsBlob,
            .destination = inputs.csgClipContextSlots,
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!inOutUploadTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT CSG clip-context upload"));
        return false;
    }
    outCsgStreamsUploaded = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
