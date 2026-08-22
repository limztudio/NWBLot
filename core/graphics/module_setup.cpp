// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module_internal.h"

#include "backend_selection.h"
#include "task_graph/compiler.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_setup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ComputeTextureUploadByteSize(const Graphics::TextureSetupDesc& desc, usize& outRequiredBytes){
    outRequiredBytes = 0;

    const TextureDesc& textureDesc = desc.textureDesc;
    if(textureDesc.width == 0 || textureDesc.height == 0 || textureDesc.depth == 0 || textureDesc.mipLevels == 0 || textureDesc.arraySize == 0)
        return false;
    if(textureDesc.sampleCount != 1)
        return false;
    if(desc.mipLevel >= textureDesc.mipLevels || desc.arraySlice >= textureDesc.arraySize)
        return false;
    if(static_cast<usize>(textureDesc.format) >= static_cast<usize>(Format::kCount))
        return false;

    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);
    if(formatBlockWidth == 0 || formatBlockHeight == 0 || formatInfo.bytesPerBlock == 0)
        return false;

    const u32 width = Max<u32>(1u, textureDesc.width >> desc.mipLevel);
    const u32 height = Max<u32>(1u, textureDesc.height >> desc.mipLevel);
    const u32 depth = Max<u32>(1u, textureDesc.depth >> desc.mipLevel);

    const u64 blockCountX = DivideUp(static_cast<u64>(width), static_cast<u64>(formatBlockWidth));
    const u64 blockCountY = DivideUp(static_cast<u64>(height), static_cast<u64>(formatBlockHeight));
    if(blockCountX > Limit<u64>::s_Max / formatInfo.bytesPerBlock)
        return false;

    const u64 naturalRowPitch = blockCountX * formatInfo.bytesPerBlock;
    const u64 effectiveRowPitch = desc.rowPitch != 0 ? static_cast<u64>(desc.rowPitch) : naturalRowPitch;
    if(effectiveRowPitch == 0 || effectiveRowPitch < naturalRowPitch || (effectiveRowPitch % formatInfo.bytesPerBlock) != 0)
        return false;
    if(blockCountY > Limit<u64>::s_Max / effectiveRowPitch)
        return false;

    const u64 packedSlicePitch = effectiveRowPitch * blockCountY;
    const u64 effectiveDepthPitch = desc.depthPitch != 0 ? static_cast<u64>(desc.depthPitch) : packedSlicePitch;
    if(effectiveDepthPitch == 0 || effectiveDepthPitch < packedSlicePitch || (effectiveDepthPitch % effectiveRowPitch) != 0)
        return false;

    if(depth > 1 && static_cast<u64>(depth - 1) > (Limit<u64>::s_Max - packedSlicePitch) / effectiveDepthPitch)
        return false;

    const u64 requiredBytes = depth > 1
        ? effectiveDepthPitch * static_cast<u64>(depth - 1) + packedSlicePitch
        : packedSlicePitch
    ;
    if(requiredBytes > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    outRequiredBytes = static_cast<usize>(requiredBytes);
    return true;
}


struct BufferSetupSubmissionData{
    BufferHandle& buffer;
    const Graphics::BufferSetupDesc& setupDesc;
    const BufferDesc& uploadDesc;
    CommandQueue::Enum uploadQueue = CommandQueue::Graphics;
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting;
    QueueSubmissionToken& uploadToken;
};

[[nodiscard]] static GpuTaskId DeclareBufferSetupUpload(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *static_cast<BufferSetupSubmissionData*>(userData);
    const GpuGraphResourceId destination = graph.importBuffer(
        submissionData.buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("graphics.setup_buffer.resource"))
            .setMarkerLabel("Setup Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(submissionData.uploadDesc.initialState)
            .setQueueSharing(submissionData.uploadDesc.queueSharing)
    );
    const GpuUploadBlobId source = graph.copyUploadData(
        submissionData.setupDesc.data,
        submissionData.setupDesc.dataSize,
        alignof(u32)
    );
    if(!destination.valid() || !source.valid())
        return {};

    GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(Name("graphics.setup_buffer.upload"))
        .setMarkerLabel("Setup Buffer Upload")
        .setQueue(GraphicsModuleDetail::SetupUploadGraphQueueRequest(submissionData.uploadQueue))
        .setScheduling(GraphicsModuleDetail::SetupUploadGraphScheduling(
            submissionData.setupDesc.dataSize,
            submissionData.sameClassRouting.enabled,
            submissionData.sameClassRouting.crossesQueueFamily
        ))
    ;
    return graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destination,
            .destinationOffsetBytes = submissionData.setupDesc.destOffsetBytes,
            .finalState = GraphicsModuleDetail::SetupUploadGraphFinalState(submissionData.uploadDesc.initialState),
            .acceptedToken = &submissionData.uploadToken,
        }
    );
}


struct TextureSetupSubmissionData{
    TextureHandle& texture;
    const Graphics::TextureSetupDesc& setupDesc;
    const TextureDesc& uploadDesc;
    CommandQueue::Enum uploadQueue = CommandQueue::Graphics;
    GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting;
    QueueSubmissionToken& uploadToken;
};

[[nodiscard]] static GpuTaskId DeclareTextureSetupUpload(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *static_cast<TextureSetupSubmissionData*>(userData);
    const GpuGraphResourceId destination = graph.importTexture(
        submissionData.texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("graphics.setup_texture.resource"))
            .setMarkerLabel("Setup Texture")
            .setType(GpuGraphResourceType::Texture)
            // Vulkan creates this texture in UNDEFINED layout. The upload task owns its first concrete transition;
            // the task's finalState still publishes the caller-requested logical state.
            .setInitialState(ResourceStates::Unknown)
            .setQueueSharing(submissionData.uploadDesc.queueSharing)
    );
    const GpuUploadBlobId source = graph.copyUploadData(
        submissionData.setupDesc.data,
        submissionData.setupDesc.uploadDataSize,
        alignof(u32)
    );
    if(!destination.valid() || !source.valid())
        return {};

    GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(Name("graphics.setup_texture.upload"))
        .setMarkerLabel("Setup Texture Upload")
        .setQueue(GraphicsModuleDetail::SetupUploadGraphQueueRequest(submissionData.uploadQueue))
        .setScheduling(GraphicsModuleDetail::SetupUploadGraphScheduling(
            submissionData.setupDesc.uploadDataSize,
            submissionData.sameClassRouting.enabled,
            submissionData.sameClassRouting.crossesQueueFamily
        ))
    ;
    return graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destination,
            .arraySlice = submissionData.setupDesc.arraySlice,
            .mipLevel = submissionData.setupDesc.mipLevel,
            .rowPitch = submissionData.setupDesc.rowPitch,
            .depthPitch = submissionData.setupDesc.depthPitch,
            .finalState = GraphicsModuleDetail::SetupUploadGraphFinalState(submissionData.uploadDesc.initialState),
            .acceptedToken = &submissionData.uploadToken,
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateBufferSetupUpload(const Graphics::BufferSetupDesc& desc){
    if(desc.dataSize == 0)
        return true;
    if(!desc.data){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up buffer '{}': upload data is null"), StringConvert(desc.bufferDesc.debugName.c_str()));
        return false;
    }
    if(desc.destOffsetBytes > desc.bufferDesc.byteSize || static_cast<u64>(desc.dataSize) > desc.bufferDesc.byteSize - desc.destOffsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up buffer '{}': upload range offset {} size {} exceeds buffer size {}")
            , StringConvert(desc.bufferDesc.debugName.c_str())
            , desc.destOffsetBytes
            , static_cast<u64>(desc.dataSize)
            , desc.bufferDesc.byteSize
        );
        return false;
    }
    // Both the legacy CommandList staging path and the graph-owned upload task lower to VkBufferCopy. The API
    // cannot truthfully report a successful upload for a region Vulkan rejects, so fail before creating either
    // a native command list or a graph packet.
    if((desc.destOffsetBytes & (sizeof(u32) - 1u)) != 0u || (desc.dataSize & (sizeof(u32) - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up buffer '{}': upload offset and size must be 4-byte aligned")
            , StringConvert(desc.bufferDesc.debugName.c_str())
        );
        return false;
    }
    // A retained buffer must publish a concrete state. Unknown would be restored at native close without a
    // graph-visible final-state contract for the next consumer.
    if(desc.bufferDesc.keepInitialState && desc.bufferDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up buffer '{}': keep-initial-state uploads require a concrete initial state")
            , StringConvert(desc.bufferDesc.debugName.c_str())
        );
        return false;
    }

    return true;
}

bool ValidateTextureSetupUpload(const Graphics::TextureSetupDesc& desc){
    if(!desc.data && desc.uploadDataSize == 0)
        return true;
    if(!desc.data || desc.uploadDataSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': upload data and size must both be provided"), StringConvert(desc.textureDesc.name.c_str()));
        return false;
    }

    usize requiredBytes = 0;
    if(!__hidden_graphics_setup::ComputeTextureUploadByteSize(desc, requiredBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': invalid upload layout"), StringConvert(desc.textureDesc.name.c_str()));
        return false;
    }

    const FormatInfo& formatInfo = GetFormatInfo(desc.textureDesc.format);
    // VkBufferImageCopy permits exactly one depth/stencil aspect per copy. The public setup descriptor has one
    // packed payload and cannot state separate depth/stencil byte layouts, so accepting it would either skip the
    // upload in debug or issue an explicitly unsupported native copy in release.
    if(formatInfo.hasDepth && formatInfo.hasStencil){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': combined depth/stencil uploads require an explicit per-aspect upload API"), StringConvert(desc.textureDesc.name.c_str()));
        return false;
    }
    // A retained texture must publish a concrete state. Leaving it Unknown makes command-list close restore an
    // untracked layout, so no graph task or later consumer can safely describe the uploaded contents.
    if(desc.textureDesc.keepInitialState && desc.textureDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': keep-initial-state uploads require a concrete initial state"), StringConvert(desc.textureDesc.name.c_str()));
        return false;
    }
    if(desc.uploadDataSize < requiredBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': upload data size {} is smaller than required size {}")
            , StringConvert(desc.textureDesc.name.c_str())
            , desc.uploadDataSize
            , requiredBytes
        );
        return false;
    }

    return true;
}

bool ValidateMeshSetupDesc(const Graphics::MeshSetupDesc& desc){
    if(!desc.vertexData || desc.vertexDataSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': vertex data is missing"), StringConvert(desc.vertexBufferName.c_str()));
        return false;
    }
    if(desc.vertexStride == 0 || (desc.vertexDataSize % static_cast<usize>(desc.vertexStride)) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': vertex data size is not aligned to vertex stride"), StringConvert(desc.vertexBufferName.c_str()));
        return false;
    }

    const usize vertexCount = desc.vertexDataSize / static_cast<usize>(desc.vertexStride);
    if(vertexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': vertex count exceeds u32 range"), StringConvert(desc.vertexBufferName.c_str()));
        return false;
    }

    if((desc.indexData == nullptr) != (desc.indexDataSize == 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': index data and size must both be provided"), StringConvert(desc.indexBufferName.c_str()));
        return false;
    }
    if(desc.indexDataSize > 0){
        const usize indexStride = desc.use32BitIndices ? sizeof(u32) : sizeof(u16);
        if((desc.indexDataSize % indexStride) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': index data size is not aligned to index stride"), StringConvert(desc.indexBufferName.c_str()));
            return false;
        }
        const usize indexCount = desc.indexDataSize / indexStride;
        if(indexCount > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh '{}': index count exceeds u32 range"), StringConvert(desc.indexBufferName.c_str()));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BufferHandle Graphics::setupBuffer(const BufferSetupDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};
    if(!GraphicsModuleDetail::ValidateBufferSetupUpload(desc))
        return {};

    if(!desc.data || desc.dataSize == 0)
        return device.createBuffer(desc.bufferDesc);

    const CommandQueue::Enum uploadQueue = GraphicsModuleDetail::ResolveSetupUploadQueue(
        device,
        desc.queue,
        desc.dataSize,
        desc.bufferDesc.initialState != ResourceStates::Unknown
    );
    const GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting =
        GraphicsModuleDetail::ResolveSetupUploadSameClassRouting(device, uploadQueue, desc.dataSize)
    ;
    BufferDesc uploadDesc = desc.bufferDesc;
    uploadDesc.queueSharing = GraphicsModuleDetail::ResolveSetupUploadQueueSharing(
        uploadDesc.queueSharing,
        uploadQueue,
        sameClassRouting.crossesQueueFamily
    );
    BufferHandle buffer = device.createBuffer(uploadDesc);
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create setup buffer '{}'"), StringConvert(desc.bufferDesc.debugName.c_str()));
        return {};
    }

    QueueSubmissionToken uploadToken;
    __hidden_graphics_setup::BufferSetupSubmissionData submissionData{
        .buffer = buffer,
        .setupDesc = desc,
        .uploadDesc = uploadDesc,
        .uploadQueue = uploadQueue,
        .sameClassRouting = sameClassRouting,
        .uploadToken = uploadToken,
    };
    const bool submitted = GraphicsModuleDetail::SubmitGraphOwnedSetupUpload(
        *this,
        m_allocator.getObjectArena(),
        uploadDesc.queueSharing,
        uploadQueue,
        &submissionData,
        &__hidden_graphics_setup::DeclareBufferSetupUpload,
        uploadToken,
        sameClassRouting.enabled,
        sameClassRouting.enabled ? sameClassRouting.primaryQueue : GpuPhysicalQueueId{}
    );
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to submit graph-owned setup buffer upload '{}'"), StringConvert(desc.bufferDesc.debugName.c_str()));
        return {};
    }
    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;

    return buffer;
}

TextureHandle Graphics::setupTexture(const TextureSetupDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};
    if(!GraphicsModuleDetail::ValidateTextureSetupUpload(desc))
        return {};

    if(!desc.data || desc.uploadDataSize == 0)
        return device.createTexture(desc.textureDesc);
    if(
        desc.textureDesc.keepInitialState
        && (desc.textureDesc.arraySize != 1u || desc.textureDesc.mipLevels != 1u)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': a fresh retained setup upload must cover every mip and array slice; use uploadTextureBatch")
            , StringConvert(desc.textureDesc.name.c_str())
        );
        return {};
    }

    const CommandQueue::Enum uploadQueue = GraphicsModuleDetail::ResolveSetupUploadQueue(
        device,
        desc.queue,
        desc.uploadDataSize,
        desc.textureDesc.initialState != ResourceStates::Unknown
    );
    const GraphicsModuleDetail::SetupUploadSameClassRouting sameClassRouting =
        GraphicsModuleDetail::ResolveSetupUploadSameClassRouting(device, uploadQueue, desc.uploadDataSize)
    ;
    TextureDesc uploadDesc = desc.textureDesc;
    uploadDesc.queueSharing = GraphicsModuleDetail::ResolveSetupUploadQueueSharing(
        uploadDesc.queueSharing,
        uploadQueue,
        sameClassRouting.crossesQueueFamily
    );
    TextureHandle texture = device.createTexture(uploadDesc);
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create setup texture '{}'"), StringConvert(desc.textureDesc.name.c_str()));
        return {};
    }

    QueueSubmissionToken uploadToken;
    __hidden_graphics_setup::TextureSetupSubmissionData submissionData{
        .texture = texture,
        .setupDesc = desc,
        .uploadDesc = uploadDesc,
        .uploadQueue = uploadQueue,
        .sameClassRouting = sameClassRouting,
        .uploadToken = uploadToken,
    };
    const bool submitted = GraphicsModuleDetail::SubmitGraphOwnedSetupUpload(
        *this,
        m_allocator.getObjectArena(),
        uploadDesc.queueSharing,
        uploadQueue,
        &submissionData,
        &__hidden_graphics_setup::DeclareTextureSetupUpload,
        uploadToken,
        sameClassRouting.enabled,
        sameClassRouting.enabled ? sameClassRouting.primaryQueue : GpuPhysicalQueueId{}
    );
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to submit graph-owned setup texture upload '{}'"), StringConvert(desc.textureDesc.name.c_str()));
        return {};
    }
    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;

    return texture;
}

Graphics::MeshResource Graphics::setupMesh(const MeshSetupDesc& desc)const{
    if(!GraphicsModuleDetail::ValidateMeshSetupDesc(desc))
        return {};

    MeshResource output;
    output.vertexStride = desc.vertexStride;

    if(desc.vertexData && desc.vertexDataSize > 0){
        BufferDesc vertexBufferDesc;
        vertexBufferDesc.setByteSize(static_cast<u64>(desc.vertexDataSize));
        vertexBufferDesc.setIsVertexBuffer(true);
        vertexBufferDesc.setDebugName(desc.vertexBufferName);
        vertexBufferDesc.enableAutomaticStateTracking(ResourceStates::VertexBuffer);

        BufferSetupDesc vertexSetup;
        vertexSetup.bufferDesc = vertexBufferDesc;
        vertexSetup.data = desc.vertexData;
        vertexSetup.dataSize = desc.vertexDataSize;
        vertexSetup.queue = desc.queue;

        output.vertexBuffer = setupBuffer(vertexSetup);
        if(!output.vertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh vertex buffer '{}'"), StringConvert(desc.vertexBufferName.c_str()));
            return MeshResource{};
        }
    }

    if(desc.indexData && desc.indexDataSize > 0){
        BufferDesc indexBufferDesc;
        indexBufferDesc.setByteSize(static_cast<u64>(desc.indexDataSize));
        indexBufferDesc.setIsIndexBuffer(true);
        indexBufferDesc.setDebugName(desc.indexBufferName);
        indexBufferDesc.enableAutomaticStateTracking(ResourceStates::IndexBuffer);

        BufferSetupDesc indexSetup;
        indexSetup.bufferDesc = indexBufferDesc;
        indexSetup.data = desc.indexData;
        indexSetup.dataSize = desc.indexDataSize;
        indexSetup.queue = desc.queue;

        output.indexBuffer = setupBuffer(indexSetup);
        if(!output.indexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up mesh index buffer '{}'"), StringConvert(desc.indexBufferName.c_str()));
            return MeshResource{};
        }
    }

    if(output.vertexStride > 0 && desc.vertexDataSize > 0)
        output.vertexCount = static_cast<u32>(desc.vertexDataSize / static_cast<usize>(output.vertexStride));

    if(desc.indexDataSize > 0){
        const usize indexStride = desc.use32BitIndices ? sizeof(u32) : sizeof(u16);
        output.indexFormat = desc.use32BitIndices ? Format::R32_UINT : Format::R16_UINT;
        output.indexCount = static_cast<u32>(desc.indexDataSize / indexStride);
    }

    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

