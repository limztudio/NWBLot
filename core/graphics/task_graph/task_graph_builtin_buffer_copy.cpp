// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiled_graph.h"

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_builtin_buffer_copy{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
            commandList.endRenderPass();
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    using CopyTask = __hidden_gpu_task_graph_builtin_buffer_copy::CopyBufferTask;
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
            && __hidden_gpu_task_graph_builtin_buffer_copy::BuiltInTaskCanMaterializeRetainedState(
                sourceResource.buffer->getDescription(),
                sourceResource.initialState,
                sourceResource.externalFinalState
            )
            && __hidden_gpu_task_graph_builtin_buffer_copy::BuiltInTaskCanMaterializeRetainedState(
                destinationResource.buffer->getDescription(),
                destinationResource.initialState,
                destinationResource.externalFinalState
            )
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
        &DestroyPayload<CopyTask::Payload>,
        sizeof(CopyTask::Payload)
    );
    if(!task.valid())
        discardAndDestroyUnappendedPayload(
            payload,
            &DiscardPayload<CopyTask>,
            &DestroyPayload<CopyTask::Payload>
        );
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

