// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_internal.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_graph_setup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_StandaloneTaskGraphRecoveryIdentity("graphics.standalone_task_graph.recovery");
inline constexpr Name s_SetupUploadReadinessBridgeIdentity("graphics.setup_upload.readiness_bridge");
inline constexpr Name s_FrameTimingResetIdentity("graphics.frame_timing.reset");
inline constexpr Name s_StandaloneTaskGraphScratchArena("graphics.standalone_task_graph_scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


    [[nodiscard]] static bool Record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    )noexcept{
        static_cast<void>(payload);
        static_cast<void>(commandList);
        static_cast<void>(context);
        return true;
    }
};

// A standalone graph owns no renderer finalization packet. Predeclare this no-op Graphics tail so a later rejection can join every accepted physical queue before this call returns.
struct StandaloneTaskGraphRecoveryTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::None, true };

    struct Payload{
        GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
    };


    [[nodiscard]] static bool Record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    )noexcept{
        static_cast<void>(context);
        return !payload.frameTimingTransaction
            || !payload.frameTimingTransaction->needsRetirement()
            || payload.frameTimingTransaction->recordEnd(commandList)
        ;
    }
};

[[nodiscard]] static GpuTaskId DeclareStandaloneTaskGraphRecoveryTask(
    GpuTaskGraph& graph,
    GpuTimingFrameTransaction* const frameTimingTransaction
){
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = false;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.joinsAcceptedQueueFrontier = true;
    scheduling.isRecoverySubmission = true;
    GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(s_StandaloneTaskGraphRecoveryIdentity)
        .setMarkerLabel("Standalone Task Graph Recovery")
        .setScheduling(scheduling)
    ;
    return graph.addTask<StandaloneTaskGraphRecoveryTask>(
        recoveryDesc,
        StandaloneTaskGraphRecoveryTask::Payload{ frameTimingTransaction }
    );
}

[[nodiscard]] static GpuTaskId DeclareSetupUploadReadinessBridgeTasks(
    GpuTaskGraph& graph,
    GraphicsBackend::Device& device,
    const ResourceQueueSharing::Mask queueSharing,
    const CommandQueue::Enum directConsumerQueue,
    const GpuTaskId uploadTask){
    if(!uploadTask.valid())
        return {};

    GpuTaskId terminalTask = uploadTask;
    constexpr CommandQueue::Enum consumerQueues[] = {
        CommandQueue::Graphics,
        CommandQueue::Compute,
        CommandQueue::Transfer,
    };
    const auto appendBridge = [&graph, &device, uploadTask, &terminalTask](const CommandQueue::Enum consumerQueue){
        GpuTaskSchedulingHint scheduling = GraphicsModuleDetail::SetupUploadGraphScheduling(0u);
        scheduling.overlapPreferred = false;
        GpuTaskDesc bridgeDesc;
        bridgeDesc
            .setIdentity(s_SetupUploadReadinessBridgeIdentity)
            .setMarkerLabel("Setup Upload Readiness Bridge")
            .setScheduling(scheduling)
            .setDependencies(&uploadTask, 1u)
        ;
        const GpuTaskId bridgeTask = graph.addTask<SetupUploadReadinessBridgeGraphTask>(
            bridgeDesc,
            SetupUploadReadinessBridgeGraphTask::Payload{ device.getPrimaryPhysicalQueue(consumerQueue) }
        );
        if(!bridgeTask.valid())
            return false;
        terminalTask = bridgeTask;
        return true;
    };
    for(const CommandQueue::Enum consumerQueue : consumerQueues){
        if(
            consumerQueue == directConsumerQueue
            || !ResourceQueueSharing::IncludesQueueClass(queueSharing, consumerQueue)
            || !device.getQueue(consumerQueue)
        )
            continue;
        if(!appendBridge(consumerQueue))
            return {};
    }
    // The returned resource is ready on its direct consumer timeline even when the scheduler offloads its producer.
    if(!device.getQueue(directConsumerQueue) || !appendBridge(directConsumerQueue))
        return {};
    return terminalTask;
}


struct SetupUploadSubmissionData{
    GraphicsBackend::Device& device;
    void* userData = nullptr;
    GraphicsModuleDetail::GraphTaskDeclaration declareTask = nullptr;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    CommandQueue::Enum consumerQueue = CommandQueue::Graphics;
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
        submissionData.consumerQueue,
        uploadTask
    );
}


struct FrameTimingResetGraphTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{
        GpuQueueCapability::None,
        true,
        GpuQueueCapability::Compute | GpuQueueCapability::Graphics,
    };

    struct Payload{
        GpuTimingRecorder* timing = nullptr;
    };


    [[nodiscard]] static bool Record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    )noexcept{
        static_cast<void>(context);
        if(!payload.timing)
            return false;

        payload.timing->recordFrameReset(commandList);
        return true;
    }

    static void Accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.timing && token.valid())
            payload.timing->confirmFrameReset(token);
    }

    static void Discarded(Payload& payload){
        if(payload.timing)
            payload.timing->discardFrameReset();
    }
};

[[nodiscard]] static GpuTaskSchedulingHint FrameTimingResetScheduling()noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    // The reset is a complete, CPU-visible preamble boundary. Later renderer submissions may only reserve their timestamp scopes after this packet accepts, so it must not merge with unrelated graph work.
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
        .setIdentity(s_FrameTimingResetIdentity)
        .setMarkerLabel("Frame GPU-Timing Reset")
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


SetupUploadSameClassRouting ResolveSetupUploadSameClassRouting(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum consumerQueue,
    const usize uploadBytes)noexcept{
    SetupUploadSameClassRouting result;
    if(uploadBytes < s_SetupUploadLargeMinimumBytes)
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
        if(
            candidate.id == result.primaryQueue
            || candidate.queueClass != consumerQueue
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

ResourceQueueSharing::Mask ResolveSetupUploadConsumerSharing(
    const ResourceQueueSharing::Mask requestedSharing,
    const CommandQueue::Enum consumerQueue,
    const bool crossFamilySameClassRouting)noexcept{
    if(crossFamilySameClassRouting){
        const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive
            ? ResourceQueueSharing::ForQueueClass(consumerQueue)
            : requestedSharing
        ;
        return static_cast<ResourceQueueSharing::Mask>(
            static_cast<u8>(baseSharing) | static_cast<u8>(ResourceQueueSharing::ForQueueClass(consumerQueue))
        );
    }

    if(consumerQueue == CommandQueue::Graphics)
        return requestedSharing;

    const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive
        ? ResourceQueueSharing::Graphics
        : requestedSharing
    ;
    return static_cast<ResourceQueueSharing::Mask>(
        static_cast<u8>(baseSharing) | static_cast<u8>(ResourceQueueSharing::ForQueueClass(consumerQueue))
    );
}

CommandQueue::Enum ResolveSetupUploadConsumerQueue(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum requestedConsumerQueue,
    const usize uploadBytes,
    const bool hasKnownFinalState,
    const bool requiresGraphicsConsumerQueue)noexcept{
    if(requiresGraphicsConsumerQueue)
        return CommandQueue::Graphics;

    switch(requestedConsumerQueue){
    case CommandQueue::kCount:
        if(uploadBytes < s_SetupUploadLargeMinimumBytes || !hasKnownFinalState)
            return CommandQueue::Graphics;
        return __hidden_graphics_graph_setup::ResolveAvailableSetupConsumerQueue(device);
    case CommandQueue::Transfer:
        return hasKnownFinalState
            ? __hidden_graphics_graph_setup::ResolveAvailableSetupConsumerQueue(device)
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
        GLB_ASSERT_MSG(false, GLB_TEXT("GraphicsRuntime: setup upload requested an invalid command queue"));
        return CommandQueue::Graphics;
    }
}

GpuTaskSchedulingHint SetupUploadGraphScheduling(
    const usize byteCount,
    const bool sameClassRouting,
    const bool crossFamilySameClassRouting)noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = byteCount >= s_SetupUploadLargeMinimumBytes
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
    // Uploads with an Unknown descriptor state publish the concrete CopyDest post-write state.
    return declaredInitialState == ResourceStates::Unknown
        ? ResourceStates::CopyDest
        : declaredInitialState
    ;
}


bool SubmitGraphOwnedStandaloneTask(
    const GraphicsRuntime& graphics,
    GraphicsArena& graphArena,
    void* const userData,
    const GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outSubmissionToken,
    const GpuPhysicalQueueId requiredTerminalQueue,
    CpuTaskScheduler* const readyFrontierScheduler,
    GpuTimingRecorder* const timingRecorder,
    GpuTimingFrameTransaction* const frameTimingTransaction
){
    outSubmissionToken = {};
    if(!declareTask || (frameTimingTransaction && !timingRecorder))
        return false;

    auto& device = graphics.getDevice();

    GpuTaskGraph graph(graphArena);
    const GpuTaskId terminalTask = declareTask(userData, graph);
    if(!terminalTask.valid())
        return false;
    GpuTaskId frameTimingBeginTask;
    if(frameTimingTransaction){
        const GpuTaskGraph::DeclarationReadView declaredTasks(graph);
        if(!declaredTasks.valid() || declaredTasks.taskCount() < 2u)
            return false;
        frameTimingBeginTask = declaredTasks.taskAt(0u).id;
        if(!frameTimingBeginTask.valid() || frameTimingBeginTask == terminalTask)
            return false;
    }
    const GpuTaskId recoveryTask = __hidden_graphics_graph_setup::DeclareStandaloneTaskGraphRecoveryTask(
        graph, frameTimingTransaction
    );
    if(!recoveryTask.valid())
        return false;

    GpuTaskGraphAnalysis analysis(graphArena);
    GpuTaskGraphQueueAssignments assignments(graphArena);
    GpuCompiledGraph compiledGraph(graphArena);
    GpuRecordedGraph recordedGraph(graphArena);
    GpuGraphSubmissionTransaction transaction(graphArena);
    Alloc::ScratchArena scratchArena(__hidden_graphics_graph_setup::s_StandaloneTaskGraphScratchArena);
    const GpuTaskScheduler& scheduler = graphics.gpuTasks();
    GpuTaskGraphCompileOptions compileOptions;
    if(frameTimingTransaction){
        compileOptions.packetTimingEnvelope.firstTask = frameTimingBeginTask;
        compileOptions.packetTimingEnvelope.lastTask = terminalTask;
    }
    if(!scheduler.scheduleGraph(
        graph,
        analysis,
        assignments,
        compiledGraph,
        recordedGraph,
        transaction,
        scratchArena,
        compileOptions
    )){
        const auto& analysisDiagnostic = analysis.diagnostic();
        const auto& queueDiagnostic = assignments.diagnostic();
        NWB_LOGGER_WARNING(GLB_TEXT("GraphicsRuntime: standalone graph scheduling failed: analysis={} task={} resource={} version={} queue={} queueTask={}")
            , static_cast<u32>(analysisDiagnostic.status), analysisDiagnostic.task.index, analysisDiagnostic.resource.index
            , analysisDiagnostic.resourceVersion.index, static_cast<u32>(queueDiagnostic.status), queueDiagnostic.task.index
        );
        return false;
    }

    const GpuTaskGraph::DeclarationReadView declarations(graph);
    const GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
    if(!declarations.valid() || !compiledPlan.validFor(declarations))
        return false;

    const GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalTask);
    const GpuSubmissionPacketId recoveryPacket = compiledPlan.packetForTask(recoveryTask);
    const GpuSubmissionPacketId frameTimingBeginPacket = frameTimingTransaction
        ? compiledPlan.packetForTask(frameTimingBeginTask)
        : GpuSubmissionPacketId{}
    ;
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(
        !terminalPacket.valid()
        || !recoveryPacket.valid()
        || !graphicsQueue.valid()
        || compiledPlan.packetCount() < 2u
        || terminalPacket == recoveryPacket
        || recoveryPacket.index == 0u
        || recoveryPacket != compiledPlan.packetIdAt(compiledPlan.packetCount() - 1u)
        || !compiledPlan.taskJoinsAcceptedQueueFrontier(recoveryTask)
    )
        return false;

    const GpuCompiledPacketView terminalPacketView = compiledPlan.packet(terminalPacket);
    const GpuCompiledPacketView recoveryPacketView = compiledPlan.packet(recoveryPacket);
    if(!terminalPacketView.valid() || !recoveryPacketView.valid())
        return false;
    if(requiredTerminalQueue.valid() && terminalPacketView.plan->queue != requiredTerminalQueue)
        return false;
    if(frameTimingTransaction){
        const GpuCompiledPacketView beginPacketView = compiledPlan.packet(frameTimingBeginPacket);
        const GpuSubmissionPacketRange timingEnvelope = compiledPlan.packetTimingEnvelopeRange();
        if(
            !beginPacketView.valid()
            || frameTimingBeginPacket != compiledPlan.packetIdAt(0u)
            || frameTimingBeginPacket == terminalPacket
            || beginPacketView.plan->taskCount != 1u
            || beginPacketView.tasks[0u] != frameTimingBeginTask
            || beginPacketView.plan->queue != graphicsQueue
            || terminalPacketView.plan->queue != graphicsQueue
            || !beginPacketView.plan->recordsTiming
            || !terminalPacketView.plan->recordsTiming
            || !timingEnvelope.valid()
            || timingEnvelope.first != frameTimingBeginPacket
            || timingEnvelope.first.index + timingEnvelope.packetCount - 1u != terminalPacket.index
        )
            return false;
    }

    const GpuSubmissionPacket& recoveryPacketPlan = *recoveryPacketView.plan;
    if(
        recoveryPacketPlan.queue != graphicsQueue
        || recoveryPacketPlan.dependencyCount != 0u
        || recoveryPacketPlan.externalDependencyCount != 0u
        || !recoveryPacketPlan.joinsAcceptedQueueFrontier
    )
        return false;

    // Setup and timing callers preserve their established serial behavior. The public standalone graph boundary supplies the Graphics worker pool
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.readyFrontierScheduler = readyFrontierScheduler;
    const bool graphAccepted = scheduler.executeGraph(
        graph,
        compiledGraph,
        recordedGraph,
        normalExecution,
        transaction,
        timingRecorder,
        scratchArena
    );
    const QueueSubmissionToken terminalToken = transaction.taskToken(compiledPlan, terminalTask);
    if(!graphAccepted){
        bool timingRecovered = true;
        if(frameTimingTransaction){
            if(terminalToken.valid())
                timingRecovered = frameTimingTransaction->confirmEndSubmission(terminalToken, false);
            else if(frameTimingTransaction->needsRetirement())
                timingRecovered = frameTimingTransaction->prepareForRecovery();
            if(!timingRecovered)
                frameTimingTransaction->discard();
        }
        const bool recovered = !transaction.hasAcceptedPackets() || scheduler.executeAcceptedFrontierTask(
            graph,
            compiledGraph,
            recordedGraph,
            recoveryTask,
            transaction,
            timingRecorder,
            scratchArena
        );
        if(recovered && frameTimingTransaction && frameTimingTransaction->needsRetirement()){
            const QueueSubmissionToken recoveryToken = transaction.taskToken(compiledPlan, recoveryTask);
            timingRecovered = frameTimingTransaction->confirmEndSubmission(recoveryToken, false) && timingRecovered;
        }
        if(!timingRecovered){
            NWB_LOGGER_WARNING(GLB_TEXT("GraphicsRuntime: standalone frame timing could not be retired after graph rejection"));
            frameTimingTransaction->discard();
        }
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

    const bool discarded = transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    );
    const bool timingConfirmed = !frameTimingTransaction
        || frameTimingTransaction->confirmEndSubmission(terminalToken, discarded && terminalToken.valid())
    ;
    if(!timingConfirmed){
        NWB_LOGGER_WARNING(GLB_TEXT("GraphicsRuntime: standalone frame timing confirmation failed"));
        frameTimingTransaction->discard();
    }
    if(!discarded || !terminalToken.valid()){
        graphics.requestDeviceRecreation();
        outSubmissionToken = {};
        return false;
    }
    outSubmissionToken = terminalToken;
    return true;
}

bool SubmitGraphOwnedSetupUpload(
    const GraphicsRuntime& graphics,
    GraphicsArena& graphArena,
    const ResourceQueueSharing::Mask queueSharing,
    const CommandQueue::Enum consumerQueue,
    void* const userData,
    const GraphTaskDeclaration declareTask,
    QueueSubmissionToken& outUploadToken,
    const GpuPhysicalQueueId requiredTerminalQueue){
    outUploadToken = {};
    if(!declareTask)
        return false;

    auto& device = graphics.getDevice();
    __hidden_graphics_graph_setup::SetupUploadSubmissionData submissionData{
        .device = device,
        .userData = userData,
        .declareTask = declareTask,
        .queueSharing = queueSharing,
        .consumerQueue = consumerQueue,
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
        // The producer's accepted callback supplies the public token. A successful bridge graph without that token would weaken the existing setup API contract, so reject it rather than returning a falsely ready handle.
        return false;
    }
    return true;
}

bool SubmitGraphOwnedFrameTimingReset(
    const GraphicsRuntime& graphics,
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


bool GraphicsRuntime::submitStandaloneTaskGraph(
    void* const userData,
    const StandaloneTaskGraphDeclaration declareTask,
    QueueSubmissionToken& outSubmissionToken,
    const GpuPhysicalQueueId requiredTerminalQueue,
    GpuTimingRecorder* const timingRecorder,
    GpuTimingFrameTransaction* const frameTimingTransaction
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
        &m_cpuScheduler,
        timingRecorder,
        frameTimingTransaction
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

