// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_setup_upload_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_setup_upload_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GraphicsInternalNames::s_SetupUploadReadinessBridgeIdentity;


[[nodiscard]] static CommandQueue::Enum ResolveAvailableSetupConsumerQueue(GraphicsBackend::Device& device)noexcept{
    if(device.getQueue(CommandQueue::Transfer))
        return CommandQueue::Transfer;
    if(device.getQueue(CommandQueue::Compute))
        return CommandQueue::Compute;
    return CommandQueue::Graphics;
}


// A returned setup resource has no external-completion object for later direct consumers.
// Record one explicit graph packet per declared consumer queue; its producer dependency lowers the exact timeline wait.
struct SetupUploadReadinessBridgeGraphTask{
    struct Payload{
        GpuPhysicalQueueId consumerQueue;
    };

    [[nodiscard]] static GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept{
        GpuTaskCommandRequirements commands;
        commands.externalQueue = payload.consumerQueue;
        return commands;
    }


    [[nodiscard]] static b8 Record([[maybe_unused]] const Payload& payload, [[maybe_unused]] CommandList& commandList, [[maybe_unused]] const GpuTaskRecordContext& context)noexcept{
        return true;
    }
};

[[nodiscard]] static Expected<GpuTaskId> DeclareSetupUploadReadinessBridgeTasks(GpuTaskGraph& graph, GraphicsBackend::Device& device, const ResourceQueueSharing::Mask queueSharing, const CommandQueue::Enum directConsumerQueue, const GpuTaskId uploadTask){
    NWB_ASSERT(uploadTask.valid());

    GpuTaskId terminalTask = uploadTask;
    constexpr Array<CommandQueue::Enum, 3> s_ConsumerQueues = {{ CommandQueue::Graphics, CommandQueue::Compute, CommandQueue::Transfer }};
    const auto appendBridge = [&graph, &device, uploadTask, &terminalTask](const CommandQueue::Enum consumerQueue){
        GpuTaskSchedulingHint scheduling = GraphicsModuleDetail::SetupUploadGraph::scheduling(0u);
        scheduling.overlapPreferred = false;
        GpuTaskDesc bridgeDesc;
        bridgeDesc.setIdentity(s_SetupUploadReadinessBridgeIdentity).setMarkerLabel("Setup Upload Readiness Bridge").setScheduling(scheduling).setDependencies(Span<const GpuTaskId>(&uploadTask, 1u));
        const auto bridgeTaskResult = graph.addTask<SetupUploadReadinessBridgeGraphTask>(bridgeDesc, SetupUploadReadinessBridgeGraphTask::Payload{ device.getPrimaryPhysicalQueue(consumerQueue) });
        if(!bridgeTaskResult)
            return false;
        const GpuTaskId bridgeTask = *bridgeTaskResult;
        terminalTask = bridgeTask;
        return true;
    };
    for(const CommandQueue::Enum consumerQueue : s_ConsumerQueues){
        if(consumerQueue == directConsumerQueue || !ResourceQueueSharing::IncludesQueueClass(queueSharing, consumerQueue) || !device.getQueue(consumerQueue))
            continue;
        if(!appendBridge(consumerQueue))
            return MakeUnexpected(Failure{});
    }
    // The returned resource is ready on its direct consumer timeline even when the scheduler offloads its producer.
    if(!device.getQueue(directConsumerQueue) || !appendBridge(directConsumerQueue))
        return MakeUnexpected(Failure{});
    return terminalTask;
}


struct SetupUploadSubmissionData{
    GraphicsBackend::Device& device;
    void* userData = nullptr;
    NotNull<GraphicsModuleDetail::GraphTaskDeclaration> declareTask;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    CommandQueue::Enum consumerQueue = CommandQueue::Graphics;
};

[[nodiscard]] static Expected<GpuTaskId> DeclareSetupUploadGraph(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *MakeNotNull(static_cast<SetupUploadSubmissionData*>(userData));
    const auto uploadTask = submissionData.declareTask.get()(submissionData.userData, graph);
    if(!uploadTask)
        return MakeUnexpected(uploadTask.error());
    return DeclareSetupUploadReadinessBridgeTasks(graph, submissionData.device, submissionData.queueSharing, submissionData.consumerQueue, *uploadTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SetupUploadSameClassRouting SetupUploadGraph::resolveSameClassRouting(GraphicsBackend::Device& device, const CommandQueue::Enum consumerQueue, const usize uploadBytes)noexcept{
    SetupUploadSameClassRouting result;
    if(uploadBytes < s_LargeMinimumBytes)
        return result;

    result.primaryQueue = device.getPrimaryPhysicalQueue(consumerQueue);
    const GpuPhysicalQueueInfo* const primaryInfo = device.getPhysicalQueueInfo(result.primaryQueue);
    if(!primaryInfo)
        return result;

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* alternateInfo = nullptr;
    const u8 requiredCapabilities = static_cast<u8>(GpuQueueCapability::Transfer);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.id == result.primaryQueue || candidate.queueClass != consumerQueue || (static_cast<u8>(candidate.capabilities) & requiredCapabilities) != requiredCapabilities || (alternateInfo && candidate.id.index >= alternateInfo->id.index))
            continue;
        alternateInfo = &candidate;
    }
    if(!alternateInfo)
        return result;

    result.enabled = true;
    result.crossesQueueFamily = alternateInfo->familyIndex != primaryInfo->familyIndex;
    return result;
}

ResourceQueueSharing::Mask SetupUploadGraph::resolveConsumerSharing(const ResourceQueueSharing::Mask requestedSharing, const CommandQueue::Enum consumerQueue, const b8 crossFamilySameClassRouting)noexcept{
    if(crossFamilySameClassRouting){
        const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive ? ResourceQueueSharing::ForQueueClass(consumerQueue) : requestedSharing;
        return static_cast<ResourceQueueSharing::Mask>(static_cast<u8>(baseSharing) | static_cast<u8>(ResourceQueueSharing::ForQueueClass(consumerQueue)));
    }

    if(consumerQueue == CommandQueue::Graphics)
        return requestedSharing;

    const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive ? ResourceQueueSharing::Graphics : requestedSharing;
    return static_cast<ResourceQueueSharing::Mask>(static_cast<u8>(baseSharing) | static_cast<u8>(ResourceQueueSharing::ForQueueClass(consumerQueue)));
}

CommandQueue::Enum SetupUploadGraph::resolveConsumerQueue(GraphicsBackend::Device& device, const CommandQueue::Enum requestedConsumerQueue, const usize uploadBytes, const b8 hasKnownFinalState)noexcept{
    switch(requestedConsumerQueue){
    case CommandQueue::kCount:
        if(uploadBytes < s_LargeMinimumBytes || !hasKnownFinalState)
            return CommandQueue::Graphics;
        return __hidden_setup_upload_graph::ResolveAvailableSetupConsumerQueue(device);
    case CommandQueue::Transfer:
        return hasKnownFinalState ? __hidden_setup_upload_graph::ResolveAvailableSetupConsumerQueue(device) : CommandQueue::Graphics;
    case CommandQueue::Compute:
        return hasKnownFinalState && device.getQueue(CommandQueue::Compute) ? CommandQueue::Compute : CommandQueue::Graphics;
    case CommandQueue::Graphics:
        return CommandQueue::Graphics;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("GraphicsRuntime: setup upload requested an invalid command queue"));
        return CommandQueue::Graphics;
    }
}

GpuTaskSchedulingHint SetupUploadGraph::scheduling(const usize byteCount, const b8 sameClassRouting, const b8 crossFamilySameClassRouting)noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = byteCount >= s_LargeMinimumBytes ? GpuTaskCostHint::Large : GpuTaskCostHint::Tiny;
    // A public setup call returns one accepted producer token, so retain a distinct explicit packet.
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowSameClassQueueRouting = sameClassRouting;
    scheduling.preferNonPrimarySameClassQueue = sameClassRouting;
    scheduling.allowCrossFamilySameClassQueueRouting = crossFamilySameClassRouting;
    return scheduling;
}

ResourceStates::Mask SetupUploadGraph::finalState(const ResourceStates::Mask declaredInitialState)noexcept{
    // Uploads with an Unknown descriptor state publish the concrete CopyDest post-write state.
    return declaredInitialState == ResourceStates::Unknown ? ResourceStates::CopyDest : declaredInitialState;
}


b8 SetupUploadGraph::submit(
    const GraphicsRuntime& graphics,
    const ResourceQueueSharing::Mask queueSharing,
    const CommandQueue::Enum consumerQueue,
    const NotNull<GraphTaskDeclaration> declareTask,
    const GpuPhysicalQueueId requiredTerminalQueue,
    void* const userData,
    StandaloneGraphWorkspacePool& workspaces,
    QueueSubmissionToken& outUploadToken
){
    outUploadToken = {};

    auto& device = graphics.getDevice();
    __hidden_setup_upload_graph::SetupUploadSubmissionData submissionData{ .device = device, .userData = userData, .declareTask = declareTask, .queueSharing = queueSharing, .consumerQueue = consumerQueue, };
    const auto terminalToken = SubmitGraphOwnedStandaloneTask(graphics, MakeNotNull(&__hidden_setup_upload_graph::DeclareSetupUploadGraph), &submissionData, workspaces, requiredTerminalQueue);
    if(!terminalToken)
        return false;

    if(!outUploadToken.valid()){
        // The producer's accepted callback supplies the public token. A successful bridge graph without it breaks the setup API contract, so reject it.
        return false;
    }
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

