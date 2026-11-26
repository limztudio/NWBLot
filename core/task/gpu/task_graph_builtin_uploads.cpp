// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include "compiled_graph.h"
#include "task_graph_builtin_internal.h"

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/command.h>

#include <global/algorithm.h>
#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_builtin_uploads{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UploadBufferPayload{
    GpuUploadBlobId source;
    GpuGraphResourceId destinationResource;
    BufferHandle destination;
    u64 destinationOffsetBytes = 0u;
    ResourceStates::Mask finalState = ResourceStates::CopyDest;
    QueueSubmissionToken* acceptedToken = nullptr;
};

struct UploadBufferTask : public GpuTaskGraphBuiltinDetail::SingletonTokenTaskBase<UploadBufferPayload>{
    [[nodiscard]] static b8 Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context){
        const auto bytes = context.declarations.uploadBlobData(payload.source);
        FOUNDATION_ASSERT(payload.destination && bytes && !bytes->empty() && payload.finalState != ResourceStates::Unknown);

        if(context.commandIrCapture && !context.commandIrCapture->captureUploadBuffer(context.task, context.packet, context.queue, payload.source, payload.destinationResource, payload.destinationOffsetBytes, *bytes, payload.finalState))
            return false;

        commandList.endRenderPass();
        const BufferRange uploadRange(payload.destinationOffsetBytes, bytes->size());
        commandList.setBufferState(payload.destination.get(), ResourceStates::CopyDest, false, uploadRange);
        commandList.commitBarriers();
        if(!commandList.tryWriteBuffer(*payload.destination, bytes->data(), bytes->size(), payload.destinationOffsetBytes))
            return false;
        if(payload.finalState != ResourceStates::CopyDest){
            commandList.setBufferState(payload.destination.get(), payload.finalState, false, uploadRange);
            commandList.commitBarriers();
        }
        return true;
    }
};

struct UploadTexturePayload{
    GpuUploadBlobId source;
    GpuGraphResourceId destinationResource;
    TextureHandle destination;
    TextureSlice destinationSlice;
    u32 arraySlice = 0u;
    u32 mipLevel = 0u;
    usize rowPitch = 0u;
    usize depthPitch = 0u;
    usize requiredBytes = 0u;
    ResourceStates::Mask finalState = ResourceStates::CopyDest;
    TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic;
    QueueSubmissionToken* acceptedToken = nullptr;
};

struct UploadTextureTask : public GpuTaskGraphBuiltinDetail::SingletonTokenTaskBase<UploadTexturePayload>{
    [[nodiscard]] static b8 Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context){
        const auto bytes = context.declarations.uploadBlobData(payload.source);
        FOUNDATION_ASSERT(payload.destination && bytes && payload.requiredBytes != 0u && bytes->size() >= payload.requiredBytes && payload.finalState != ResourceStates::Unknown);

        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureUploadTexture(
                context.task,
                context.packet,
                context.queue,
                payload.source,
                payload.destinationResource,
                payload.destinationSlice,
                payload.rowPitch,
                payload.depthPitch,
                payload.aspect,
                BinaryByteView{ bytes->data(), payload.requiredBytes },
                payload.finalState
            )
        )
            return false;

        commandList.endRenderPass();
        const TextureSubresourceSet subresources(payload.mipLevel, 1u, payload.arraySlice, 1u);
        commandList.setTextureState(payload.destination.get(), subresources, ResourceStates::CopyDest);
        commandList.commitBarriers();
        if(!commandList.tryWriteTexture(*payload.destination, payload.arraySlice, payload.mipLevel, bytes->data(), payload.rowPitch, payload.depthPitch, payload.aspect))
            return false;
        if(payload.finalState != ResourceStates::CopyDest){
            commandList.setTextureState(payload.destination.get(), subresources, payload.finalState);
            commandList.commitBarriers();
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuUploadBlobId> GpuTaskGraph::copyUploadData(const void* const data, const usize byteSize, const usize alignment){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(!data || byteSize == 0u || alignment == 0u || (alignment & (alignment - 1u)) != 0u || m_uploadBlobs.size() >= Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});

    const u32 index = static_cast<u32>(m_uploadBlobs.size());
    constexpr usize s_InlineMaxAlignment = alignof(GpuTaskGraphDetail::GpuUploadBlobStorageBlock);
    if(alignment <= s_InlineMaxAlignment){
        const auto alignedOffset = AlignUpPowerOfTwoChecked(m_uploadBlobStorageByteSize, alignment);
        if(!alignedOffset || byteSize > Limit<usize>::s_Max - *alignedOffset)
            return MakeUnexpected(Failure{});

        const usize endByteSize = *alignedOffset + byteSize;
        constexpr usize s_BlockSize = sizeof(GpuTaskGraphDetail::GpuUploadBlobStorageBlock);
        const auto alignedEnd = AlignUpPowerOfTwoChecked(endByteSize, s_BlockSize);
        if(!alignedEnd)
            return MakeUnexpected(Failure{});

        const usize requiredBlocks = *alignedEnd / s_BlockSize;
        if(requiredBlocks > m_uploadBlobStorage.max_size())
            return MakeUnexpected(Failure{});

        ContainerDetail::ReserveGrowingCapacity(m_uploadBlobs, m_uploadBlobs.size() + 1u);
        ContainerDetail::ReserveGrowingCapacity(m_uploadBlobStorage, requiredBlocks);
        m_uploadBlobStorage.resize(requiredBlocks);
        FOUNDATION_ASSERT((reinterpret_cast<usize>(m_uploadBlobStorage.data()) & (s_InlineMaxAlignment - 1u)) == 0u);
        FOUNDATION_MEMCPY(reinterpret_cast<u8*>(m_uploadBlobStorage.data()) + *alignedOffset, byteSize, data, byteSize);

        m_uploadBlobs.push_back(GpuUploadBlobNode{
            .dataOffset = *alignedOffset,
            .dataSize = byteSize,
            .overflowIndex = Limit<u32>::s_Max,
        });
        m_uploadBlobStorageByteSize = endByteSize;
    }
    else{
        if(m_overalignedUploadBlobs.size() >= Limit<u32>::s_Max || alignment - 1u > Limit<usize>::s_Max - byteSize)
            return MakeUnexpected(Failure{});

        const usize allocationSize = byteSize + alignment - 1u;
        GraphicsBytes bytes(m_arena);
        if(allocationSize > bytes.max_size())
            return MakeUnexpected(Failure{});
        bytes.resize(allocationSize);
        const usize dataOffset = AlignmentOffsetPowerOfTwo(reinterpret_cast<usize>(bytes.data()), alignment);
        FOUNDATION_MEMCPY(bytes.data() + dataOffset, byteSize, data, byteSize);

        ContainerDetail::ReserveGrowingCapacity(m_overalignedUploadBlobs, m_overalignedUploadBlobs.size() + 1u);
        ContainerDetail::ReserveGrowingCapacity(m_uploadBlobs, m_uploadBlobs.size() + 1u);
        const u32 overflowIndex = static_cast<u32>(m_overalignedUploadBlobs.size());
        m_overalignedUploadBlobs.push_back(Move(bytes));
        m_uploadBlobs.push_back(GpuUploadBlobNode{
            .dataOffset = dataOffset,
            .dataSize = byteSize,
            .overflowIndex = overflowIndex,
        });
    }

    bumpDeclarationRevision();
    return GpuUploadBlobId{ .generation = m_generation, .index = index };
}

const GpuTaskGraphDetail::GpuGraphResourceNode* GpuTaskGraph::validateUploadTaskInputsWithinMutation(
    const GpuTaskDesc& desc,
    const GpuUploadBlobId& sourceId,
    const GpuGraphResourceId& destinationId,
    const ResourceStates::Mask finalState,
    const GpuGraphResourceType::Enum expectedType,
    const DeclarationMutationScope& mutation,
    const GpuUploadBlobNode*& outSource
)const noexcept{
    outSource = nullptr;
    if(!mutation.valid() || !GpuTaskGraphBuiltinDetail::BuiltinDeclarationHasNoCallerResourceUses(desc) || !validUploadBlob(sourceId) || !validResource(destinationId) || finalState == ResourceStates::Unknown)
        return nullptr;

    const GpuUploadBlobNode* const source = findUploadBlob(sourceId);
    const GpuGraphResourceNode& destinationResource = m_resources[destinationId.index];
    FOUNDATION_ASSERT(source && source->dataSize != 0u);
    if(destinationResource.type != expectedType)
        return nullptr;
    if(expectedType == GpuGraphResourceType::Buffer ? !destinationResource.buffer : !destinationResource.texture)
        return nullptr;
    outSource = source;
    return &destinationResource;
}

Expected<GpuTaskId> GpuTaskGraph::addUploadBufferTask(const GpuTaskDesc& desc, const GpuUploadBufferTaskDesc& uploadDesc){
    if(uploadDesc.acceptedToken)
        *uploadDesc.acceptedToken = {};

    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    const GpuUploadBlobNode* source = nullptr;
    const GpuGraphResourceNode* const destinationResource = validateUploadTaskInputsWithinMutation(desc, uploadDesc.source, uploadDesc.destination, uploadDesc.finalState, GpuGraphResourceType::Buffer, mutation, source);
    if(!destinationResource)
        return MakeUnexpected(Failure{});

    const BufferDesc& destinationDesc = destinationResource->buffer->getCreationDescription();
    if(
        uploadDesc.destinationOffsetBytes > destinationDesc.byteSize
        || source->dataSize > destinationDesc.byteSize - uploadDesc.destinationOffsetBytes
        || (uploadDesc.destinationOffsetBytes & (sizeof(u32) - 1u)) != 0u
        || (source->dataSize & (sizeof(u32) - 1u)) != 0u
        || !GpuTaskGraphBuiltinDetail::BuiltInTaskCanMaterializeRetainedState(destinationDesc, destinationResource->initialState, destinationResource->externalFinalState)
        || (destinationDesc.keepInitialState && uploadDesc.finalState != destinationDesc.initialState)
    )
        return MakeUnexpected(Failure{});

    using UploadTask = __hidden_gpu_task_graph_builtin_uploads::UploadBufferTask;
    const GpuTaskResourceRange uploadRange{ .bufferRange = BufferRange(uploadDesc.destinationOffsetBytes, source->dataSize) };
    constexpr GpuTaskCommandRequirements s_Commands{ GpuQueueCapability::Transfer };
    return appendUploadTaskWithinMutation<UploadTask>(
        desc,
        uploadDesc.destination,
        uploadRange,
        uploadDesc.finalState,
        s_Commands,
        mutation,
        [&](UploadTask::Payload& payload)noexcept{
            payload.source = uploadDesc.source;
            payload.destinationResource = uploadDesc.destination;
            payload.destination = destinationResource->buffer;
            payload.destinationOffsetBytes = uploadDesc.destinationOffsetBytes;
            payload.finalState = uploadDesc.finalState;
            payload.acceptedToken = uploadDesc.acceptedToken;
        }
    );
}

Expected<GpuTaskId> GpuTaskGraph::addUploadTextureTask(const GpuTaskDesc& desc, const GpuUploadTextureTaskDesc& uploadDesc){
    if(uploadDesc.acceptedToken)
        *uploadDesc.acceptedToken = {};

    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    const GpuUploadBlobNode* source = nullptr;
    const GpuGraphResourceNode* const destinationResource = validateUploadTaskInputsWithinMutation(desc, uploadDesc.source, uploadDesc.destination, uploadDesc.finalState, GpuGraphResourceType::Texture, mutation, source);
    if(!destinationResource)
        return MakeUnexpected(Failure{});

    Expected<usize> requiredBytes = MakeUnexpected(Failure{});
    const TextureDesc& destinationDesc = destinationResource->texture->getCreationDescription();
    const FormatInfo& destinationFormatInfo = GetFormatInfo(destinationDesc.format);
    const auto resolvedAspect = ResolveTextureUploadAspect(destinationFormatInfo, uploadDesc.aspect);
    if(!resolvedAspect)
        return MakeUnexpected(Failure{});
    if(
        !GpuTaskGraphBuiltinDetail::UploadTextureTaskCanMaterializeRetainedState(destinationDesc, destinationResource->initialState, destinationResource->externalFinalState, uploadDesc.finalState)
        ||
        !(requiredBytes = ComputeTextureUploadByteSize(destinationDesc, uploadDesc.arraySlice, uploadDesc.mipLevel, uploadDesc.rowPitch, uploadDesc.depthPitch, uploadDesc.aspect))
        || source->dataSize < *requiredBytes
    )
        return MakeUnexpected(Failure{});

    using UploadTask = __hidden_gpu_task_graph_builtin_uploads::UploadTextureTask;
    const GpuTaskResourceRange uploadRange{ .textureSubresources = TextureSubresourceSet(uploadDesc.mipLevel, 1u, uploadDesc.arraySlice, 1u), };
    GpuTaskCommandRequirements commands{ GpuQueueCapability::Transfer };
    if(*resolvedAspect != TextureUploadAspect::Color)
        commands.requiredCapabilities |= GpuQueueCapability::Graphics;
    return appendUploadTaskWithinMutation<UploadTask>(
        desc,
        uploadDesc.destination,
        uploadRange,
        uploadDesc.finalState,
        commands,
        mutation,
        [&](UploadTask::Payload& payload)noexcept{
            payload.source = uploadDesc.source;
            payload.destinationResource = uploadDesc.destination;
            payload.destination = destinationResource->texture;
            TextureSlice destinationSlice;
            destinationSlice.mipLevel = uploadDesc.mipLevel;
            destinationSlice.arraySlice = uploadDesc.arraySlice;
            payload.destinationSlice = destinationSlice.resolve(destinationDesc);
            payload.arraySlice = uploadDesc.arraySlice;
            payload.mipLevel = uploadDesc.mipLevel;
            payload.rowPitch = uploadDesc.rowPitch;
            payload.depthPitch = uploadDesc.depthPitch;
            payload.requiredBytes = *requiredBytes;
            payload.finalState = uploadDesc.finalState;
            payload.acceptedToken = uploadDesc.acceptedToken;
            payload.aspect = uploadDesc.aspect;
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

