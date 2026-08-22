// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module_internal.h"

#include "backend_selection.h"
#include "task_graph/compiler.h"
#include "task_graph/packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_graph_setup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] static CommandQueue::Enum ResolveTransferPreferredQueue(GraphicsBackend::Device& device)noexcept{
    if(device.getQueue(CommandQueue::Transfer))
        return CommandQueue::Transfer;
    if(device.getQueue(CommandQueue::Compute))
        return CommandQueue::Compute;
    return CommandQueue::Graphics;
}


// A returned setup resource has no external-completion object that a later direct consumer can wait on. Retain the
// historical readiness rule by recording one explicit graph packet on every declared consumer queue. Its dependency
// on the producer lowers the exact timeline wait, and later native work on that queue follows it in queue order.
struct SetupUploadReadinessBridgeGraphTask{
    struct Payload{};


    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(payload);
        static_cast<void>(commandList);
        static_cast<void>(context);
        return true;
    }
};

// A standalone graph does not own a renderer finalization packet. Predeclare this no-op Graphics tail so a later
// normal-packet rejection can join every already-accepted physical queue before this synchronous call returns.
struct StandaloneTaskGraphRecoveryTask{
    struct Payload{};


    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(payload);
        static_cast<void>(commandList);
        static_cast<void>(context);
        return true;
    }
};

[[nodiscard]] static GpuTaskId DeclareStandaloneTaskGraphRecoveryTask(GpuTaskGraph& graph){
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = false;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.joinsAcceptedQueueFrontier = true;
    GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("graphics.standalone_task_graph.recovery"))
        .setMarkerLabel("Standalone Task Graph Recovery")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
    ;
    return graph.addTask<StandaloneTaskGraphRecoveryTask>(
        recoveryDesc,
        StandaloneTaskGraphRecoveryTask::Payload{}
    );
}

[[nodiscard]] static GpuTaskId DeclareSetupUploadReadinessBridgeTasks(
    GpuTaskGraph& graph,
    GraphicsBackend::Device& device,
    const ResourceQueueSharing::Mask queueSharing,
    const CommandQueue::Enum uploadQueue,
    const GpuTaskId uploadTask,
    const bool bridgePrimaryUploadQueue = false
){
    if(!uploadTask.valid())
        return {};

    GpuTaskId terminalTask = uploadTask;
    constexpr CommandQueue::Enum consumerQueues[] = {
        CommandQueue::Graphics,
        CommandQueue::Compute,
        CommandQueue::Transfer,
    };
    const auto appendBridge = [&graph, uploadTask, &terminalTask](const CommandQueue::Enum consumerQueue){
        GpuTaskSchedulingHint scheduling = GraphicsModuleDetail::SetupUploadGraphScheduling(0u);
        scheduling.overlapPreferred = false;
        GpuTaskDesc bridgeDesc;
        bridgeDesc
            .setIdentity(Name("graphics.setup_upload.readiness_bridge"))
            .setMarkerLabel("Setup Upload Readiness Bridge")
            .setQueue(GraphicsModuleDetail::SetupUploadGraphQueueRequest(consumerQueue))
            .setScheduling(scheduling)
            .setDependencies(&uploadTask, 1u)
        ;
        const GpuTaskId bridgeTask = graph.addTask<SetupUploadReadinessBridgeGraphTask>(
            bridgeDesc,
            SetupUploadReadinessBridgeGraphTask::Payload{}
        );
        if(!bridgeTask.valid())
            return false;
        terminalTask = bridgeTask;
        return true;
    };
    for(const CommandQueue::Enum consumerQueue : consumerQueues){
        if(
            consumerQueue == uploadQueue
            || !GraphicsModuleDetail::QueueSharingIncludesQueue(queueSharing, consumerQueue)
            || !device.getQueue(consumerQueue)
        )
            continue;
        if(!appendBridge(consumerQueue))
            return {};
    }
    // Append this last so the synchronous standalone caller can verify that the returned-handle bridge resolved to
    // the exact primary physical upload queue even when the descriptor names additional consumer classes.
    if(bridgePrimaryUploadQueue && (!device.getQueue(uploadQueue) || !appendBridge(uploadQueue)))
        return {};
    return terminalTask;
}


struct SetupUploadSubmissionData{
    GraphicsBackend::Device& device;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    CommandQueue::Enum uploadQueue = CommandQueue::Graphics;
    void* userData = nullptr;
    GraphicsModuleDetail::GraphTaskDeclaration declareTask = nullptr;
    bool bridgePrimaryUploadQueue = false;
};

[[nodiscard]] static GpuTaskId DeclareSetupUploadGraph(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *static_cast<SetupUploadSubmissionData*>(userData);
    if(!submissionData.declareTask)
        return {};

    const GpuTaskId uploadTask = submissionData.declareTask(submissionData.userData, graph);
    return DeclareSetupUploadReadinessBridgeTasks(
        graph,
        submissionData.device,
        submissionData.queueSharing,
        submissionData.uploadQueue,
        uploadTask,
        submissionData.bridgePrimaryUploadQueue
    );
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

struct FrameTimingResetSubmissionData{
    GpuTimingRecorder& timing;
};

[[nodiscard]] static GpuTaskId DeclareFrameTimingResetGraph(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *static_cast<FrameTimingResetSubmissionData*>(userData);
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
            .timing = &submissionData.timing,
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool QueueSharingIncludesQueue(
    const ResourceQueueSharing::Mask sharing,
    const CommandQueue::Enum queue
)noexcept{
    const u8 queueBit = static_cast<u8>(__hidden_graphics_graph_setup::QueueSharingBitForQueue(queue));
    return queueBit != 0u && (static_cast<u8>(sharing) & queueBit) != 0u;
}

SetupUploadSameClassRouting ResolveSetupUploadSameClassRouting(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum uploadQueue,
    const usize uploadBytes
)noexcept{
    SetupUploadSameClassRouting result;
    if(uploadBytes < __hidden_graphics_graph_setup::s_TransferPreferredUploadMinimumBytes)
        return result;

    result.primaryQueue = device.getPrimaryPhysicalQueue(uploadQueue);
    const GpuPhysicalQueueInfo* const primaryInfo = device.getPhysicalQueueInfo(result.primaryQueue);
    if(!primaryInfo)
        return result;

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* alternateInfo = nullptr;
    const u8 requiredCapabilities = static_cast<u8>(GpuQueueCapability::Transfer);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id == result.primaryQueue
            || candidate.queueClass != uploadQueue
            || (static_cast<u8>(candidate.capabilities) & requiredCapabilities) != requiredCapabilities
            || (alternateInfo && candidate.id.index >= alternateInfo->id.index)
        )
            continue;
        alternateInfo = &candidate;
    }
    if(!alternateInfo)
        return result;

    result.enabled = true;
    result.crossesQueueFamily = alternateInfo->familyIndex != primaryInfo->familyIndex;
    return result;
}

ResourceQueueSharing::Mask ResolveSetupUploadQueueSharing(
    const ResourceQueueSharing::Mask requestedSharing,
    const CommandQueue::Enum uploadQueue,
    const bool crossFamilySameClassRouting
)noexcept{
    if(crossFamilySameClassRouting){
        const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive
            ? __hidden_graphics_graph_setup::QueueSharingBitForQueue(uploadQueue)
            : requestedSharing
        ;
        return static_cast<ResourceQueueSharing::Mask>(
            static_cast<u8>(baseSharing) | static_cast<u8>(__hidden_graphics_graph_setup::QueueSharingBitForQueue(uploadQueue))
        );
    }

    if(uploadQueue == CommandQueue::Graphics)
        return requestedSharing;

    const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive
        ? ResourceQueueSharing::Graphics
        : requestedSharing
    ;
    return static_cast<ResourceQueueSharing::Mask>(
        static_cast<u8>(baseSharing) | static_cast<u8>(__hidden_graphics_graph_setup::QueueSharingBitForQueue(uploadQueue))
    );
}

CommandQueue::Enum ResolveSetupUploadQueue(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum requestedQueue,
    const usize uploadBytes,
    const bool hasKnownFinalState
)noexcept{
    switch(requestedQueue){
    case CommandQueue::kCount:
        if(uploadBytes < __hidden_graphics_graph_setup::s_TransferPreferredUploadMinimumBytes || !hasKnownFinalState)
            return CommandQueue::Graphics;
        return __hidden_graphics_graph_setup::ResolveTransferPreferredQueue(device);
    case CommandQueue::Transfer:
        return hasKnownFinalState
            ? __hidden_graphics_graph_setup::ResolveTransferPreferredQueue(device)
            : CommandQueue::Graphics
        ;
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

GpuQueueRequest SetupUploadGraphQueueRequest(const CommandQueue::Enum uploadQueue)noexcept{
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

GpuTaskSchedulingHint SetupUploadGraphScheduling(
    const usize byteCount,
    const bool sameClassRouting,
    const bool crossFamilySameClassRouting
)noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = byteCount >= __hidden_graphics_graph_setup::s_TransferPreferredUploadMinimumBytes
        ? GpuTaskCostHint::Large
        : GpuTaskCostHint::Tiny
    ;
    // A public setup call returns one accepted producer token, so retain a distinct explicit packet.
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowSameClassQueueRouting = sameClassRouting;
    scheduling.preferNonPrimarySameClassQueue = sameClassRouting;
    scheduling.allowCrossFamilySameClassQueueRouting = crossFamilySameClassRouting;
    return scheduling;
}

ResourceStates::Mask SetupUploadGraphFinalState(const ResourceStates::Mask declaredInitialState)noexcept{
    // Unknown is a valid legacy descriptor state. The graph still needs a concrete post-write state; CopyDest is
    // exactly what the native write leaves behind when the old setup path has no declared final transition.
    return declaredInitialState == ResourceStates::Unknown
        ? ResourceStates::CopyDest
        : declaredInitialState
    ;
}


bool SubmitGraphOwnedStandaloneTask(
    const Graphics& graphics,
    GraphicsArena& graphArena,
    void* const userData,
    const GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outSubmissionToken,
    const GpuPhysicalQueueId requiredTerminalQueue,
    Alloc::ThreadPool* const readyFrontierWorkerPool
){
    outSubmissionToken = {};
    if(!declareTask)
        return false;

    auto& device = graphics.getDevice();

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u)
        return false;

    GpuTaskGraph graph(graphArena);
    const GpuTaskId terminalTask = declareTask(userData, graph);
    if(!terminalTask.valid())
        return false;
    const GpuTaskId recoveryTask = __hidden_graphics_graph_setup::DeclareStandaloneTaskGraphRecoveryTask(graph);
    if(!recoveryTask.valid())
        return false;

    GpuTaskGraphAnalysis analysis(graphArena);
    GpuTaskGraphQueueAssignments assignments(graphArena);
    GpuCompiledGraph compiledGraph(graphArena);
    GpuRecordedGraph recordedGraph(graphArena);
    GpuGraphSubmissionTransaction transaction(graphArena);
    Alloc::ScratchArena scratchArena(Name("graphics.standalone_task_graph_scratch"));
    const GpuTaskGraphCompiler compiler;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena))
        return false;

    const GpuSubmissionPacketId terminalPacket = compiledGraph.packetForTask(terminalTask);
    const GpuSubmissionPacketId recoveryPacket = compiledGraph.packetForTask(recoveryTask);
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(
        !terminalPacket.valid()
        || !recoveryPacket.valid()
        || !graphicsQueue.valid()
        || compiledGraph.packetCount() < 2u
        || terminalPacket == recoveryPacket
        || recoveryPacket.index == 0u
        || recoveryPacket != compiledGraph.packetIdAt(compiledGraph.packetCount() - 1u)
        || !compiledGraph.taskJoinsAcceptedQueueFrontier(recoveryTask)
    )
        return false;
    if(requiredTerminalQueue.valid() && compiledGraph.packet(terminalPacket).queue != requiredTerminalQueue)
        return false;

    const GpuSubmissionPacket& recoveryPacketPlan = compiledGraph.packet(recoveryPacket);
    if(
        recoveryPacketPlan.queue != graphicsQueue
        || recoveryPacketPlan.dependencyCount != 0u
        || recoveryPacketPlan.externalDependencyCount != 0u
        || !recoveryPacketPlan.joinsAcceptedQueueFrontier
    )
        return false;

    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    // Setup and timing callers preserve their established serial behavior. The public standalone graph boundary
    // supplies the Graphics worker pool; the normal executor derives its recovery suffix and each task decides
    // whether it can safely opt into ready-frontier worker recording.
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.readyFrontierWorkerPool = readyFrontierWorkerPool;
    const bool submitted = submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena
    );
    if(!submitted){
        const bool recovered = !transaction.hasAcceptedPackets() || submitter.recordAndSubmitAcceptedFrontierTask(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            recoveryTask,
            transaction,
            scratchArena
        );
        const bool discarded = transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        );
        outSubmissionToken = {};
        if(!recovered || !discarded)
            graphics.requestDeviceRecreation();
        return false;
    }

    outSubmissionToken = transaction.packetToken(terminalPacket);
    if(!transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    )){
        graphics.requestDeviceRecreation();
        outSubmissionToken = {};
        return false;
    }
    return outSubmissionToken.valid();
}

bool SubmitGraphOwnedSetupUpload(
    const Graphics& graphics,
    GraphicsArena& graphArena,
    const ResourceQueueSharing::Mask queueSharing,
    const CommandQueue::Enum uploadQueue,
    void* const userData,
    const GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outUploadToken,
    const bool bridgePrimaryUploadQueue,
    const GpuPhysicalQueueId requiredTerminalQueue
){
    outUploadToken = {};
    if(!declareTask)
        return false;

    auto& device = graphics.getDevice();
    __hidden_graphics_graph_setup::SetupUploadSubmissionData submissionData{
        .device = device,
        .queueSharing = queueSharing,
        .uploadQueue = uploadQueue,
        .userData = userData,
        .declareTask = declareTask,
        .bridgePrimaryUploadQueue = bridgePrimaryUploadQueue,
    };
    QueueSubmissionToken terminalToken;
    if(!SubmitGraphOwnedStandaloneTask(
        graphics,
        graphArena,
        &submissionData,
        &__hidden_graphics_graph_setup::DeclareSetupUploadGraph,
        terminalToken,
        requiredTerminalQueue
    ))
        return false;

    if(!outUploadToken.valid()){
        // The producer's accepted callback supplies the public token. A successful bridge graph without that token
        // would weaken the existing setup API contract, so reject it rather than returning a falsely ready handle.
        return false;
    }
    return true;
}

bool SubmitGraphOwnedFrameTimingReset(
    const Graphics& graphics,
    GraphicsArena& graphArena,
    GpuTimingRecorder& timing
){
    auto& device = graphics.getDevice();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!graphicsQueue.valid())
        return false;

    __hidden_graphics_graph_setup::FrameTimingResetSubmissionData submissionData{
        .timing = timing,
    };
    QueueSubmissionToken acceptedToken;
    if(!SubmitGraphOwnedStandaloneTask(
        graphics,
        graphArena,
        &submissionData,
        &__hidden_graphics_graph_setup::DeclareFrameTimingResetGraph,
        acceptedToken,
        graphicsQueue
    ))
        return false;

    return acceptedToken.queue == CommandQueue::Graphics
        && acceptedToken.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Graphics::submitStandaloneTaskGraph(
    void* const userData,
    const StandaloneTaskGraphDeclaration declareTask,
    QueueSubmissionToken& outSubmissionToken,
    const GpuPhysicalQueueId requiredTerminalQueue
)const{
    outSubmissionToken = {};
    if(!declareTask)
        return false;

    return GraphicsModuleDetail::SubmitGraphOwnedStandaloneTask(
        *this,
        m_allocator.getObjectArena(),
        userData,
        declareTask,
        outSubmissionToken,
        requiredTerminalQueue,
        &m_threadPool
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

