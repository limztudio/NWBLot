// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/task_graph.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_setup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_SetupBufferResourceIdentity("graphics.setup_buffer.resource");
inline constexpr Name s_SetupBufferUploadIdentity("graphics.setup_buffer.upload");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferSetupSubmissionData{
    BufferHandle& buffer;
    const GraphicsRuntime::BufferSetupDesc& setupDesc;
    const BufferDesc& uploadDesc;
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting;
    QueueSubmissionToken& uploadToken;
    GpuPhysicalQueueId directConsumerQueue;
};

[[nodiscard]] static Expected<GpuTaskId> DeclareBufferSetupUpload(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *MakeNotNull(static_cast<BufferSetupSubmissionData*>(userData));
    const auto destinationResult = graph.importBuffer(
        submissionData.buffer,
        GpuGraphResourceDesc{}
            .setIdentity(s_SetupBufferResourceIdentity)
            .setMarkerLabel("Setup Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(submissionData.uploadDesc.initialState)
            .setQueueSharing(submissionData.uploadDesc.queueSharing)
            .setDirectConsumerQueue(submissionData.directConsumerQueue)
    );
    const auto sourceResult = graph.copyUploadData(submissionData.setupDesc.data, submissionData.setupDesc.dataSize, alignof(u32));
    if(!destinationResult || !sourceResult)
        return MakeUnexpected(Failure{});
    const GpuGraphResourceId destination = *destinationResult;
    const GpuUploadBlobId source = *sourceResult;

    GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(s_SetupBufferUploadIdentity)
        .setMarkerLabel("Setup Buffer Upload")
        .setScheduling(GraphicsModuleDetail::SetupUploadGraphScheduling(submissionData.setupDesc.dataSize, submissionData.sameClassRouting.enabled, submissionData.sameClassRouting.crossesQueueFamily))
    ;
    const auto taskResult = graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destination,
            .destinationOffsetBytes = submissionData.setupDesc.destOffsetBytes,
            .finalState = GraphicsModuleDetail::SetupUploadGraphFinalState(submissionData.uploadDesc.initialState),
            .acceptedToken = &submissionData.uploadToken,
        }
    );
    if(!taskResult)
        return MakeUnexpected(Failure{});
    const GpuTaskId task = *taskResult;
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateBufferSetupUpload(const GraphicsRuntime::BufferSetupDesc& desc){
    if(desc.dataSize == 0)
        return true;
    if(!desc.data){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up buffer '{}': upload data is null"), ConvertString(desc.bufferDesc.debugName.resolvedText()));
        return false;
    }
    if(desc.destOffsetBytes > desc.bufferDesc.byteSize || static_cast<u64>(desc.dataSize) > desc.bufferDesc.byteSize - desc.destOffsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up buffer '{}': upload range offset {} size {} exceeds buffer size {}")
            , ConvertString(desc.bufferDesc.debugName.resolvedText())
            , desc.destOffsetBytes
            , static_cast<u64>(desc.dataSize)
            , desc.bufferDesc.byteSize
        );
        return false;
    }
    // CommandList staging and graph-owned upload tasks both lower to VkBufferCopy.
    // Fail before creating a list or packet: the API cannot report success for a region Vulkan rejects.
    if((desc.destOffsetBytes & (sizeof(u32) - 1u)) != 0u || (desc.dataSize & (sizeof(u32) - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up buffer '{}': upload offset and size must be 4-byte aligned"), ConvertString(desc.bufferDesc.debugName.resolvedText()));
        return false;
    }
    // A retained buffer needs a concrete state; Unknown restores at close with no graph-visible contract for the next consumer.
    if(desc.bufferDesc.keepInitialState && desc.bufferDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up buffer '{}': keep-initial-state uploads require a concrete initial state"), ConvertString(desc.bufferDesc.debugName.resolvedText()));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<BufferHandle> GraphicsRuntime::setupBuffer(const BufferSetupDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};
    if(!GraphicsModuleDetail::ValidateBufferSetupUpload(desc))
        return MakeUnexpected(Failure{});

    if(desc.dataSize == 0){
        auto buffer = device.createBuffer(desc.bufferDesc);
        if(!buffer)
            return MakeUnexpected(Failure{});
        return buffer;
    }

    const CommandQueue::Enum consumerQueue = GraphicsModuleDetail::ResolveSetupUploadConsumerQueue(device, desc.queue, desc.dataSize, desc.bufferDesc.initialState != ResourceStates::Unknown);
    const GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting = GraphicsModuleDetail::ResolveSetupUploadSameClassRouting(device, consumerQueue, desc.dataSize);
    BufferDesc uploadDesc = desc.bufferDesc;
    uploadDesc.queueSharing = GraphicsModuleDetail::ResolveSetupUploadConsumerSharing(uploadDesc.queueSharing, consumerQueue, sameClassRouting.crossesQueueFamily);
    auto bufferResult = device.createBuffer(uploadDesc);
    if(!bufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to create setup buffer '{}'"), ConvertString(desc.bufferDesc.debugName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    BufferHandle buffer = Move(*bufferResult);
    QueueSubmissionToken uploadToken;
    __hidden_graphics_setup::BufferSetupSubmissionData submissionData{
        .buffer = buffer,
        .setupDesc = desc,
        .uploadDesc = uploadDesc,
        .sameClassRouting = sameClassRouting,
        .uploadToken = uploadToken,
        .directConsumerQueue = device.getPrimaryPhysicalQueue(consumerQueue),
    };
    const b8 submitted = GraphicsModuleDetail::SubmitGraphOwnedSetupUpload(
        uploadToken,
        *m_standaloneGraphWorkspaces,
        *this,
        uploadDesc.queueSharing,
        consumerQueue,
        &submissionData,
        MakeNotNull(&__hidden_graphics_setup::DeclareBufferSetupUpload),
        sameClassRouting.enabled ? sameClassRouting.primaryQueue : GpuPhysicalQueueId{}
    );
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to submit graph-owned setup buffer upload '{}'"), ConvertString(desc.bufferDesc.debugName.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;

    return buffer;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

