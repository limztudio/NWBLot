// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiled_graph.h"
#include "task_graph_builtin_internal.h"

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/command.h>


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
    [[nodiscard]] static bool Record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        usize byteSize = 0u;
        const void* const bytes = context.declarations.uploadBlobData(payload.source, byteSize);
        if(
            !payload.destination
            || !bytes
            || byteSize == 0u
            || payload.finalState == ResourceStates::Unknown
        )
            return false;

        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureUploadBuffer(
                context.task,
                context.packet,
                context.queue,
                payload.source,
                payload.destinationResource,
                payload.destinationOffsetBytes,
                BinaryByteView{ static_cast<const u8*>(bytes), byteSize },
                payload.finalState
            )
        )
            return false;

        commandList.endRenderPass();
        // The compiler tracks `finalState` for later tasks, but the native write itself requires CopyDest. Make the
        // internal transition explicit and commit it before vkCmdCopyBuffer; writeBuffer only queues its own state.
        const BufferRange uploadRange(payload.destinationOffsetBytes, byteSize);
        commandList.setBufferState(payload.destination.get(), ResourceStates::CopyDest, false, uploadRange);
        commandList.commitBarriers();
        if(!commandList.tryWriteBuffer(*payload.destination, bytes, byteSize, payload.destinationOffsetBytes))
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
    [[nodiscard]] static bool Record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        usize byteSize = 0u;
        const void* const bytes = context.declarations.uploadBlobData(payload.source, byteSize);
        if(
            !payload.destination
            || !bytes
            || payload.requiredBytes == 0u
            || byteSize < payload.requiredBytes
            || payload.finalState == ResourceStates::Unknown
        )
            return false;

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
                BinaryByteView{ static_cast<const u8*>(bytes), payload.requiredBytes },
                payload.finalState
            )
        )
            return false;

        commandList.endRenderPass();
        const TextureSubresourceSet subresources(payload.mipLevel, 1u, payload.arraySlice, 1u);
        // See UploadBufferTask: native writes need CopyDest even though graph analysis publishes finalState.
        commandList.setTextureState(payload.destination.get(), subresources, ResourceStates::CopyDest);
        commandList.commitBarriers();
        if(!commandList.tryWriteTexture(
            *payload.destination,
            payload.arraySlice,
            payload.mipLevel,
            bytes,
            payload.rowPitch,
            payload.depthPitch,
            payload.aspect
        ))
            return false;
        if(payload.finalState != ResourceStates::CopyDest){
            commandList.setTextureState(
                payload.destination.get(),
                subresources,
                payload.finalState
            );
            commandList.commitBarriers();
        }
        return true;
    }
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuUploadBlobId GpuTaskGraph::copyUploadData(
    const void* const data,
    const usize byteSize,
    const usize alignment
){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !data
        || byteSize == 0u
        || alignment == 0u
        || (alignment & (alignment - 1u)) != 0u
        || m_uploadBlobs.size() >= Limit<u32>::s_Max
    )
        return {};

    GpuUploadBlobNode blob(m_arena);
    const u8* const sourceBytes = static_cast<const u8*>(data);
    blob.bytes.reserve(byteSize);
    blob.bytes.assign(sourceBytes, sourceBytes + byteSize);
    if(blob.bytes.size() != byteSize)
        return {};

    const u32 index = static_cast<u32>(m_uploadBlobs.size());
    m_uploadBlobs.push_back(Move(blob));
    m_declarationRevision = AllocateGeneration();
    return GpuUploadBlobId{ .generation = m_generation, .index = index };
}

GpuTaskId GpuTaskGraph::addUploadBufferTask(
    const GpuTaskDesc& desc,
    const GpuUploadBufferTaskDesc& uploadDesc
){
    if(uploadDesc.acceptedToken)
        *uploadDesc.acceptedToken = {};

    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !GpuTaskGraphBuiltinDetail::BuiltinDeclarationHasNoCallerResourceUses(desc)
        || !validUploadBlob(uploadDesc.source)
        || !validResource(uploadDesc.destination)
        || uploadDesc.finalState == ResourceStates::Unknown
    )
        return {};

    const GpuUploadBlobNode* const source = findUploadBlob(uploadDesc.source);
    const GpuGraphResourceNode& destinationResource = m_resources[uploadDesc.destination.index];
    if(
        !source
        || source->bytes.empty()
        || destinationResource.type != GpuGraphResourceType::Buffer
        || !destinationResource.buffer
    )
        return {};

    const BufferDesc& destinationDesc = destinationResource.buffer->getCreationDescription();
    if(
        uploadDesc.destinationOffsetBytes > destinationDesc.byteSize
        || source->bytes.size() > destinationDesc.byteSize - uploadDesc.destinationOffsetBytes
        // Keep declaration acceptance aligned with CommandList::tryWriteBuffer so a built-in upload cannot fail
        // only during late packet recording.
        || (uploadDesc.destinationOffsetBytes & (sizeof(u32) - 1u)) != 0u
        || (source->bytes.size() & (sizeof(u32) - 1u)) != 0u
        || !GpuTaskGraphBuiltinDetail::BuiltInTaskCanMaterializeRetainedState(
            destinationDesc,
            destinationResource.initialState,
            destinationResource.externalFinalState
        )
        // Upload bodies perform their own CopyDest -> final-state transition. A retained descriptor restores its
        // descriptor state at packet close, so the primitive's graph-visible final state must match that restore.
        || (destinationDesc.keepInitialState && uploadDesc.finalState != destinationDesc.initialState)
    )
        return {};

    using UploadTask = __hidden_gpu_task_graph_builtin_uploads::UploadBufferTask;
    UploadTask::Payload* const payloadObject = NewArenaObject<UploadTask::Payload>(m_arena);
    if(!payloadObject)
        return {};
    ProvisionalPayloadOwner<UploadTask::Payload> payload(m_arena, payloadObject);
    payload->source = uploadDesc.source;
    payload->destinationResource = uploadDesc.destination;
    payload->destination = destinationResource.buffer;
    payload->destinationOffsetBytes = uploadDesc.destinationOffsetBytes;
    payload->finalState = uploadDesc.finalState;
    payload->acceptedToken = uploadDesc.acceptedToken;

    const GpuTaskResourceUse resourceUses[] = {
        GpuTaskResourceUse{
            .resource = uploadDesc.destination,
            .range = GpuTaskResourceRange{ .bufferRange = BufferRange(uploadDesc.destinationOffsetBytes, source->bytes.size()) },
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = uploadDesc.destination,
            .range = GpuTaskResourceRange{ .bufferRange = BufferRange(uploadDesc.destinationOffsetBytes, source->bytes.size()) },
            .requiredState = uploadDesc.finalState,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const usize resourceUseCount = uploadDesc.finalState == ResourceStates::CopyDest ? 1u : LengthOf(resourceUses);
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(resourceUses, resourceUseCount);
    return appendBuiltinTaskWithinMutation<UploadTask>(resolvedDesc, payload, mutation);
}

GpuTaskId GpuTaskGraph::addUploadTextureTask(
    const GpuTaskDesc& desc,
    const GpuUploadTextureTaskDesc& uploadDesc
){
    if(uploadDesc.acceptedToken)
        *uploadDesc.acceptedToken = {};

    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !GpuTaskGraphBuiltinDetail::BuiltinDeclarationHasNoCallerResourceUses(desc)
        || !validUploadBlob(uploadDesc.source)
        || !validResource(uploadDesc.destination)
        || uploadDesc.finalState == ResourceStates::Unknown
    )
        return {};

    const GpuUploadBlobNode* const source = findUploadBlob(uploadDesc.source);
    const GpuGraphResourceNode& destinationResource = m_resources[uploadDesc.destination.index];
    if(
        !source
        || source->bytes.empty()
        || destinationResource.type != GpuGraphResourceType::Texture
        || !destinationResource.texture
    )
        return {};

    usize requiredBytes = 0u;
    const TextureDesc& destinationDesc = destinationResource.texture->getCreationDescription();
    const FormatInfo& destinationFormatInfo = GetFormatInfo(destinationDesc.format);
    TextureUploadAspect::Enum resolvedAspect;
    if(!ResolveTextureUploadAspect(destinationFormatInfo, uploadDesc.aspect, resolvedAspect))
        return {};
    if(
        !GpuTaskGraphBuiltinDetail::UploadTextureTaskCanMaterializeRetainedState(
            destinationDesc,
            destinationResource.initialState,
            destinationResource.externalFinalState,
            uploadDesc.finalState
        )
        ||
        !GpuTaskGraphBuiltinDetail::ComputeTextureUploadByteSize(
            destinationDesc,
            uploadDesc.arraySlice,
            uploadDesc.mipLevel,
            uploadDesc.rowPitch,
            uploadDesc.depthPitch,
            uploadDesc.aspect,
            requiredBytes
        )
        || source->bytes.size() < requiredBytes
    )
        return {};

    using UploadTask = __hidden_gpu_task_graph_builtin_uploads::UploadTextureTask;
    UploadTask::Payload* const payloadObject = NewArenaObject<UploadTask::Payload>(m_arena);
    if(!payloadObject)
        return {};
    ProvisionalPayloadOwner<UploadTask::Payload> payload(m_arena, payloadObject);
    payload->source = uploadDesc.source;
    payload->destinationResource = uploadDesc.destination;
    payload->destination = destinationResource.texture;
    TextureSlice destinationSlice;
    destinationSlice.mipLevel = uploadDesc.mipLevel;
    destinationSlice.arraySlice = uploadDesc.arraySlice;
    payload->destinationSlice = destinationSlice.resolve(destinationDesc);
    payload->arraySlice = uploadDesc.arraySlice;
    payload->mipLevel = uploadDesc.mipLevel;
    payload->rowPitch = uploadDesc.rowPitch;
    payload->depthPitch = uploadDesc.depthPitch;
    payload->requiredBytes = requiredBytes;
    payload->finalState = uploadDesc.finalState;
    payload->acceptedToken = uploadDesc.acceptedToken;
    payload->aspect = uploadDesc.aspect;

    const GpuTaskResourceRange uploadRange{
        .textureSubresources = TextureSubresourceSet(uploadDesc.mipLevel, 1u, uploadDesc.arraySlice, 1u),
    };
    const GpuTaskResourceUse resourceUses[] = {
        GpuTaskResourceUse{
            .resource = uploadDesc.destination,
            .range = uploadRange,
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = uploadDesc.destination,
            .range = uploadRange,
            .requiredState = uploadDesc.finalState,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const usize resourceUseCount = uploadDesc.finalState == ResourceStates::CopyDest ? 1u : LengthOf(resourceUses);
    GpuTaskDesc resolvedDesc = desc;
    GpuTaskCommandRequirements commands{ GpuQueueCapability::Transfer };
    if(resolvedAspect != TextureUploadAspect::Color)
        commands.requiredCapabilities |= GpuQueueCapability::Graphics;
    resolvedDesc.setResourceUses(resourceUses, resourceUseCount);
    return appendBuiltinTaskWithinMutation<UploadTask>(resolvedDesc, payload, mutation, commands);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

