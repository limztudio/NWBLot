// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/task_graph/compiled_graph.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateBuiltinRecord(const GpuCommandIrBuiltinTaskRecord& record)noexcept{
    if(
        record.opcode >= GpuCommandIrOpcode::kCount
        || !record.task.valid()
        || !record.packet.valid()
        || !record.queue.valid()
        || !record.destination.valid()
        || record.destination.generation != record.task.generation
    )
        return false;

    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        return record.source.valid()
            && record.source.generation == record.task.generation
            && record.dataSizeBytes != 0u
        ;
    case GpuCommandIrOpcode::CopyTexture:
        return record.source.valid() && record.source.generation == record.task.generation;
    case GpuCommandIrOpcode::ClearBuffer:
        return true;
    case GpuCommandIrOpcode::ClearTexture:
        return record.clearTextureValueType < GpuClearTextureTaskValueType::kCount
            && record.destinationSubresources.numMipLevels != 0u
            && record.destinationSubresources.numArraySlices != 0u
            && (
                record.clearTextureValueType != GpuClearTextureTaskValueType::DepthStencil
                || record.clearDepth
                || record.clearStencil
            )
        ;
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        return record.destinationSubresources.numMipLevels != 0u
            && record.destinationSubresources.numArraySlices != 0u
            && record.clearRect.maxX > record.clearRect.minX
            && record.clearRect.maxY > record.clearRect.minY
        ;
    default:
        return false;
    }
}

template<typename RecordT>
[[nodiscard]] static bool AppendEncodedRecord(GraphicsBytes& outBytes, const RecordT& record){
    static_assert(IsStandardLayout_V<RecordT>, "Command IR records must be standard layout");
    static_assert(IsTriviallyCopyable_V<RecordT>, "Command IR records must be trivially copyable");
    static_assert(sizeof(RecordT) <= Limit<u16>::s_Max, "Command IR record exceeds its u16 byte-size field");

    if(
        outBytes.size() < sizeof(GpuCommandIrStreamHeader)
        || !BinaryDetail::CanAppendBytes(outBytes, sizeof(RecordT))
        || outBytes.size() - sizeof(GpuCommandIrStreamHeader) > Limit<u64>::s_Max - sizeof(RecordT)
    )
        return false;

    BinaryDetail::ReserveAppendBytesIfSupported(outBytes, sizeof(RecordT));
    AppendPOD(outBytes, record);
    return true;
}

[[nodiscard]] static GpuCommandIrRecordContext EncodeContext(const GpuCommandIrBuiltinTaskRecord& record)noexcept{
    return GpuCommandIrRecordContext{
        .taskIndex = record.task.index,
        .packetIndex = record.packet.index,
        .queueIndex = record.queue.index,
        .queueDeviceGeneration = record.queue.deviceGeneration,
    };
}

[[nodiscard]] static GpuCommandIrTextureSlice EncodeTextureSlice(const TextureSlice& slice)noexcept{
    return GpuCommandIrTextureSlice{
        .x = slice.x,
        .y = slice.y,
        .z = slice.z,
        .width = slice.width,
        .height = slice.height,
        .depth = slice.depth,
        .mipLevel = slice.mipLevel,
        .arraySlice = slice.arraySlice,
    };
}

[[nodiscard]] static GpuCommandIrTextureSubresourceSet EncodeSubresources(const TextureSubresourceSet& subresources)noexcept{
    return GpuCommandIrTextureSubresourceSet{
        .baseMipLevel = subresources.baseMipLevel,
        .numMipLevels = subresources.numMipLevels,
        .baseArraySlice = subresources.baseArraySlice,
        .numArraySlices = subresources.numArraySlices,
    };
}

[[nodiscard]] static GpuCommandIrRect EncodeRect(const Rect& rect)noexcept{
    return GpuCommandIrRect{
        .minX = rect.minX,
        .maxX = rect.maxX,
        .minY = rect.minY,
        .maxY = rect.maxY,
    };
}

[[nodiscard]] static GpuCommandIrFloatColor EncodeColor(const Color& color)noexcept{
    return GpuCommandIrFloatColor{
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
}

[[nodiscard]] static GpuCommandIrUIntColor EncodeColor(const UIntColor& color)noexcept{
    return GpuCommandIrUIntColor{
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
}

[[nodiscard]] static GpuCommandIrIntColor EncodeColor(const IntColor& color)noexcept{
    return GpuCommandIrIntColor{
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
}

template<typename RecordT>
static void InitializeRecord(RecordT& record, const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    static_assert(IsTriviallyCopyable_V<RecordT>, "Command IR records must be trivially copyable");
    NWB_MEMSET(&record, 0, sizeof(record));
    record.header.opcode = opcode;
    record.header.byteSize = static_cast<u16>(sizeof(record));
}

[[nodiscard]] static bool DecodeContext(
    const GpuCommandIrRecordContext& context,
    const u64 graphGeneration,
    const u64 planGeneration,
    GpuCommandIrBuiltinTaskRecord& outRecord
)noexcept{
    outRecord.task = GpuTaskId{ context.taskIndex, graphGeneration };
    outRecord.packet = GpuSubmissionPacketId{ context.packetIndex, planGeneration };
    outRecord.queue = GpuPhysicalQueueId{ context.queueIndex, context.queueDeviceGeneration };
    return outRecord.task.valid() && outRecord.packet.valid() && outRecord.queue.valid();
}

[[nodiscard]] static bool DecodeResource(
    const u32 resourceIndex,
    const u64 graphGeneration,
    GpuGraphResourceId& outResource
)noexcept{
    outResource = GpuGraphResourceId{ resourceIndex, graphGeneration };
    return outResource.valid();
}

[[nodiscard]] static TextureSlice DecodeTextureSlice(const GpuCommandIrTextureSlice& slice)noexcept{
    return TextureSlice{
        .x = slice.x,
        .y = slice.y,
        .z = slice.z,
        .width = slice.width,
        .height = slice.height,
        .depth = slice.depth,
        .mipLevel = slice.mipLevel,
        .arraySlice = slice.arraySlice,
    };
}

[[nodiscard]] static TextureSubresourceSet DecodeSubresources(
    const GpuCommandIrTextureSubresourceSet& subresources
)noexcept{
    return TextureSubresourceSet(
        subresources.baseMipLevel,
        subresources.numMipLevels,
        subresources.baseArraySlice,
        subresources.numArraySlices
    );
}

[[nodiscard]] static Rect DecodeRect(const GpuCommandIrRect& rect)noexcept{
    return Rect(rect.minX, rect.maxX, rect.minY, rect.maxY);
}

[[nodiscard]] static Color DecodeColor(const GpuCommandIrFloatColor& color)noexcept{
    return Color(color.r, color.g, color.b, color.a);
}

[[nodiscard]] static UIntColor DecodeColor(const GpuCommandIrUIntColor& color)noexcept{
    return UIntColor(color.r, color.g, color.b, color.a);
}

[[nodiscard]] static IntColor DecodeColor(const GpuCommandIrIntColor& color)noexcept{
    return IntColor(color.r, color.g, color.b, color.a);
}

[[nodiscard]] static bool ValidateClearTextureRecord(
    const GpuCommandIrClearTextureRecord& record
)noexcept{
    constexpr u8 allowedFlags = static_cast<u8>(
        GpuCommandIrClearTextureFlag::ClearDepth | GpuCommandIrClearTextureFlag::ClearStencil
    );
    const u8 clearFlags = static_cast<u8>(record.clearFlags);
    if(
        record.destinationSubresources.numMipLevels == 0u
        || record.destinationSubresources.numArraySlices == 0u
        || record.clearTextureValueType >= static_cast<u8>(GpuClearTextureTaskValueType::kCount)
        || (clearFlags & ~allowedFlags) != 0u
        || record.reserved != 0u
    )
        return false;

    return record.clearTextureValueType == GpuClearTextureTaskValueType::DepthStencil
        ? clearFlags != GpuCommandIrClearTextureFlag::None
        : clearFlags == GpuCommandIrClearTextureFlag::None
    ;
}

[[nodiscard]] static bool ValidateClearTextureRectUIntRecord(
    const GpuCommandIrClearTextureRectUIntRecord& record
)noexcept{
    return record.destinationSubresources.numMipLevels != 0u
        && record.destinationSubresources.numArraySlices != 0u
        && record.clearRect.maxX > record.clearRect.minX
        && record.clearRect.maxY > record.clearRect.minY
    ;
}

[[nodiscard]] static GpuCommandIrReplayResult ReplayFailure(
    const GpuCommandIrReplayError::Enum error,
    const GpuCommandIrStreamValidationResult& streamValidation = {},
    const u64 recordIndex = Limit<u64>::s_Max
)noexcept{
    return GpuCommandIrReplayResult{
        .error = error,
        .streamValidation = streamValidation,
        .recordIndex = recordIndex,
    };
}

[[nodiscard]] static bool QueueHasTransferCapability(const GpuPhysicalQueueInfo& queue)noexcept{
    return (static_cast<u8>(queue.capabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u;
}

[[nodiscard]] static const GpuTaskResourceUse* FindTaskResourceUse(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const ResourceStates::Mask state,
    const GpuTaskResourceAccess::Enum access
)noexcept{
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = task.resourceUses[useIndex];
        if(use.resource == resource && use.requiredState == state && use.access == access)
            return &use;
    }
    return nullptr;
}

[[nodiscard]] static bool BufferRangeContains(
    const BufferDesc& description,
    const BufferRange& declaration,
    const u64 offsetBytes,
    const u64 sizeBytes
)noexcept{
    if(
        !GraphicsBackend::VulkanDetail::IsBufferRangeInBounds(description, offsetBytes, sizeBytes)
        || declaration.byteOffset > description.byteSize
    )
        return false;

    const u64 declaredSize = declaration.byteSize == BufferRange::AllBytes
        ? description.byteSize - declaration.byteOffset
        : declaration.byteSize
    ;
    if(declaredSize > description.byteSize - declaration.byteOffset || offsetBytes < declaration.byteOffset)
        return false;
    return sizeBytes <= declaredSize - (offsetBytes - declaration.byteOffset);
}

[[nodiscard]] static bool TextureSubresourcesAreCanonical(
    const TextureDesc& description,
    const TextureSubresourceSet& subresources
)noexcept{
    if(
        subresources.numMipLevels == 0u
        || subresources.numArraySlices == 0u
        || subresources.baseMipLevel >= description.mipLevels
    )
        return false;
    return subresources == subresources.resolve(description, TextureSubresourceMipResolve::Range);
}

[[nodiscard]] static bool TextureSubresourcesContain(
    const TextureSubresourceSet& declaration,
    const TextureDesc& description,
    const TextureSubresourceSet& requested
)noexcept{
    if(!TextureSubresourcesAreCanonical(description, requested))
        return false;

    const TextureSubresourceSet resolvedDeclaration = declaration.resolve(
        description,
        TextureSubresourceMipResolve::Range
    );
    if(
        resolvedDeclaration.numMipLevels == 0u
        || resolvedDeclaration.numArraySlices == 0u
        || requested.baseMipLevel < resolvedDeclaration.baseMipLevel
        || requested.baseMipLevel - resolvedDeclaration.baseMipLevel >= resolvedDeclaration.numMipLevels
        || requested.numMipLevels > resolvedDeclaration.numMipLevels
            - (requested.baseMipLevel - resolvedDeclaration.baseMipLevel)
        || requested.baseArraySlice < resolvedDeclaration.baseArraySlice
        || requested.baseArraySlice - resolvedDeclaration.baseArraySlice >= resolvedDeclaration.numArraySlices
        || requested.numArraySlices > resolvedDeclaration.numArraySlices
            - (requested.baseArraySlice - resolvedDeclaration.baseArraySlice)
    )
        return false;
    return true;
}

[[nodiscard]] static bool TextureSliceIsDeclared(
    const TextureSubresourceSet& declaration,
    const TextureDesc& description,
    const TextureSlice& resolvedSlice
)noexcept{
    const TextureSubresourceSet requested(
        resolvedSlice.mipLevel,
        1u,
        resolvedSlice.arraySlice,
        1u
    );
    return TextureSubresourcesContain(declaration, description, requested);
}

[[nodiscard]] static bool ClearTextureValueMatchesFormat(
    const TextureDesc& description,
    const GpuCommandIrBuiltinTaskRecord& record
)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(description.format);
    const bool depthStencilFormat = formatInfo.hasDepth || formatInfo.hasStencil;
    switch(record.clearTextureValueType){
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
            && (record.clearDepth || record.clearStencil)
            && (!record.clearDepth || formatInfo.hasDepth)
            && (!record.clearStencil || formatInfo.hasStencil)
        ;
    default:
        return false;
    }
}

[[nodiscard]] static bool ClearTextureRectUIntValueMatchesFormat(const TextureDesc& description)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(description.format);
    return !formatInfo.hasDepth && !formatInfo.hasStencil
        && formatInfo.kind == FormatKind::Integer
        && !formatInfo.isSigned
    ;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateRecordContext(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueInfo& queue,
    u32& inOutPreviousTaskOrder,
    bool& inOutHasPreviousTask
)noexcept{
    if(record.packet != packet)
        return GpuCommandIrReplayError::RecordPacketMismatch;
    if(record.queue != queue.id)
        return GpuCommandIrReplayError::RecordQueueMismatch;
    if(!graph.validTask(record.task))
        return GpuCommandIrReplayError::InvalidTask;

    const GpuCompiledTask* const compiledTask = compiledGraph.findTask(record.task);
    if(
        !compiledTask
        || compiledTask->packet != packet
        || compiledTask->queue != queue.id
        || compiledGraph.packetForTask(record.task) != packet
    )
        return GpuCommandIrReplayError::CompiledTaskMismatch;

    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const packetTasks = compiledGraph.packetTasks(packet);
    if(!packetTasks || packetPlan.taskCount == 0u)
        return GpuCommandIrReplayError::CompiledTaskMismatch;

    u32 taskOrder = Limit<u32>::s_Max;
    for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(packetTasks[taskIndex] == record.task){
            taskOrder = taskIndex;
            break;
        }
    }
    if(taskOrder == Limit<u32>::s_Max)
        return GpuCommandIrReplayError::CompiledTaskMismatch;
    if(inOutHasPreviousTask && taskOrder < inOutPreviousTaskOrder)
        return GpuCommandIrReplayError::TaskOrderMismatch;

    inOutPreviousTaskOrder = taskOrder;
    inOutHasPreviousTask = true;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferCopyRecord(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    if(
        !graph.validResource(record.source)
        || !graph.validResource(record.destination)
        || record.source == record.destination
    )
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView sourceView = graph.resourceAt(record.source.index);
    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(sourceView.type != GpuGraphResourceType::Buffer || destinationView.type != GpuGraphResourceType::Buffer)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Buffer* const source = graph.bufferForResource(record.source);
    Buffer* const destination = graph.bufferForResource(record.destination);
    if(!source || !destination)
        return GpuCommandIrReplayError::MissingBackendResource;

    const GpuTaskResourceUse* const sourceUse = FindTaskResourceUse(
        task,
        record.source,
        ResourceStates::CopySource,
        GpuTaskResourceAccess::Read
    );
    const GpuTaskResourceUse* const destinationUse = FindTaskResourceUse(
        task,
        record.destination,
        ResourceStates::CopyDest,
        GpuTaskResourceAccess::Write
    );
    if(!sourceUse || !destinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const BufferDesc& sourceDescription = source->getDescription();
    const BufferDesc& destinationDescription = destination->getDescription();
    if(
        record.dataSizeBytes == 0u
        || !BufferRangeContains(sourceDescription, sourceUse->range.bufferRange, record.sourceOffsetBytes, record.dataSizeBytes)
        || !BufferRangeContains(
            destinationDescription,
            destinationUse->range.bufferRange,
            record.destinationOffsetBytes,
            record.dataSizeBytes
        )
        || (
            source == destination
            && GraphicsBackend::VulkanDetail::BufferRangesOverlap(
                record.sourceOffsetBytes,
                record.dataSizeBytes,
                record.destinationOffsetBytes,
                record.dataSizeBytes
            )
        )
    )
        return GpuCommandIrReplayError::InvalidBufferCopy;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureCopyRecord(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    if(
        !graph.validResource(record.source)
        || !graph.validResource(record.destination)
        || record.source == record.destination
    )
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView sourceView = graph.resourceAt(record.source.index);
    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(sourceView.type != GpuGraphResourceType::Texture || destinationView.type != GpuGraphResourceType::Texture)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Texture* const source = graph.textureForResource(record.source);
    Texture* const destination = graph.textureForResource(record.destination);
    if(!source || !destination)
        return GpuCommandIrReplayError::MissingBackendResource;
    if(source == destination)
        return GpuCommandIrReplayError::InvalidTextureCopy;

    const GpuTaskResourceUse* const sourceUse = FindTaskResourceUse(
        task,
        record.source,
        ResourceStates::CopySource,
        GpuTaskResourceAccess::Read
    );
    const GpuTaskResourceUse* const destinationUse = FindTaskResourceUse(
        task,
        record.destination,
        ResourceStates::CopyDest,
        GpuTaskResourceAccess::Write
    );
    if(!sourceUse || !destinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const TextureDesc& sourceDescription = source->getDescription();
    const TextureDesc& destinationDescription = destination->getDescription();
    GraphicsBackend::VulkanDetail::TextureFormatBlockLayout sourceLayout;
    GraphicsBackend::VulkanDetail::TextureFormatBlockLayout destinationLayout;
    TextureSlice resolvedSource;
    TextureSlice resolvedDestination;
    if(
        sourceDescription.format != destinationDescription.format
        || sourceDescription.sampleCount != destinationDescription.sampleCount
        || !GraphicsBackend::VulkanDetail::GetTextureFormatBlockLayout(GetFormatInfo(sourceDescription.format), sourceLayout)
        || !GraphicsBackend::VulkanDetail::GetTextureFormatBlockLayout(GetFormatInfo(destinationDescription.format), destinationLayout)
        || !GraphicsBackend::VulkanDetail::IsTextureSliceInBounds(
            sourceDescription,
            record.sourceSlice,
            sourceLayout,
            &resolvedSource
        )
        || !GraphicsBackend::VulkanDetail::IsTextureSliceInBounds(
            destinationDescription,
            record.destinationSlice,
            destinationLayout,
            &resolvedDestination
        )
        || resolvedSource.width != resolvedDestination.width
        || resolvedSource.height != resolvedDestination.height
        || resolvedSource.depth != resolvedDestination.depth
        || !TextureSliceIsDeclared(sourceUse->range.textureSubresources, sourceDescription, resolvedSource)
        || !TextureSliceIsDeclared(destinationUse->range.textureSubresources, destinationDescription, resolvedDestination)
    )
        return GpuCommandIrReplayError::InvalidTextureCopy;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferClearRecord(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    if(!graph.validResource(record.destination))
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(destinationView.type != GpuGraphResourceType::Buffer)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Buffer* const destination = graph.bufferForResource(record.destination);
    if(!destination)
        return GpuCommandIrReplayError::MissingBackendResource;

    const GpuTaskResourceUse* const destinationUse = FindTaskResourceUse(
        task,
        record.destination,
        ResourceStates::CopyDest,
        GpuTaskResourceAccess::Write
    );
    if(!destinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const BufferDesc& description = destination->getDescription();
    if(
        description.byteSize == 0u
        || (description.byteSize & (sizeof(u32) - 1u)) != 0u
        || !destinationUse->range.bufferRange.isEntireBuffer(description)
    )
        return GpuCommandIrReplayError::InvalidBufferClear;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureClearRecord(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    if(!graph.validResource(record.destination))
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(destinationView.type != GpuGraphResourceType::Texture)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Texture* const destination = graph.textureForResource(record.destination);
    if(!destination)
        return GpuCommandIrReplayError::MissingBackendResource;

    const GpuTaskResourceUse* const destinationUse = FindTaskResourceUse(
        task,
        record.destination,
        ResourceStates::CopyDest,
        GpuTaskResourceAccess::Write
    );
    if(!destinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const TextureDesc& description = destination->getDescription();
    if(
        description.sampleCount != 1u
        || !TextureSubresourcesAreCanonical(description, record.destinationSubresources)
        || !TextureSubresourcesContain(
            destinationUse->range.textureSubresources,
            description,
            record.destinationSubresources
        )
        || !ClearTextureValueMatchesFormat(description, record)
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureRectUIntClearRecord(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    if(!graph.validResource(record.destination))
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(destinationView.type != GpuGraphResourceType::Texture)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Texture* const destination = graph.textureForResource(record.destination);
    if(!destination)
        return GpuCommandIrReplayError::MissingBackendResource;

    const GpuTaskResourceUse* const destinationUse = FindTaskResourceUse(
        task,
        record.destination,
        ResourceStates::CopyDest,
        GpuTaskResourceAccess::Write
    );
    if(!destinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const TextureDesc& description = destination->getDescription();
    if(
        record.clearRect.maxX <= record.clearRect.minX
        || record.clearRect.maxY <= record.clearRect.minY
        || description.sampleCount != 1u
        || !TextureSubresourcesAreCanonical(description, record.destinationSubresources)
        || !TextureSubresourcesContain(
            destinationUse->range.textureSubresources,
            description,
            record.destinationSubresources
        )
        || !ClearTextureRectUIntValueMatchesFormat(description)
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        return ValidateBufferCopyRecord(record, graph, task);
    case GpuCommandIrOpcode::CopyTexture:
        return ValidateTextureCopyRecord(record, graph, task);
    case GpuCommandIrOpcode::ClearBuffer:
        return ValidateBufferClearRecord(record, graph, task);
    case GpuCommandIrOpcode::ClearTexture:
        return ValidateTextureClearRecord(record, graph, task);
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        return ValidateTextureRectUIntClearRecord(record, graph, task);
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}

static void LowerOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    CommandList& commandList
)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        commandList.copyBuffer(
            graph.bufferForResource(record.destination),
            record.destinationOffsetBytes,
            graph.bufferForResource(record.source),
            record.sourceOffsetBytes,
            record.dataSizeBytes
        );
        return;
    case GpuCommandIrOpcode::CopyTexture:
        commandList.copyTexture(
            graph.textureForResource(record.destination),
            record.destinationSlice,
            graph.textureForResource(record.source),
            record.sourceSlice
        );
        return;
    case GpuCommandIrOpcode::ClearBuffer:
        commandList.clearBufferUInt(graph.bufferForResource(record.destination), record.uintClearValue.r);
        return;
    case GpuCommandIrOpcode::ClearTexture:
        switch(record.clearTextureValueType){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(
                graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.floatClearValue
            );
            return;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(
                graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.uintClearValue
            );
            return;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(
                graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.intClearValue
            );
            return;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(
                graph.textureForResource(record.destination),
                record.destinationSubresources,
                record.clearDepth,
                record.depthClearValue,
                record.clearStencil,
                record.stencilClearValue
            );
            return;
        default:
            NWB_ASSERT_MSG(false, NWB_TEXT("Validated command IR clear type lost its lowerer"));
            return;
        }
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        commandList.clearTextureRectUInt(
            graph.textureForResource(record.destination),
            record.destinationSubresources,
            record.clearRect,
            record.uintClearValue
        );
        return;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Validated command IR opcode lost its lowerer"));
        return;
    }
}

// Direct Vulkan lowering is intentionally a prototype with a single opcode. Scan the complete selected packet
// after graph-aware preflight and before issuing any native command so an unsupported later record cannot leave an
// earlier copy in the command buffer.
[[nodiscard]] static GpuCommandIrReplayResult ValidateDirectVulkanOpcodeSupport(
    const BinaryByteView bytes,
    const GpuSubmissionPacketId packet,
    const GpuCommandIrStreamValidationResult& expectedStreamValidation
)noexcept{
    GpuCommandIrStreamReader reader(bytes);
    if(reader.validation().failed()){
        return ReplayFailure(
            GpuCommandIrReplayError::StreamChangedDuringReplay,
            reader.validation(),
            reader.validation().recordIndex
        );
    }

    u64 recordIndex = 0u;
    GpuCommandIrBuiltinTaskRecord record;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End){
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        }
        if(status == GpuCommandIrStreamReadStatus::Error){
            return ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        if(record.packet == packet && record.opcode != GpuCommandIrOpcode::CopyBuffer){
            return ReplayFailure(
                GpuCommandIrReplayError::UnsupportedDirectVulkanOpcode,
                expectedStreamValidation,
                recordIndex
            );
        }
        ++recordIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrStreamReader::GpuCommandIrStreamReader(const BinaryByteView bytes)noexcept
    : m_bytes(bytes)
{
    if(m_bytes.size() != 0u && !m_bytes.data()){
        fail(GpuCommandIrStreamValidationError::NullData, 0u, Limit<u64>::s_Max);
        return;
    }
    if(m_bytes.size() < sizeof(GpuCommandIrStreamHeaderPrefix)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }

    usize cursor = 0u;
    GpuCommandIrStreamHeaderPrefix prefix;
    if(!ReadPOD(m_bytes, cursor, prefix)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }
    if(prefix.magic != s_GpuCommandIrStreamMagic){
        fail(GpuCommandIrStreamValidationError::InvalidMagic, 0u, Limit<u64>::s_Max);
        return;
    }
    if(
        prefix.version < s_GpuCommandIrStreamFirstSupportedVersion
        || prefix.version > s_GpuCommandIrStreamVersion
    ){
        fail(GpuCommandIrStreamValidationError::UnsupportedVersion, 0u, Limit<u64>::s_Max);
        return;
    }
    if(prefix.reserved != 0u){
        fail(GpuCommandIrStreamValidationError::InvalidHeaderReserved, 0u, Limit<u64>::s_Max);
        return;
    }
    if(m_bytes.size() < sizeof(GpuCommandIrStreamHeader)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }

    cursor = 0u;
    GpuCommandIrStreamHeader header;
    if(!ReadPOD(m_bytes, cursor, header)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.payloadBytes > static_cast<u64>(Limit<usize>::s_Max)){
        fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
        return;
    }

    const usize payloadBytes = static_cast<usize>(header.payloadBytes);
    if(payloadBytes != m_bytes.size() - sizeof(GpuCommandIrStreamHeader)){
        fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
        return;
    }
    if(
        (header.recordCount == 0u && header.graphGeneration != 0u)
        || (header.recordCount != 0u && header.graphGeneration == 0u)
    ){
        fail(GpuCommandIrStreamValidationError::InvalidGraphGeneration, 0u, Limit<u64>::s_Max);
        return;
    }
    if(
        (header.recordCount == 0u && header.planGeneration != 0u)
        || (header.recordCount != 0u && header.planGeneration == 0u)
    ){
        fail(GpuCommandIrStreamValidationError::InvalidPlanGeneration, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.recordCount > header.payloadBytes / sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::InvalidRecordCount, 0u, Limit<u64>::s_Max);
        return;
    }

    m_cursor = cursor;
    m_payloadEnd = m_bytes.size();
    m_streamVersion = header.version;
    m_graphGeneration = header.graphGeneration;
    m_planGeneration = header.planGeneration;
    m_recordCount = header.recordCount;
}

GpuCommandIrStreamReadStatus::Enum GpuCommandIrStreamReader::next(
    GpuCommandIrBuiltinTaskRecord& outRecord
)noexcept{
    if(m_validation.failed())
        return GpuCommandIrStreamReadStatus::Error;

    if(m_nextRecordIndex == m_recordCount){
        if(m_cursor != m_payloadEnd){
            fail(
                GpuCommandIrStreamValidationError::TrailingPayload,
                m_cursor,
                m_nextRecordIndex
            );
            return GpuCommandIrStreamReadStatus::Error;
        }
        m_validation.byteOffset = m_payloadEnd;
        m_validation.recordIndex = m_nextRecordIndex;
        m_validation.complete = true;
        return GpuCommandIrStreamReadStatus::End;
    }

    const usize recordOffset = m_cursor;
    if(m_payloadEnd - recordOffset < sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    usize recordCursor = recordOffset;
    GpuCommandIrHeader header;
    if(!ReadPOD(m_bytes, recordCursor, header)){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(header.byteSize < sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    usize expectedByteSize = 0u;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:
        expectedByteSize = sizeof(GpuCommandIrCopyBufferRecord);
        break;
    case GpuCommandIrWireOpcode::CopyTexture:
        expectedByteSize = sizeof(GpuCommandIrCopyTextureRecord);
        break;
    case GpuCommandIrWireOpcode::ClearBuffer:
        expectedByteSize = sizeof(GpuCommandIrClearBufferRecord);
        break;
    case GpuCommandIrWireOpcode::ClearTexture:
        expectedByteSize = sizeof(GpuCommandIrClearTextureRecord);
        break;
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:
        if(m_streamVersion < 2u){
            fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        expectedByteSize = sizeof(GpuCommandIrClearTextureRectUIntRecord);
        break;
    default:
        fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(header.byteSize != expectedByteSize){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(expectedByteSize > m_payloadEnd - recordOffset){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    GpuCommandIrBuiltinTaskRecord decoded;
    bool decodedRecord = false;
    using namespace __hidden_gpu_command_ir;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        GpuCommandIrCopyBufferRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::CopyBuffer;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.sourceResourceIndex, m_graphGeneration, decoded.source)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && record.dataSizeBytes != 0u
        ;
        decoded.sourceOffsetBytes = record.sourceOffsetBytes;
        decoded.destinationOffsetBytes = record.destinationOffsetBytes;
        decoded.dataSizeBytes = record.dataSizeBytes;
        break;
    }
    case GpuCommandIrWireOpcode::CopyTexture:{
        GpuCommandIrCopyTextureRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::CopyTexture;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.sourceResourceIndex, m_graphGeneration, decoded.source)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
        ;
        decoded.sourceSlice = DecodeTextureSlice(record.sourceSlice);
        decoded.destinationSlice = DecodeTextureSlice(record.destinationSlice);
        break;
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        GpuCommandIrClearBufferRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::ClearBuffer;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
        ;
        decoded.uintClearValue = UIntColor(record.clearValue);
        break;
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        GpuCommandIrClearTextureRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::ClearTexture;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && ValidateClearTextureRecord(record)
        ;
        decoded.destinationSubresources = DecodeSubresources(record.destinationSubresources);
        decoded.clearTextureValueType = static_cast<GpuClearTextureTaskValueType::Enum>(
            record.clearTextureValueType
        );
        decoded.floatClearValue = DecodeColor(record.floatClearValue);
        decoded.uintClearValue = DecodeColor(record.uintClearValue);
        decoded.intClearValue = DecodeColor(record.intClearValue);
        decoded.depthClearValue = record.depthClearValue;
        decoded.stencilClearValue = record.stencilClearValue;
        decoded.clearDepth = (static_cast<u8>(record.clearFlags) & GpuCommandIrClearTextureFlag::ClearDepth) != 0u;
        decoded.clearStencil = (static_cast<u8>(record.clearFlags) & GpuCommandIrClearTextureFlag::ClearStencil) != 0u;
        break;
    }
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:{
        GpuCommandIrClearTextureRectUIntRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::ClearTextureRectUInt;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && ValidateClearTextureRectUIntRecord(record)
        ;
        decoded.destinationSubresources = DecodeSubresources(record.destinationSubresources);
        decoded.clearRect = DecodeRect(record.clearRect);
        decoded.uintClearValue = DecodeColor(record.uintClearValue);
        break;
    }
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Known command IR opcode lost its decoder"));
        fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    if(!decodedRecord || !ValidateBuiltinRecord(decoded)){
        fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    m_cursor = recordCursor;
    ++m_nextRecordIndex;
    outRecord = decoded;
    return GpuCommandIrStreamReadStatus::Record;
}

void GpuCommandIrStreamReader::fail(
    const GpuCommandIrStreamValidationError::Enum error,
    const usize byteOffset,
    const u64 recordIndex
)noexcept{
    if(m_validation.failed())
        return;

    m_validation.error = error;
    m_validation.byteOffset = byteOffset;
    m_validation.recordIndex = recordIndex;
    m_validation.complete = true;
}

GpuCommandIrStreamValidationResult ValidateGpuCommandIrStream(const BinaryByteView bytes)noexcept{
    GpuCommandIrStreamReader reader(bytes);
    GpuCommandIrBuiltinTaskRecord record;
    for(;;){
        switch(reader.next(record)){
        case GpuCommandIrStreamReadStatus::Record:
            break;
        case GpuCommandIrStreamReadStatus::End:
        case GpuCommandIrStreamReadStatus::Error:
            return reader.validation();
        default:
            NWB_ASSERT_MSG(false, NWB_TEXT("Unknown command IR reader status"));
            return reader.validation();
        }
    }
}


GpuCommandIrReplayResult PreflightGpuCommandIrPacket(
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet
)noexcept{
    const GpuCommandIrStreamValidationResult streamValidation = ValidateGpuCommandIrStream(bytes);
    if(!streamValidation.valid())
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::InvalidStream,
            streamValidation,
            streamValidation.recordIndex
        );
    if(!compiledGraph.validFor(graph))
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::InvalidCompiledGraph,
            streamValidation
        );
    if(!graph.validForDeviceGeneration(compiledGraph.deviceGeneration()))
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::InvalidCompiledGraph,
            streamValidation
        );
    if(!compiledGraph.validPacket(packet))
        return __hidden_gpu_command_ir::ReplayFailure(GpuCommandIrReplayError::InvalidPacket, streamValidation);

    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetPlan.queue);
    if(!queue || queue->queueClass >= CommandQueue::kCount)
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::PacketQueueUnavailable,
            streamValidation
        );
    if(!__hidden_gpu_command_ir::QueueHasTransferCapability(*queue))
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::MissingTransferCapability,
            streamValidation
        );

    GpuCommandIrStreamReader reader(bytes);
    if(reader.validation().failed())
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::InvalidStream,
            reader.validation(),
            reader.validation().recordIndex
        );
    if(reader.recordCount() != 0u && reader.graphGeneration() != graph.generation()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::GraphGenerationMismatch,
            streamValidation
        );
    }
    if(reader.recordCount() != 0u && reader.planGeneration() != compiledGraph.planGeneration()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::PlanGenerationMismatch,
            streamValidation
        );
    }

    u32 previousTaskOrder = 0u;
    bool hasPreviousTask = false;
    u64 recordIndex = 0u;
    GpuCommandIrBuiltinTaskRecord record;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End)
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        if(status == GpuCommandIrStreamReadStatus::Error){
            return __hidden_gpu_command_ir::ReplayFailure(
                GpuCommandIrReplayError::InvalidStream,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        // Captures normally span the packet range passed to GpuNativePacketRecorder. The requested packet is a
        // self-contained lowering scope, so semantic validation deliberately filters other packet bodies after the
        // stream's complete syntax has already been checked above.
        if(record.packet != packet){
            ++recordIndex;
            continue;
        }

        const GpuCommandIrReplayError::Enum contextError = __hidden_gpu_command_ir::ValidateRecordContext(
            record,
            graph,
            compiledGraph,
            packet,
            *queue,
            previousTaskOrder,
            hasPreviousTask
        );
        if(contextError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir::ReplayFailure(contextError, streamValidation, recordIndex);

        const GpuTaskGraphTaskView task = graph.taskAt(record.task.index);
        const GpuCommandIrReplayError::Enum operationError = __hidden_gpu_command_ir::ValidateOperation(
            record,
            graph,
            task
        );
        if(operationError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir::ReplayFailure(operationError, streamValidation, recordIndex);

        ++recordIndex;
    }
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacket(
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList
)noexcept{
    GpuCommandIrReplayResult result = PreflightGpuCommandIrPacket(bytes, graph, compiledGraph, packet);
    if(!result.valid())
        return result;

    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetPlan.queue);
    if(!queue){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::PacketQueueUnavailable,
            result.streamValidation
        );
    }
    if(!commandList.isRecording()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListNotRecording,
            result.streamValidation
        );
    }
    if(commandList.isRenderPassActive()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListRenderPassActive,
            result.streamValidation
        );
    }
    const CommandListParameters& commandListDescription = commandList.getDescription();
    if(commandListDescription.physicalQueue.deviceGeneration != compiledGraph.deviceGeneration()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }
    if(commandListDescription.queueType != queue->queueClass){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }

    // Preflight above establishes all graph/resource legality before any void Core::CommandList operation can
    // mutate its state tracker. The byte view and graph are caller-stable for this tooling call, so a second walk
    // can lower without allocating a duplicate command list or per-command object graph.
    GpuCommandIrStreamReader reader(bytes);
    GpuCommandIrBuiltinTaskRecord record;
    u64 recordIndex = 0u;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End)
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        if(status == GpuCommandIrStreamReadStatus::Error){
            return __hidden_gpu_command_ir::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        if(record.packet != packet){
            ++recordIndex;
            continue;
        }

        // The graph and bytes are immutable for the call. These resolution checks defend the release build if a
        // caller violates that contract before replay begins; no revalidation is possible after a native command
        // has been emitted, so concurrent mutation remains unsupported by design.
        if(!graph.validResource(record.destination) || (
            (record.opcode == GpuCommandIrOpcode::CopyBuffer || record.opcode == GpuCommandIrOpcode::CopyTexture)
            && !graph.validResource(record.source)
        )){
            return __hidden_gpu_command_ir::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                result.streamValidation,
                recordIndex
            );
        }

        __hidden_gpu_command_ir::LowerOperation(record, graph, commandList);
        ++recordIndex;
    }
}

GpuCommandIrReplayResult ReplayGpuCommandIrPacketDirectVulkan(
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    CommandList& commandList
)noexcept{
    GpuCommandIrReplayResult result = PreflightGpuCommandIrPacket(bytes, graph, compiledGraph, packet);
    if(!result.valid())
        return result;

    result = __hidden_gpu_command_ir::ValidateDirectVulkanOpcodeSupport(
        bytes,
        packet,
        result.streamValidation
    );
    if(!result.valid())
        return result;

    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetPlan.queue);
    if(!queue){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::PacketQueueUnavailable,
            result.streamValidation
        );
    }
    if(!commandList.isRecording()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListNotRecording,
            result.streamValidation
        );
    }
    if(commandList.isRenderPassActive()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListRenderPassActive,
            result.streamValidation
        );
    }
    const CommandListParameters& commandListDescription = commandList.getDescription();
    if(commandListDescription.physicalQueue.deviceGeneration != compiledGraph.deviceGeneration()){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }
    if(commandListDescription.queueType != queue->queueClass){
        return __hidden_gpu_command_ir::ReplayFailure(
            GpuCommandIrReplayError::CommandListQueueMismatch,
            result.streamValidation
        );
    }

    // The caller has already lowered the graph-owned state seed and packet barriers into commandList. This second
    // reader walk deliberately bypasses CommandList::copyBuffer, so it can measure/directly exercise only Vulkan
    // command emission without resurrecting automatic state tracking in the replay path.
    GpuCommandIrStreamReader reader(bytes);
    GpuCommandIrBuiltinTaskRecord record;
    u64 recordIndex = 0u;
    for(;;){
        const GpuCommandIrStreamReadStatus::Enum status = reader.next(record);
        if(status == GpuCommandIrStreamReadStatus::End){
            return GpuCommandIrReplayResult{
                .streamValidation = reader.validation(),
                .recordIndex = recordIndex,
            };
        }
        if(status == GpuCommandIrStreamReadStatus::Error){
            return __hidden_gpu_command_ir::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                reader.validation(),
                reader.validation().recordIndex
            );
        }

        if(record.packet != packet){
            ++recordIndex;
            continue;
        }

        // Preflight above and the direct-opcode scan establish the normal contract. Repeat only the resolution
        // checks needed to turn caller mutation into a diagnostic instead of dereferencing a stale graph ID.
        if(
            record.opcode != GpuCommandIrOpcode::CopyBuffer
            || !graph.validResource(record.source)
            || !graph.validResource(record.destination)
        ){
            return __hidden_gpu_command_ir::ReplayFailure(
                GpuCommandIrReplayError::StreamChangedDuringReplay,
                result.streamValidation,
                recordIndex
            );
        }
        if(!commandList.recordPreflightedCopyBufferDirectVulkan(
            graph.bufferForResource(record.destination),
            record.destinationOffsetBytes,
            graph.bufferForResource(record.source),
            record.sourceOffsetBytes,
            record.dataSizeBytes
        )){
            return __hidden_gpu_command_ir::ReplayFailure(
                GpuCommandIrReplayError::DirectVulkanLoweringFailed,
                result.streamValidation,
                recordIndex
            );
        }
        ++recordIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuCommandIrCapture::reset()noexcept{
    m_records.clear();
    m_commandBytes.resize(sizeof(GpuCommandIrStreamHeader));
    m_graphGeneration = 0u;
    m_planGeneration = 0u;
    writeStreamHeader();
}

const GpuCommandIrBuiltinTaskRecord* GpuCommandIrCapture::recordAt(const usize index)const noexcept{
    return index < m_records.size() ? &m_records[index] : nullptr;
}

void GpuCommandIrCapture::rollback(const usize recordCount)noexcept{
    if(recordCount > m_records.size())
        return;

    const usize byteOffset = byteOffsetAfterRecordCount(recordCount);
    if(byteOffset == Limit<usize>::s_Max){
        NWB_ASSERT_MSG(false, NWB_TEXT("Command IR capture stream lost a record boundary"));
        return;
    }

    m_records.resize(recordCount);
    m_commandBytes.resize(byteOffset);
    m_graphGeneration = m_records.empty() ? 0u : m_records[0u].task.generation;
    m_planGeneration = m_records.empty() ? 0u : m_records[0u].packet.generation;
    writeStreamHeader();
}

bool GpuCommandIrCapture::captureCopyBuffer(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId source,
    const u64 sourceOffsetBytes,
    const GpuGraphResourceId destination,
    const u64 destinationOffsetBytes,
    const u64 dataSizeBytes
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::CopyBuffer;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.source = source;
    record.destination = destination;
    record.sourceOffsetBytes = sourceOffsetBytes;
    record.destinationOffsetBytes = destinationOffsetBytes;
    record.dataSizeBytes = dataSizeBytes;
    return append(record);
}

bool GpuCommandIrCapture::captureCopyTexture(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId source,
    const TextureSlice sourceSlice,
    const GpuGraphResourceId destination,
    const TextureSlice destinationSlice
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::CopyTexture;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.source = source;
    record.destination = destination;
    record.sourceSlice = sourceSlice;
    record.destinationSlice = destinationSlice;
    return append(record);
}

bool GpuCommandIrCapture::captureClearBuffer(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId destination,
    const u32 clearValue
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::ClearBuffer;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.destination = destination;
    record.uintClearValue = UIntColor(clearValue);
    return append(record);
}

bool GpuCommandIrCapture::captureClearTexture(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId destination,
    const GpuClearTextureTaskDesc& clearDesc
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::ClearTexture;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.destination = destination;
    record.destinationSubresources = clearDesc.subresources;
    record.clearTextureValueType = clearDesc.valueType;
    record.floatClearValue = clearDesc.floatValue;
    record.uintClearValue = clearDesc.uintValue;
    record.intClearValue = clearDesc.intValue;
    record.depthClearValue = clearDesc.depthValue;
    record.stencilClearValue = clearDesc.stencilValue;
    record.clearDepth = clearDesc.clearDepth;
    record.clearStencil = clearDesc.clearStencil;
    return append(record);
}

bool GpuCommandIrCapture::captureClearTextureRectUInt(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId destination,
    const GpuClearTextureRectUIntTaskDesc& clearDesc
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::ClearTextureRectUInt;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.destination = destination;
    record.destinationSubresources = clearDesc.subresources;
    record.clearRect = clearDesc.rect;
    record.uintClearValue = clearDesc.uintValue;
    return append(record);
}

bool GpuCommandIrCapture::append(const GpuCommandIrBuiltinTaskRecord& record){
    if(!__hidden_gpu_command_ir::ValidateBuiltinRecord(record))
        return false;

    if(m_graphGeneration != 0u && m_graphGeneration != record.task.generation)
        return false;
    if(m_planGeneration != 0u && m_planGeneration != record.packet.generation)
        return false;
    const usize recordCountBefore = m_records.size();
    const usize byteCountBefore = m_commandBytes.size();
    try{
        // Keep the legacy inspection cache and the POD stream transactional as one capture record. In particular,
        // an allocator failure in either update must not strand an uncounted byte record in the stream.
        m_records.push_back(record);
        if(!appendCommandBytes(record)){
            m_records.pop_back();
            return false;
        }
    }
    catch(...){
        m_records.resize(recordCountBefore);
        m_commandBytes.resize(byteCountBefore);
        return false;
    }

    if(m_graphGeneration == 0u)
        m_graphGeneration = record.task.generation;
    if(m_planGeneration == 0u)
        m_planGeneration = record.packet.generation;
    writeStreamHeader();
    return true;
}

bool GpuCommandIrCapture::appendCommandBytes(const GpuCommandIrBuiltinTaskRecord& record){
    using namespace __hidden_gpu_command_ir;

    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:{
        GpuCommandIrCopyBufferRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::CopyBuffer);
        encoded.context = EncodeContext(record);
        encoded.sourceResourceIndex = record.source.index;
        encoded.destinationResourceIndex = record.destination.index;
        encoded.sourceOffsetBytes = record.sourceOffsetBytes;
        encoded.destinationOffsetBytes = record.destinationOffsetBytes;
        encoded.dataSizeBytes = record.dataSizeBytes;
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::CopyTexture:{
        GpuCommandIrCopyTextureRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::CopyTexture);
        encoded.context = EncodeContext(record);
        encoded.sourceResourceIndex = record.source.index;
        encoded.destinationResourceIndex = record.destination.index;
        encoded.sourceSlice = EncodeTextureSlice(record.sourceSlice);
        encoded.destinationSlice = EncodeTextureSlice(record.destinationSlice);
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::ClearBuffer:{
        GpuCommandIrClearBufferRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::ClearBuffer);
        encoded.context = EncodeContext(record);
        encoded.destinationResourceIndex = record.destination.index;
        encoded.clearValue = record.uintClearValue.r;
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::ClearTexture:{
        GpuCommandIrClearTextureRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::ClearTexture);
        encoded.context = EncodeContext(record);
        encoded.destinationResourceIndex = record.destination.index;
        encoded.destinationSubresources = EncodeSubresources(record.destinationSubresources);
        encoded.floatClearValue = EncodeColor(record.floatClearValue);
        encoded.uintClearValue = EncodeColor(record.uintClearValue);
        encoded.intClearValue = EncodeColor(record.intClearValue);
        encoded.depthClearValue = record.depthClearValue;
        encoded.stencilClearValue = record.stencilClearValue;
        encoded.clearTextureValueType = static_cast<u8>(record.clearTextureValueType);
        // Color clears retain legacy inspection fields, but native color-clear lowering ignores aspect flags. The
        // stream is canonical and only encodes depth/stencil selection for a depth/stencil clear command.
        if(record.clearTextureValueType == GpuClearTextureTaskValueType::DepthStencil){
            if(record.clearDepth)
                encoded.clearFlags = static_cast<GpuCommandIrClearTextureFlag::Mask>(
                    static_cast<u8>(encoded.clearFlags) | GpuCommandIrClearTextureFlag::ClearDepth
                );
            if(record.clearStencil)
                encoded.clearFlags = static_cast<GpuCommandIrClearTextureFlag::Mask>(
                    static_cast<u8>(encoded.clearFlags) | GpuCommandIrClearTextureFlag::ClearStencil
                );
        }
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::ClearTextureRectUInt:{
        GpuCommandIrClearTextureRectUIntRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::ClearTextureRectUInt);
        encoded.context = EncodeContext(record);
        encoded.destinationResourceIndex = record.destination.index;
        encoded.destinationSubresources = EncodeSubresources(record.destinationSubresources);
        encoded.clearRect = EncodeRect(record.clearRect);
        encoded.uintClearValue = EncodeColor(record.uintClearValue);
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    default:
        return false;
    }
}

usize GpuCommandIrCapture::byteOffsetAfterRecordCount(const usize recordCount)const noexcept{
    if(recordCount > m_records.size() || m_commandBytes.size() < sizeof(GpuCommandIrStreamHeader))
        return Limit<usize>::s_Max;

    const BinaryByteView bytes = commandBytes();
    usize cursor = sizeof(GpuCommandIrStreamHeader);
    for(usize recordIndex = 0u; recordIndex < recordCount; ++recordIndex){
        const usize recordOffset = cursor;
        GpuCommandIrHeader header;
        if(
            !ReadPOD(bytes, cursor, header)
            || header.byteSize < sizeof(header)
            || header.byteSize > bytes.size() - recordOffset
        )
            return Limit<usize>::s_Max;
        cursor = recordOffset + header.byteSize;
    }
    return cursor;
}

void GpuCommandIrCapture::writeStreamHeader()noexcept{
    NWB_ASSERT(m_commandBytes.size() >= sizeof(GpuCommandIrStreamHeader));
    GpuCommandIrStreamHeader header;
    header.graphGeneration = m_graphGeneration;
    header.planGeneration = m_planGeneration;
    header.recordCount = static_cast<u64>(m_records.size());
    header.payloadBytes = static_cast<u64>(m_commandBytes.size() - sizeof(GpuCommandIrStreamHeader));
    NWB_MEMCPY(m_commandBytes.data(), sizeof(header), &header, sizeof(header));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

