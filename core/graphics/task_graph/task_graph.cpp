// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include "compiler.h"

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>
#include <core/telemetry/frame_graph_contributor.h>

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextGeneration{ 1u };

[[nodiscard]] static u64 AllocateGeneration()noexcept{
    u64 generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    if(generation == 0u)
        generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    return generation;
}

// Keep primitive copies in the graph layer rather than duplicating a task payload in each renderer subsystem.
// The handles retain the typed imports until late native recording, while resource-state transitions remain
// compiler-owned through the helper-generated task uses below.
struct CopyTextureTask{
    struct Copy{
        GpuGraphResourceId sourceResource;
        TextureHandle source;
        TextureSlice sourceSlice;
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        TextureSlice destinationSlice;
    };

    struct Payload{
        explicit Payload(GraphicsArena& arena)
            : copies(arena)
        {}

        GraphicsVector<Copy> copies;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.copies.empty())
            return false;

        for(const Copy& copy : payload.copies){
            if(!copy.source || !copy.destination)
                return false;
            if(
                context.commandIrCapture
                && !context.commandIrCapture->captureCopyTexture(
                    context.task,
                    context.packet,
                    context.queue,
                    copy.sourceResource,
                    copy.sourceSlice,
                    copy.destinationResource,
                    copy.destinationSlice
                )
            )
                return false;
            commandList.copyTexture(
                copy.destination.get(),
                copy.destinationSlice,
                copy.source.get(),
                copy.sourceSlice
            );
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

struct CopyBufferTask{
    struct Copy{
        GpuGraphResourceId sourceResource;
        BufferHandle source;
        u64 sourceOffsetBytes = 0u;
        GpuGraphResourceId destinationResource;
        BufferHandle destination;
        u64 destinationOffsetBytes = 0u;
        u64 dataSizeBytes = 0u;
    };

    struct Payload{
        explicit Payload(GraphicsArena& arena)
            : copies(arena)
        {}

        GraphicsVector<Copy> copies;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.copies.empty())
            return false;

        for(const Copy& copy : payload.copies){
            if(!copy.source || !copy.destination || copy.dataSizeBytes == 0u)
                return false;
            if(
                context.commandIrCapture
                && !context.commandIrCapture->captureCopyBuffer(
                    context.task,
                    context.packet,
                    context.queue,
                    copy.sourceResource,
                    copy.sourceOffsetBytes,
                    copy.destinationResource,
                    copy.destinationOffsetBytes,
                    copy.dataSizeBytes
                )
            )
                return false;
            commandList.copyBuffer(
                copy.destination.get(),
                copy.destinationOffsetBytes,
                copy.source.get(),
                copy.sourceOffsetBytes,
                copy.dataSizeBytes
            );
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

// Upload payloads retain only a graph-local blob handle plus destination metadata. The graph owns caller bytes until
// late native recording, where CommandList copies them into its ordinary upload chunk; no second GPU allocator is
// introduced by the task graph.
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

struct ClearBufferTask{
    struct Payload{
        GpuGraphResourceId destinationResource;
        BufferHandle destination;
        u32 clearValue = 0u;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(!payload.destination)
            return false;
        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureClearBuffer(
                context.task,
                context.packet,
                context.queue,
                payload.destinationResource,
                payload.clearValue
            )
        )
            return false;
        commandList.clearBufferUInt(payload.destination.get(), payload.clearValue);
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

struct ClearTextureTask{
    struct Payload{
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        GpuClearTextureTaskDesc clearDesc;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(!payload.destination || payload.clearDesc.valueType >= GpuClearTextureTaskValueType::kCount)
            return false;
        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureClearTexture(
                context.task,
                context.packet,
                context.queue,
                payload.destinationResource,
                payload.clearDesc
            )
        )
            return false;
        if(
            payload.clearDesc.recordHooks.beforeClear
            && !payload.clearDesc.recordHooks.beforeClear(
                payload.clearDesc.recordHooks.context,
                commandList,
                context
            )
        )
            return false;

        bool clearRecorded = false;
        switch(payload.clearDesc.valueType){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.floatValue
            );
            clearRecorded = true;
            break;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.uintValue
            );
            clearRecorded = true;
            break;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.intValue
            );
            clearRecorded = true;
            break;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.clearDepth,
                payload.clearDesc.depthValue,
                payload.clearDesc.clearStencil,
                payload.clearDesc.stencilValue
            );
            clearRecorded = true;
            break;
        default:
            return false;
        }
        return clearRecorded
            && (
                !payload.clearDesc.recordHooks.afterClear
                || payload.clearDesc.recordHooks.afterClear(
                    payload.clearDesc.recordHooks.context,
                    commandList,
                    context
                )
            )
        ;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.clearDesc.acceptedToken)
            *payload.clearDesc.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.clearDesc.acceptedToken)
            *payload.clearDesc.acceptedToken = {};
        if(payload.clearDesc.recordHooks.discarded)
            payload.clearDesc.recordHooks.discarded(payload.clearDesc.recordHooks.context);
    }
};

struct ClearTextureRectUIntTask{
    struct Payload{
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        GpuClearTextureRectUIntTaskDesc clearDesc;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(!payload.destination)
            return false;
        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureClearTextureRectUInt(
                context.task,
                context.packet,
                context.queue,
                payload.destinationResource,
                payload.clearDesc
            )
        )
            return false;
        if(
            payload.clearDesc.recordHooks.beforeClear
            && !payload.clearDesc.recordHooks.beforeClear(
                payload.clearDesc.recordHooks.context,
                commandList,
                context
            )
        )
            return false;

        commandList.clearTextureRectUInt(
            payload.destination.get(),
            payload.clearDesc.subresources,
            payload.clearDesc.rect,
            payload.clearDesc.uintValue
        );
        return !payload.clearDesc.recordHooks.afterClear
            || payload.clearDesc.recordHooks.afterClear(
                payload.clearDesc.recordHooks.context,
                commandList,
                context
            )
        ;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.clearDesc.acceptedToken)
            *payload.clearDesc.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.clearDesc.acceptedToken)
            *payload.clearDesc.acceptedToken = {};
        if(payload.clearDesc.recordHooks.discarded)
            payload.clearDesc.recordHooks.discarded(payload.clearDesc.recordHooks.context);
    }
};

[[nodiscard]] static bool CompatibleResourceMetadata(
    const GpuTaskGraphResourceView& resource,
    const CommandListResourceStateHandoff* const initialOwnerStateSourceIdentity,
    const GpuGraphResourceDesc& desc
)noexcept{
    // Re-importing a multi-producer source through the ordinary typed-import overload could silently exchange one
    // of its immutable ownership snapshots. Require callers to reuse its graph resource ID instead.
    if(
        resource.initialOwnerHandoffSourceCount != 0u
        || desc.initialOwnerHandoffSources
        || desc.initialOwnerHandoffSourceCount != 0u
    )
        return false;
    return resource.identity == desc.identity
        && resource.type == desc.type
        && resource.initialState == desc.initialState
        && resource.externalFinalState == desc.externalFinalState
        && resource.externalFinalReleaseDestinationQueue == desc.externalFinalReleaseDestinationQueue
        && resource.initialOwnerQueue == desc.initialOwnerQueue
        && resource.initialOwnerReleaseDestinationQueue == desc.initialOwnerReleaseDestinationQueue
        && resource.initialOwnerCompletion == desc.initialOwnerCompletion
        && initialOwnerStateSourceIdentity == desc.initialOwnerStateSource
        && resource.queueSharing == desc.queueSharing;
}

[[nodiscard]] static bool ValidTextureRange(const TextureSubresourceSet& range)noexcept{
    return range.numMipLevels != 0u && range.numArraySlices != 0u;
}

[[nodiscard]] static u64 TextureRangeEnd(const u32 base, const u32 count, const u32 all)noexcept{
    return count == all ? Limit<u64>::s_Max : static_cast<u64>(base) + static_cast<u64>(count);
}

[[nodiscard]] static bool TextureRangesOverlap(
    const TextureSubresourceSet& lhs,
    const TextureSubresourceSet& rhs
)noexcept{
    const u64 lhsMipEnd = TextureRangeEnd(
        lhs.baseMipLevel,
        lhs.numMipLevels,
        TextureSubresourceSet::AllMipLevels
    );
    const u64 rhsMipEnd = TextureRangeEnd(
        rhs.baseMipLevel,
        rhs.numMipLevels,
        TextureSubresourceSet::AllMipLevels
    );
    const u64 lhsArrayEnd = TextureRangeEnd(
        lhs.baseArraySlice,
        lhs.numArraySlices,
        TextureSubresourceSet::AllArraySlices
    );
    const u64 rhsArrayEnd = TextureRangeEnd(
        rhs.baseArraySlice,
        rhs.numArraySlices,
        TextureSubresourceSet::AllArraySlices
    );
    return lhs.baseMipLevel < rhsMipEnd
        && rhs.baseMipLevel < lhsMipEnd
        && lhs.baseArraySlice < rhsArrayEnd
        && rhs.baseArraySlice < lhsArrayEnd;
}

[[nodiscard]] static bool CompatiblePipelineMetadata(
    const GpuTaskGraphPipelineView& pipeline,
    const GpuGraphPipelineDesc& desc
)noexcept{
    // Identity and concrete pipeline kind define the graph-side table key.  Marker text is observational metadata,
    // matching resource imports where a later compatible import reuses the original graph-owned label.
    return pipeline.identity == desc.identity && pipeline.type == desc.type;
}

[[nodiscard]] static bool ClearTextureValueMatchesFormat(
    const TextureDesc& textureDesc,
    const GpuClearTextureTaskDesc& clearDesc
)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const bool depthStencilFormat = formatInfo.hasDepth || formatInfo.hasStencil;
    switch(clearDesc.valueType){
    case GpuClearTextureTaskValueType::Float:
        return !depthStencilFormat
            && (formatInfo.kind == FormatKind::Normalized || formatInfo.kind == FormatKind::Float)
        ;
    case GpuClearTextureTaskValueType::UInt:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && !formatInfo.isSigned;
    case GpuClearTextureTaskValueType::Int:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && formatInfo.isSigned;
    case GpuClearTextureTaskValueType::DepthStencil:
        return depthStencilFormat
            && (!clearDesc.clearDepth || formatInfo.hasDepth)
            && (!clearDesc.clearStencil || formatInfo.hasStencil)
        ;
    default:
        return false;
    }
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


GpuTaskGraph::GpuTaskGraph(GraphicsArena& arena)
    : m_arena(arena)
    , m_tasks(arena)
    , m_dependencies(arena)
    , m_externalDependencies(arena)
    , m_externalStateSources(arena)
    , m_externalStateSnapshots(arena)
    , m_resourceUses(arena)
    , m_resources(arena)
    , m_initialOwnerHandoffSources(arena)
    , m_resourceSets(arena)
    , m_resourceSetMembers(arena)
    , m_pipelines(arena)
    , m_externalCompletions(arena)
    , m_uploadBlobs(arena)
    , m_markerText(arena)
    , m_generation(__hidden_gpu_task_graph::AllocateGeneration())
{}

GpuTaskGraph::~GpuTaskGraph(){
    destroyTaskPayloads();
    destroyTaskStateSnapshots();
    destroyResourceStateSnapshots();
}


GpuTaskId GpuTaskGraph::addTask(const GpuTaskDesc& desc){
    return appendTask(desc, nullptr, nullptr, nullptr, nullptr, nullptr);
}

GpuTaskId GpuTaskGraph::addCopyBufferTask(const GpuTaskDesc& desc, const GpuCopyBufferTaskDesc& copyDesc){
    if(copyDesc.acceptedToken)
        *copyDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !copyDesc.regions
        || copyDesc.regionCount == 0u
        || copyDesc.regionCount > Limit<u32>::s_Max
        || copyDesc.regionCount > Limit<usize>::s_Max / 2u
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    using CopyTask = __hidden_gpu_task_graph::CopyBufferTask;
    CopyTask::Payload* const payload = NewArenaObject<CopyTask::Payload>(m_arena, m_arena);
    if(!payload)
        return {};
    payload->copies.reserve(copyDesc.regionCount);
    payload->acceptedToken = copyDesc.acceptedToken;

    GraphicsVector<GpuTaskResourceUse> resourceUses(m_arena);
    resourceUses.reserve(copyDesc.regionCount * 2u);
    const auto appendResourceUse = [&](const GpuGraphResourceId resource, const ResourceStates::Mask state, const GpuTaskResourceAccess::Enum access){
        for(const GpuTaskResourceUse& existing : resourceUses){
            if(existing.resource != resource)
                continue;
            // A primitive copy cannot safely read and write the same imported buffer inside one task. Keep that
            // sequencing explicit in separate tasks instead of silently weakening its graph declarations.
            return existing.requiredState == state && existing.access == access;
        }
        // Buffer ownership and recorded-state handoffs are still whole-buffer. Keep the graph declaration equally
        // conservative even though the native payload validates and records an exact byte range.
        resourceUses.push_back(GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = state,
            .access = access,
        });
        return true;
    };
    const auto validCopyRange = [](const BufferDesc& bufferDesc, const u64 offsetBytes, const u64 dataSizeBytes){
        return dataSizeBytes != 0u
            && offsetBytes <= bufferDesc.byteSize
            && dataSizeBytes <= bufferDesc.byteSize - offsetBytes
        ;
    };

    bool valid = true;
    for(usize regionIndex = 0u; regionIndex < copyDesc.regionCount && valid; ++regionIndex){
        const GpuCopyBufferTaskRegion& region = copyDesc.regions[regionIndex];
        if(!validResource(region.source) || !validResource(region.destination)){
            valid = false;
            break;
        }
        const GpuGraphResourceNode& sourceResource = m_resources[region.source.index];
        const GpuGraphResourceNode& destinationResource = m_resources[region.destination.index];
        valid = region.source != region.destination
            && sourceResource.type == GpuGraphResourceType::Buffer
            && destinationResource.type == GpuGraphResourceType::Buffer
            && sourceResource.buffer
            && destinationResource.buffer
            && validCopyRange(sourceResource.buffer->getDescription(), region.sourceOffsetBytes, region.dataSizeBytes)
            && validCopyRange(destinationResource.buffer->getDescription(), region.destinationOffsetBytes, region.dataSizeBytes)
            && appendResourceUse(region.source, ResourceStates::CopySource, GpuTaskResourceAccess::Read)
            && appendResourceUse(region.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write)
        ;
        if(valid){
            payload->copies.push_back(CopyTask::Copy{
                .sourceResource = region.source,
                .source = sourceResource.buffer,
                .sourceOffsetBytes = region.sourceOffsetBytes,
                .destinationResource = region.destination,
                .destination = destinationResource.buffer,
                .destinationOffsetBytes = region.destinationOffsetBytes,
                .dataSizeBytes = region.dataSizeBytes,
            });
        }
    }
    if(!valid){
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<CopyTask>,
            &DestroyPayload<CopyTask::Payload>
        );
        return {};
    }

    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(resourceUses.data(), resourceUses.size());
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<CopyTask>,
        &AcceptPayload<CopyTask>,
        &DiscardPayload<CopyTask>,
        &DestroyPayload<CopyTask::Payload>
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<CopyTask>,
            &DestroyPayload<CopyTask::Payload>
        );
    return task;
}

GpuTaskId GpuTaskGraph::addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc){
    if(copyDesc.acceptedToken)
        *copyDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !copyDesc.regions
        || copyDesc.regionCount == 0u
        || copyDesc.regionCount > Limit<u32>::s_Max
        || copyDesc.regionCount > Limit<usize>::s_Max / 2u
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    using CopyTask = __hidden_gpu_task_graph::CopyTextureTask;
    CopyTask::Payload* const payload = NewArenaObject<CopyTask::Payload>(m_arena, m_arena);
    if(!payload)
        return {};
    payload->copies.reserve(copyDesc.regionCount);
    payload->acceptedToken = copyDesc.acceptedToken;

    GraphicsVector<GpuTaskResourceUse> resourceUses(m_arena);
    resourceUses.reserve(copyDesc.regionCount * 2u);
    const auto appendResourceUse = [&](const GpuGraphResourceId resource, const ResourceStates::Mask state, const GpuTaskResourceAccess::Enum access){
        for(const GpuTaskResourceUse& existing : resourceUses){
            if(existing.resource != resource)
                continue;
            // A primitive copy cannot safely read and write the same imported image inside one task. Keep that
            // sequencing explicit in separate tasks instead of silently weakening its graph declarations.
            return existing.requiredState == state && existing.access == access;
        }
        resourceUses.push_back(GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = state,
            .access = access,
        });
        return true;
    };

    bool valid = true;
    for(usize regionIndex = 0u; regionIndex < copyDesc.regionCount && valid; ++regionIndex){
        const GpuCopyTextureTaskRegion& region = copyDesc.regions[regionIndex];
        if(!validResource(region.source) || !validResource(region.destination)){
            valid = false;
            break;
        }
        const GpuGraphResourceNode& sourceResource = m_resources[region.source.index];
        const GpuGraphResourceNode& destinationResource = m_resources[region.destination.index];
        valid = region.source != region.destination
            && sourceResource.type == GpuGraphResourceType::Texture
            && destinationResource.type == GpuGraphResourceType::Texture
            && sourceResource.texture
            && destinationResource.texture
            && appendResourceUse(region.source, ResourceStates::CopySource, GpuTaskResourceAccess::Read)
            && appendResourceUse(region.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write)
        ;
        if(valid){
            payload->copies.push_back(CopyTask::Copy{
                .sourceResource = region.source,
                .source = sourceResource.texture,
                .sourceSlice = region.sourceSlice,
                .destinationResource = region.destination,
                .destination = destinationResource.texture,
                .destinationSlice = region.destinationSlice,
            });
        }
    }
    if(!valid){
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<CopyTask>,
            &DestroyPayload<CopyTask::Payload>
        );
        return {};
    }

    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(resourceUses.data(), resourceUses.size());
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<CopyTask>,
        &AcceptPayload<CopyTask>,
        &DiscardPayload<CopyTask>,
        &DestroyPayload<CopyTask::Payload>
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<CopyTask>,
            &DestroyPayload<CopyTask::Payload>
        );
    return task;
}

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
        // vkCmdCopyBuffer requires every region offset and size to be four-byte aligned. The staging allocator
        // already provides an aligned source offset; reject graph inputs that cannot form a valid native region.
        || (uploadDesc.destinationOffsetBytes & (sizeof(u32) - 1u)) != 0u
        || (source->bytes.size() & (sizeof(u32) - 1u)) != 0u
        // CommandList::close restores keepInitialState resources after recording. Any different graph-visible final
        // state would therefore lie to a later packet's compiler-owned state seed.
        || (destinationDesc.keepInitialState && uploadDesc.finalState != destinationDesc.initialState)
    )
        return {};

    using UploadTask = __hidden_gpu_task_graph::UploadBufferTask;
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
        &DestroyPayload<UploadTask::Payload>
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
        (destinationDesc.keepInitialState && uploadDesc.finalState != destinationDesc.initialState)
        ||
        !__hidden_gpu_task_graph::ComputeTextureUploadByteSize(
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

    using UploadTask = __hidden_gpu_task_graph::UploadTextureTask;
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
        &DestroyPayload<UploadTask::Payload>
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<UploadTask>,
            &DestroyPayload<UploadTask::Payload>
        );
    return task;
}

GpuTaskId GpuTaskGraph::addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc){
    if(clearDesc.acceptedToken)
        *clearDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !validResource(clearDesc.destination)
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Buffer
        || !destinationResource.buffer
        || destinationResource.buffer->getDescription().byteSize == 0u
        || (destinationResource.buffer->getDescription().byteSize & (sizeof(u32) - 1u)) != 0u
    )
        return {};

    using ClearTask = __hidden_gpu_task_graph::ClearBufferTask;
    ClearTask::Payload* const payload = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payload)
        return {};
    payload->destinationResource = clearDesc.destination;
    payload->destination = destinationResource.buffer;
    payload->clearValue = clearDesc.clearValue;
    payload->acceptedToken = clearDesc.acceptedToken;

    const GpuTaskResourceUse resourceUse{
        .resource = clearDesc.destination,
        .range = {},
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ClearTask>,
        &AcceptPayload<ClearTask>,
        &DiscardPayload<ClearTask>,
        &DestroyPayload<ClearTask::Payload>
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<ClearTask>,
            &DestroyPayload<ClearTask::Payload>
        );
    return task;
}

GpuTaskId GpuTaskGraph::addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc){
    if(clearDesc.acceptedToken)
        *clearDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !validResource(clearDesc.destination)
        || clearDesc.valueType >= GpuClearTextureTaskValueType::kCount
        || (
            clearDesc.valueType == GpuClearTextureTaskValueType::DepthStencil
            && !clearDesc.clearDepth
            && !clearDesc.clearStencil
        )
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Texture
        || !destinationResource.texture
        // vkCmdClear*Image cannot operate on multisampled images outside a render pass. This primitive helper has no
        // framebuffer/render-pass lowering, so preserve its transfer-only contract by rejecting MSAA up front.
        || destinationResource.texture->getDescription().sampleCount != 1u
    )
        return {};
    if(!__hidden_gpu_task_graph::ClearTextureValueMatchesFormat(
        destinationResource.texture->getDescription(),
        clearDesc
    ))
        return {};
    const TextureSubresourceSet resolvedSubresources = clearDesc.subresources.resolve(
        destinationResource.texture->getDescription(),
        TextureSubresourceMipResolve::Range
    );
    if(resolvedSubresources.numMipLevels == 0u || resolvedSubresources.numArraySlices == 0u)
        return {};

    using ClearTask = __hidden_gpu_task_graph::ClearTextureTask;
    ClearTask::Payload* const payload = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payload)
        return {};
    payload->destinationResource = clearDesc.destination;
    payload->destination = destinationResource.texture;
    payload->clearDesc = clearDesc;
    // Capture and native lowering retain the same exact graph-declared range rather than an all-subresources alias
    // that would need the original TextureDesc to resolve during a later validation/replay phase.
    payload->clearDesc.subresources = resolvedSubresources;

    const GpuTaskResourceUse resourceUse{
        .resource = clearDesc.destination,
        .range = GpuTaskResourceRange{
            .textureSubresources = resolvedSubresources,
        },
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ClearTask>,
        &AcceptPayload<ClearTask>,
        &DiscardPayload<ClearTask>,
        &DestroyPayload<ClearTask::Payload>
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<ClearTask>,
            &DestroyPayload<ClearTask::Payload>
        );
    return task;
}

GpuTaskId GpuTaskGraph::addClearTextureRectUIntTask(
    const GpuTaskDesc& desc,
    const GpuClearTextureRectUIntTaskDesc& clearDesc
){
    if(clearDesc.acceptedToken)
        *clearDesc.acceptedToken = {};

    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || desc.resourceSetUses
        || desc.resourceSetUseCount != 0u
        || !validResource(clearDesc.destination)
        || clearDesc.rect.maxX <= clearDesc.rect.minX
        || clearDesc.rect.maxY <= clearDesc.rect.minY
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Texture
        || !destinationResource.texture
        // vkCmdClear*Image cannot operate on multisampled images outside a render pass. This primitive helper has no
        // framebuffer/render-pass lowering, so preserve its transfer-only contract by rejecting MSAA up front.
        || destinationResource.texture->getDescription().sampleCount != 1u
    )
        return {};
    const GpuClearTextureTaskDesc formatValidation{
        .destination = {},
        .valueType = GpuClearTextureTaskValueType::UInt,
    };
    if(!__hidden_gpu_task_graph::ClearTextureValueMatchesFormat(
        destinationResource.texture->getDescription(),
        formatValidation
    ))
        return {};
    const TextureSubresourceSet resolvedSubresources = clearDesc.subresources.resolve(
        destinationResource.texture->getDescription(),
        TextureSubresourceMipResolve::Range
    );
    if(resolvedSubresources.numMipLevels == 0u || resolvedSubresources.numArraySlices == 0u)
        return {};

    using ClearTask = __hidden_gpu_task_graph::ClearTextureRectUIntTask;
    ClearTask::Payload* const payload = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payload)
        return {};
    payload->destinationResource = clearDesc.destination;
    payload->destination = destinationResource.texture;
    payload->clearDesc = clearDesc;
    payload->clearDesc.subresources = resolvedSubresources;

    const GpuTaskResourceUse resourceUse{
        .resource = clearDesc.destination,
        .range = GpuTaskResourceRange{
            .textureSubresources = resolvedSubresources,
        },
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ClearTask>,
        &AcceptPayload<ClearTask>,
        &DiscardPayload<ClearTask>,
        &DestroyPayload<ClearTask::Payload>
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<ClearTask>,
            &DestroyPayload<ClearTask::Payload>
        );
    return task;
}

GpuGraphResourceId GpuTaskGraph::importResource(const GpuGraphResourceDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphResourceType::kCount)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuTaskGraphResourceView existing = resourceAt(resourceIndex);
        if(existing.identity != desc.identity)
            continue;
        if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(
            existing,
            m_resources[resourceIndex].initialOwnerStateSourceIdentity,
            desc
        ))
            return {};
        return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }

    return appendResource(desc);
}

GpuGraphResourceId GpuTaskGraph::importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc){
    if(!texture || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Texture)
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = texture->getDescription().initialState;
    if(resolvedDesc.queueSharing == ResourceQueueSharing::Exclusive)
        resolvedDesc.queueSharing = texture->getDescription().queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Texture && existing.texture.get() == texture.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(
                resourceAt(resourceIndex),
                existing.initialOwnerStateSourceIdentity,
                resolvedDesc
            ))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid()){
        GpuGraphResourceNode& importedResource = m_resources[resource.index];
        importedResource.texture = texture;
        importedResource.deviceGeneration = texture->getDeviceGeneration();
    }
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc){
    if(!buffer || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Buffer)
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = buffer->getDescription().initialState;
    if(resolvedDesc.queueSharing == ResourceQueueSharing::Exclusive)
        resolvedDesc.queueSharing = buffer->getDescription().queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Buffer && existing.buffer.get() == buffer.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(
                resourceAt(resourceIndex),
                existing.initialOwnerStateSourceIdentity,
                resolvedDesc
            ))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid()){
        GpuGraphResourceNode& importedResource = m_resources[resource.index];
        importedResource.buffer = buffer;
        importedResource.deviceGeneration = buffer->getDeviceGeneration();
    }
    return resource;
}

GpuGraphResourceId GpuTaskGraph::findImportedTexture(const TextureHandle& texture)const noexcept{
    if(!texture)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& resource = m_resources[resourceIndex];
        if(resource.type == GpuGraphResourceType::Texture && resource.texture.get() == texture.get())
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }
    return {};
}

GpuGraphResourceId GpuTaskGraph::findImportedBuffer(const BufferHandle& buffer)const noexcept{
    if(!buffer)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Buffer && existing.buffer.get() == buffer.get())
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }
    return {};
}

GpuGraphResourceId GpuTaskGraph::importAccelStruct(
    const RayTracingAccelStructHandle& accelStruct,
    const GpuGraphResourceDesc& desc
){
    if(!accelStruct || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::AccelStruct)
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(resolvedDesc.queueSharing == ResourceQueueSharing::Exclusive)
        resolvedDesc.queueSharing = accelStruct->getDescription().queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::AccelStruct && existing.accelStruct.get() == accelStruct.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(
                resourceAt(resourceIndex),
                existing.initialOwnerStateSourceIdentity,
                resolvedDesc
            ))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid()){
        GpuGraphResourceNode& importedResource = m_resources[resource.index];
        importedResource.accelStruct = accelStruct;
        importedResource.deviceGeneration = accelStruct->getDeviceGeneration();
    }
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importHazardDomain(const GpuGraphResourceDesc& desc){
    if(desc.type != GpuGraphResourceType::HazardDomain)
        return {};
    return importResource(desc);
}

GpuGraphResourceSetId GpuTaskGraph::importResourceSet(const GpuGraphResourceSetDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || (desc.memberCount != 0u && !desc.members)
    )
        return {};

    for(usize resourceSetIndex = 0u; resourceSetIndex < m_resourceSets.size(); ++resourceSetIndex){
        const GpuTaskGraphResourceSetView existing = resourceSetAt(resourceSetIndex);
        if(existing.identity != desc.identity)
            continue;
        if(existing.memberCount != desc.memberCount)
            return {};
        for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex){
            if(existing.members[memberIndex] != desc.members[memberIndex])
                return {};
        }
        return existing.id;
    }

    return appendResourceSet(desc);
}

GpuGraphPipelineId GpuTaskGraph::importPipeline(const GpuGraphPipelineDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphPipelineType::kCount)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuTaskGraphPipelineView existing = pipelineAt(pipelineIndex);
        if(existing.identity != desc.identity)
            continue;
        if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(existing, desc))
            return {};
        return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
    }

    return appendPipeline(desc);
}

GpuGraphPipelineId GpuTaskGraph::importGraphicsPipeline(
    const GraphicsPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Graphics)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::Graphics && existing.graphicsPipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.graphicsPipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuGraphPipelineId GpuTaskGraph::importComputePipeline(
    const ComputePipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Compute)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::Compute && existing.computePipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.computePipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuGraphPipelineId GpuTaskGraph::importMeshletPipeline(
    const MeshletPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Meshlet)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::Meshlet && existing.meshletPipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.meshletPipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuGraphPipelineId GpuTaskGraph::importRayTracingPipeline(
    const RayTracingPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::RayTracing)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::RayTracing && existing.rayTracingPipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.rayTracingPipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuExternalCompletionId GpuTaskGraph::importExternalCompletion(const GpuExternalCompletionDesc& desc){
    if(!desc.identity || desc.markerLabel.empty())
        return {};

    for(usize completionIndex = 0u; completionIndex < m_externalCompletions.size(); ++completionIndex){
        if(m_externalCompletions[completionIndex].identity == desc.identity)
            return GpuExternalCompletionId{ static_cast<u32>(completionIndex), m_generation };
    }

    return appendExternalCompletion(desc);
}

void GpuTaskGraph::reset(){
    destroyTaskPayloads();
    destroyTaskStateSnapshots();
    destroyResourceStateSnapshots();
    m_tasks.clear();
    m_dependencies.clear();
    m_externalDependencies.clear();
    m_externalStateSources.clear();
    m_resourceUses.clear();
    m_resources.clear();
    m_initialOwnerHandoffSources.clear();
    m_resourceSets.clear();
    m_resourceSetMembers.clear();
    m_pipelines.clear();
    m_externalCompletions.clear();
    m_uploadBlobs.clear();
    m_markerText.clear();
    m_generation = __hidden_gpu_task_graph::AllocateGeneration();
}

bool GpuTaskGraph::validForDeviceGeneration(const u16 deviceGeneration)const noexcept{
    if(deviceGeneration == 0u)
        return false;

    const auto validStateSource = [deviceGeneration](const CommandListResourceStateHandoff* const states){
        // Invalid declarations intentionally remain a record-time failure so legacy callers retain their existing
        // diagnostic. A valid snapshot, however, must never cross a native Device lifetime.
        return !states || !states->valid() || states->validForDeviceGeneration(deviceGeneration);
    };

    for(const GpuTaskExternalStateSource& source : m_externalStateSources){
        if(!validStateSource(source.states))
            return false;
    }
    for(const GpuGraphResourceNode& resource : m_resources){
        if(
            (resource.texture != nullptr && resource.deviceGeneration != deviceGeneration)
            || (resource.buffer != nullptr && resource.deviceGeneration != deviceGeneration)
            || (resource.accelStruct != nullptr && resource.deviceGeneration != deviceGeneration)
            || !validStateSource(resource.initialOwnerStateSource)
        )
            return false;
    }
    for(const GpuTaskGraphInitialOwnerHandoffSourceView& source : m_initialOwnerHandoffSources){
        if(!validStateSource(source.stateSource))
            return false;
    }
    for(const GpuGraphPipelineNode& pipeline : m_pipelines){
        if(
            (pipeline.graphicsPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.computePipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.meshletPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.rayTracingPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
        )
            return false;
    }
    return true;
}

bool GpuTaskGraph::validTask(const GpuTaskId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_tasks.size();
}

bool GpuTaskGraph::validResource(const GpuGraphResourceId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resources.size();
}

bool GpuTaskGraph::validResourceSet(const GpuGraphResourceSetId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resourceSets.size();
}

bool GpuTaskGraph::validUploadBlob(const GpuUploadBlobId& id)const noexcept{
    return id.valid()
        && id.generation == m_generation
        && id.index < m_uploadBlobs.size()
        && !m_uploadBlobs[id.index].bytes.empty()
    ;
}

bool GpuTaskGraph::validPipeline(const GpuGraphPipelineId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_pipelines.size();
}

bool GpuTaskGraph::validExternalCompletion(const GpuExternalCompletionId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_externalCompletions.size();
}

GpuTaskGraphTaskView GpuTaskGraph::taskAt(const usize index)const{
    NWB_ASSERT(index < m_tasks.size());
    const GpuTaskNode& task = m_tasks[index];
    return GpuTaskGraphTaskView{
        .id = GpuTaskId{ static_cast<u32>(index), m_generation },
        .identity = task.identity,
        .markerLabel = markerLabel(task.markerLabelOffset, task.markerLabelSize),
        .queue = task.queue,
        .scheduling = task.scheduling,
        .dependencies = task.dependencyCount > 0u ? m_dependencies.data() + task.dependencyOffset : nullptr,
        .dependencyCount = task.dependencyCount,
        .externalDependencies = task.externalDependencyCount > 0u
            ? m_externalDependencies.data() + task.externalDependencyOffset
            : nullptr,
        .externalDependencyCount = task.externalDependencyCount,
        .externalStateSources = task.externalStateSourceCount > 0u
            ? m_externalStateSources.data() + task.externalStateSourceOffset
            : nullptr,
        .externalStateSourceCount = task.externalStateSourceCount,
        .resourceUses = task.resourceUseCount > 0u ? m_resourceUses.data() + task.resourceUseOffset : nullptr,
        .resourceUseCount = task.resourceUseCount,
        .hasPayload = task.payload != nullptr,
        .hasRecordPayload = task.recordPayload != nullptr,
    };
}

GpuTaskGraphResourceView GpuTaskGraph::resourceAt(const usize index)const{
    NWB_ASSERT(index < m_resources.size());
    const GpuGraphResourceNode& resource = m_resources[index];
    return GpuTaskGraphResourceView{
        .id = GpuGraphResourceId{ static_cast<u32>(index), m_generation },
        .identity = resource.identity,
        .markerLabel = markerLabel(resource.markerLabelOffset, resource.markerLabelSize),
        .type = resource.type,
        .initialState = resource.initialState,
        .externalFinalState = resource.externalFinalState,
        .externalFinalReleaseDestinationQueue = resource.externalFinalReleaseDestinationQueue,
        .initialOwnerQueue = resource.initialOwnerQueue,
        .initialOwnerReleaseDestinationQueue = resource.initialOwnerReleaseDestinationQueue,
        .initialOwnerCompletion = resource.initialOwnerCompletion,
        .initialOwnerStateSource = resource.initialOwnerStateSource,
        .initialOwnerHandoffSources = resource.initialOwnerHandoffSourceCount != 0u
            ? m_initialOwnerHandoffSources.data() + resource.initialOwnerHandoffSourceOffset
            : nullptr,
        .initialOwnerHandoffSourceCount = resource.initialOwnerHandoffSourceCount,
        .queueSharing = resource.queueSharing,
        .hasBackendResource = resource.texture != nullptr || resource.buffer != nullptr || resource.accelStruct != nullptr,
    };
}

GpuTaskGraphResourceSetView GpuTaskGraph::resourceSetAt(const usize index)const{
    NWB_ASSERT(index < m_resourceSets.size());
    const GpuGraphResourceSetNode& resourceSet = m_resourceSets[index];
    return GpuTaskGraphResourceSetView{
        .id = GpuGraphResourceSetId{ static_cast<u32>(index), m_generation },
        .identity = resourceSet.identity,
        .markerLabel = markerLabel(resourceSet.markerLabelOffset, resourceSet.markerLabelSize),
        .members = resourceSet.memberCount > 0u ? m_resourceSetMembers.data() + resourceSet.memberOffset : nullptr,
        .memberCount = resourceSet.memberCount,
    };
}

GpuTaskGraphPipelineView GpuTaskGraph::pipelineAt(const usize index)const{
    NWB_ASSERT(index < m_pipelines.size());
    const GpuGraphPipelineNode& pipeline = m_pipelines[index];
    return GpuTaskGraphPipelineView{
        .id = GpuGraphPipelineId{ static_cast<u32>(index), m_generation },
        .identity = pipeline.identity,
        .markerLabel = markerLabel(pipeline.markerLabelOffset, pipeline.markerLabelSize),
        .type = pipeline.type,
        .hasBackendPipeline = pipeline.graphicsPipeline != nullptr
            || pipeline.computePipeline != nullptr
            || pipeline.meshletPipeline != nullptr
            || pipeline.rayTracingPipeline != nullptr,
    };
}

GpuTaskGraphExternalCompletionView GpuTaskGraph::externalCompletionAt(const usize index)const{
    NWB_ASSERT(index < m_externalCompletions.size());
    const GpuExternalCompletionNode& completion = m_externalCompletions[index];
    return GpuTaskGraphExternalCompletionView{
        .id = GpuExternalCompletionId{ static_cast<u32>(index), m_generation },
        .identity = completion.identity,
        .markerLabel = markerLabel(completion.markerLabelOffset, completion.markerLabelSize),
    };
}

Texture* GpuTaskGraph::textureForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Texture ? node.texture.get() : nullptr;
}

Buffer* GpuTaskGraph::bufferForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Buffer ? node.buffer.get() : nullptr;
}

RayTracingAccelStruct* GpuTaskGraph::accelStructForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::AccelStruct ? node.accelStruct.get() : nullptr;
}

const void* GpuTaskGraph::uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const noexcept{
    outByteSize = 0u;
    const GpuUploadBlobNode* const node = findUploadBlob(blob);
    if(!node || node->bytes.empty())
        return nullptr;
    outByteSize = node->bytes.size();
    return node->bytes.data();
}

GraphicsPipeline* GpuTaskGraph::graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Graphics ? node.graphicsPipeline.get() : nullptr;
}

ComputePipeline* GpuTaskGraph::computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Compute ? node.computePipeline.get() : nullptr;
}

MeshletPipeline* GpuTaskGraph::meshletPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Meshlet ? node.meshletPipeline.get() : nullptr;
}

RayTracingPipeline* GpuTaskGraph::rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::RayTracing ? node.rayTracingPipeline.get() : nullptr;
}

bool GpuTaskGraph::recordTask(
    const GpuTaskId& taskID,
    CommandList& commandList,
    const GpuTaskRecordContext& context
)const{
    if(!validTask(taskID))
        return false;

    const GpuTaskNode& task = m_tasks[taskID.index];
    return task.payload && task.recordPayload && task.recordPayload(task.payload, commandList, context);
}

bool GpuTaskGraph::applyCompiledBarrier(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledBarrier& barrier,
    CommandList& commandList
)const{
    if(
        !compiledGraph.validFor(*this)
        || !validResource(barrier.resource)
        || barrier.type >= GpuCompiledBarrierType::kCount
    )
        return false;

    const GpuGraphResourceNode& resource = m_resources[barrier.resource.index];
    const auto resolveOwnershipQueues = [&]{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        return sourceQueue
            && destinationQueue
            && sourceQueue->queueClass < CommandQueue::kCount
            && destinationQueue->queueClass < CommandQueue::kCount
        ;
    };
    switch(barrier.type){
    case GpuCompiledBarrierType::TextureTransition:
    case GpuCompiledBarrierType::TextureUav:
        if(resource.type != GpuGraphResourceType::Texture || !resource.texture)
            return false;
        if(barrier.isGraphInitialState){
            if(barrier.before == ResourceStates::Unknown)
                return false;
            const TextureSubresourceSet subresources = barrier.range.textureSubresources.resolve(
                resource.texture->getDescription(),
                TextureSubresourceMipResolve::Range
            );
            const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
            const ArraySlice arrayEnd = subresources.baseArraySlice + subresources.numArraySlices;
            for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
                    // CommandList::open can seed this exact subresource from an external handoff. Preserve that
                    // producer truth, but do not mistake a keep-initial-state descriptor fallback for a packet
                    // handoff: the graph declaration is authoritative when no explicit tracker state exists.
                    if(commandList.hasExplicitTextureSubresourceState(resource.texture.get(), arraySlice, mipLevel))
                        continue;
                    commandList.beginTrackingTextureState(
                        resource.texture.get(),
                        TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
                        barrier.before
                    );
                }
            }
        }
        commandList.setTextureState(
            resource.texture.get(),
            barrier.range.textureSubresources,
            barrier.after
        );
        return true;
    case GpuCompiledBarrierType::BufferTransition:
    case GpuCompiledBarrierType::BufferUav:
        if(resource.type != GpuGraphResourceType::Buffer || !resource.buffer)
            return false;
        if(barrier.isGraphInitialState){
            if(barrier.before == ResourceStates::Unknown)
                return false;
            if(!commandList.hasExplicitBufferState(resource.buffer.get()))
                commandList.beginTrackingBufferState(resource.buffer.get(), barrier.before);
        }
        commandList.setBufferState(resource.buffer.get(), barrier.after);
        return true;
    case GpuCompiledBarrierType::TextureStateExport:
        if(resource.type != GpuGraphResourceType::Texture || !resource.texture)
            return false;
        // A task thunk may transition internally after its declared entry use. Reapply the compiler-required
        // final state against the native tracker, then retain it even when no Vulkan transition was needed so
        // packet close publishes a complete external handoff.
        commandList.setTextureState(
            resource.texture.get(),
            barrier.range.textureSubresources,
            barrier.after
        );
        commandList.beginTrackingTextureState(
            resource.texture.get(),
            barrier.range.textureSubresources,
            barrier.after
        );
        return true;
    case GpuCompiledBarrierType::BufferStateExport:
        if(resource.type != GpuGraphResourceType::Buffer || !resource.buffer)
            return false;
        commandList.setBufferState(resource.buffer.get(), barrier.after);
        commandList.beginTrackingBufferState(resource.buffer.get(), barrier.after);
        return true;
    case GpuCompiledBarrierType::AccelStructStateExport:{
        if(resource.type != GpuGraphResourceType::AccelStruct || !resource.accelStruct)
            return false;
        Buffer* const backingBuffer = resource.accelStruct->getBackingBuffer();
        if(!backingBuffer)
            return false;
        // Acceleration-structure state and ownership are represented by Vulkan through the allocation that backs
        // the AS. Keep that lowering private to the graph runtime while retaining one typed graph resource.
        commandList.setAccelStructState(resource.accelStruct.get(), barrier.after);
        commandList.beginTrackingBufferState(backingBuffer, barrier.after);
        return true;
    }
    case GpuCompiledBarrierType::AccelStructTransition:
    case GpuCompiledBarrierType::AccelStructUav:
        if(resource.type != GpuGraphResourceType::AccelStruct || !resource.accelStruct)
            return false;
        if(barrier.isGraphInitialState){
            if(barrier.before == ResourceStates::Unknown)
                return false;
            Buffer* const backingBuffer = resource.accelStruct->getBackingBuffer();
            if(!backingBuffer)
                return false;
            if(!commandList.hasExplicitBufferState(backingBuffer))
                commandList.beginTrackingBufferState(backingBuffer, barrier.before);
        }
        commandList.setAccelStructState(resource.accelStruct.get(), barrier.after);
        return true;
    case GpuCompiledBarrierType::TextureOwnershipRelease:{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(
            resource.type != GpuGraphResourceType::Texture
            || !resource.texture
            || !resolveOwnershipQueues()
            || commandList.getDescription().physicalQueue != sourceQueue->id
        )
            return false;
        commandList.releaseTextureOwnership(
            resource.texture.get(),
            barrier.range.textureSubresources,
            destinationQueue->id
        );
        return true;
    }
    case GpuCompiledBarrierType::BufferOwnershipRelease:{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(
            resource.type != GpuGraphResourceType::Buffer
            || !resource.buffer
            || !resolveOwnershipQueues()
            || commandList.getDescription().physicalQueue != sourceQueue->id
        )
            return false;
        commandList.releaseBufferOwnership(resource.buffer.get(), destinationQueue->id);
        return true;
    }
    case GpuCompiledBarrierType::AccelStructOwnershipRelease:{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(
            resource.type != GpuGraphResourceType::AccelStruct
            || !resource.accelStruct
            || !resolveOwnershipQueues()
            || commandList.getDescription().physicalQueue != sourceQueue->id
        )
            return false;
        Buffer* const backingBuffer = resource.accelStruct->getBackingBuffer();
        if(!backingBuffer)
            return false;
        commandList.releaseBufferOwnership(backingBuffer, destinationQueue->id);
        return true;
    }
    case GpuCompiledBarrierType::TextureOwnershipAcquire:
    case GpuCompiledBarrierType::BufferOwnershipAcquire:
    case GpuCompiledBarrierType::AccelStructOwnershipAcquire:{
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(!resolveOwnershipQueues() || commandList.getDescription().physicalQueue != destinationQueue->id)
            return false;

        // CommandList::open imports the compiler-selected producer state seed before packet prologue lowering. That
        // import emits the paired Vulkan acquire barrier with the exact exported layout, so this record is a checked
        // graph-plan marker rather than a second native acquire.
        return true;
    }
    default:
        return false;
    }
}

bool GpuTaskGraph::seedTaskRetainedResourceStates(
    const GpuTaskId& taskID,
    CommandList& commandList
)const{
    if(!validTask(taskID))
        return false;

    const GpuTaskNode& task = m_tasks[taskID.index];
    const GpuTaskResourceUse* const resourceUses = task.resourceUseCount != 0u
        ? m_resourceUses.data() + task.resourceUseOffset
        : nullptr
    ;
    if(task.resourceUseCount != 0u && !resourceUses)
        return false;
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = resourceUses[useIndex];
        if(!validResource(use.resource) || use.requiredState == ResourceStates::Unknown)
            return false;

        const GpuGraphResourceNode& resource = m_resources[use.resource.index];
        switch(resource.type){
        case GpuGraphResourceType::Texture:{
            if(!resource.texture)
                return false;
            const TextureDesc& description = resource.texture->getDescription();
            // Only seed a state that the Vulkan backend will retain exactly at packet close. Other graph resources
            // must already have an explicit compiler transition or native record-time state before they can become
            // a source.
            if(!description.keepInitialState || description.initialState != use.requiredState)
                continue;

            const TextureSubresourceSet subresources = use.range.textureSubresources.resolve(
                description,
                TextureSubresourceMipResolve::Range
            );
            if(subresources.numMipLevels == 0u || subresources.numArraySlices == 0u)
                return false;

            const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
            const ArraySlice arrayEnd = subresources.baseArraySlice + subresources.numArraySlices;
            bool stateMatches = true;
            for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
                    if(commandList.getTextureSubresourceState(resource.texture.get(), arraySlice, mipLevel) != use.requiredState){
                        stateMatches = false;
                        break;
                    }
                }
                if(!stateMatches)
                    break;
            }
            // A graph task can establish this state inside its own recording body (for example an upload changes
            // CopyDest back to a texture's retained ShaderResource state). Only materialize a state that is already
            // present before the task; the record body publishes its own state through the ordinary native tracker.
            if(!stateMatches)
                continue;
            commandList.beginTrackingTextureState(resource.texture.get(), subresources, use.requiredState);
            break;
        }
        case GpuGraphResourceType::Buffer:{
            bool alreadySeededByTask = false;
            for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                const GpuTaskResourceUse& previousUse = resourceUses[previousUseIndex];
                if(previousUse.resource == use.resource){
                    alreadySeededByTask = true;
                    break;
                }
            }
            if(alreadySeededByTask)
                continue;

            if(!resource.buffer)
                return false;
            const BufferDesc& description = resource.buffer->getDescription();
            if(!description.keepInitialState || description.initialState != use.requiredState)
                continue;
            if(commandList.getBufferState(resource.buffer.get()) != use.requiredState)
                return false;
            commandList.beginTrackingBufferState(resource.buffer.get(), use.requiredState);
            break;
        }
        case GpuGraphResourceType::AccelStruct:{
            bool alreadySeededByTask = false;
            for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                const GpuTaskResourceUse& previousUse = resourceUses[previousUseIndex];
                if(previousUse.resource == use.resource){
                    alreadySeededByTask = true;
                    break;
                }
            }
            if(alreadySeededByTask)
                continue;

            RayTracingAccelStruct* const accelStruct = resource.accelStruct.get();
            Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
            if(!backingBuffer)
                return false;
            const BufferDesc& description = backingBuffer->getDescription();
            if(!description.keepInitialState || description.initialState != use.requiredState)
                continue;
            if(commandList.getBufferState(backingBuffer) != use.requiredState)
                return false;
            commandList.beginTrackingBufferState(backingBuffer, use.requiredState);
            break;
        }
        default:
            break;
        }
    }
    return true;
}

void GpuTaskGraph::acceptTask(const GpuTaskId& taskID, const QueueSubmissionToken& token)noexcept{
    if(!validTask(taskID) || !token.valid())
        return;

    GpuTaskNode& task = m_tasks[taskID.index];
    if(task.lifecycleState != GpuTaskLifecycleState::Declared)
        return;
    task.lifecycleState = GpuTaskLifecycleState::Accepted;
    if(task.payload && task.acceptPayload)
        task.acceptPayload(task.payload, token);
}

void GpuTaskGraph::discardTask(const GpuTaskId& taskID)noexcept{
    if(!validTask(taskID))
        return;

    GpuTaskNode& task = m_tasks[taskID.index];
    if(task.lifecycleState != GpuTaskLifecycleState::Declared)
        return;
    task.lifecycleState = GpuTaskLifecycleState::Discarded;
    if(task.payload && task.discardPayload)
        task.discardPayload(task.payload);
}

bool GpuTaskGraph::appendFrameGraphTelemetry(
    Telemetry::FrameGraphBuilder& builder,
    const GpuTaskGraphAnalysis& analysis,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphTelemetryOptions& options
)const{
    if(
        !analysis.validFor(*this)
        || m_tasks.empty()
        || (options.queueAssignments && !options.queueAssignments->validFor(*this))
    )
        return false;
    if(options.queueAssignments){
        for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
            if(!options.queueAssignments->find(taskAt(taskIndex).id))
                return false;
        }
    }

    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> resourceNodes(scratchArena);
    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> completionNodes(scratchArena);
    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> taskNodes(scratchArena);
    resourceNodes.reserve(m_resources.size());
    completionNodes.reserve(m_externalCompletions.size());
    taskNodes.reserve(m_tasks.size());

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = resourceAt(resourceIndex);
        resourceNodes.push_back(builder.addResource(resource.identity, resource.markerLabel));
    }
    for(usize completionIndex = 0u; completionIndex < m_externalCompletions.size(); ++completionIndex){
        const GpuTaskGraphExternalCompletionView completion = externalCompletionAt(completionIndex);
        completionNodes.push_back(builder.addExternal(completion.identity, completion.markerLabel));
    }
    for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
        const GpuTaskGraphTaskView task = taskAt(taskIndex);
        u8 flags = GpuTaskGraphTelemetryNodeFlag::None;
        if(options.queueAssignments){
            const GpuTaskQueueAssignment* const assignment = options.queueAssignments->find(task.id);
            NWB_ASSERT(assignment);
            switch(assignment->queueClass){
            case CommandQueue::Graphics:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue;
                break;
            case CommandQueue::Compute:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedComputeQueue;
                break;
            case CommandQueue::Transfer:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedTransferQueue;
                break;
            default:
                return false;
            }
            if(assignment->dedicated)
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedDedicatedQueue;
            if(assignment->reason == GpuTaskQueueAssignmentReason::Fallback)
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentFallback;
            if(assignment->reason == GpuTaskQueueAssignmentReason::SameClassRouting)
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentSameClassRouting;
        }
        taskNodes.push_back(builder.addPass(task.identity, task.markerLabel, flags));
    }

    for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
        const GpuTaskGraphTaskView task = taskAt(taskIndex);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const Telemetry::FrameGraphNodeHandle resourceNode = resourceNodes[use.resource.index];
            const Telemetry::FrameGraphNodeHandle taskNode = taskNodes[taskIndex];
            if(use.access == GpuTaskResourceAccess::Read || use.access == GpuTaskResourceAccess::ReadWrite)
                builder.addEdge(resourceNode, taskNode, Telemetry::FrameGraphEdgeKind::Reads);
            if(use.access == GpuTaskResourceAccess::Write || use.access == GpuTaskResourceAccess::ReadWrite)
                builder.addEdge(taskNode, resourceNode, Telemetry::FrameGraphEdgeKind::Writes);
        }
    }
    for(const GpuTaskExternalDependencyEdge& edge : analysis.externalDependencies())
        builder.addEdge(completionNodes[edge.completion.index], taskNodes[edge.consumer.index], Telemetry::FrameGraphEdgeKind::DependsOn);
    for(const GpuTaskDependencyEdge& edge : analysis.edges()){
        u8 flags = GpuTaskGraphTelemetryEdgeFlag::None;
        if(analysis.hasExplicitEdge(edge.producer, edge.consumer))
            flags |= GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency;
        if(analysis.hasInferredEdge(edge.producer, edge.consumer))
            flags |= GpuTaskGraphTelemetryEdgeFlag::InferredDependency;
        builder.addEdge(
            taskNodes[edge.producer.index],
            taskNodes[edge.consumer.index],
            Telemetry::FrameGraphEdgeKind::DependsOn,
            flags
        );
    }

    return true;
}


GpuTaskId GpuTaskGraph::appendTask(
    const GpuTaskDesc& desc,
    void* const payload,
    const GpuTaskRecordThunk recordPayload,
    const GpuTaskAcceptedThunk acceptPayload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload
){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || m_tasks.size() >= Limit<u32>::s_Max
        || desc.dependencyCount > Limit<u32>::s_Max - m_dependencies.size()
        || desc.externalDependencyCount > Limit<u32>::s_Max - m_externalDependencies.size()
        || desc.externalStateSourceCount > Limit<u32>::s_Max - m_externalStateSources.size()
        || desc.resourceUseCount > Limit<u32>::s_Max
        || desc.resourceSetUseCount > Limit<u32>::s_Max
        || (desc.dependencyCount > 0u && !desc.dependencies)
        || (desc.externalDependencyCount > 0u && !desc.externalDependencies)
        || (desc.externalStateSourceCount > 0u && !desc.externalStateSources)
        || (desc.resourceUseCount > 0u && !desc.resourceUses)
        || (desc.resourceSetUseCount > 0u && !desc.resourceSetUses)
    )
        return {};

    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        if(!desc.externalStateSources[sourceIndex].states)
            return {};
    }

    // Resource sets are immutable graph data. Expand them now rather than introducing an opaque aggregate into
    // hazard analysis or barrier lowering; every later stage sees the same concrete member uses as an explicit
    // declaration would have supplied.
    usize expandedResourceUseCount = desc.resourceUseCount;
    const usize remainingResourceUseCapacity = static_cast<usize>(Limit<u32>::s_Max) - m_resourceUses.size();
    if(expandedResourceUseCount > remainingResourceUseCapacity)
        return {};
    for(usize resourceSetUseIndex = 0u; resourceSetUseIndex < desc.resourceSetUseCount; ++resourceSetUseIndex){
        const GpuTaskResourceSetUse& resourceSetUse = desc.resourceSetUses[resourceSetUseIndex];
        if(!validResourceSet(resourceSetUse.resourceSet))
            return {};
        const GpuGraphResourceSetNode& resourceSet = m_resourceSets[resourceSetUse.resourceSet.index];
        if(resourceSet.memberCount > remainingResourceUseCapacity - expandedResourceUseCount)
            return {};
        expandedResourceUseCount += resourceSet.memberCount;
    }

    // Task declarations previously retained a borrowed native handoff until late packet recording. Capture each
    // source when the graph accepts the declaration instead, preserving an invalid source as invalid so its
    // established record-time diagnostic remains intact for malformed legacy callers.
    GraphicsVector<CommandListResourceStateHandoff*> externalStateSnapshots(m_arena);
    externalStateSnapshots.reserve(desc.externalStateSourceCount);
    const auto destroyExternalStateSnapshots = [&]{
        for(CommandListResourceStateHandoff* const states : externalStateSnapshots)
            DestroyArenaObject(m_arena, states);
    };
    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        const CommandListResourceStateHandoff* const source = desc.externalStateSources[sourceIndex].states;
        CommandListResourceStateHandoff* const snapshot = NewArenaObject<CommandListResourceStateHandoff>(m_arena, m_arena);
        if(!snapshot){
            destroyExternalStateSnapshots();
            return {};
        }
        if(source->valid() && !snapshot->copyFrom(*source)){
            DestroyArenaObject(m_arena, snapshot);
            destroyExternalStateSnapshots();
            return {};
        }
        externalStateSnapshots.push_back(snapshot);
    }

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize)){
        destroyExternalStateSnapshots();
        return {};
    }

    GpuTaskNode task;
    task.identity = desc.identity;
    task.queue = desc.queue;
    task.scheduling = desc.scheduling;
    task.markerLabelOffset = markerLabelOffset;
    task.markerLabelSize = markerLabelSize;
    task.dependencyOffset = static_cast<u32>(m_dependencies.size());
    task.dependencyCount = static_cast<u32>(desc.dependencyCount);
    task.externalDependencyOffset = static_cast<u32>(m_externalDependencies.size());
    task.externalDependencyCount = static_cast<u32>(desc.externalDependencyCount);
    task.externalStateSourceOffset = static_cast<u32>(m_externalStateSources.size());
    task.externalStateSourceCount = static_cast<u32>(desc.externalStateSourceCount);
    task.resourceUseOffset = static_cast<u32>(m_resourceUses.size());
    task.resourceUseCount = static_cast<u32>(expandedResourceUseCount);
    task.payload = payload;
    task.recordPayload = recordPayload;
    task.acceptPayload = acceptPayload;
    task.discardPayload = discardPayload;
    task.destroyPayload = destroyPayload;

    for(usize dependencyIndex = 0u; dependencyIndex < desc.dependencyCount; ++dependencyIndex)
        m_dependencies.push_back(desc.dependencies[dependencyIndex]);
    for(usize dependencyIndex = 0u; dependencyIndex < desc.externalDependencyCount; ++dependencyIndex)
        m_externalDependencies.push_back(desc.externalDependencies[dependencyIndex]);
    m_externalStateSources.reserve(m_externalStateSources.size() + desc.externalStateSourceCount);
    m_externalStateSnapshots.reserve(m_externalStateSnapshots.size() + desc.externalStateSourceCount);
    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        const CommandListResourceStateHandoff* const snapshot = externalStateSnapshots[sourceIndex];
        m_externalStateSources.push_back(GpuTaskExternalStateSource{ .states = snapshot });
        m_externalStateSnapshots.push_back(externalStateSnapshots[sourceIndex]);
    }
    for(usize useIndex = 0u; useIndex < desc.resourceUseCount; ++useIndex)
        m_resourceUses.push_back(desc.resourceUses[useIndex]);
    for(usize resourceSetUseIndex = 0u; resourceSetUseIndex < desc.resourceSetUseCount; ++resourceSetUseIndex){
        const GpuTaskResourceSetUse& resourceSetUse = desc.resourceSetUses[resourceSetUseIndex];
        const GpuGraphResourceSetNode& resourceSet = m_resourceSets[resourceSetUse.resourceSet.index];
        for(usize memberIndex = 0u; memberIndex < resourceSet.memberCount; ++memberIndex){
            m_resourceUses.push_back(GpuTaskResourceUse{
                .resource = m_resourceSetMembers[resourceSet.memberOffset + memberIndex],
                .range = resourceSetUse.range,
                .requiredState = resourceSetUse.requiredState,
                .access = resourceSetUse.access,
                .hasIndependentStateSource = resourceSetUse.hasIndependentStateSource,
            });
        }
    }

    const u32 index = static_cast<u32>(m_tasks.size());
    m_tasks.push_back(Move(task));
    return GpuTaskId{ index, m_generation };
}

void GpuTaskGraph::discardAndDestroyUnappendedPayload(
    void* const payload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload
)noexcept{
    if(!payload)
        return;

    if(discardPayload)
        discardPayload(payload);
    if(destroyPayload)
        destroyPayload(m_arena, payload);
}

GpuGraphResourceId GpuTaskGraph::appendResource(const GpuGraphResourceDesc& desc){
    const bool hasInitialOwnerHandoff =
        desc.initialOwnerReleaseDestinationQueue.valid()
        || desc.initialOwnerCompletion.valid()
        || desc.initialOwnerStateSource != nullptr
    ;
    const bool hasMultiInitialOwnerHandoff =
        desc.initialOwnerHandoffSources != nullptr
        || desc.initialOwnerHandoffSourceCount != 0u
    ;
    const bool hasExternalFinalRelease = desc.externalFinalReleaseDestinationQueue.valid();
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.type >= GpuGraphResourceType::kCount
        || (
            desc.externalFinalState != ResourceStates::Unknown
            && desc.type != GpuGraphResourceType::Texture
            && desc.type != GpuGraphResourceType::Buffer
            && desc.type != GpuGraphResourceType::AccelStruct
        )
        || (
            hasExternalFinalRelease
            && (
                desc.externalFinalState == ResourceStates::Unknown
                || (
                    desc.type != GpuGraphResourceType::Texture
                    && desc.type != GpuGraphResourceType::Buffer
                    && desc.type != GpuGraphResourceType::AccelStruct
                )
            )
        )
        || (
            hasMultiInitialOwnerHandoff
            && (
                desc.type != GpuGraphResourceType::Texture
                || !desc.initialOwnerHandoffSources
                || desc.initialOwnerHandoffSourceCount == 0u
                || hasInitialOwnerHandoff
                || desc.initialOwnerQueue.valid()
                || desc.initialState == ResourceStates::Unknown
                || desc.queueSharing != ResourceQueueSharing::Exclusive
                || desc.initialOwnerHandoffSourceCount
                    > static_cast<usize>(Limit<u32>::s_Max) - m_initialOwnerHandoffSources.size()
            )
        )
        || (
            desc.initialOwnerQueue.valid()
            && desc.type != GpuGraphResourceType::Texture
            && desc.type != GpuGraphResourceType::Buffer
            && desc.type != GpuGraphResourceType::AccelStruct
        )
        || (
            hasInitialOwnerHandoff
            && (
                !desc.initialOwnerQueue.valid()
                || !desc.initialOwnerReleaseDestinationQueue.valid()
                || desc.initialOwnerReleaseDestinationQueue == desc.initialOwnerQueue
                || !desc.initialOwnerCompletion.valid()
                || !validExternalCompletion(desc.initialOwnerCompletion)
                || !desc.initialOwnerStateSource
                || desc.initialState == ResourceStates::Unknown
            )
        )
        || m_resources.size() >= Limit<u32>::s_Max
    )
        return {};

    if(hasMultiInitialOwnerHandoff){
        for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
            const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
            if(
                !__hidden_gpu_task_graph::ValidTextureRange(source.range.textureSubresources)
                || !source.sourceQueue.valid()
                || !source.destinationQueue.valid()
                || !source.completion.valid()
                || !validExternalCompletion(source.completion)
                || !source.minimumCompletionToken.valid()
                || !source.minimumCompletionToken.matchesPhysicalQueue(
                    source.sourceQueue.index,
                    source.sourceQueue.deviceGeneration
                )
                || !source.stateSource
            )
                return {};
            for(usize previousSourceIndex = 0u; previousSourceIndex < sourceIndex; ++previousSourceIndex){
                if(__hidden_gpu_task_graph::TextureRangesOverlap(
                    source.range.textureSubresources,
                    desc.initialOwnerHandoffSources[previousSourceIndex].range.textureSubresources
                ))
                    return {};
            }
        }
    }

    // Initial-owner imports previously borrowed this producer snapshot until late recording. Freeze it while the
    // resource is declared instead, so the declaration owns the exact state metadata. Preserve an invalid snapshot
    // as invalid so the established record-time diagnostic remains intact for malformed legacy callers.
    CommandListResourceStateHandoff* initialOwnerStateSnapshot = nullptr;
    if(desc.initialOwnerStateSource){
        initialOwnerStateSnapshot = NewArenaObject<CommandListResourceStateHandoff>(m_arena, m_arena);
        if(!initialOwnerStateSnapshot)
            return {};
        if(
            desc.initialOwnerStateSource->valid()
            && !initialOwnerStateSnapshot->copyFrom(*desc.initialOwnerStateSource)
        ){
            DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
            return {};
        }
    }

    const usize initialOwnerHandoffSourceOffset = m_initialOwnerHandoffSources.size();
    const auto discardInitialOwnerHandoffSources = [&]{
        while(m_initialOwnerHandoffSources.size() > initialOwnerHandoffSourceOffset){
            GpuTaskGraphInitialOwnerHandoffSourceView& source = m_initialOwnerHandoffSources.back();
            if(source.stateSource)
                DestroyArenaObject(m_arena, const_cast<CommandListResourceStateHandoff*>(source.stateSource));
            m_initialOwnerHandoffSources.pop_back();
        }
    };
    if(hasMultiInitialOwnerHandoff){
        m_initialOwnerHandoffSources.reserve(
            m_initialOwnerHandoffSources.size() + desc.initialOwnerHandoffSourceCount
        );
        for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
            const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
            CommandListResourceStateHandoff* const stateSnapshot =
                NewArenaObject<CommandListResourceStateHandoff>(m_arena, m_arena)
            ;
            if(!stateSnapshot){
                discardInitialOwnerHandoffSources();
                if(initialOwnerStateSnapshot)
                    DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
                return {};
            }
            if(source.stateSource->valid() && !stateSnapshot->copyFrom(*source.stateSource)){
                DestroyArenaObject(m_arena, stateSnapshot);
                discardInitialOwnerHandoffSources();
                if(initialOwnerStateSnapshot)
                    DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
                return {};
            }
            m_initialOwnerHandoffSources.push_back(GpuTaskGraphInitialOwnerHandoffSourceView{
                .range = source.range,
                .sourceQueue = source.sourceQueue,
                .destinationQueue = source.destinationQueue,
                .completion = source.completion,
                .minimumCompletionToken = source.minimumCompletionToken,
                .stateSource = stateSnapshot,
            });
        }
    }

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize)){
        discardInitialOwnerHandoffSources();
        if(initialOwnerStateSnapshot)
            DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
        return {};
    }

    GpuGraphResourceNode resource;
    resource.identity = desc.identity;
    resource.type = desc.type;
    resource.initialState = desc.initialState;
    resource.externalFinalState = desc.externalFinalState;
    resource.externalFinalReleaseDestinationQueue = desc.externalFinalReleaseDestinationQueue;
    resource.initialOwnerQueue = desc.initialOwnerQueue;
    resource.initialOwnerReleaseDestinationQueue = desc.initialOwnerReleaseDestinationQueue;
    resource.initialOwnerCompletion = desc.initialOwnerCompletion;
    resource.initialOwnerStateSource = initialOwnerStateSnapshot;
    resource.initialOwnerStateSourceIdentity = desc.initialOwnerStateSource;
    resource.initialOwnerHandoffSourceOffset = static_cast<u32>(initialOwnerHandoffSourceOffset);
    resource.initialOwnerHandoffSourceCount = static_cast<u32>(desc.initialOwnerHandoffSourceCount);
    resource.queueSharing = desc.queueSharing;
    resource.markerLabelOffset = markerLabelOffset;
    resource.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_resources.size());
    m_resources.push_back(Move(resource));
    return GpuGraphResourceId{ index, m_generation };
}

GpuGraphResourceSetId GpuTaskGraph::appendResourceSet(const GpuGraphResourceSetDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || (desc.memberCount != 0u && !desc.members)
        || desc.memberCount > static_cast<usize>(Limit<u32>::s_Max) - m_resourceSetMembers.size()
        || m_resourceSets.size() >= Limit<u32>::s_Max
    )
        return {};

    for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex){
        const GpuGraphResourceId member = desc.members[memberIndex];
        if(!validResource(member))
            return {};
        for(usize previousMemberIndex = 0u; previousMemberIndex < memberIndex; ++previousMemberIndex){
            if(desc.members[previousMemberIndex] == member)
                return {};
        }
    }

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuGraphResourceSetNode resourceSet;
    resourceSet.identity = desc.identity;
    resourceSet.markerLabelOffset = markerLabelOffset;
    resourceSet.markerLabelSize = markerLabelSize;
    resourceSet.memberOffset = static_cast<u32>(m_resourceSetMembers.size());
    resourceSet.memberCount = static_cast<u32>(desc.memberCount);
    for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex)
        m_resourceSetMembers.push_back(desc.members[memberIndex]);

    const u32 index = static_cast<u32>(m_resourceSets.size());
    m_resourceSets.push_back(Move(resourceSet));
    return GpuGraphResourceSetId{ index, m_generation };
}

GpuGraphPipelineId GpuTaskGraph::appendPipeline(const GpuGraphPipelineDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.type >= GpuGraphPipelineType::kCount
        || m_pipelines.size() >= Limit<u32>::s_Max
    )
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuGraphPipelineNode pipeline;
    pipeline.identity = desc.identity;
    pipeline.type = desc.type;
    pipeline.markerLabelOffset = markerLabelOffset;
    pipeline.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_pipelines.size());
    m_pipelines.push_back(Move(pipeline));
    return GpuGraphPipelineId{ index, m_generation };
}

GpuExternalCompletionId GpuTaskGraph::appendExternalCompletion(const GpuExternalCompletionDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || m_externalCompletions.size() >= Limit<u32>::s_Max)
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuExternalCompletionNode completion;
    completion.identity = desc.identity;
    completion.markerLabelOffset = markerLabelOffset;
    completion.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_externalCompletions.size());
    m_externalCompletions.push_back(Move(completion));
    return GpuExternalCompletionId{ index, m_generation };
}

const GpuTaskGraph::GpuUploadBlobNode* GpuTaskGraph::findUploadBlob(
    const GpuUploadBlobId& blob
)const noexcept{
    if(!validUploadBlob(blob))
        return nullptr;
    return &m_uploadBlobs[blob.index];
}

bool GpuTaskGraph::appendMarkerLabel(const AStringView text, u32& outOffset, u32& outSize){
    if(
        text.empty()
        || !text.data()
        || text.size() > Limit<u32>::s_Max
        || text.size() > Limit<u32>::s_Max - m_markerText.size()
    )
        return false;

    outOffset = static_cast<u32>(m_markerText.size());
    outSize = static_cast<u32>(text.size());
    const usize nextSize = m_markerText.size() + text.size();
    m_markerText.resize(nextSize);
    NWB_MEMCPY(m_markerText.data() + outOffset, outSize, text.data(), text.size());
    return true;
}

AStringView GpuTaskGraph::markerLabel(const u32 offset, const u32 size)const{
    NWB_ASSERT(offset <= m_markerText.size());
    NWB_ASSERT(size <= m_markerText.size() - offset);
    return AStringView(reinterpret_cast<const char*>(m_markerText.data() + offset), size);
}

void GpuTaskGraph::destroyTaskPayloads()noexcept{
    for(GpuTaskNode& task : m_tasks){
        if(task.lifecycleState == GpuTaskLifecycleState::Declared){
            task.lifecycleState = GpuTaskLifecycleState::Discarded;
            if(task.payload && task.discardPayload)
                task.discardPayload(task.payload);
        }
        if(task.payload && task.destroyPayload)
            task.destroyPayload(m_arena, task.payload);
        task.payload = nullptr;
        task.destroyPayload = nullptr;
    }
}

void GpuTaskGraph::destroyTaskStateSnapshots()noexcept{
    for(CommandListResourceStateHandoff* const states : m_externalStateSnapshots)
        DestroyArenaObject(m_arena, states);
    m_externalStateSnapshots.clear();
}

void GpuTaskGraph::destroyResourceStateSnapshots()noexcept{
    for(GpuGraphResourceNode& resource : m_resources){
        if(resource.initialOwnerStateSource)
            DestroyArenaObject(m_arena, resource.initialOwnerStateSource);
        resource.initialOwnerStateSource = nullptr;
        resource.initialOwnerStateSourceIdentity = nullptr;
    }
    for(GpuTaskGraphInitialOwnerHandoffSourceView& source : m_initialOwnerHandoffSources){
        if(source.stateSource)
            DestroyArenaObject(m_arena, const_cast<CommandListResourceStateHandoff*>(source.stateSource));
        source.stateSource = nullptr;
    }
    m_initialOwnerHandoffSources.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

