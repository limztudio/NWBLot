// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_internal.h"
#include "runtime_standalone_graph.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>
#include <core/graphics/rhi/queue_sharing.h>

#include <global/exception.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_graph_setup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_StandaloneTaskGraphRecoveryIdentity("graphics.standalone_task_graph.recovery");
inline constexpr Name s_SetupUploadReadinessBridgeIdentity("graphics.setup_upload.readiness_bridge");
inline constexpr Name s_FrameTimingResetIdentity("graphics.frame_timing.reset");
inline constexpr Name s_StandaloneTaskGraphScratchArena("graphics.standalone_task_graph_scratch");
inline constexpr u32 s_MinFrameTimingTaskCount = 2u;
inline constexpr u32 s_MinFrameTimingPacketCount = 2u;


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


    [[nodiscard]] static bool Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context)noexcept{
        static_cast<void>(payload);
        static_cast<void>(commandList);
        static_cast<void>(context);
        return true;
    }
};

// A standalone graph owns no renderer finalization packet.
// Predeclare this no-op Graphics tail so a later rejection can join every accepted physical queue before this call returns.
// The plain variant covers graphs without frame timing, so no task carries a nullable pointer with a silent no-op guard.
struct StandaloneTaskGraphRecoveryTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::None, true };

    struct Payload{
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context)noexcept{
        static_cast<void>(payload);
        static_cast<void>(commandList);
        static_cast<void>(context);
        return true;
    }
};

struct StandaloneTaskGraphTimingRetirementTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::None, true };

    struct Payload{
        GpuTimingFrameTransaction& frameTimingTransaction;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context){
        static_cast<void>(context);
        return !payload.frameTimingTransaction.needsRetirement() || payload.frameTimingTransaction.recordEnd(commandList);
    }
};

[[nodiscard]] static Expected<GpuTaskId> DeclareStandaloneTaskGraphRecoveryTask(GpuTaskGraph& graph, GpuTimingFrameTransaction* const frameTimingTransaction){
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = false;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.joinsAcceptedQueueFrontier = true;
    scheduling.isRecoverySubmission = true;
    GpuTaskDesc recoveryDesc;
    recoveryDesc.setIdentity(s_StandaloneTaskGraphRecoveryIdentity).setMarkerLabel("Standalone Task Graph Recovery").setScheduling(scheduling);
    Expected<GpuTaskId> task = MakeUnexpected(Failure{});
    if(frameTimingTransaction == nullptr)
        task = graph.addTask<StandaloneTaskGraphRecoveryTask>(recoveryDesc, StandaloneTaskGraphRecoveryTask::Payload{});
    else
        task = graph.addTask<StandaloneTaskGraphTimingRetirementTask>(recoveryDesc, StandaloneTaskGraphTimingRetirementTask::Payload{ *frameTimingTransaction });
    if(!task)
        return MakeUnexpected(task.error());
    return task;
}

[[nodiscard]] static Expected<GpuTaskId> DeclareSetupUploadReadinessBridgeTasks(GpuTaskGraph& graph, GraphicsBackend::Device& device, const ResourceQueueSharing::Mask queueSharing, const CommandQueue::Enum directConsumerQueue, const GpuTaskId uploadTask){
    NWB_ASSERT(uploadTask.valid());

    GpuTaskId terminalTask = uploadTask;
    constexpr CommandQueue::Enum s_ConsumerQueues[] = { CommandQueue::Graphics, CommandQueue::Compute, CommandQueue::Transfer, };
    const auto appendBridge = [&graph, &device, uploadTask, &terminalTask](const CommandQueue::Enum consumerQueue){
        GpuTaskSchedulingHint scheduling = GraphicsModuleDetail::SetupUploadGraphScheduling(0u);
        scheduling.overlapPreferred = false;
        GpuTaskDesc bridgeDesc;
        bridgeDesc.setIdentity(s_SetupUploadReadinessBridgeIdentity).setMarkerLabel("Setup Upload Readiness Bridge").setScheduling(scheduling).setDependencies(&uploadTask, 1u);
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
    auto& submissionData = *static_cast<SetupUploadSubmissionData*>(userData);
    const auto uploadTask = submissionData.declareTask.get()(submissionData.userData, graph);
    if(!uploadTask)
        return MakeUnexpected(uploadTask.error());
    return DeclareSetupUploadReadinessBridgeTasks(graph, submissionData.device, submissionData.queueSharing, submissionData.consumerQueue, *uploadTask);
}


struct FrameTimingResetGraphTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::None, true, GpuQueueCapability::Compute | GpuQueueCapability::Graphics, };

    struct Payload{
        NotNull<GpuTimingRecorder*> timing;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context){
        static_cast<void>(context);

        payload.timing->recordFrameReset(commandList);
        return true;
    }

    static void Accepted(Payload& payload, const QueueSubmissionToken& token){
        if(token.valid())
            payload.timing->confirmFrameReset(token);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.timing->discardFrameReset();
    }
};

[[nodiscard]] static GpuTaskSchedulingHint FrameTimingResetScheduling()noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    // The reset is a complete, CPU-visible preamble boundary. Later renderer submissions may only reserve their timestamp scopes after this packet accepts, so it must not merge with unrelated work.
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.overlapPreferred = false;
    return scheduling;
}

struct FrameTimingResetSubmissionData{
    GpuTimingRecorder& timing;
};

[[nodiscard]] static Expected<GpuTaskId> DeclareFrameTimingResetGraph(void* const userData, GpuTaskGraph& graph){
    auto& submissionData = *static_cast<FrameTimingResetSubmissionData*>(userData);
    GpuTaskDesc resetDesc;
    resetDesc.setIdentity(s_FrameTimingResetIdentity).setMarkerLabel("Frame GPU-Timing Reset").setScheduling(FrameTimingResetScheduling());
    const auto taskResult = graph.addTask<FrameTimingResetGraphTask>(resetDesc, FrameTimingResetGraphTask::Payload{ .timing = MakeNotNull(&submissionData.timing), });
    if(!taskResult)
        return MakeUnexpected(Failure{});
    const GpuTaskId task = *taskResult;
    return task;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void StandaloneGraphWorkspace::reset(){
    {
        const GpuCompiledGraph::ReadView planAccess(compiledGraph);
        if(planAccess.valid()){
            const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
            if(recordingAttemptGeneration != 0u && !transaction.discardUnaccepted(graph, compiledGraph, recordingAttemptGeneration))
                NWB_FATAL_ASSERT_MSG(false, "standalone graph workspace reuse requires every unaccepted packet to resolve");
            transaction.reset(compiledGraph);
            recordedGraph.reset(compiledGraph);
        }
    }

    graph.reset();
    analysis.reset();
    assignments.reset();
    compiledGraph.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StandaloneGraphWorkspacePool::Lease::Lease(StandaloneGraphWorkspacePool& pool)
    : m_pool(pool)
    , m_workspace(pool.acquire())
    , m_exceptionCount(UncaughtExceptionCount())
{}

StandaloneGraphWorkspacePool::Lease::~Lease()noexcept(false){
    if(!m_workspace)
        return;
    // An unwinding submission destroys its workspace instead of resetting it, so no reset callback runs during the unwind.
    if(UncaughtExceptionCount() > m_exceptionCount){
        m_pool.discard(*m_workspace);
        return;
    }
    m_pool.release(*m_workspace);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool StandaloneGraphWorkspacePool::releaseCommandListCaches()noexcept{
    NothrowScopedLock lock(m_mutex);
    bool released = true;
    for(WorkspaceOwner& workspace : m_workspaces){
        if(workspace->leased || !workspace->recordedGraph.releaseCommandListCache())
            released = false;
    }
    return released;
}

StandaloneGraphWorkspace* StandaloneGraphWorkspacePool::acquire(){
    ScopedLock lock(m_mutex);
    for(WorkspaceOwner& workspace : m_workspaces){
        if(!workspace->leased){
            workspace->leased = true;
            return workspace.get();
        }
    }

    // Only a new peak of overlapping or reentrant submissions creates another owner set.
    WorkspaceOwner created = MakeGlobalUnique<StandaloneGraphWorkspace>(m_arena, m_arena);
    if(!created)
        return nullptr;
    StandaloneGraphWorkspace& workspace = *created;
    m_workspaces.push_back(Move(created));
    workspace.leased = true;
    return &workspace;
}

void StandaloneGraphWorkspacePool::release(StandaloneGraphWorkspace& workspace){
    // A throwing reset destroys this owner set during the unwind, so a stuck lease never blocks device teardown.
    ScopeExit discardOnUnwind([this, &workspace]()noexcept{ discard(workspace); });

    // Reset outside the lock because payload teardown may run task callbacks.
    workspace.reset();
    discardOnUnwind.release();

    ScopedLock lock(m_mutex);
    workspace.leased = false;
}

void StandaloneGraphWorkspacePool::discard(StandaloneGraphWorkspace& workspace){
    WorkspaceOwner discarded;
    {
        ScopedLock lock(m_mutex);
        for(auto it = m_workspaces.begin(); it != m_workspaces.end(); ++it){
            if(it->get() != &workspace)
                continue;
            discarded = Move(*it);
            m_workspaces.erase(it);
            break;
        }
    }
    NWB_ASSERT(discarded);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SetupUploadSameClassRouting ResolveSetupUploadSameClassRouting(GraphicsBackend::Device& device, const CommandQueue::Enum consumerQueue, const usize uploadBytes)noexcept{
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

ResourceQueueSharing::Mask ResolveSetupUploadConsumerSharing(const ResourceQueueSharing::Mask requestedSharing, const CommandQueue::Enum consumerQueue, const bool crossFamilySameClassRouting)noexcept{
    if(crossFamilySameClassRouting){
        const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive ? ResourceQueueSharing::ForQueueClass(consumerQueue) : requestedSharing;
        return static_cast<ResourceQueueSharing::Mask>(static_cast<u8>(baseSharing) | static_cast<u8>(ResourceQueueSharing::ForQueueClass(consumerQueue)));
    }

    if(consumerQueue == CommandQueue::Graphics)
        return requestedSharing;

    const ResourceQueueSharing::Mask baseSharing = requestedSharing == ResourceQueueSharing::Exclusive ? ResourceQueueSharing::Graphics : requestedSharing;
    return static_cast<ResourceQueueSharing::Mask>(static_cast<u8>(baseSharing) | static_cast<u8>(ResourceQueueSharing::ForQueueClass(consumerQueue)));
}

CommandQueue::Enum ResolveSetupUploadConsumerQueue(GraphicsBackend::Device& device, const CommandQueue::Enum requestedConsumerQueue, const usize uploadBytes, const bool hasKnownFinalState, const bool requiresGraphicsConsumerQueue)noexcept{
    if(requiresGraphicsConsumerQueue)
        return CommandQueue::Graphics;

    switch(requestedConsumerQueue){
    case CommandQueue::kCount:
        if(uploadBytes < s_SetupUploadLargeMinimumBytes || !hasKnownFinalState)
            return CommandQueue::Graphics;
        return __hidden_graphics_graph_setup::ResolveAvailableSetupConsumerQueue(device);
    case CommandQueue::Transfer:
        return hasKnownFinalState ? __hidden_graphics_graph_setup::ResolveAvailableSetupConsumerQueue(device) : CommandQueue::Graphics;
    case CommandQueue::Compute:
        return hasKnownFinalState && device.getQueue(CommandQueue::Compute) ? CommandQueue::Compute : CommandQueue::Graphics;
    case CommandQueue::Graphics:
        return CommandQueue::Graphics;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("GraphicsRuntime: setup upload requested an invalid command queue"));
        return CommandQueue::Graphics;
    }
}

GpuTaskSchedulingHint SetupUploadGraphScheduling(const usize byteCount, const bool sameClassRouting, const bool crossFamilySameClassRouting)noexcept{
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = byteCount >= s_SetupUploadLargeMinimumBytes ? GpuTaskCostHint::Large : GpuTaskCostHint::Tiny;
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
    return declaredInitialState == ResourceStates::Unknown ? ResourceStates::CopyDest : declaredInitialState;
}


Expected<QueueSubmissionToken> SubmitGraphOwnedStandaloneTask(
    const GraphicsRuntime& graphics,
    StandaloneGraphWorkspacePool& workspaces,
    void* const userData,
    const NotNull<GraphTaskDeclaration> declareTask,
    const GpuPhysicalQueueId requiredTerminalQueue,
    CpuTaskScheduler* const readyFrontierScheduler,
    GpuTimingRecorder* const timingRecorder,
    GpuTimingFrameTransaction* const frameTimingTransaction
){
    if(frameTimingTransaction && !timingRecorder)
        return MakeUnexpected(Failure{});

    auto& device = graphics.getDevice();

    // Every view and scratch owner below ends before this lease resets the workspace for the next submission.
    const StandaloneGraphWorkspacePool::Lease lease(workspaces);
    StandaloneGraphWorkspace* const workspace = lease.workspace();
    if(!workspace)
        return MakeUnexpected(Failure{});
    GpuTaskGraph& graph = workspace->graph;
    const auto declaredTerminal = declareTask.get()(userData, graph);
    if(!declaredTerminal)
        return MakeUnexpected(declaredTerminal.error());
    if(!declaredTerminal->valid())
        return MakeUnexpected(Failure{});
    const GpuTaskId terminalTask = *declaredTerminal;
    GpuTaskId frameTimingBeginTask;
    if(frameTimingTransaction){
        const GpuTaskGraph::DeclarationReadView declaredTasks(graph);
        if(!declaredTasks.valid() || declaredTasks.taskCount() < __hidden_graphics_graph_setup::s_MinFrameTimingTaskCount)
            return MakeUnexpected(Failure{});
        frameTimingBeginTask = declaredTasks.taskAt(0u).id;
        if(!frameTimingBeginTask.valid() || frameTimingBeginTask == terminalTask)
            return MakeUnexpected(Failure{});
    }
    const auto declaredRecovery = __hidden_graphics_graph_setup::DeclareStandaloneTaskGraphRecoveryTask(graph, frameTimingTransaction);
    if(!declaredRecovery)
        return MakeUnexpected(declaredRecovery.error());
    const GpuTaskId recoveryTask = *declaredRecovery;

    GpuTaskGraphAnalysis& analysis = workspace->analysis;
    GpuTaskGraphQueueAssignments& assignments = workspace->assignments;
    GpuCompiledGraph& compiledGraph = workspace->compiledGraph;
    GpuRecordedGraph& recordedGraph = workspace->recordedGraph;
    GpuGraphSubmissionTransaction& transaction = workspace->transaction;
    Alloc::ScratchArena scratchArena(__hidden_graphics_graph_setup::s_StandaloneTaskGraphScratchArena);
    const GpuTaskScheduler& scheduler = graphics.gpuTasks();
    GpuTaskGraphCompileOptions compileOptions;
    if(frameTimingTransaction){
        compileOptions.packetTimingEnvelope.firstTask = frameTimingBeginTask;
        compileOptions.packetTimingEnvelope.lastTask = terminalTask;
    }
    if(!scheduler.scheduleGraph(graph, analysis, assignments, compiledGraph, recordedGraph, transaction, scratchArena, compileOptions)){
        const auto& analysisDiagnostic = analysis.diagnostic();
        const auto& queueDiagnostic = assignments.diagnostic();
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: standalone graph scheduling failed: analysis={} task={} resource={} version={} queue={} queueTask={}")
            , static_cast<u32>(analysisDiagnostic.status), analysisDiagnostic.task.index, analysisDiagnostic.resource.index
            , analysisDiagnostic.resourceVersion.index, static_cast<u32>(queueDiagnostic.status), queueDiagnostic.task.index
        );
        return MakeUnexpected(Failure{});
    }

    const GpuTaskGraph::DeclarationReadView declarations(graph);
    const GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
    if(!declarations.valid() || !compiledPlan.validFor(declarations))
        return MakeUnexpected(Failure{});

    const GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalTask);
    const GpuSubmissionPacketId recoveryPacket = compiledPlan.packetForTask(recoveryTask);
    const GpuSubmissionPacketId frameTimingBeginPacket = frameTimingTransaction ? compiledPlan.packetForTask(frameTimingBeginTask) : GpuSubmissionPacketId{};
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(
        !terminalPacket.valid()
        || !recoveryPacket.valid()
        || !graphicsQueue.valid()
        || compiledPlan.packetCount() < __hidden_graphics_graph_setup::s_MinFrameTimingPacketCount
        || terminalPacket == recoveryPacket
        || recoveryPacket.index == 0u
        || recoveryPacket != compiledPlan.packetIdAt(compiledPlan.packetCount() - 1u)
        || !compiledPlan.taskJoinsAcceptedQueueFrontier(recoveryTask)
    )
        return MakeUnexpected(Failure{});

    const GpuCompiledPacketView terminalPacketView = compiledPlan.packet(terminalPacket);
    const GpuCompiledPacketView recoveryPacketView = compiledPlan.packet(recoveryPacket);
    if(!terminalPacketView.valid() || !recoveryPacketView.valid())
        return MakeUnexpected(Failure{});
    if(requiredTerminalQueue.valid() && terminalPacketView.plan->queue != requiredTerminalQueue)
        return MakeUnexpected(Failure{});
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
            return MakeUnexpected(Failure{});
    }

    const GpuSubmissionPacket& recoveryPacketPlan = *recoveryPacketView.plan;
    if(recoveryPacketPlan.queue != graphicsQueue || recoveryPacketPlan.dependencyCount != 0u || recoveryPacketPlan.externalDependencyCount != 0u || !recoveryPacketPlan.joinsAcceptedQueueFrontier)
        return MakeUnexpected(Failure{});

    // Setup and timing callers preserve their established serial behavior. The public standalone graph boundary supplies the Graphics worker pool
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.readyFrontierScheduler = readyFrontierScheduler;
    const bool graphAccepted = scheduler.executeGraph(graph, compiledGraph, recordedGraph, normalExecution, transaction, timingRecorder, scratchArena);
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
        const bool recovered = !transaction.hasAcceptedPackets() || scheduler.executeAcceptedFrontierTask(graph, compiledGraph, recordedGraph, recoveryTask, transaction, timingRecorder, scratchArena);
        if(recovered && frameTimingTransaction && frameTimingTransaction->needsRetirement()){
            const QueueSubmissionToken recoveryToken = transaction.taskToken(compiledPlan, recoveryTask);
            timingRecovered = frameTimingTransaction->confirmEndSubmission(recoveryToken, false) && timingRecovered;
        }
        if(!timingRecovered){
            NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: standalone frame timing could not be retired after graph rejection"));
            frameTimingTransaction->discard();
        }
        const bool discarded = transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration());
        if(!recovered || !discarded)
            graphics.requestDeviceRecreation();
        return MakeUnexpected(Failure{});
    }

    const bool discarded = transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration());
    const bool timingConfirmed = !frameTimingTransaction || frameTimingTransaction->confirmEndSubmission(terminalToken, discarded && terminalToken.valid());
    if(!timingConfirmed){
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: standalone frame timing confirmation failed"));
        frameTimingTransaction->discard();
    }
    if(!discarded || !terminalToken.valid()){
        graphics.requestDeviceRecreation();
        return MakeUnexpected(Failure{});
    }
    return terminalToken;
}

bool SubmitGraphOwnedSetupUpload(
    const GraphicsRuntime& graphics,
    StandaloneGraphWorkspacePool& workspaces,
    const ResourceQueueSharing::Mask queueSharing,
    const CommandQueue::Enum consumerQueue,
    void* const userData,
    const NotNull<GraphTaskDeclaration> declareTask,
    QueueSubmissionToken& inOutUploadToken,
    const GpuPhysicalQueueId requiredTerminalQueue
){
    inOutUploadToken = {};

    auto& device = graphics.getDevice();
    __hidden_graphics_graph_setup::SetupUploadSubmissionData submissionData{ .device = device, .userData = userData, .declareTask = declareTask, .queueSharing = queueSharing, .consumerQueue = consumerQueue, };
    const auto terminalToken = SubmitGraphOwnedStandaloneTask(graphics, workspaces, &submissionData, MakeNotNull(&__hidden_graphics_graph_setup::DeclareSetupUploadGraph), requiredTerminalQueue);
    if(!terminalToken)
        return false;

    if(!inOutUploadToken.valid()){
        // The producer's accepted callback supplies the public token. A successful bridge graph without it breaks the setup API contract, so reject it.
        return false;
    }
    return true;
}

bool SubmitGraphOwnedFrameTimingReset(const GraphicsRuntime& graphics, StandaloneGraphWorkspacePool& workspaces, GpuTimingRecorder& timing){
    auto& device = graphics.getDevice();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    if(!graphicsQueue.valid())
        return false;

    __hidden_graphics_graph_setup::FrameTimingResetSubmissionData submissionData{ .timing = timing, };
    const auto acceptedToken = SubmitGraphOwnedStandaloneTask(graphics, workspaces, &submissionData, MakeNotNull(&__hidden_graphics_graph_setup::DeclareFrameTimingResetGraph), graphicsQueue);
    if(!acceptedToken)
        return false;

    return acceptedToken->queue == CommandQueue::Graphics && acceptedToken->matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<QueueSubmissionToken> GraphicsRuntime::submitStandaloneTaskGraph(
    void* const userData,
    const StandaloneTaskGraphDeclaration declareTask,
    const GpuPhysicalQueueId requiredTerminalQueue,
    GpuTimingRecorder* const timingRecorder,
    GpuTimingFrameTransaction* const frameTimingTransaction
)const{
    if(!declareTask)
        return MakeUnexpected(Failure{});

    return GraphicsModuleDetail::SubmitGraphOwnedStandaloneTask(*this, *m_standaloneGraphWorkspaces, userData, MakeNotNull(declareTask), requiredTerminalQueue, &m_cpuScheduler, timingRecorder, frameTimingTransaction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

