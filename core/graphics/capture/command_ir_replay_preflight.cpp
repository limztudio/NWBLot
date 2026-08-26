// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/task_graph/compiled_graph.h>
#include <core/graphics/task_graph/task_graph.h>
#include <core/graphics/vulkan/texture_clear_contract.h>
#include <core/graphics/vulkan/texture_copy_contract.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_replay_preflight{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] static bool TaskDeclaresTextureSlice(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const ResourceStates::Mask state,
    const GpuTaskResourceAccess::Enum access,
    const TextureDesc& description,
    const TextureSlice& resolvedSlice
)noexcept{
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = task.resourceUses[useIndex];
        if(
            use.resource == resource
            && use.requiredState == state
            && use.access == access
            && TextureSliceIsDeclared(use.range.textureSubresources, description, resolvedSlice)
        )
            return true;
    }
    return false;
}

[[nodiscard]] static bool TryMapTextureClearValueKind(
    const GpuClearTextureTaskValueType::Enum valueType,
    GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::Enum& outValueKind
)noexcept{
    outValueKind = GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::Float;
    switch(valueType){
    case GpuClearTextureTaskValueType::Float:
        return true;
    case GpuClearTextureTaskValueType::UInt:
        outValueKind = GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::UInt;
        return true;
    case GpuClearTextureTaskValueType::Int:
        outValueKind = GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::Int;
        return true;
    case GpuClearTextureTaskValueType::DepthStencil:
        outValueKind = GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::DepthStencil;
        return true;
    default:
        return false;
    }
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
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
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

    if(
        !FindTaskResourceUse(
            task,
            record.source,
            ResourceStates::CopySource,
            GpuTaskResourceAccess::Read
        )
        || !FindTaskResourceUse(
            task,
            record.destination,
            ResourceStates::CopyDest,
            GpuTaskResourceAccess::Write
        )
    )
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const TextureDesc& sourceDescription = source->getCreationDescription();
    const TextureDesc& destinationDescription = destination->getCreationDescription();
    GraphicsBackend::VulkanTextureDetail::TextureCopyContract contract;
    if(!GraphicsBackend::VulkanTextureDetail::ResolveTextureCopyContract(
        sourceDescription,
        record.sourceSlice,
        destinationDescription,
        record.destinationSlice,
        contract
    ))
        return GpuCommandIrReplayError::InvalidTextureCopy;
    if(
        !TaskDeclaresTextureSlice(
            task,
            record.source,
            ResourceStates::CopySource,
            GpuTaskResourceAccess::Read,
            sourceDescription,
            contract.sourceSlice
        )
        || !TaskDeclaresTextureSlice(
            task,
            record.destination,
            ResourceStates::CopyDest,
            GpuTaskResourceAccess::Write,
            destinationDescription,
            contract.destinationSlice
        )
    )
        return GpuCommandIrReplayError::InvalidTextureCopy;

    const u8 taskCapabilities = static_cast<u8>(task.queue.requiredCapabilities);
    const u8 physicalQueueCapabilities = static_cast<u8>(queue.capabilities);
    const bool taskAndQueueShareComputeOrGraphics = (
        taskCapabilities
        & physicalQueueCapabilities
        & static_cast<u8>(GpuQueueCapability::Compute | GpuQueueCapability::Graphics)
    ) != 0u;
    const bool taskHasGraphics = (taskCapabilities & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
    const bool queueHasGraphics = (
        physicalQueueCapabilities
        & static_cast<u8>(GpuQueueCapability::Graphics)
    ) != 0u;
    if(
        (
            contract.queueRequirement
            == GraphicsBackend::VulkanTextureDetail::TextureCopyQueueRequirement::ComputeOrGraphics
            && !taskAndQueueShareComputeOrGraphics
        )
        || (
            contract.queueRequirement
            == GraphicsBackend::VulkanTextureDetail::TextureCopyQueueRequirement::Graphics
            && (!taskHasGraphics || !queueHasGraphics)
        )
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
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
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

    const TextureDesc& description = destination->getCreationDescription();
    GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::Enum valueKind;
    GraphicsBackend::VulkanTextureDetail::TextureClearContract clearContract;
    if(
        !TextureSubresourcesAreCanonical(description, record.destinationSubresources)
        || !TextureSubresourcesContain(
            destinationUse->range.textureSubresources,
            description,
            record.destinationSubresources
        )
        || !TryMapTextureClearValueKind(record.clearTextureValueType, valueKind)
        || !GraphicsBackend::VulkanTextureDetail::ResolveTextureClearContract(
            description,
            record.destinationSubresources,
            valueKind,
            record.clearDepth,
            record.clearStencil,
            clearContract
        )
        || clearContract.subresources != record.destinationSubresources
        || !GraphicsBackend::VulkanTextureDetail::TextureClearQueueRequirementSatisfied(
            clearContract.queueRequirement,
            task.queue.requiredCapabilities,
            queue.capabilities
        )
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureRectUIntClearRecord(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
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

    const TextureDesc& description = destination->getCreationDescription();
    GraphicsBackend::VulkanTextureDetail::TextureClearContract clearContract;
    const Box clearBox(record.clearRect, 0, Limit<i32>::s_Max);
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
        || !GraphicsBackend::VulkanTextureDetail::ResolveTextureClearContract(
            description,
            record.destinationSubresources,
            GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::UInt,
            false,
            false,
            clearContract
        )
        || clearContract.subresources != record.destinationSubresources
        || !GraphicsBackend::VulkanTextureDetail::TextureClearQueueRequirementSatisfied(
            GraphicsBackend::VulkanTextureDetail::TextureClearBoxQueueRequirement(
                description,
                clearContract.subresources,
                clearBox
            ),
            task.queue.requiredCapabilities,
            queue.capabilities
        )
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraph& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        return ValidateBufferCopyRecord(record, graph, task);
    case GpuCommandIrOpcode::CopyTexture:
        return ValidateTextureCopyRecord(record, graph, task, queue);
    case GpuCommandIrOpcode::ClearBuffer:
        return ValidateBufferClearRecord(record, graph, task);
    case GpuCommandIrOpcode::ClearTexture:
        return ValidateTextureClearRecord(record, graph, task, queue);
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        return ValidateTextureRectUIntClearRecord(record, graph, task, queue);
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayResult PreflightGpuCommandIrPacket(
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet
)noexcept{
    const GpuCommandIrStreamValidationResult streamValidation = ValidateGpuCommandIrStream(bytes);
    if(!streamValidation.valid())
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::InvalidStream,
            streamValidation,
            streamValidation.recordIndex
        );
    if(!compiledGraph.validFor(graph))
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::InvalidCompiledGraph,
            streamValidation
        );
    if(!graph.validForDeviceGeneration(compiledGraph.deviceGeneration()))
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::InvalidCompiledGraph,
            streamValidation
        );
    if(!compiledGraph.validPacket(packet))
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::InvalidPacket,
            streamValidation
        );

    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packetPlan.queue);
    if(!queue || queue->queueClass >= CommandQueue::kCount)
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::PacketQueueUnavailable,
            streamValidation
        );
    if(!__hidden_gpu_command_ir_replay_preflight::QueueHasTransferCapability(*queue))
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::MissingTransferCapability,
            streamValidation
        );

    GpuCommandIrStreamReader reader(bytes);
    if(reader.validation().failed())
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::InvalidStream,
            reader.validation(),
            reader.validation().recordIndex
        );
    if(reader.recordCount() != 0u && reader.graphGeneration() != graph.generation()){
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
            GpuCommandIrReplayError::GraphGenerationMismatch,
            streamValidation
        );
    }
    if(reader.recordCount() != 0u && reader.planGeneration() != compiledGraph.planGeneration()){
        return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
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
            return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(
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

        const GpuCommandIrReplayError::Enum contextError = __hidden_gpu_command_ir_replay_preflight::ValidateRecordContext(
            record,
            graph,
            compiledGraph,
            packet,
            *queue,
            previousTaskOrder,
            hasPreviousTask
        );
        if(contextError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(contextError, streamValidation, recordIndex);

        const GpuTaskGraphTaskView task = graph.taskAt(record.task.index);
        const GpuCommandIrReplayError::Enum operationError = __hidden_gpu_command_ir_replay_preflight::ValidateOperation(
            record,
            graph,
            task,
            *queue
        );
        if(operationError != GpuCommandIrReplayError::None)
            return __hidden_gpu_command_ir_replay_preflight::ReplayFailure(operationError, streamValidation, recordIndex);

        ++recordIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

