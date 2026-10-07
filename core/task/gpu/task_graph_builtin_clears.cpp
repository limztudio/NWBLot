// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiled_graph.h"
#include "task_graph_builtin_internal.h"
#include "texture_clear_value.h"

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/command.h>
#include <core/graphics/backend_selection/texture_clear_contract.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_builtin_clears{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuTaskCommandRequirements TextureClearCommandRequirements(
    const GraphicsBackend::TextureClearQueueRequirement::Enum requirement)noexcept{
    GpuTaskCommandRequirements commands;
    switch(requirement){
    case GraphicsBackend::TextureClearQueueRequirement::Transfer:
        commands.requiredCapabilities = GpuQueueCapability::Transfer;
        break;
    case GraphicsBackend::TextureClearQueueRequirement::Graphics:
        commands.requiredCapabilities = GpuQueueCapability::Graphics;
        break;
    case GraphicsBackend::TextureClearQueueRequirement::ComputeOrGraphics:
        commands.alternativeCapabilities = GpuQueueCapability::Compute | GpuQueueCapability::Graphics;
        break;
    default:
        NWB_ASSERT(false);
        break;
    }
    return commands;
}

[[nodiscard]] static bool IncludeHookCommandRequirements(
    GpuTaskCommandRequirements& commands,
    const GpuTaskCommandRequirements& hookCommands)noexcept{
    // A hook declares one alternative clause; the additional clause belongs to the composed built-in contract.
    if(hookCommands.additionalAlternativeCapabilities != GpuQueueCapability::None)
        return false;
    commands.requiredCapabilities |= hookCommands.requiredCapabilities;
    commands.requiresPrimaryGraphicsQueue = commands.requiresPrimaryGraphicsQueue || hookCommands.requiresPrimaryGraphicsQueue;
    commands.externalQueue = hookCommands.externalQueue;
    if(commands.alternativeCapabilities == GpuQueueCapability::None)
        commands.alternativeCapabilities = hookCommands.alternativeCapabilities;
    else if(hookCommands.alternativeCapabilities != commands.alternativeCapabilities)
        commands.additionalAlternativeCapabilities = hookCommands.alternativeCapabilities;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClearBufferPayload{
    GpuGraphResourceId destinationResource;
    BufferHandle destination;
    u32 clearValue = 0u;
    QueueSubmissionToken* acceptedToken = nullptr;
};

struct ClearBufferTask : public GpuTaskGraphBuiltinDetail::SingletonTokenTaskBase<ClearBufferPayload>{
    [[nodiscard]] static bool Record(
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
        commandList.endRenderPass();
        commandList.clearBufferUInt(*payload.destination, payload.clearValue);
        return true;
    }
};

struct ClearTextureTask{
    struct Payload{
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        GpuClearTextureTaskDesc clearDesc;
    };

    [[nodiscard]] static bool Record(
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
        if(commandList.commandRecordingFailed())
            return false;

        commandList.endRenderPass();
        if(commandList.commandRecordingFailed())
            return false;
        bool clearRecorded = false;
        switch(payload.clearDesc.valueType){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(
                *payload.destination,
                payload.clearDesc.subresources,
                payload.clearDesc.floatValue
            );
            clearRecorded = true;
            break;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(
                *payload.destination,
                payload.clearDesc.subresources,
                payload.clearDesc.uintValue
            );
            clearRecorded = true;
            break;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(
                *payload.destination,
                payload.clearDesc.subresources,
                payload.clearDesc.intValue
            );
            clearRecorded = true;
            break;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(
                *payload.destination,
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
        if(commandList.commandRecordingFailed())
            return false;
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

    static void Accepted(Payload& payload, const QueueSubmissionToken& token)noexcept{
        GpuTaskGraphBuiltinDetail::PublishAcceptedToken(payload.clearDesc.acceptedToken, token);
    }

    static void Discarded(Payload& payload){
        GpuTaskGraphBuiltinDetail::ClearAcceptedToken(payload.clearDesc.acceptedToken);
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

    [[nodiscard]] static bool Record(
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
        if(commandList.commandRecordingFailed())
            return false;

        commandList.endRenderPass();
        if(commandList.commandRecordingFailed())
            return false;
        commandList.clearTextureRectUInt(
            *payload.destination,
            payload.clearDesc.subresources,
            payload.clearDesc.rect,
            payload.clearDesc.uintValue
        );
        if(commandList.commandRecordingFailed())
            return false;
        return !payload.clearDesc.recordHooks.afterClear
            || payload.clearDesc.recordHooks.afterClear(
                payload.clearDesc.recordHooks.context,
                commandList,
                context
            )
        ;
    }

    static void Accepted(Payload& payload, const QueueSubmissionToken& token)noexcept{
        GpuTaskGraphBuiltinDetail::PublishAcceptedToken(payload.clearDesc.acceptedToken, token);
    }

    static void Discarded(Payload& payload){
        GpuTaskGraphBuiltinDetail::ClearAcceptedToken(payload.clearDesc.acceptedToken);
        if(payload.clearDesc.recordHooks.discarded)
            payload.clearDesc.recordHooks.discarded(payload.clearDesc.recordHooks.context);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc){
    if(clearDesc.acceptedToken)
        *clearDesc.acceptedToken = {};

    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !GpuTaskGraphBuiltinDetail::BuiltinDeclarationHasNoCallerResourceUses(desc)
        || !validResource(clearDesc.destination)
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Buffer
        || !destinationResource.buffer
        || destinationResource.buffer->getCreationDescription().byteSize == 0u
        || (destinationResource.buffer->getCreationDescription().byteSize & (sizeof(u32) - 1u)) != 0u
        || !GpuTaskGraphBuiltinDetail::BuiltInTaskCanMaterializeRetainedState(
            destinationResource.buffer->getCreationDescription(),
            destinationResource.initialState,
            destinationResource.externalFinalState
        )
    )
        return {};

    using ClearTask = __hidden_gpu_task_graph_builtin_clears::ClearBufferTask;
    ClearTask::Payload* const payloadObject = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payloadObject)
        return {};
    ProvisionalPayloadOwner<ClearTask::Payload> payload(m_arena, payloadObject);
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
    return appendBuiltinTaskWithinMutation<ClearTask>(resolvedDesc, payload, mutation);
}

GpuTaskId GpuTaskGraph::addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc){
    if(clearDesc.acceptedToken)
        *clearDesc.acceptedToken = {};

    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !GpuTaskGraphBuiltinDetail::BuiltinDeclarationHasNoCallerResourceUses(desc)
        || !validResource(clearDesc.destination)
        || clearDesc.valueType >= GpuClearTextureTaskValueType::kCount
        || (
            clearDesc.valueType == GpuClearTextureTaskValueType::DepthStencil
            && !clearDesc.clearDepth
            && !clearDesc.clearStencil
        )
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Texture
        || !destinationResource.texture
        || !GpuTaskGraphBuiltinDetail::CopyOrClearTextureDestinationCanMaterializeRetainedState(
            destinationResource.texture->getCreationDescription(),
            destinationResource.initialState,
            destinationResource.externalFinalState
        )
    )
        return {};
    GraphicsBackend::TextureClearValueKind::Enum valueKind;
    GraphicsBackend::TextureClearContract clearContract;
    if(
        !GpuTaskGraphClearDetail::TryMapTextureClearValueKind(clearDesc.valueType, valueKind)
        || !GraphicsBackend::ResolveTextureClearContract(
            destinationResource.texture->getCreationDescription(),
            clearDesc.subresources,
            valueKind,
            clearDesc.clearDepth,
            clearDesc.clearStencil,
            clearContract
        )
    )
        return {};
    const TextureSubresourceSet resolvedSubresources = clearContract.subresources;

    using ClearTask = __hidden_gpu_task_graph_builtin_clears::ClearTextureTask;
    ClearTask::Payload* const payloadObject = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payloadObject)
        return {};
    ProvisionalPayloadOwner<ClearTask::Payload> payload(m_arena, payloadObject);
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
    GpuTaskCommandRequirements commands = __hidden_gpu_task_graph_builtin_clears::TextureClearCommandRequirements(
        clearContract.queueRequirement
    );
    if(!__hidden_gpu_task_graph_builtin_clears::IncludeHookCommandRequirements(
        commands,
        clearDesc.recordHooks.commands
    ))
        return {};
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    return appendBuiltinTaskWithinMutation<ClearTask>(resolvedDesc, payload, mutation, commands);
}

GpuTaskId GpuTaskGraph::addClearTextureRectUIntTask(
    const GpuTaskDesc& desc,
    const GpuClearTextureRectUIntTaskDesc& clearDesc
){
    if(clearDesc.acceptedToken)
        *clearDesc.acceptedToken = {};

    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !GpuTaskGraphBuiltinDetail::BuiltinDeclarationHasNoCallerResourceUses(desc)
        || !validResource(clearDesc.destination)
        || clearDesc.rect.maxX <= clearDesc.rect.minX
        || clearDesc.rect.maxY <= clearDesc.rect.minY
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Texture
        || !destinationResource.texture
        // Bounded multisample clears require an active attachment. Graph recording ends rendering before this
        // primitive and has no framebuffer lowering, so every multisample rectangle remains unsupported.
        || destinationResource.texture->getCreationDescription().sampleCount != 1u
        || !GpuTaskGraphBuiltinDetail::BuiltInTaskCanMaterializeRetainedState(
            destinationResource.texture->getCreationDescription(),
            destinationResource.initialState,
            destinationResource.externalFinalState
        )
    )
        return {};
    GraphicsBackend::TextureClearContract clearContract;
    if(!GraphicsBackend::ResolveTextureClearContract(
        destinationResource.texture->getCreationDescription(),
        clearDesc.subresources,
        GraphicsBackend::TextureClearValueKind::UInt,
        false,
        false,
        clearContract
    ))
        return {};
    const TextureSubresourceSet resolvedSubresources = clearContract.subresources;

    using ClearTask = __hidden_gpu_task_graph_builtin_clears::ClearTextureRectUIntTask;
    ClearTask::Payload* const payloadObject = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payloadObject)
        return {};
    ProvisionalPayloadOwner<ClearTask::Payload> payload(m_arena, payloadObject);
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
    const Box clearBox(clearDesc.rect, 0, Limit<i32>::s_Max);
    const GraphicsBackend::TextureClearQueueRequirement::Enum queueRequirement =
        GraphicsBackend::TextureClearBoxQueueRequirement(
            destinationResource.texture->getCreationDescription(),
            resolvedSubresources,
            clearBox
        )
    ;
    GpuTaskCommandRequirements commands = __hidden_gpu_task_graph_builtin_clears::TextureClearCommandRequirements(
        queueRequirement
    );
    if(!__hidden_gpu_task_graph_builtin_clears::IncludeHookCommandRequirements(
        commands,
        clearDesc.recordHooks.commands
    ))
        return {};
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    return appendBuiltinTaskWithinMutation<ClearTask>(resolvedDesc, payload, mutation, commands);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

