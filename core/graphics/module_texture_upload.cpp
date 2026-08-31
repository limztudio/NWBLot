// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module_internal.h"

#include "backend_selection.h"
#include "task_graph/compiler.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_texture_upload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_TransferPreferredUploadMinimumBytes = 1024u * 1024u;

[[nodiscard]] static bool TextureUploadRequiresGraphicsQueue(const TextureDesc& textureDesc)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    return formatInfo.hasDepth || formatInfo.hasStencil;
}


[[nodiscard]] static bool ValidateTextureUploadBatch(
    const Graphics::TextureUploadBatchDesc& desc,
    usize& outTotalByteCount
){
    outTotalByteCount = 0u;
    if(!desc.destination){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch: destination texture is null"));
        return false;
    }
    if(!desc.regions || desc.regionCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': regions are empty")
            , StringConvert(desc.destination->getCreationDescription().name.c_str())
        );
        return false;
    }
    if(desc.finalState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': final state is unknown")
            , StringConvert(desc.destination->getCreationDescription().name.c_str())
        );
        return false;
    }

    const TextureDesc& textureDesc = desc.destination->getCreationDescription();
    if(textureDesc.keepInitialState && textureDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': keep-initial-state uploads require a concrete initial state")
            , StringConvert(textureDesc.name.c_str())
        );
        return false;
    }
    if(static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::kCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': texture format is invalid")
            , StringConvert(textureDesc.name.c_str())
        );
        return false;
    }
    if(textureDesc.keepInitialState && desc.finalState != textureDesc.initialState){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': keep-initial-state requires final state {}")
            , StringConvert(textureDesc.name.c_str())
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
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': retained textures require their declared physical initial state to match initialState")
            , StringConvert(textureDesc.name.c_str())
        );
        return false;
    }

    for(usize regionIndex = 0u; regionIndex < desc.regionCount; ++regionIndex){
        const Graphics::TextureUploadRegion& region = desc.regions[regionIndex];
        Graphics::TextureSetupDesc regionDesc;
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
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': byte count overflows")
                , StringConvert(textureDesc.name.c_str())
            );
            return false;
        }
        outTotalByteCount += region.dataSize;
    }
    // Retained subresources publish their state only after this batch is accepted. A partial fresh upload leaves
    // the texture mixed, so later unspecified typed imports resolve to Unknown until every subresource is known.
    return true;
}

[[nodiscard]] static CommandQueue::Enum ResolveTextureUploadBatchQueue(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum requestedQueue,
    const usize uploadBytes,
    const TextureDesc& textureDesc
)noexcept{
    if(TextureUploadRequiresGraphicsQueue(textureDesc))
        return CommandQueue::Graphics;

    const auto canUse = [&](const CommandQueue::Enum queue){
        return queue == CommandQueue::Graphics
            || (device.getQueue(queue) && ResourceQueueSharing::IncludesQueueClass(textureDesc.queueSharing, queue))
        ;
    };
    const auto transferPreferred = [&](){
        if(canUse(CommandQueue::Transfer))
            return CommandQueue::Transfer;
        if(canUse(CommandQueue::Compute))
            return CommandQueue::Compute;
        return CommandQueue::Graphics;
    };

    switch(requestedQueue){
    case CommandQueue::kCount:
        return uploadBytes < s_TransferPreferredUploadMinimumBytes
            ? CommandQueue::Graphics
            : transferPreferred()
        ;
    case CommandQueue::Transfer:
        return transferPreferred();
    case CommandQueue::Compute:
        return canUse(CommandQueue::Compute)
            ? CommandQueue::Compute
            : CommandQueue::Graphics
        ;
    case CommandQueue::Graphics:
        return CommandQueue::Graphics;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Graphics: texture batch upload requested an invalid command queue"));
        return CommandQueue::Graphics;
    }
}


struct TextureUploadBatchSubmissionData{
    const Graphics::TextureUploadBatchDesc& setupDesc;
    const TextureDesc& textureDesc;
    ResourceStates::Mask graphInitialState = ResourceStates::Unknown;
    CommandQueue::Enum uploadQueue = CommandQueue::Graphics;
    bool requiresGraphicsQueue = false;
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting;
    QueueSubmissionToken& uploadToken;
};

[[nodiscard]] static GpuTaskId DeclareTextureUploadBatch(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *static_cast<TextureUploadBatchSubmissionData*>(userData);
    const GpuGraphResourceId destination = graph.importTexture(
        submissionData.setupDesc.destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("graphics.upload_texture_batch.resource"))
            .setMarkerLabel("Texture Upload Batch")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(submissionData.graphInitialState)
            .setQueueSharing(submissionData.textureDesc.queueSharing)
    );
    if(!destination.valid())
        return {};

    GpuTaskId previousTask;
    for(usize regionIndex = 0u; regionIndex < submissionData.setupDesc.regionCount; ++regionIndex){
        const Graphics::TextureUploadRegion& region = submissionData.setupDesc.regions[regionIndex];
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
            .setIdentity(Name("graphics.upload_texture_batch.upload"))
            .setMarkerLabel("Texture Upload Batch")
            .setQueue(GraphicsModuleDetail::SetupUploadGraphQueueRequest(
                submissionData.uploadQueue,
                submissionData.requiresGraphicsQueue
            ))
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
                .acceptedToken = regionIndex + 1u == submissionData.setupDesc.regionCount ? &submissionData.uploadToken : nullptr,
                .aspect = region.aspect,
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


bool Graphics::uploadTextureBatch(const TextureUploadBatchDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};

    usize totalByteCount = 0u;
    if(!__hidden_graphics_texture_upload::ValidateTextureUploadBatch(desc, totalByteCount))
        return false;

    const TextureDesc& textureDesc = desc.destination->getCreationDescription();
    const bool requiresGraphicsQueue = __hidden_graphics_texture_upload::TextureUploadRequiresGraphicsQueue(textureDesc);
    const ResourceStates::Mask graphInitialState = desc.hasPhysicalInitialState
        ? desc.physicalInitialState
        : textureDesc.initialState
    ;
    const CommandQueue::Enum uploadQueue = __hidden_graphics_texture_upload::ResolveTextureUploadBatchQueue(
        device,
        desc.queue,
        totalByteCount,
        textureDesc
    );
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting =
        GraphicsModuleDetail::ResolveSetupUploadSameClassRouting(device, uploadQueue, totalByteCount)
    ;
    // Existing batch destinations cannot be recreated with a wider sharing contract. A cross-family producer is
    // therefore valid only when the texture was already created for that broad queue class; same-family offload
    // retains ordinary exclusive sharing.
    if(
        sameClassRouting.crossesQueueFamily
        && !ResourceQueueSharing::IncludesQueueClass(textureDesc.queueSharing, uploadQueue)
    )
        sameClassRouting = {};
    QueueSubmissionToken uploadToken;
    __hidden_graphics_texture_upload::TextureUploadBatchSubmissionData submissionData{
        .setupDesc = desc,
        .textureDesc = textureDesc,
        .graphInitialState = graphInitialState,
        .uploadQueue = uploadQueue,
        .requiresGraphicsQueue = requiresGraphicsQueue,
        .sameClassRouting = sameClassRouting,
        .uploadToken = uploadToken,
    };
    const bool submitted = GraphicsModuleDetail::SubmitGraphOwnedSetupUpload(
        *this,
        m_allocator.getObjectArena(),
        textureDesc.queueSharing,
        uploadQueue,
        &submissionData,
        &__hidden_graphics_texture_upload::DeclareTextureUploadBatch,
        uploadToken,
        sameClassRouting.enabled,
        sameClassRouting.enabled ? sameClassRouting.primaryQueue : GpuPhysicalQueueId{}
    );
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to submit graph-owned texture upload batch '{}'"), StringConvert(textureDesc.name.c_str()));
        return false;
    }

    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;
    return uploadToken.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

