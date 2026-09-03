// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiled_graph.h"
#include "task_graph_builtin_internal.h"
#include "texture_clear_value.h"

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>
#include <core/graphics/vulkan/texture_clear_contract.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_builtin_clears{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        commandList.endRenderPass();
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
        if(commandList.commandRecordingFailed())
            return false;

        commandList.endRenderPass();
        if(commandList.commandRecordingFailed())
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
        if(commandList.commandRecordingFailed())
            return false;

        commandList.endRenderPass();
        if(commandList.commandRecordingFailed())
            return false;
        commandList.clearTextureRectUInt(
            payload.destination.get(),
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        &DestroyPayload<ClearTask::Payload>,
        sizeof(ClearTask::Payload)
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
        || !GpuTaskGraphBuiltinDetail::CopyOrClearTextureDestinationCanMaterializeRetainedState(
            destinationResource.texture->getCreationDescription(),
            destinationResource.initialState,
            destinationResource.externalFinalState
        )
    )
        return {};
    GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::Enum valueKind;
    GraphicsBackend::VulkanTextureDetail::TextureClearContract clearContract;
    if(
        !GpuTaskGraphClearDetail::TryMapTextureClearValueKind(clearDesc.valueType, valueKind)
        || !GraphicsBackend::VulkanTextureDetail::ResolveTextureClearContract(
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
    if(clearContract.queueRequirement == GraphicsBackend::VulkanTextureDetail::TextureClearQueueRequirement::Graphics)
        resolvedDesc.queue.requiredCapabilities |= GpuQueueCapability::Graphics;
    else if(
        clearContract.queueRequirement
            == GraphicsBackend::VulkanTextureDetail::TextureClearQueueRequirement::ComputeOrGraphics
        && (
            static_cast<u8>(resolvedDesc.queue.requiredCapabilities)
            & static_cast<u8>(GpuQueueCapability::Graphics)
        ) == 0u
    )
        resolvedDesc.queue.requiredCapabilities |= GpuQueueCapability::Compute;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ClearTask>,
        &AcceptPayload<ClearTask>,
        &DiscardPayload<ClearTask>,
        &DestroyPayload<ClearTask::Payload>,
        sizeof(ClearTask::Payload)
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
    GraphicsBackend::VulkanTextureDetail::TextureClearContract clearContract;
    if(!GraphicsBackend::VulkanTextureDetail::ResolveTextureClearContract(
        destinationResource.texture->getCreationDescription(),
        clearDesc.subresources,
        GraphicsBackend::VulkanTextureDetail::TextureClearValueKind::UInt,
        false,
        false,
        clearContract
    ))
        return {};
    const TextureSubresourceSet resolvedSubresources = clearContract.subresources;

    using ClearTask = __hidden_gpu_task_graph_builtin_clears::ClearTextureRectUIntTask;
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
    const Box clearBox(clearDesc.rect, 0, Limit<i32>::s_Max);
    const GraphicsBackend::VulkanTextureDetail::TextureClearQueueRequirement::Enum queueRequirement =
        GraphicsBackend::VulkanTextureDetail::TextureClearBoxQueueRequirement(
            destinationResource.texture->getCreationDescription(),
            resolvedSubresources,
            clearBox
        )
    ;
    if(
        queueRequirement == GraphicsBackend::VulkanTextureDetail::TextureClearQueueRequirement::ComputeOrGraphics
        && (
            static_cast<u8>(resolvedDesc.queue.requiredCapabilities)
            & static_cast<u8>(GpuQueueCapability::Graphics)
        ) == 0u
    )
        resolvedDesc.queue.requiredCapabilities |= GpuQueueCapability::Compute;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ClearTask>,
        &AcceptPayload<ClearTask>,
        &DiscardPayload<ClearTask>,
        &DestroyPayload<ClearTask::Payload>,
        sizeof(ClearTask::Payload)
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<ClearTask>,
            &DestroyPayload<ClearTask::Payload>
        );
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

