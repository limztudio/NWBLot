// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiled_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_builtin_uploads{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UploadBufferTask{
    struct Payload{
        GpuUploadBlobId source;
        BufferHandle destination;
        u64 destinationOffsetBytes = 0u;
        ResourceStates::Mask finalState = ResourceStates::CopyDest;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        usize byteSize = 0u;
        const void* const bytes = context.taskGraph.uploadBlobData(payload.source, byteSize);
        if(
            !payload.destination
            || !bytes
            || byteSize == 0u
            || payload.finalState == ResourceStates::Unknown
            // Upload bytes are intentionally not part of the optional command-IR format. Reject capture rather than
            // emitting an incomplete record that could replay with stale caller data.
            || context.commandIrCapture
        )
            return false;

        // The compiler tracks `finalState` for later tasks, but the native write itself requires CopyDest. Make the
        // internal transition explicit and commit it before vkCmdCopyBuffer; writeBuffer only queues its own state.
        commandList.setBufferState(payload.destination.get(), ResourceStates::CopyDest);
        commandList.commitBarriers();
        if(!commandList.tryWriteBuffer(payload.destination.get(), bytes, byteSize, payload.destinationOffsetBytes))
            return false;
        if(payload.finalState != ResourceStates::CopyDest){
            commandList.setBufferState(payload.destination.get(), payload.finalState);
            commandList.commitBarriers();
        }
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.acceptedToken)
            *payload.acceptedToken = {};
    }
};

struct UploadTextureTask{
    struct Payload{
        GpuUploadBlobId source;
        TextureHandle destination;
        u32 arraySlice = 0u;
        u32 mipLevel = 0u;
        usize rowPitch = 0u;
        usize depthPitch = 0u;
        ResourceStates::Mask finalState = ResourceStates::CopyDest;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        usize byteSize = 0u;
        const void* const bytes = context.taskGraph.uploadBlobData(payload.source, byteSize);
        if(
            !payload.destination
            || !bytes
            || byteSize == 0u
            || payload.finalState == ResourceStates::Unknown
            || context.commandIrCapture
        )
            return false;

        const TextureSubresourceSet subresources(payload.mipLevel, 1u, payload.arraySlice, 1u);
        // See UploadBufferTask: native writes need CopyDest even though graph analysis publishes finalState.
        commandList.setTextureState(payload.destination.get(), subresources, ResourceStates::CopyDest);
        commandList.commitBarriers();
        if(!commandList.tryWriteTexture(
            payload.destination.get(),
            payload.arraySlice,
            payload.mipLevel,
            bytes,
            payload.rowPitch,
            payload.depthPitch
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

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.acceptedToken)
            *payload.acceptedToken = {};
    }
};
template<typename ResourceDesc>
[[nodiscard]] static bool BuiltInTaskCanMaterializeRetainedState(
    const ResourceDesc& resourceDesc,
    const ResourceStates::Mask graphInitialState,
    const ResourceStates::Mask externalFinalState
)noexcept{
    // Retained resources restore to their descriptor state when a native packet closes. The graph may still use a
    // different built-in state when it explicitly starts from that descriptor state: compiler-owned barriers then
    // establish the primitive state and the recorded packet exports the restored state for the next packet. A
    // mismatched graph declaration has no native source that this helper can prove, and a terminal external state
    // must agree with the close-time restoration before the graph publishes its handoff.
    if(!resourceDesc.keepInitialState)
        return true;
    return resourceDesc.initialState != ResourceStates::Unknown
        && graphInitialState == resourceDesc.initialState
        && (
            externalFinalState == ResourceStates::Unknown
            || externalFinalState == resourceDesc.initialState
        )
    ;
}

[[nodiscard]] static bool UploadTextureTaskCanMaterializeRetainedState(
    const TextureDesc& resourceDesc,
    const ResourceStates::Mask graphInitialState,
    const ResourceStates::Mask externalFinalState,
    const ResourceStates::Mask uploadFinalState
)noexcept{
    if(!resourceDesc.keepInitialState)
        return true;
    if(
        resourceDesc.initialState == ResourceStates::Unknown
        || uploadFinalState != resourceDesc.initialState
        || (
            externalFinalState != ResourceStates::Unknown
            && externalFinalState != resourceDesc.initialState
        )
    )
        return false;
    // A texture upload is a first write. Its recorder materializes CopyDest and then publishes finalState, so an
    // explicit Unknown graph import is safe for a fresh image while all other built-ins retain the stricter source
    // requirement above.
    return graphInitialState == ResourceStates::Unknown || graphInitialState == resourceDesc.initialState;
}

[[nodiscard]] static bool ComputeTextureUploadByteSize(
    const TextureDesc& textureDesc,
    const u32 arraySlice,
    const u32 mipLevel,
    const usize rowPitch,
    const usize depthPitch,
    usize& outRequiredBytes
)noexcept{
    outRequiredBytes = 0u;
    if(
        textureDesc.width == 0u
        || textureDesc.height == 0u
        || textureDesc.depth == 0u
        || textureDesc.mipLevels == 0u
        || textureDesc.arraySize == 0u
        || textureDesc.sampleCount != 1u
        || mipLevel >= textureDesc.mipLevels
        || arraySlice >= textureDesc.arraySize
        || static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::kCount)
    )
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const u32 blockWidth = GetFormatBlockWidth(formatInfo);
    const u32 blockHeight = GetFormatBlockHeight(formatInfo);
    // CommandList::writeTexture emits one aspect for the entire texture upload. Keep the graph helper on the same
    // contract: combined depth/stencil images need a specialized per-aspect path rather than this color-style copy.
    if(
        blockWidth == 0u
        || blockHeight == 0u
        || formatInfo.bytesPerBlock == 0u
        || (formatInfo.hasDepth && formatInfo.hasStencil)
    )
        return false;

    const u32 width = Max<u32>(1u, textureDesc.width >> mipLevel);
    const u32 height = Max<u32>(1u, textureDesc.height >> mipLevel);
    const u32 depth = textureDesc.dimension == TextureDimension::Texture3D
        ? Max<u32>(1u, textureDesc.depth >> mipLevel)
        : 1u
    ;
    const u64 blockCountX = DivideUp(static_cast<u64>(width), static_cast<u64>(blockWidth));
    const u64 blockCountY = DivideUp(static_cast<u64>(height), static_cast<u64>(blockHeight));
    if(blockCountX > Limit<u64>::s_Max / formatInfo.bytesPerBlock)
        return false;

    const u64 naturalRowPitch = blockCountX * formatInfo.bytesPerBlock;
    const u64 effectiveRowPitch = rowPitch != 0u ? static_cast<u64>(rowPitch) : naturalRowPitch;
    if(
        effectiveRowPitch == 0u
        || effectiveRowPitch < naturalRowPitch
        || (effectiveRowPitch % formatInfo.bytesPerBlock) != 0u
        || blockCountY > Limit<u64>::s_Max / effectiveRowPitch
    )
        return false;

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = depthPitch != 0u ? static_cast<u64>(depthPitch) : packedSlicePitch;
    if(
        effectiveDepthPitch == 0u
        || effectiveDepthPitch < packedSlicePitch
        || (effectiveDepthPitch % effectiveRowPitch) != 0u
    )
        return false;

    // These pitches become VkBufferImageCopy's 32-bit texel fields in CommandList::writeTexture. Validate them at
    // declaration time so an accepted graph upload cannot lower to a native no-op after the command list rejects it.
    const u64 bufferRowBlocks = effectiveRowPitch / formatInfo.bytesPerBlock;
    const u64 bufferImageBlocks = effectiveDepthPitch / effectiveRowPitch;
    if(
        bufferRowBlocks > Limit<u64>::s_Max / blockWidth
        || bufferImageBlocks > Limit<u64>::s_Max / blockHeight
        || bufferRowBlocks * blockWidth > Limit<u32>::s_Max
        || bufferImageBlocks * blockHeight > Limit<u32>::s_Max
    )
        return false;

    if(depth > 1u && static_cast<u64>(depth - 1u) > (Limit<u64>::s_Max - packedSlicePitch) / effectiveDepthPitch)
        return false;

    const u64 requiredBytes = depth > 1u
        ? effectiveDepthPitch * static_cast<u64>(depth - 1u) + packedSlicePitch
        : packedSlicePitch
    ;
    if(requiredBytes > static_cast<u64>(Limit<usize>::s_Max))
        return false;
    outRequiredBytes = static_cast<usize>(requiredBytes);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuUploadBlobId GpuTaskGraph::copyUploadData(
    const void* const data,
    const usize byteSize,
    const usize alignment
){
    if(
        !data
        || byteSize == 0u
        || alignment == 0u
        || (alignment & (alignment - 1u)) != 0u
        || m_uploadBlobs.size() >= Limit<u32>::s_Max
    )
        return {};

    m_uploadBlobs.emplace_back(m_arena);
    GpuUploadBlobNode& blob = m_uploadBlobs.back();
    const u8* const sourceBytes = static_cast<const u8*>(data);
    blob.bytes.assign(sourceBytes, sourceBytes + byteSize);
    if(blob.bytes.size() != byteSize){
        m_uploadBlobs.pop_back();
        return {};
    }
    return GpuUploadBlobId{ static_cast<u32>(m_uploadBlobs.size() - 1u), m_generation };
}

GpuTaskId GpuTaskGraph::addUploadBufferTask(
    const GpuTaskDesc& desc,
    const GpuUploadBufferTaskDesc& uploadDesc
){
    if(uploadDesc.acceptedToken)
        *uploadDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !validUploadBlob(uploadDesc.source)
        || !validResource(uploadDesc.destination)
        || uploadDesc.finalState == ResourceStates::Unknown
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
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

    const BufferDesc& destinationDesc = destinationResource.buffer->getDescription();
    if(
        uploadDesc.destinationOffsetBytes > destinationDesc.byteSize
        || source->bytes.size() > destinationDesc.byteSize - uploadDesc.destinationOffsetBytes
        // Keep declaration acceptance aligned with CommandList::tryWriteBuffer so a built-in upload cannot fail
        // only during late packet recording.
        || (uploadDesc.destinationOffsetBytes & (sizeof(u32) - 1u)) != 0u
        || (source->bytes.size() & (sizeof(u32) - 1u)) != 0u
        || !__hidden_gpu_task_graph_builtin_uploads::BuiltInTaskCanMaterializeRetainedState(
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
    UploadTask::Payload* const payload = NewArenaObject<UploadTask::Payload>(m_arena);
    if(!payload)
        return {};
    payload->source = uploadDesc.source;
    payload->destination = destinationResource.buffer;
    payload->destinationOffsetBytes = uploadDesc.destinationOffsetBytes;
    payload->finalState = uploadDesc.finalState;
    payload->acceptedToken = uploadDesc.acceptedToken;

    const GpuTaskResourceUse resourceUse{
        .resource = uploadDesc.destination,
        .range = {},
        .requiredState = uploadDesc.finalState,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<UploadTask>,
        &AcceptPayload<UploadTask>,
        &DiscardPayload<UploadTask>,
        &DestroyPayload<UploadTask::Payload>,
        sizeof(UploadTask::Payload)
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<UploadTask>,
            &DestroyPayload<UploadTask::Payload>
        );
    return task;
}

GpuTaskId GpuTaskGraph::addUploadTextureTask(
    const GpuTaskDesc& desc,
    const GpuUploadTextureTaskDesc& uploadDesc
){
    if(uploadDesc.acceptedToken)
        *uploadDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !validUploadBlob(uploadDesc.source)
        || !validResource(uploadDesc.destination)
        || uploadDesc.finalState == ResourceStates::Unknown
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
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
    const TextureDesc& destinationDesc = destinationResource.texture->getDescription();
    if(
        !__hidden_gpu_task_graph_builtin_uploads::UploadTextureTaskCanMaterializeRetainedState(
            destinationDesc,
            destinationResource.initialState,
            destinationResource.externalFinalState,
            uploadDesc.finalState
        )
        ||
        !__hidden_gpu_task_graph_builtin_uploads::ComputeTextureUploadByteSize(
            destinationDesc,
            uploadDesc.arraySlice,
            uploadDesc.mipLevel,
            uploadDesc.rowPitch,
            uploadDesc.depthPitch,
            requiredBytes
        )
        || source->bytes.size() < requiredBytes
    )
        return {};

    using UploadTask = __hidden_gpu_task_graph_builtin_uploads::UploadTextureTask;
    UploadTask::Payload* const payload = NewArenaObject<UploadTask::Payload>(m_arena);
    if(!payload)
        return {};
    payload->source = uploadDesc.source;
    payload->destination = destinationResource.texture;
    payload->arraySlice = uploadDesc.arraySlice;
    payload->mipLevel = uploadDesc.mipLevel;
    payload->rowPitch = uploadDesc.rowPitch;
    payload->depthPitch = uploadDesc.depthPitch;
    payload->finalState = uploadDesc.finalState;
    payload->acceptedToken = uploadDesc.acceptedToken;

    const GpuTaskResourceUse resourceUse{
        .resource = uploadDesc.destination,
        .range = GpuTaskResourceRange{
            .textureSubresources = TextureSubresourceSet(uploadDesc.mipLevel, 1u, uploadDesc.arraySlice, 1u),
        },
        .requiredState = uploadDesc.finalState,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<UploadTask>,
        &AcceptPayload<UploadTask>,
        &DiscardPayload<UploadTask>,
        &DestroyPayload<UploadTask::Payload>,
        sizeof(UploadTask::Payload)
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<UploadTask>,
            &DestroyPayload<UploadTask::Payload>
        );
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

