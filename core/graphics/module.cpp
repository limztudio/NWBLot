// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "backend_selection.h"
#include "task_graph/compiler.h"
#include "task_graph/packet_runtime.h"

#include <core/common/log.h>
#include <core/telemetry/session.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UploadBytes = Vector<u8, Alloc::GlobalArena>;
constexpr u32 s_DefaultWaveLaneCount = 64u;
// Before per-upload timing exists, retain small setup copies on Graphics. Large asset payloads are the first
// Transfer migration target; callers that know a small upload benefits from a dedicated transport can still request
// CommandQueue::Transfer explicitly.
constexpr usize s_TransferPreferredUploadMinimumBytes = 1024u * 1024u;


[[nodiscard]] static ResourceQueueSharing::Mask QueueSharingBitForQueue(const CommandQueue::Enum queue)noexcept{
    switch(queue){
    case CommandQueue::Graphics:
        return ResourceQueueSharing::Graphics;
    case CommandQueue::Compute:
        return ResourceQueueSharing::AsyncCompute;
    case CommandQueue::Transfer:
        return ResourceQueueSharing::Transfer;
    default:
        return ResourceQueueSharing::Exclusive;
    }
}

[[nodiscard]] static bool QueueSharingIncludesQueue(
    const ResourceQueueSharing::Mask sharing,
    const CommandQueue::Enum queue
)noexcept{
    const u8 queueBit = static_cast<u8>(QueueSharingBitForQueue(queue));
    return queueBit != 0u && (static_cast<u8>(sharing) & queueBit) != 0u;
}

// The public setup descriptors predate physical Transfer transport. When an upload moves away from Graphics, retain
// the descriptor's declared consumers and add the producer family. An otherwise-exclusive setup resource keeps its
// established Graphics consumer contract, which lets the returned handle remain immediately usable by legacy code.
[[nodiscard]] static ResourceQueueSharing::Mask ResolveSetupUploadQueueSharing(
    const ResourceQueueSharing::Mask requestedSharing,
    const CommandQueue::Enum uploadQueue
)noexcept{
    if(uploadQueue == CommandQueue::Graphics)
        return requestedSharing;

    const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive
        ? ResourceQueueSharing::Graphics
        : requestedSharing
    ;
    return static_cast<ResourceQueueSharing::Mask>(
        static_cast<u8>(baseSharing) | static_cast<u8>(QueueSharingBitForQueue(uploadQueue))
    );
}

[[nodiscard]] static CommandQueue::Enum ResolveTransferPreferredQueue(GraphicsBackend::Device& device)noexcept{
    if(device.getQueue(CommandQueue::Transfer))
        return CommandQueue::Transfer;
    if(device.getQueue(CommandQueue::Compute))
        return CommandQueue::Compute;
    return CommandQueue::Graphics;
}

[[nodiscard]] static CommandQueue::Enum ResolveSetupUploadQueue(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum requestedQueue,
    const usize uploadBytes,
    const bool hasKnownFinalState
)noexcept{
    switch(requestedQueue){
    case CommandQueue::kCount:
        if(uploadBytes < s_TransferPreferredUploadMinimumBytes || !hasKnownFinalState)
            return CommandQueue::Graphics;
        return ResolveTransferPreferredQueue(device);
    case CommandQueue::Transfer:
        return hasKnownFinalState ? ResolveTransferPreferredQueue(device) : CommandQueue::Graphics;
    case CommandQueue::Compute:
        return hasKnownFinalState && device.getQueue(CommandQueue::Compute)
            ? CommandQueue::Compute
            : CommandQueue::Graphics
        ;
    case CommandQueue::Graphics:
        return CommandQueue::Graphics;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Graphics: setup upload requested an invalid command queue"));
        return CommandQueue::Graphics;
    }
}

// A setup API returns only a resource handle, not an external-completion handle that every future graph import must
// consume. Bridge an accepted upload onto every declared consumer queue before returning, so later submissions retain
// the old same-queue readiness guarantee while the producer may use Transfer or Compute.
[[nodiscard]] static bool BridgeSetupUploadToConsumerQueues(
    GraphicsBackend::Device& device,
    const QueueSubmissionToken& uploadToken,
    const ResourceQueueSharing::Mask queueSharing
){
    if(!uploadToken.valid())
        return false;

    constexpr CommandQueue::Enum consumerQueues[] = {
        CommandQueue::Graphics,
        CommandQueue::Compute,
        CommandQueue::Transfer,
    };
    for(const CommandQueue::Enum consumerQueue : consumerQueues){
        if(
            consumerQueue == uploadToken.queue
            || !QueueSharingIncludesQueue(queueSharing, consumerQueue)
            || !device.getQueue(consumerQueue)
        )
            continue;

        QueueSubmissionDesc bridgeDesc;
        bridgeDesc.setWaitTokens(&uploadToken, 1u);
        const QueueSubmissionToken bridgeToken = device.executeCommandLists(
            nullptr,
            0u,
            consumerQueue,
            bridgeDesc
        );
        if(!bridgeToken.valid()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Graphics: failed to bridge setup upload readiness from queue {} to queue {}"),
                static_cast<u32>(uploadToken.queue),
                static_cast<u32>(consumerQueue)
            );
            return false;
        }
    }
    return true;
}

// The synchronous setup APIs predate frame-owned graph declaration, but their ordinary buffer/texture uploads
// have all of the information a graph task needs: an immutable source blob, one destination resource, an exact
// final state, and an already-resolved physical transport. Keep the public API synchronous while using the same
// compiler/recorder/transaction path as graph-owned frame uploads. The compatibility bridge remains below the
// graph packet because setup callers receive a resource handle rather than an external-completion handle.
[[nodiscard]] static GpuQueueRequest SetupUploadGraphQueueRequest(const CommandQueue::Enum uploadQueue)noexcept{
    GpuQueueRequest request;
    request.requiredCapabilities = GpuQueueCapability::Transfer;
    request.allowFallback = false;
    request.compilerMayOverridePreference = false;
    switch(uploadQueue){
    case CommandQueue::Graphics:
        request.preferredQueue = GpuQueuePreference::Graphics;
        break;
    case CommandQueue::Compute:
        request.preferredQueue = GpuQueuePreference::Compute;
        break;
    case CommandQueue::Transfer:
        request.preferredQueue = GpuQueuePreference::Transfer;
        break;
    default:
        request.requiredCapabilities = GpuQueueCapability::None;
        request.preferredQueue = GpuQueuePreference::Any;
        break;
    }
    return request;
}

[[nodiscard]] static GpuTaskSchedulingHint SetupUploadGraphScheduling(const usize byteCount)noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = byteCount >= s_TransferPreferredUploadMinimumBytes
        ? GpuTaskCostHint::Large
        : GpuTaskCostHint::Tiny
    ;
    // A public setup call returns one accepted producer token, so retain a distinct explicit packet.
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    return scheduling;
}

[[nodiscard]] static ResourceStates::Mask SetupUploadGraphFinalState(
    const ResourceStates::Mask declaredInitialState
)noexcept{
    // Unknown is a valid legacy descriptor state. The graph still needs a concrete post-write state; CopyDest is
    // exactly what the native write leaves behind when the old setup path has no declared final transition.
    return declaredInitialState == ResourceStates::Unknown
        ? ResourceStates::CopyDest
        : declaredInitialState
    ;
}

template<typename DeclareTask>
[[nodiscard]] static bool SubmitGraphOwnedStandaloneTask(
    GraphicsBackend::Device& device,
    GraphicsArena& graphArena,
    DeclareTask&& declareTask,
    QueueSubmissionToken& outSubmissionToken,
    const GpuPhysicalQueueId requiredTerminalQueue = {}
){
    outSubmissionToken = {};

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u)
        return false;

    GpuTaskGraph graph(graphArena);
    const GpuTaskId terminalTask = declareTask(graph);
    if(!terminalTask.valid())
        return false;

    GpuTaskGraphAnalysis analysis(graphArena);
    GpuTaskGraphQueueAssignments assignments(graphArena);
    GpuCompiledGraph compiledGraph(graphArena);
    GpuRecordedGraph recordedGraph(graphArena);
    GpuGraphSubmissionTransaction transaction(graphArena);
    Alloc::ScratchArena scratchArena(Name("graphics.setup_upload_graph_scratch"));
    const GpuTaskGraphCompiler compiler;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena))
        return false;

    const GpuSubmissionPacketId terminalPacket = compiledGraph.packetForTask(terminalTask);
    if(!terminalPacket.valid())
        return false;
    if(requiredTerminalQueue.valid() && compiledGraph.packet(terminalPacket).queue != requiredTerminalQueue)
        return false;

    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    if(!recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph
    )){
        transaction.discardUnaccepted(graph, compiledGraph);
        return false;
    }

    const GpuTaskGraphSubmitter submitter(device);
    if(!submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    )){
        transaction.discardUnaccepted(graph, compiledGraph);
        outSubmissionToken = {};
        return false;
    }

    outSubmissionToken = transaction.packetToken(terminalPacket);
    return outSubmissionToken.valid();
}

template<typename DeclareTask>
[[nodiscard]] static bool SubmitGraphOwnedSetupUpload(
    GraphicsBackend::Device& device,
    GraphicsArena& graphArena,
    const ResourceQueueSharing::Mask queueSharing,
    DeclareTask&& declareTask,
    QueueSubmissionToken& outUploadToken
){
    if(!SubmitGraphOwnedStandaloneTask(
        device,
        graphArena,
        Forward<DeclareTask>(declareTask),
        outUploadToken
    ))
        return false;

    return BridgeSetupUploadToConsumerQueues(device, outUploadToken, queueSharing);
}


struct FrameTimingResetGraphTask{
    struct Payload{
        GpuTimingRecorder* timing = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.timing)
            return false;

        payload.timing->recordFrameReset(commandList);
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.timing && token.valid())
            payload.timing->confirmFrameReset();
    }

    static void discarded(Payload& payload){
        if(payload.timing)
            payload.timing->discardFrameReset();
    }
};

[[nodiscard]] static GpuQueueRequest FrameTimingResetQueueRequest()noexcept{
    return GpuQueueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
}

[[nodiscard]] static GpuTaskSchedulingHint FrameTimingResetScheduling()noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    // The reset is a complete, CPU-visible preamble boundary. Later renderer submissions may only reserve their
    // timestamp scopes after this packet accepts, so it must not merge with unrelated graph work.
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.overlapPreferred = false;
    return scheduling;
}

[[nodiscard]] static bool SubmitGraphOwnedFrameTimingReset(
    GraphicsBackend::Device& device,
    GraphicsArena& graphArena,
    GpuTimingRecorder& timing
){
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!graphicsQueue.valid())
        return false;

    QueueSubmissionToken acceptedToken;
    if(!SubmitGraphOwnedStandaloneTask(
        device,
        graphArena,
        [&timing](GpuTaskGraph& graph){
            GpuTaskDesc resetDesc;
            resetDesc
                .setIdentity(Name("graphics.frame_timing.reset"))
                .setMarkerLabel("Frame GPU-Timing Reset")
                .setQueue(FrameTimingResetQueueRequest())
                .setScheduling(FrameTimingResetScheduling())
            ;
            return graph.addTask<FrameTimingResetGraphTask>(
                resetDesc,
                FrameTimingResetGraphTask::Payload{
                    .timing = &timing,
                }
            );
        },
        acceptedToken,
        graphicsQueue
    ))
        return false;

    return acceptedToken.queue == CommandQueue::Graphics
        && acceptedToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ComputeTextureUploadByteSize(const Graphics::TextureSetupDesc& desc, usize& outRequiredBytes){
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

static bool ValidateBufferSetupUpload(const Graphics::BufferSetupDesc& desc){
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
    // Both the legacy CommandList staging path and the graph-owned upload task lower to VkBufferCopy.  The API
    // cannot truthfully report a successful upload for a region Vulkan rejects, so fail before creating either
    // a native command list or a graph packet.
    if((desc.destOffsetBytes & (sizeof(u32) - 1u)) != 0u || (desc.dataSize & (sizeof(u32) - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up buffer '{}': upload offset and size must be 4-byte aligned")
            , StringConvert(desc.bufferDesc.debugName.c_str())
        );
        return false;
    }
    // A retained buffer must publish a concrete state.  Unknown would be restored at native close without a
    // graph-visible final-state contract for the next consumer.
    if(desc.bufferDesc.keepInitialState && desc.bufferDesc.initialState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up buffer '{}': keep-initial-state uploads require a concrete initial state")
            , StringConvert(desc.bufferDesc.debugName.c_str())
        );
        return false;
    }

    return true;
}

static bool ValidateTextureSetupUpload(const Graphics::TextureSetupDesc& desc){
    if(!desc.data && desc.uploadDataSize == 0)
        return true;
    if(!desc.data || desc.uploadDataSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to set up texture '{}': upload data and size must both be provided"), StringConvert(desc.textureDesc.name.c_str()));
        return false;
    }

    usize requiredBytes = 0;
    if(!ComputeTextureUploadByteSize(desc, requiredBytes)){
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
            , StringConvert(desc.destination->getDescription().name.c_str())
        );
        return false;
    }
    if(desc.finalState == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': final state is unknown")
            , StringConvert(desc.destination->getDescription().name.c_str())
        );
        return false;
    }

    const TextureDesc& textureDesc = desc.destination->getDescription();
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
    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    if(formatInfo.hasDepth && formatInfo.hasStencil){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': combined depth/stencil uploads require an explicit per-aspect upload API")
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
        if(!ValidateTextureSetupUpload(regionDesc))
            return false;
        if(AddOverflows<usize>(outTotalByteCount, region.dataSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to upload texture batch '{}': byte count overflows")
                , StringConvert(textureDesc.name.c_str())
            );
            return false;
        }
        outTotalByteCount += region.dataSize;
    }
    return true;
}

[[nodiscard]] static CommandQueue::Enum ResolveTextureUploadBatchQueue(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum requestedQueue,
    const usize uploadBytes,
    const TextureDesc& textureDesc
)noexcept{
    const auto canUse = [&](const CommandQueue::Enum queue){
        return queue == CommandQueue::Graphics
            || (device.getQueue(queue) && QueueSharingIncludesQueue(textureDesc.queueSharing, queue))
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

static bool ValidateMeshSetupDesc(const Graphics::MeshSetupDesc& desc){
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

struct BufferSetupJobData{
    Graphics::BufferSetupDesc setupDesc;
    UploadBytes uploadBytes;
    BufferHandle& outBuffer;


    BufferSetupJobData(Alloc::GlobalArena& arena, const Graphics::BufferSetupDesc& desc, BufferHandle& output)
        : setupDesc(desc)
        , uploadBytes(arena)
        , outBuffer(output)
    {}
};

struct TextureSetupJobData{
    Graphics::TextureSetupDesc setupDesc;
    UploadBytes uploadBytes;
    TextureHandle& outTexture;


    TextureSetupJobData(Alloc::GlobalArena& arena, const Graphics::TextureSetupDesc& desc, TextureHandle& output)
        : setupDesc(desc)
        , uploadBytes(arena)
        , outTexture(output)
    {}
};

struct MeshSetupJobData{
    Graphics::MeshSetupDesc setupDesc;
    UploadBytes vertexBytes;
    UploadBytes indexBytes;
    Graphics::MeshResource& outMesh;


    MeshSetupJobData(Alloc::GlobalArena& arena, const Graphics::MeshSetupDesc& desc, Graphics::MeshResource& output)
        : setupDesc(desc)
        , vertexBytes(arena)
        , indexBytes(arena)
        , outMesh(output)
    {}
};


static UploadBytes CopyBytes(Alloc::GlobalArena& arena, const void* data, usize dataSize){
    UploadBytes bytes{arena};
    if(!data || dataSize == 0)
        return bytes;

    const u8* const byteData = static_cast<const u8*>(data);
    bytes.assign(byteData, byteData + dataSize);

    return bytes;
}

template<typename JobData, typename Desc, typename Output, typename Validate, typename ConfigurePayload, typename ExecutePayload>
static Graphics::JobHandle SubmitSetupUploadJob(
    Graphics& graphics,
    Alloc::GlobalArena& arena,
    Alloc::JobSystem& jobSystem,
    const Desc& desc,
    Output& output,
    Validate&& validate,
    ConfigurePayload&& configurePayload,
    ExecutePayload&& executePayload
){
    if(!validate(desc)){
        output = nullptr;
        return {};
    }

    auto payload = MakeGlobalUnique<JobData>(arena, arena, desc, output);
    configurePayload(*payload, arena);

    return jobSystem.submit([&graphics, payload = Move(payload), executePayload = Forward<ExecutePayload>(executePayload)]() mutable{
        executePayload(graphics, *payload);
    });
}

static void ConfigureBufferSetupPayload(BufferSetupJobData& payload, Alloc::GlobalArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.dataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.dataSize = payload.uploadBytes.size();
}

static void ExecuteBufferSetupPayload(Graphics& graphics, BufferSetupJobData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.dataSize = payload.uploadBytes.size();
    payload.outBuffer = graphics.setupBuffer(payload.setupDesc);
}

static void ConfigureTextureSetupPayload(TextureSetupJobData& payload, Alloc::GlobalArena& arena){
    payload.uploadBytes = CopyBytes(arena, payload.setupDesc.data, payload.setupDesc.uploadDataSize);
    payload.setupDesc.data = nullptr;
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
}

static void ExecuteTextureSetupPayload(Graphics& graphics, TextureSetupJobData& payload){
    payload.setupDesc.data = payload.uploadBytes.empty() ? nullptr : payload.uploadBytes.data();
    payload.setupDesc.uploadDataSize = payload.uploadBytes.size();
    payload.outTexture = graphics.setupTexture(payload.setupDesc);
}

static bool CopyInstanceParameters(DeviceCreationParameters& dst, const InstanceParameters& src){
    if(src.enableDebugRuntime && !CanEnableDebugRuntime())
        return false;

    static_cast<InstanceParameters&>(dst) = src;
    return true;
}

constexpr bool IsFp16CoopVecFormat(const CooperativeVectorMatMulFormatCombo& combo){
    return
        combo.inputType == CooperativeVectorDataType::Float16
        && combo.inputInterpretation == CooperativeVectorDataType::Float16
        && combo.matrixInterpretation == CooperativeVectorDataType::Float16
        && combo.outputType == CooperativeVectorDataType::Float16
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Graphics::BackBufferResizingCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResizing();
}

void Graphics::BackBufferResizedCallback(void* userData){
    if(auto* graphics = static_cast<Graphics*>(userData))
        graphics->backBufferResized();
}


Graphics::Graphics(
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool,
    Alloc::JobSystem& jobSystem,
    Perf::TimingSink& gpuTiming
)
    : m_allocator(allocator)
    , m_threadPool(threadPool)
    , m_jobSystem(jobSystem)
    , m_deviceCreationParams(m_allocator.getObjectArena())
    , m_gpuTiming(m_allocator.getObjectArena(), gpuTiming)
    , m_backend(MakeNotNullUnique(MakeGlobalUnique<Backend>(m_allocator.getObjectArena(), m_deviceCreationParams, m_swapChainState, m_allocator, m_threadPool)))
    , m_renderPasses(m_allocator.getObjectArena())
    , m_swapChainFramebuffers(m_allocator.getObjectArena())
    , m_windowTitle(m_allocator.getObjectArena())
{
    m_deviceCreationParams.enableRayTracingExtensions = true;
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
}
Graphics::~Graphics(){
    destroy();
}

bool Graphics::init(const Common::FrameData& data){
    m_deviceCreationParams.headlessDevice = false;
    m_hasPresentedFrame = false;

    m_swapChainState.backBufferWidth = data.width();
    m_swapChainState.backBufferHeight = data.height();
    m_swapChainState.backBufferFormat = m_deviceCreationParams.swapChainFormat;
    m_swapChainState.outputMode = SwapChainOutputMode::SDR;

    m_backend->setPlatformFrameParam(data.frameParam());

    if(!m_instanceCreated){
        if(!m_backend->createInstance())
            return false;
        m_instanceCreated = true;
    }

    if(!m_backend->createDevice())
        return false;

    m_deviceRecreationRequested = false;

    if(!m_backend->createSwapChain())
        return false;

    m_swapChainState.backBufferWidth = 0;
    m_swapChainState.backBufferHeight = 0;
    updateWindowState(data.width(), data.height(), true, true);
    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Graphics: window device and swap chain created ({}x{})")
        , data.width()
        , data.height()
    );
    return validateRenderPassResources();
}

bool Graphics::createHeadlessDevice(){
    m_deviceCreationParams.headlessDevice = true;
    m_hasPresentedFrame = false;

    if(!m_instanceCreated){
        if(!m_backend->createInstance())
            return false;
        m_instanceCreated = true;
    }

    if(!m_backend->createDevice())
        return false;

    m_deviceRecreationRequested = false;

    m_previousFrameTimestamp = TimerNow();

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Graphics: headless device created"));
    return validateRenderPassResources();
}

bool Graphics::createInstance(const InstanceParameters& params){
    if(!__hidden_graphics::CopyInstanceParameters(m_deviceCreationParams, params)){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: debug runtime is only available in non-final builds"));
        return false;
    }

    if(!m_backend->createInstance())
        return false;

    m_instanceCreated = true;
    return true;
}

bool Graphics::setDebugRuntimeEnabled(bool enabled){
    if(enabled && !CanEnableDebugRuntime())
        return false;
    if(m_instanceCreated && m_deviceCreationParams.enableDebugRuntime != enabled)
        return false;

    m_deviceCreationParams.enableDebugRuntime = enabled;
    return true;
}

bool Graphics::setAsyncComputeLaneEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableAsyncComputeLane = enabled;
    return true;
}

bool Graphics::setTransferQueueEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableTransferQueue = enabled;
    return true;
}

bool Graphics::setSameClassMultiQueueEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSameClassMultiQueue = enabled;
    return true;
}

bool Graphics::setAdapterIndex(const i32 index){
    if(index < -1 || m_backend->getDevice())
        return false;

    m_deviceCreationParams.adapterIndex = index;
    return true;
}

bool Graphics::setHDR10OutputEnabled(const bool enabled){
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableHDR10Output = enabled;
    return true;
}

bool Graphics::setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi){
    if(!abi.valid() || m_backend->getDevice())
        return false;

    m_deviceCreationParams.bindlessHeapAbi = abi;
    return true;
}

void Graphics::setPipelineCacheDirectory(const Path& directory){
    m_deviceCreationParams.pipelineCacheDirectory = directory;
}

void Graphics::requestDeviceRecreation(){
    if(m_deviceRecreationRequested)
        return;

    m_deviceRecreationRequested = true;
    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Graphics: device recreation requested; ending the current graphics session before another submission."));
}

void Graphics::updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    if(m_deviceRecreationRequested)
        return;
    if(auto* device = m_backend->getDevice(); device && device->isDeviceLost()){
        requestDeviceRecreation();
        return;
    }

    m_windowVisible = windowVisible;
    m_windowIsInFocus = windowIsInFocus;

    if(!m_windowVisible)
        return;

    if(width == 0 || height == 0){
        m_windowVisible = false;
        return;
    }

    if(
        static_cast<i32>(m_swapChainState.backBufferWidth) != static_cast<i32>(width)
        || static_cast<i32>(m_swapChainState.backBufferHeight) != static_cast<i32>(height)
        || m_swapChainState.vsyncEnabled != m_requestedVSync
    ){
        backBufferResizing();

        m_swapChainState.backBufferWidth = width;
        m_swapChainState.backBufferHeight = height;
        m_swapChainState.vsyncEnabled = m_requestedVSync;

        m_backend->resizeSwapChain();
        backBufferResized();
    }

    m_swapChainState.vsyncEnabled = m_requestedVSync;
}

void Graphics::destroy(){
    waitAllJobs();
    waitForIdle();

    invalidateRenderPassResources();
    m_renderPasses.clear();
    m_gpuTiming.resetQueries();

    m_swapChainFramebuffers.clear();
    m_backend->destroy();
    m_instanceCreated = false;
    m_deviceRecreationRequested = false;
}

void Graphics::waitForIdle(){
    if(auto* device = m_backend->getDevice())
        device->waitForIdle();
}

GraphicsBackend::Device& Graphics::getDevice()const noexcept{
    GraphicsBackend::Device* const device = m_backend->getDevice();
    NWB_ASSERT(device);
    return *device;
}

bool Graphics::enumerateAdapters(GraphicsVector<AdapterInfo>& outAdapters){
    return m_backend->enumerateAdapters(outAdapters);
}

bool Graphics::getSelectedAdapterInfo(AdapterInfo& outAdapter)const{
    return m_backend->getSelectedAdapterInfo(outAdapter);
}

QueueSubmissionPreSubmitHook Graphics::claimFramePresentationSignal()noexcept{
    return m_backend->claimFramePresentationSignal();
}

bool Graphics::confirmFramePresentationSignal(const QueueSubmissionToken& token)noexcept{
    return m_backend->confirmFramePresentationSignal(token);
}

void Graphics::cancelFramePresentationSignal()noexcept{
    m_backend->cancelFramePresentationSignal();
}

void Graphics::addRenderPassToFront(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_front(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("Graphics: front render pass failed to validate resources after registration"));
}

void Graphics::addRenderPassToBack(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_back(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("Graphics: back render pass failed to validate resources after registration"));
}

void Graphics::removeRenderPass(IRenderPass& pass){
    waitAllJobs();
    waitForIdle();

    pass.invalidateResources();
    m_renderPasses.remove(&pass);
}

const tchar* Graphics::getRendererString()const{
    return m_backend->getRendererString();
}

GraphicsAPI::Enum Graphics::getGraphicsAPI()const{
    return GraphicsBackend::s_Api;
}

void Graphics::reportLiveObjects()const{
    m_backend->reportLiveObjects();
}

void Graphics::getWindowDimensions(i32& width, i32& height)const{
    width = m_swapChainState.backBufferWidth;
    height = m_swapChainState.backBufferHeight;
}

void Graphics::getDPIScaleInfo(f32& x, f32& y)const{
    x = m_dpiScaleFactorX;
    y = m_dpiScaleFactorY;
}

void Graphics::setWindowTitle(NotNull<const tchar*> title){
    if(m_windowTitle == title.get())
        return;

    m_windowTitle = title.get();
}

void Graphics::setPointerScaleChangedCallback(PointerScaleChangedCallback callback, void* userData){
    m_pointerScaleChangedCallback = callback;
    m_pointerScaleChangedUserData = userData;
    notifyPointerScaleChanged();
}

Texture* Graphics::getCurrentBackBuffer()const{
    return m_backend->getCurrentBackBuffer();
}

Texture* Graphics::getBackBuffer(u32 index)const{
    return m_backend->getBackBuffer(index);
}

u32 Graphics::getCurrentBackBufferIndex()const{
    return m_backend->getCurrentBackBufferIndex();
}

u32 Graphics::getBackBufferCount()const{
    return m_backend->getBackBufferCount();
}

Framebuffer* Graphics::getFramebuffer(u32 index)const{
    if(index < m_swapChainFramebuffers.size())
        return m_swapChainFramebuffers[index].get();
    return nullptr;
}

BufferHandle Graphics::createBuffer(const BufferDesc& desc)const{
    return getDevice().createBuffer(desc);
}

TextureHandle Graphics::createTexture(const TextureDesc& desc)const{
    return getDevice().createTexture(desc);
}

void Graphics::backBufferResizing(){
    waitAllJobs();
    waitForIdle();

    invalidateRenderPassResources();
    m_swapChainFramebuffers.clear();

    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResizing();
}

void Graphics::backBufferResized(){
    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);

    const u32 backBufferCount = getBackBufferCount();
    m_swapChainFramebuffers.clear();
    m_swapChainFramebuffers.reserve(backBufferCount);
    for(u32 index = 0; index < backBufferCount; ++index)
        m_swapChainFramebuffers.push_back(getDevice().createFramebuffer(FramebufferDesc().addColorAttachment(getBackBuffer(index))));

    if(!validateRenderPassResources())
        NWB_LOGGER_WARNING(NWB_TEXT("Graphics: one or more render passes failed to validate resources after back buffer resize"));
    NWB_LOGGER_INFO(NWB_TEXT("Graphics: Back buffer resized to {}x{}"), m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight);
}

void Graphics::invalidateRenderPassResources(){
    for(auto* renderPass : m_renderPasses)
        renderPass->invalidateResources();
}

bool Graphics::validateRenderPassResources(){
    bool valid = true;
    for(auto* renderPass : m_renderPasses){
        valid =
            renderPass->validateResources(
                m_swapChainState.backBufferWidth,
                m_swapChainState.backBufferHeight,
                m_deviceCreationParams.swapChainSampleCount
            )
            && valid
        ;
    }
    return valid;
}

void Graphics::displayScaleChanged(){
    notifyPointerScaleChanged();

    for(auto* renderPass : m_renderPasses)
        renderPass->displayScaleChanged(m_dpiScaleFactorX, m_dpiScaleFactorY);
}

void Graphics::animate(f64 elapsedTime){
    for(auto* renderPass : m_renderPasses){
        renderPass->animate(static_cast<f32>(elapsedTime));
        renderPass->setLatewarpOptions();
    }
}

bool Graphics::prepareFramePreamble(){
    auto& device = getDevice();
    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return false;
    }

    m_gpuTiming.collect(device, m_frameIndex);
    m_gpuTiming.beginFrame(m_frameIndex);

    // Materialize and reset every declared timer-query pool before any pass preparation can record a timestamp.
    // Per-pass preparation submits skinning and shadow packets, so this must remain ahead of it as well as every
    // later render packet on the same GPU timeline.
    if(m_gpuTiming.queryCollectionEnabled()){
        if(!m_gpuTiming.materializeRequestedQueries(device))
            NWB_LOGGER_WARNING(NWB_TEXT("Graphics: failed to materialize one or more requested GPU-timing query pools"));

        // Do not allow render-pass scopes to reuse a prior frame's reset if graph recording or submission fails.
        // The task's accepted callback reenables only the pools covered by the successfully submitted packet.
        m_gpuTiming.discardFrameReset();
        if(!__hidden_graphics::SubmitGraphOwnedFrameTimingReset(
            device,
            m_allocator.getObjectArena(),
            m_gpuTiming
        )){
            m_gpuTiming.discardFrameReset();
            NWB_LOGGER_WARNING(NWB_TEXT("Graphics: failed to submit the graph-owned frame GPU-timing reset packet"));
        }
    }

    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return false;
    }

    return true;
}

void Graphics::render(){
    Framebuffer* framebuffer = getCurrentFramebuffer();
    auto& device = getDevice();
    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return;
    }

    // Keep prepare -> render interleaved in registration order. Skinning submits its deformation work before the
    // renderer prepares the dependent mesh, CSG, and ray-tracing packets; a global all-pass prepare phase would
    // observe stale runtime meshes.
    for(auto* renderPass : m_renderPasses){
        if(m_deviceRecreationRequested || device.isDeviceLost()){
            if(device.isDeviceLost())
                requestDeviceRecreation();
            return;
        }
        if(!renderPass->prepareResources(framebuffer)){
            NWB_LOGGER_WARNING(NWB_TEXT("Graphics: render pass skipped after resource preparation failed"));
            continue;
        }

        renderPass->render(framebuffer);

        // A render pass can request recreation after an unrecoverable cross-queue ownership failure. Do not let a
        // later pass record or submit against this device generation in the same frame.
        if(m_deviceRecreationRequested || device.isDeviceLost()){
            if(device.isDeviceLost())
                requestDeviceRecreation();
            return;
        }
    }
}

void Graphics::updateAverageFrameTime(f64 elapsedTime){
    m_frameTimeSum += elapsedTime;
    m_numberOfAccumulatedFrames += 1;

    if(m_frameTimeSum > m_averageTimeUpdateInterval && m_numberOfAccumulatedFrames > 0){
        m_averageFrameTime = m_frameTimeSum / static_cast<f64>(m_numberOfAccumulatedFrames);
        m_numberOfAccumulatedFrames = 0;
        m_frameTimeSum = 0.0;
    }
}

void Graphics::notifyPointerScaleChanged()const{
    if(!m_pointerScaleChangedCallback)
        return;

    if(m_deviceCreationParams.supportExplicitDisplayScaling)
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, 1.f, 1.f);
    else
        m_pointerScaleChangedCallback(m_pointerScaleChangedUserData, m_dpiScaleFactorX, m_dpiScaleFactorY);
}

bool Graphics::shouldRenderUnfocused()const{
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
}

bool Graphics::runFrame(){
    if(!m_frameSubmissionSuspended)
        return animateRenderPresent();

    // Do not let a capture hold hide a device-loss/recreation request. No backend beginFrame, render, or present call
    // is made here, so the last completed frame stays on screen while the platform loop remains responsive.
    if(m_deviceRecreationRequested)
        return false;

    auto& device = getDevice();
    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return false;
    }

    YieldThread();
    return true;
}

bool Graphics::animateRenderPresent(){
    if(m_deviceRecreationRequested)
        return false;

    auto& device = getDevice();
    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return false;
    }

    Timer now = TimerNow();
    const f64 elapsedTime = DurationInSeconds<f64>(now, m_previousFrameTimestamp);
    const bool shouldBootstrapWindowPresentation = !m_hasPresentedFrame;

    if(m_windowVisible && (m_windowIsInFocus || shouldRenderUnfocused() || shouldBootstrapWindowPresentation)){
        if(m_prevDPIScaleFactorX != m_dpiScaleFactorX || m_prevDPIScaleFactorY != m_dpiScaleFactorY){
            displayScaleChanged();
            m_prevDPIScaleFactorX = m_dpiScaleFactorX;
            m_prevDPIScaleFactorY = m_dpiScaleFactorY;
        }

        animate(elapsedTime);

        if(m_frameIndex > 0 || !m_skipRenderOnFirstFrame){
            const BackBufferResizeCallbacks resizeCallbacks = {
                this,
                &Graphics::BackBufferResizingCallback,
                &Graphics::BackBufferResizedCallback,
            };
            if(m_backend->beginFrame(resizeCallbacks)){
                if(!prepareFramePreamble()){
                    if(device.isDeviceLost())
                        requestDeviceRecreation();
                    return false;
                }

                render();

                if(m_deviceRecreationRequested || device.isDeviceLost()){
                    if(device.isDeviceLost())
                        requestDeviceRecreation();
                    return false;
                }

                if(!m_backend->present()){
                    if(device.isDeviceLost())
                        requestDeviceRecreation();
                    return false;
                }

                if(device.isDeviceLost()){
                    requestDeviceRecreation();
                    return false;
                }

                m_hasPresentedFrame = true;
            }
        }
    }

    YieldThread();

    device.runGarbageCollection();
    if(device.isDeviceLost()){
        requestDeviceRecreation();
        return false;
    }

    updateAverageFrameTime(elapsedTime);
    m_previousFrameTimestamp = now;

    ++m_frameIndex;
    return true;
}

BufferHandle Graphics::setupBuffer(const BufferSetupDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};
    if(!__hidden_graphics::ValidateBufferSetupUpload(desc))
        return {};

    if(!desc.data || desc.dataSize == 0)
        return device.createBuffer(desc.bufferDesc);

    const CommandQueue::Enum uploadQueue = __hidden_graphics::ResolveSetupUploadQueue(
        device,
        desc.queue,
        desc.dataSize,
        desc.bufferDesc.initialState != ResourceStates::Unknown
    );
    BufferDesc uploadDesc = desc.bufferDesc;
    uploadDesc.queueSharing = __hidden_graphics::ResolveSetupUploadQueueSharing(
        uploadDesc.queueSharing,
        uploadQueue
    );
    BufferHandle buffer = device.createBuffer(uploadDesc);
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create setup buffer '{}'"), StringConvert(desc.bufferDesc.debugName.c_str()));
        return {};
    }

    QueueSubmissionToken uploadToken;
    const bool submitted = __hidden_graphics::SubmitGraphOwnedSetupUpload(
        device,
        m_allocator.getObjectArena(),
        uploadDesc.queueSharing,
        [&buffer, &desc, &uploadDesc, uploadQueue, &uploadToken](GpuTaskGraph& graph){
            const GpuGraphResourceId destination = graph.importBuffer(
                buffer,
                GpuGraphResourceDesc{}
                    .setIdentity(Name("graphics.setup_buffer.resource"))
                    .setMarkerLabel("Setup Buffer")
                    .setType(GpuGraphResourceType::Buffer)
                    .setInitialState(uploadDesc.initialState)
                    .setQueueSharing(uploadDesc.queueSharing)
            );
            const GpuUploadBlobId source = graph.copyUploadData(
                desc.data,
                desc.dataSize,
                alignof(u32)
            );
            if(!destination.valid() || !source.valid())
                return GpuTaskId{};

            GpuTaskDesc uploadTaskDesc;
            uploadTaskDesc
                .setIdentity(Name("graphics.setup_buffer.upload"))
                .setMarkerLabel("Setup Buffer Upload")
                .setQueue(__hidden_graphics::SetupUploadGraphQueueRequest(uploadQueue))
                .setScheduling(__hidden_graphics::SetupUploadGraphScheduling(desc.dataSize))
            ;
            return graph.addUploadBufferTask(
                uploadTaskDesc,
                GpuUploadBufferTaskDesc{
                    .source = source,
                    .destination = destination,
                    .destinationOffsetBytes = desc.destOffsetBytes,
                    .finalState = __hidden_graphics::SetupUploadGraphFinalState(uploadDesc.initialState),
                    .acceptedToken = &uploadToken,
                }
            );
        },
        uploadToken
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
    if(!__hidden_graphics::ValidateTextureSetupUpload(desc))
        return {};

    if(!desc.data || desc.uploadDataSize == 0)
        return device.createTexture(desc.textureDesc);

    const CommandQueue::Enum uploadQueue = __hidden_graphics::ResolveSetupUploadQueue(
        device,
        desc.queue,
        desc.uploadDataSize,
        desc.textureDesc.initialState != ResourceStates::Unknown
    );
    TextureDesc uploadDesc = desc.textureDesc;
    uploadDesc.queueSharing = __hidden_graphics::ResolveSetupUploadQueueSharing(
        uploadDesc.queueSharing,
        uploadQueue
    );
    TextureHandle texture = device.createTexture(uploadDesc);
    if(!texture){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to create setup texture '{}'"), StringConvert(desc.textureDesc.name.c_str()));
        return {};
    }

    QueueSubmissionToken uploadToken;
    const bool submitted = __hidden_graphics::SubmitGraphOwnedSetupUpload(
        device,
        m_allocator.getObjectArena(),
        uploadDesc.queueSharing,
        [&texture, &desc, &uploadDesc, uploadQueue, &uploadToken](GpuTaskGraph& graph){
            const GpuGraphResourceId destination = graph.importTexture(
                texture,
                GpuGraphResourceDesc{}
                    .setIdentity(Name("graphics.setup_texture.resource"))
                    .setMarkerLabel("Setup Texture")
                    .setType(GpuGraphResourceType::Texture)
                    .setInitialState(uploadDesc.initialState)
                    .setQueueSharing(uploadDesc.queueSharing)
            );
            const GpuUploadBlobId source = graph.copyUploadData(desc.data, desc.uploadDataSize, alignof(u32));
            if(!destination.valid() || !source.valid())
                return GpuTaskId{};

            GpuTaskDesc uploadTaskDesc;
            uploadTaskDesc
                .setIdentity(Name("graphics.setup_texture.upload"))
                .setMarkerLabel("Setup Texture Upload")
                .setQueue(__hidden_graphics::SetupUploadGraphQueueRequest(uploadQueue))
                .setScheduling(__hidden_graphics::SetupUploadGraphScheduling(desc.uploadDataSize))
            ;
            return graph.addUploadTextureTask(
                uploadTaskDesc,
                GpuUploadTextureTaskDesc{
                    .source = source,
                    .destination = destination,
                    .arraySlice = desc.arraySlice,
                    .mipLevel = desc.mipLevel,
                    .rowPitch = desc.rowPitch,
                    .depthPitch = desc.depthPitch,
                    .finalState = __hidden_graphics::SetupUploadGraphFinalState(uploadDesc.initialState),
                    .acceptedToken = &uploadToken,
                }
            );
        },
        uploadToken
    );
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to submit graph-owned setup texture upload '{}'"), StringConvert(desc.textureDesc.name.c_str()));
        return {};
    }
    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;

    return texture;
}

bool Graphics::uploadTextureBatch(const TextureUploadBatchDesc& desc)const{
    auto& device = getDevice();
    if(desc.acceptedToken)
        *desc.acceptedToken = {};

    usize totalByteCount = 0u;
    if(!__hidden_graphics::ValidateTextureUploadBatch(desc, totalByteCount))
        return false;

    const TextureDesc& textureDesc = desc.destination->getDescription();
    const CommandQueue::Enum uploadQueue = __hidden_graphics::ResolveTextureUploadBatchQueue(
        device,
        desc.queue,
        totalByteCount,
        textureDesc
    );
    QueueSubmissionToken uploadToken;
    const bool submitted = __hidden_graphics::SubmitGraphOwnedSetupUpload(
        device,
        m_allocator.getObjectArena(),
        textureDesc.queueSharing,
        [&desc, &textureDesc, uploadQueue, &uploadToken](GpuTaskGraph& graph){
            const GpuGraphResourceId destination = graph.importTexture(
                desc.destination,
                GpuGraphResourceDesc{}
                    .setIdentity(Name("graphics.upload_texture_batch.resource"))
                    .setMarkerLabel("Texture Upload Batch")
                    .setType(GpuGraphResourceType::Texture)
                    .setInitialState(textureDesc.initialState)
                    .setQueueSharing(textureDesc.queueSharing)
            );
            if(!destination.valid())
                return GpuTaskId{};

            GpuTaskId previousTask;
            for(usize regionIndex = 0u; regionIndex < desc.regionCount; ++regionIndex){
                const TextureUploadRegion& region = desc.regions[regionIndex];
                const GpuUploadBlobId source = graph.copyUploadData(
                    region.data,
                    region.dataSize,
                    alignof(u32)
                );
                if(!source.valid())
                    return GpuTaskId{};

                GpuTaskSchedulingHint scheduling = __hidden_graphics::SetupUploadGraphScheduling(region.dataSize);
                scheduling.forceSubmissionBoundary = false;
                scheduling.allowPacketMerge = true;
                scheduling.mergeWithPrevious = previousTask.valid();
                GpuTaskDesc uploadTaskDesc;
                uploadTaskDesc
                    .setIdentity(Name("graphics.upload_texture_batch.upload"))
                    .setMarkerLabel("Texture Upload Batch")
                    .setQueue(__hidden_graphics::SetupUploadGraphQueueRequest(uploadQueue))
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
                        .finalState = desc.finalState,
                        .acceptedToken = regionIndex + 1u == desc.regionCount ? &uploadToken : nullptr,
                    }
                );
                if(!uploadTask.valid())
                    return GpuTaskId{};
                previousTask = uploadTask;
            }
            return previousTask;
        },
        uploadToken
    );
    if(!submitted){
        NWB_LOGGER_ERROR(NWB_TEXT("Graphics: failed to submit graph-owned texture upload batch '{}'"), StringConvert(textureDesc.name.c_str()));
        return false;
    }

    if(desc.acceptedToken)
        *desc.acceptedToken = uploadToken;
    return uploadToken.valid();
}

Graphics::MeshResource Graphics::setupMesh(const MeshSetupDesc& desc)const{
    if(!__hidden_graphics::ValidateMeshSetupDesc(desc))
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

Graphics::JobHandle Graphics::setupBufferAsync(const BufferSetupDesc& desc, BufferHandle& outBuffer){
    return __hidden_graphics::SubmitSetupUploadJob<__hidden_graphics::BufferSetupJobData>(
        *this,
        m_allocator.getObjectArena(),
        m_jobSystem,
        desc,
        outBuffer,
        __hidden_graphics::ValidateBufferSetupUpload,
        __hidden_graphics::ConfigureBufferSetupPayload,
        __hidden_graphics::ExecuteBufferSetupPayload
    );
}

Graphics::JobHandle Graphics::setupTextureAsync(const TextureSetupDesc& desc, TextureHandle& outTexture){
    return __hidden_graphics::SubmitSetupUploadJob<__hidden_graphics::TextureSetupJobData>(
        *this,
        m_allocator.getObjectArena(),
        m_jobSystem,
        desc,
        outTexture,
        __hidden_graphics::ValidateTextureSetupUpload,
        __hidden_graphics::ConfigureTextureSetupPayload,
        __hidden_graphics::ExecuteTextureSetupPayload
    );
}

Graphics::JobHandle Graphics::setupMeshAsync(const MeshSetupDesc& desc, MeshResource& outMesh){
    if(!__hidden_graphics::ValidateMeshSetupDesc(desc)){
        outMesh = {};
        return {};
    }

    auto payload = MakeGlobalUnique<__hidden_graphics::MeshSetupJobData>(
        m_allocator.getObjectArena(),
        m_allocator.getObjectArena(),
        desc,
        outMesh
    );
    payload->vertexBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.vertexData, desc.vertexDataSize);
    payload->indexBytes = __hidden_graphics::CopyBytes(m_allocator.getObjectArena(), desc.indexData, desc.indexDataSize);
    payload->setupDesc.vertexData = nullptr;
    payload->setupDesc.vertexDataSize = payload->vertexBytes.size();
    payload->setupDesc.indexData = nullptr;
    payload->setupDesc.indexDataSize = payload->indexBytes.size();

    return m_jobSystem.submit([this, payload = Move(payload)]() mutable{
        payload->setupDesc.vertexData = payload->vertexBytes.empty() ? nullptr : payload->vertexBytes.data();
        payload->setupDesc.vertexDataSize = payload->vertexBytes.size();
        payload->setupDesc.indexData = payload->indexBytes.empty() ? nullptr : payload->indexBytes.data();
        payload->setupDesc.indexDataSize = payload->indexBytes.size();
        payload->outMesh = setupMesh(payload->setupDesc);
    });
}

Graphics::CoopVectorSupport Graphics::queryCoopVecSupport()const{
    CoopVectorSupport output;

    output.inferencingSupported = queryFeatureSupport(Feature::CooperativeVectorInferencing);
    output.trainingSupported = queryFeatureSupport(Feature::CooperativeVectorTraining);

    auto& device = getDevice();
    const CooperativeVectorDeviceFeatures features = device.queryCoopVecFeatures();
    output.fp32TrainingSupported = output.trainingSupported && features.trainingFloat32;

    for(const auto& combo : features.matMulFormats){
        if(__hidden_graphics::IsFp16CoopVecFormat(combo)){
            output.fp16InferencingSupported = output.inferencingSupported;
            output.fp16TrainingSupported = output.trainingSupported && features.trainingFloat16;
            break;
        }
    }

    return output;
}

bool Graphics::queryFeatureSupport(const Feature::Enum feature, void* featureInfo, const usize featureInfoSize)const{
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
    if((m_disabledFeatureSupportMask & BitMask<u64>(static_cast<u32>(feature))) != 0u)
        return false;
#endif

    auto& device = getDevice();
    return device.queryFeatureSupport(feature, featureInfo, featureInfoSize);
}

u32 Graphics::queryWaveLaneCount()const noexcept{
    WaveLaneCountMinMaxFeatureInfo info{};
    if(queryFeatureSupport(Feature::WaveLaneCountMinMax, &info, sizeof(info)) && info.maxWaveLaneCount > 0u)
        return info.maxWaveLaneCount;
    // Conservative fallback for backends/paths that cannot report a wave size: 64 lanes is the safe upper
    // bound across all desktop GPUs and keeps groupshared reductions correct without wave intrinsics.
    return __hidden_graphics::s_DefaultWaveLaneCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Graphics::setFeatureSupportDisabledForTesting(const Feature::Enum feature, const bool disabled){
    const u64 featureBit = BitMask<u64>(static_cast<u32>(feature));
    if(disabled)
        m_disabledFeatureSupportMask |= featureBit;
    else
        m_disabledFeatureSupportMask &= ~featureBit;
}

void Graphics::clearFeatureSupportDisabledForTesting(){
    m_disabledFeatureSupportMask = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CooperativeVectorDeviceFeatures Graphics::queryCoopVecFeatures()const{
    auto& device = getDevice();
    return device.queryCoopVecFeatures();
}

usize Graphics::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns)const{
    auto& device = getDevice();
    return device.getCoopVecMatrixSize(type, layout, rows, columns);
}

void Graphics::waitJob(JobHandle handle)const{
    if(!handle.valid())
        return;

    m_jobSystem.wait(handle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

