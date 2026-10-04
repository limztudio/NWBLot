// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/task_graph.h>
#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_texture_upload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_UploadTextureBatchResourceIdentity("graphics.upload_texture_batch.resource");
inline constexpr Name s_UploadTextureBatchUploadIdentity("graphics.upload_texture_batch.upload");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool TextureUploadRequiresGraphicsConsumerQueue(const TextureDesc& textureDesc)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    return formatInfo.hasDepth || formatInfo.hasStencil;
}


[[nodiscard]] static bool ValidateTextureUploadBatch(
    const GraphicsRuntime::TextureUploadBatchDesc& desc,
    usize& outTotalByteCount
){
    outTotalByteCount = 0u;
    if(!desc.destination){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch: destination texture is null"));
        return false;
    }
    if(!desc.regions || desc.regionCount == 0u){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': regions are empty")
            , StringConvert(desc.destination->getCreationDescription().name.resolvedText())
        );
        return false;
    }
    if(desc.finalState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': final state is unknown")
            , StringConvert(desc.destination->getCreationDescription().name.resolvedText())
        );
        return false;
    }

    const TextureDesc& textureDesc = desc.destination->getCreationDescription();
    if(textureDesc.keepInitialState && textureDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': keep-initial-state uploads require a concrete initial state")
            , StringConvert(textureDesc.name.resolvedText())
        );
        return false;
    }
    if(static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::kCount)){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': texture format is invalid")
            , StringConvert(textureDesc.name.resolvedText())
        );
        return false;
    }
    if(textureDesc.keepInitialState && desc.finalState != textureDesc.initialState){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': keep-initial-state requires final state {}")
            , StringConvert(textureDesc.name.resolvedText())
            , static_cast<u32>(textureDesc.initialState)
        );
        return false;
    }
    if(
        textureDesc.keepInitialState
        && desc.hasPhysicalInitialState
        && desc.physicalInitialState != ResourceStates::Unknown
        && desc.physicalInitialState != textureDesc.initialState
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': retained textures require their declared physical initial state to match initialState")
            , StringConvert(textureDesc.name.resolvedText())
        );
        return false;
    }

    for(usize regionIndex = 0u; regionIndex < desc.regionCount; ++regionIndex){
        const GraphicsRuntime::TextureUploadRegion& region = desc.regions[regionIndex];
        GraphicsRuntime::TextureSetupDesc regionDesc;
        regionDesc.textureDesc = textureDesc;
        regionDesc.data = region.data;
        regionDesc.uploadDataSize = region.dataSize;
        regionDesc.rowPitch = region.rowPitch;
        regionDesc.depthPitch = region.depthPitch;
        regionDesc.arraySlice = region.arraySlice;
        regionDesc.mipLevel = region.mipLevel;
        regionDesc.aspect = region.aspect;
        if(!GraphicsModuleDetail::ValidateTextureSetupUpload(regionDesc))
            return false;
        if(AddOverflows<usize>(outTotalByteCount, region.dataSize)){
            NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': byte count overflows")
                , StringConvert(textureDesc.name.resolvedText())
            );
            return false;
        }
        outTotalByteCount += region.dataSize;
    }
    // Retained subresources publish their state only after this batch is accepted. A partial fresh upload leaves
    // the texture mixed, so later unspecified typed imports resolve to Unknown until every subresource is known.
    return true;
}

[[nodiscard]] static CommandQueue::Enum ResolveTextureUploadBatchConsumerQueue(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum requestedConsumerQueue,
    const usize uploadBytes,
    const TextureDesc& textureDesc)noexcept{
    if(TextureUploadRequiresGraphicsConsumerQueue(textureDesc))
        return CommandQueue::Graphics;

    const auto canUse = [&](const CommandQueue::Enum queue){
        return queue == CommandQueue::Graphics
            || (device.getQueue(queue) && ResourceQueueSharing::IncludesQueueClass(textureDesc.queueSharing, queue))
        ;
    };
    const auto availableConsumerQueue = [&](){
        if(canUse(CommandQueue::Transfer))
            return CommandQueue::Transfer;
        if(canUse(CommandQueue::Compute))
            return CommandQueue::Compute;
        return CommandQueue::Graphics;
    };

    switch(requestedConsumerQueue){
    case CommandQueue::kCount:
        return uploadBytes < GraphicsModuleDetail::s_SetupUploadLargeMinimumBytes
            ? CommandQueue::Graphics
            : availableConsumerQueue()
        ;
    case CommandQueue::Transfer:
        return availableConsumerQueue();
    case CommandQueue::Compute:
        return canUse(CommandQueue::Compute)
            ? CommandQueue::Compute
            : CommandQueue::Graphics
        ;
    case CommandQueue::Graphics:
        return CommandQueue::Graphics;
    default:
        GLB_ASSERT_MSG(false, GLB_TEXT("GraphicsRuntime: texture batch upload requested an invalid command queue"));
        return CommandQueue::Graphics;
    }
}


struct TextureUploadBatchSubmissionData{
    const GraphicsRuntime::TextureUploadBatchDesc& setupDesc;
    const TextureDesc& textureDesc;
    ResourceStates::Mask graphInitialState = ResourceStates::Unknown;
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting;
    QueueSubmissionToken& uploadToken;
    GpuPhysicalQueueId directConsumerQueue;
};

[[nodiscard]] static GpuTaskId DeclareTextureUploadBatch(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *static_cast<TextureUploadBatchSubmissionData*>(userData);
    const GpuGraphResourceId destination = graph.importTexture(
        submissionData.setupDesc.destination,
        GpuGraphResourceDesc{}
            .setIdentity(s_UploadTextureBatchResourceIdentity)
            .setMarkerLabel("Texture Upload Batch")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(submissionData.graphInitialState)
            .setQueueSharing(submissionData.textureDesc.queueSharing)
            .setDirectConsumerQueue(submissionData.directConsumerQueue)
    );
    if(!destination.valid())
        return {};

    GpuTaskId previousTask;
    for(usize regionIndex = 0u; regionIndex < submissionData.setupDesc.regionCount; ++regionIndex){
        const GraphicsRuntime::TextureUploadRegion& region = submissionData.setupDesc.regions[regionIndex];
        const GpuUploadBlobId source = graph.copyUploadData(
            region.data,
            region.dataSize,
            alignof(u32)
        );
        if(!source.valid())
            return {};

        GpuTaskSchedulingHint scheduling = GraphicsModuleDetail::SetupUploadGraphScheduling(
            region.dataSize,
            submissionData.sameClassRouting.enabled,
            submissionData.sameClassRouting.crossesQueueFamily
        );
        scheduling.forceSubmissionBoundary = false;
        scheduling.allowPacketMerge = true;
        scheduling.mergeWithPrevious = previousTask.valid();
        scheduling.preserveSameClassQueueWithDirectDependency = previousTask.valid();
        GpuTaskDesc uploadTaskDesc;
        uploadTaskDesc
            .setIdentity(s_UploadTextureBatchUploadIdentity)
            .setMarkerLabel("Texture Upload Batch")
            .setScheduling(scheduling)
        ;
        if(previousTask.valid())
            uploadTaskDesc.setDependencies(&previousTask, 1u);

        const GpuTaskId uploadTask = graph.addUploadTextureTask(
            uploadTaskDesc,
            GpuUploadTextureTaskDesc{
                .source = source,
                .destination = destination,
                .arraySlice = region.arraySlice,
                .mipLevel = region.mipLevel,
                .rowPitch = region.rowPitch,
                .depthPitch = region.depthPitch,
                .finalState = submissionData.setupDesc.finalState,
                .aspect = region.aspect,
                .acceptedToken = regionIndex + 1u == submissionData.setupDesc.regionCount ? &submissionData.uploadToken : nullptr,
            }
        );
        if(!uploadTask.valid())
            return {};
        previousTask = uploadTask;
    }
    return previousTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GraphicsRuntime::uploadTextureBatch(const TextureUploadBatchDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};

    usize totalByteCount = 0u;
    if(!__hidden_graphics_texture_upload::ValidateTextureUploadBatch(desc, totalByteCount))
        return false;

    const TextureDesc& textureDesc = desc.destination->getCreationDescription();
    const ResourceStates::Mask graphInitialState = desc.hasPhysicalInitialState
        ? desc.physicalInitialState
        : textureDesc.initialState
    ;
    const CommandQueue::Enum consumerQueue = __hidden_graphics_texture_upload::ResolveTextureUploadBatchConsumerQueue(
        device,
        desc.queue,
        totalByteCount,
        textureDesc
    );
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting =
        GraphicsModuleDetail::ResolveSetupUploadSameClassRouting(device, consumerQueue, totalByteCount)
    ;
    // Existing batch destinations cannot be recreated with a wider sharing contract. A cross-family producer is
    // therefore valid only when the texture was already created for that broad queue class; same-family offload
    // retains ordinary exclusive sharing.
    if(
        sameClassRouting.crossesQueueFamily
        && !ResourceQueueSharing::IncludesQueueClass(textureDesc.queueSharing, consumerQueue)
    )
        sameClassRouting = {};
    QueueSubmissionToken uploadToken;
    __hidden_graphics_texture_upload::TextureUploadBatchSubmissionData submissionData{
        .setupDesc = desc,
        .textureDesc = textureDesc,
        .graphInitialState = graphInitialState,
        .sameClassRouting = sameClassRouting,
        .uploadToken = uploadToken,
        .directConsumerQueue = device.getPrimaryPhysicalQueue(consumerQueue),
    };
    const bool submitted = GraphicsModuleDetail::SubmitGraphOwnedSetupUpload(
        *this,
        m_allocator.getObjectArena(),
        textureDesc.queueSharing,
        consumerQueue,
        &submissionData,
        &__hidden_graphics_texture_upload::DeclareTextureUploadBatch,
        uploadToken,
        sameClassRouting.enabled ? sameClassRouting.primaryQueue : GpuPhysicalQueueId{}
    );
    if(!submitted){
        NWB_LOGGER_ERROR(GLB_TEXT("GraphicsRuntime: failed to submit graph-owned texture upload batch '{}'"), StringConvert(textureDesc.name.resolvedText()));
        return false;
    }

    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;
    return uploadToken.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

