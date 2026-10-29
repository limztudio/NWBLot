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
inline constexpr AStringView s_UploadTextureBatchMarkerLabel = "Texture Upload Batch";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 TextureUploadRequiresGraphicsConsumerQueue(const TextureDesc& textureDesc)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    return formatInfo.hasDepth || formatInfo.hasStencil;
}

[[nodiscard]] static Expected<usize> ComputeTextureUploadByteSize(const TextureDesc& textureDesc, const GraphicsRuntime::TextureUploadRegion& region)noexcept{
    if(textureDesc.width == 0 || textureDesc.height == 0 || textureDesc.depth == 0 || textureDesc.mipLevels == 0 || textureDesc.arraySize == 0)
        return MakeUnexpected(Failure{});
    if(textureDesc.sampleCount != 1)
        return MakeUnexpected(Failure{});
    if(region.mipLevel >= textureDesc.mipLevels || region.mipLevel >= sizeof(u32) * s_BitsPerByte || region.arraySlice >= textureDesc.arraySize)
        return MakeUnexpected(Failure{});
    if(static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::Count))
        return MakeUnexpected(Failure{});

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const auto aspectLayout = GetTextureUploadAspectLayout(formatInfo, region.aspect);
    if(!aspectLayout)
        return MakeUnexpected(Failure{});

    const u32 width = Max<u32>(1u, textureDesc.width >> region.mipLevel);
    const u32 height = Max<u32>(1u, textureDesc.height >> region.mipLevel);
    const u32 depth = Max<u32>(1u, textureDesc.depth >> region.mipLevel);

    const u64 blockCountX = DivideUp(static_cast<u64>(width), static_cast<u64>(aspectLayout->blockWidth));
    const u64 blockCountY = DivideUp(static_cast<u64>(height), static_cast<u64>(aspectLayout->blockHeight));
    if(blockCountX > Limit<u64>::s_Max / aspectLayout->bytesPerBlock)
        return MakeUnexpected(Failure{});

    const u64 naturalRowPitch = blockCountX * aspectLayout->bytesPerBlock;
    const u64 effectiveRowPitch = region.rowPitch != 0 ? static_cast<u64>(region.rowPitch) : naturalRowPitch;
    if(effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % aspectLayout->bytesPerBlock) != 0)
        return MakeUnexpected(Failure{});
    if(blockCountY > Limit<u64>::s_Max / effectiveRowPitch)
        return MakeUnexpected(Failure{});

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = region.depthPitch != 0 ? static_cast<u64>(region.depthPitch) : packedSlicePitch;
    if(effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0)
        return MakeUnexpected(Failure{});

    if(depth > 1 && static_cast<u64>(depth - 1) > (Limit<u64>::s_Max - packedSlicePitch) / effectiveDepthPitch)
        return MakeUnexpected(Failure{});

    const u64 requiredBytes = depth > 1 ? effectiveDepthPitch * static_cast<u64>(depth - 1) + packedSlicePitch : packedSlicePitch;
    if(requiredBytes > static_cast<u64>(Limit<usize>::s_Max))
        return MakeUnexpected(Failure{});

    return static_cast<usize>(requiredBytes);
}

[[nodiscard]] static b8 ValidateTextureUploadRegion(const TextureDesc& textureDesc, const GraphicsRuntime::TextureUploadRegion& region){
    if(!region.data && region.dataSize == 0)
        return true;
    if(!region.data || region.dataSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up texture '{}': upload data and size must both be provided"), ConvertString(textureDesc.name.resolvedText()));
        return false;
    }

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    if(!ResolveTextureUploadAspect(formatInfo, region.aspect)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up texture '{}': upload aspect must name one aspect present in the texture format; D24S8/D32S8 require Depth or Stencil"), ConvertString(textureDesc.name.resolvedText()));
        return false;
    }
    const auto requiredBytes = ComputeTextureUploadByteSize(textureDesc, region);
    if(!requiredBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up texture '{}': invalid upload layout"), ConvertString(textureDesc.name.resolvedText()));
        return false;
    }
    if(textureDesc.keepInitialState && textureDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up texture '{}': keep-initial-state uploads require a concrete initial state"), ConvertString(textureDesc.name.resolvedText()));
        return false;
    }
    if(region.dataSize < *requiredBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to set up texture '{}': upload data size {} is smaller than required size {}"), ConvertString(textureDesc.name.resolvedText()), region.dataSize, *requiredBytes);
        return false;
    }

    return true;
}


[[nodiscard]] static Expected<usize> ValidateTextureUploadBatch(const GraphicsRuntime::TextureUploadBatchDesc& desc){
    usize totalByteCount = 0u;
    if(!desc.destination){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch: destination texture is null"));
        return MakeUnexpected(Failure{});
    }
    if(!desc.regions || desc.regionCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': regions are empty"), ConvertString(desc.destination->getCreationDescription().name.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    if(desc.finalState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': final state is unknown"), ConvertString(desc.destination->getCreationDescription().name.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const TextureDesc& textureDesc = desc.destination->getCreationDescription();
    if(textureDesc.keepInitialState && textureDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': keep-initial-state uploads require a concrete initial state"), ConvertString(textureDesc.name.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    if(static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::Count)){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': texture format is invalid"), ConvertString(textureDesc.name.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    if(textureDesc.keepInitialState && desc.finalState != textureDesc.initialState){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': keep-initial-state requires final state {}"), ConvertString(textureDesc.name.resolvedText()), static_cast<u32>(textureDesc.initialState));
        return MakeUnexpected(Failure{});
    }
    if(textureDesc.keepInitialState && desc.hasPhysicalInitialState && desc.physicalInitialState != ResourceStates::Unknown && desc.physicalInitialState != textureDesc.initialState){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': retained textures require their declared physical initial state to match initialState"), ConvertString(textureDesc.name.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    for(usize regionIndex = 0u; regionIndex < desc.regionCount; ++regionIndex){
        const GraphicsRuntime::TextureUploadRegion& region = desc.regions[regionIndex];
        if(!ValidateTextureUploadRegion(textureDesc, region))
            return MakeUnexpected(Failure{});
        if(AddOverflows<usize>(totalByteCount, region.dataSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to upload texture batch '{}': byte count overflows"), ConvertString(textureDesc.name.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        totalByteCount += region.dataSize;
    }
    // Retained subresources publish their state only after this batch is accepted.
    // A partial fresh upload leaves the texture mixed, so later unspecified typed imports resolve to Unknown until every subresource is known.
    return totalByteCount;
}

[[nodiscard]] static CommandQueue::Enum ResolveTextureUploadBatchConsumerQueue(GraphicsBackend::Device& device, const CommandQueue::Enum requestedConsumerQueue, const usize uploadBytes, const TextureDesc& textureDesc)noexcept{
    if(TextureUploadRequiresGraphicsConsumerQueue(textureDesc))
        return CommandQueue::Graphics;

    const auto canUse = [&](const CommandQueue::Enum queue)noexcept{
        return queue == CommandQueue::Graphics || (device.getQueue(queue) && ResourceQueueSharing::IncludesQueueClass(textureDesc.queueSharing, queue));
    };
    const auto availableConsumerQueue = [&]()noexcept{
        if(canUse(CommandQueue::Transfer))
            return CommandQueue::Transfer;
        if(canUse(CommandQueue::Compute))
            return CommandQueue::Compute;
        return CommandQueue::Graphics;
    };

    switch(requestedConsumerQueue){
    case CommandQueue::kCount:
        return uploadBytes < GraphicsModuleDetail::s_SetupUploadLargeMinimumBytes ? CommandQueue::Graphics : availableConsumerQueue();
    case CommandQueue::Transfer:
        return availableConsumerQueue();
    case CommandQueue::Compute:
        return canUse(CommandQueue::Compute) ? CommandQueue::Compute : CommandQueue::Graphics;
    case CommandQueue::Graphics:
        return CommandQueue::Graphics;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("GraphicsRuntime: texture batch upload requested an invalid command queue"));
        return CommandQueue::Graphics;
    }
}


struct TextureUploadBatchSubmissionData{
    const GraphicsRuntime::TextureUploadBatchDesc& setupDesc;
    const TextureDesc& textureDesc;
    QueueSubmissionToken& uploadToken;
    ResourceStates::Mask graphInitialState = ResourceStates::Unknown;
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting;
    GpuPhysicalQueueId directConsumerQueue;
};

[[nodiscard]] static Expected<GpuTaskId> DeclareTextureUploadBatch(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *MakeNotNull(static_cast<TextureUploadBatchSubmissionData*>(userData));
    const auto destinationResult = graph.importTexture(
        submissionData.setupDesc.destination,
        GpuGraphResourceDesc{}
            .setIdentity(s_UploadTextureBatchResourceIdentity)
            .setMarkerLabel(s_UploadTextureBatchMarkerLabel)
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(submissionData.graphInitialState)
            .setQueueSharing(submissionData.textureDesc.queueSharing)
            .setDirectConsumerQueue(submissionData.directConsumerQueue)
    );
    if(!destinationResult)
        return MakeUnexpected(Failure{});
    const GpuGraphResourceId destination = *destinationResult;

    GpuTaskId previousTask;
    for(usize regionIndex = 0u; regionIndex < submissionData.setupDesc.regionCount; ++regionIndex){
        const GraphicsRuntime::TextureUploadRegion& region = submissionData.setupDesc.regions[regionIndex];
        const auto sourceResult = graph.copyUploadData(region.data, region.dataSize, alignof(u32));
        if(!sourceResult)
            return MakeUnexpected(Failure{});
        const GpuUploadBlobId source = *sourceResult;

        GpuTaskSchedulingHint scheduling = GraphicsModuleDetail::SetupUploadGraphScheduling(region.dataSize, submissionData.sameClassRouting.enabled, submissionData.sameClassRouting.crossesQueueFamily);
        scheduling.forceSubmissionBoundary = false;
        scheduling.allowPacketMerge = true;
        scheduling.mergeWithPrevious = previousTask.valid();
        scheduling.preserveSameClassQueueWithDirectDependency = previousTask.valid();
        GpuTaskDesc uploadTaskDesc;
        uploadTaskDesc.setIdentity(s_UploadTextureBatchUploadIdentity).setMarkerLabel(s_UploadTextureBatchMarkerLabel).setScheduling(scheduling);
        if(previousTask.valid())
            uploadTaskDesc.setDependencies(&previousTask, 1u);

        const auto uploadTaskResult = graph.addUploadTextureTask(
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
        if(!uploadTaskResult)
            return MakeUnexpected(Failure{});
        const GpuTaskId uploadTask = *uploadTaskResult;
        previousTask = uploadTask;
    }
    NWB_ASSERT(previousTask.valid());
    return previousTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GraphicsRuntime::uploadTextureBatch(const TextureUploadBatchDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};

    const auto totalByteCount = __hidden_graphics_texture_upload::ValidateTextureUploadBatch(desc);
    if(!totalByteCount)
        return false;

    const TextureDesc& textureDesc = desc.destination->getCreationDescription();
    const ResourceStates::Mask graphInitialState = desc.hasPhysicalInitialState ? desc.physicalInitialState : textureDesc.initialState;
    const CommandQueue::Enum consumerQueue = __hidden_graphics_texture_upload::ResolveTextureUploadBatchConsumerQueue(device, desc.queue, *totalByteCount, textureDesc);
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting = GraphicsModuleDetail::ResolveSetupUploadSameClassRouting(device, consumerQueue, *totalByteCount);
    // Existing batch destinations cannot be recreated with a wider sharing contract.
    // Cross-family producer needs the texture already created for that queue class; same-family offload keeps exclusive sharing.
    if(sameClassRouting.crossesQueueFamily && !ResourceQueueSharing::IncludesQueueClass(textureDesc.queueSharing, consumerQueue))
        sameClassRouting = {};
    QueueSubmissionToken uploadToken;
    __hidden_graphics_texture_upload::TextureUploadBatchSubmissionData submissionData{
        .setupDesc = desc,
        .textureDesc = textureDesc,
        .uploadToken = uploadToken,
        .graphInitialState = graphInitialState,
        .sameClassRouting = sameClassRouting,
        .directConsumerQueue = device.getPrimaryPhysicalQueue(consumerQueue),
    };
    const b8 submitted = GraphicsModuleDetail::SubmitGraphOwnedSetupUpload(
        uploadToken,
        *m_standaloneGraphWorkspaces,
        *this,
        textureDesc.queueSharing,
        consumerQueue,
        &submissionData,
        MakeNotNull(&__hidden_graphics_texture_upload::DeclareTextureUploadBatch),
        sameClassRouting.enabled ? sameClassRouting.primaryQueue : GpuPhysicalQueueId{}
    );
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to submit graph-owned texture upload batch '{}'"), ConvertString(textureDesc.name.resolvedText()));
        return false;
    }

    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

