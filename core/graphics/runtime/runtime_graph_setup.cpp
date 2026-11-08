// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_internal.h"
#include "runtime_standalone_graph.h"

#include <core/graphics/internal_names.adhd.h>
#include <core/graphics/runtime/runtime_values.adhd.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_graph_setup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GraphicsInternalNames::s_StandaloneTaskGraphRecoveryIdentity;
using GraphicsInternalNames::s_StandaloneTaskGraphScratchArena;
inline constexpr u32 s_MinFrameTimingTaskCount = GraphicsRuntimeValues::s_MinFrameTimingTaskCount;
inline constexpr u32 s_MinFrameTimingPacketCount = GraphicsRuntimeValues::s_MinFrameTimingPacketCount;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A standalone graph owns no renderer finalization packet.
// Predeclare this no-op Graphics tail so a later rejection can join every accepted physical queue before this call returns.
// The plain variant covers graphs without frame timing, so no task carries a nullable pointer with a silent no-op guard.
struct StandaloneTaskGraphRecoveryTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::None, true };

    struct Payload{
    };


public:
    [[nodiscard]] static b8 Record([[maybe_unused]] const Payload& payload, [[maybe_unused]] CommandList& commandList, [[maybe_unused]] const GpuTaskRecordContext& context)noexcept{
        return true;
    }
};

struct StandaloneTaskGraphTimingRetirementTask{
    inline static constexpr GpuTaskCommandRequirements s_CommandRequirements{ GpuQueueCapability::None, true };

    struct Payload{
        GpuTimingFrameTransaction& frameTimingTransaction;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, CommandList& commandList, [[maybe_unused]] const GpuTaskRecordContext& context){
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




Expected<QueueSubmissionToken> SubmitGraphOwnedStandaloneTask(
    const GraphicsRuntime& graphics,
    const NotNull<GraphTaskDeclaration> declareTask,
    void* const userData,
    StandaloneGraphWorkspacePool& workspaces,
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
    if(!scheduler.scheduleGraph(graph, compileOptions, scratchArena, analysis, assignments, compiledGraph, recordedGraph, transaction)){
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
    if(!compiledPlan.validFor(declarations))
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

    // Setup and timing callers preserve their established serial behavior. The public standalone graph boundary supplies the Graphics worker pool.
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.readyFrontierScheduler = readyFrontierScheduler;
    const b8 graphAccepted = scheduler.executeGraph(normalExecution, scratchArena, graph, compiledGraph, recordedGraph, transaction, timingRecorder);
    const QueueSubmissionToken terminalToken = transaction.taskToken(compiledPlan, terminalTask);
    if(!graphAccepted){
        b8 timingRecovered = true;
        if(frameTimingTransaction){
            if(terminalToken.valid())
                timingRecovered = frameTimingTransaction->confirmEndSubmission(terminalToken, false);
            else if(frameTimingTransaction->needsRetirement())
                timingRecovered = frameTimingTransaction->prepareForRecovery();
            if(!timingRecovered)
                frameTimingTransaction->discard();
        }
        const b8 recovered = !transaction.hasAcceptedPackets() || scheduler.executeAcceptedFrontierTask(compiledGraph, recoveryTask, scratchArena, graph, recordedGraph, transaction, timingRecorder);
        if(recovered && frameTimingTransaction && frameTimingTransaction->needsRetirement()){
            const QueueSubmissionToken recoveryToken = transaction.taskToken(compiledPlan, recoveryTask);
            timingRecovered = frameTimingTransaction->confirmEndSubmission(recoveryToken, false) && timingRecovered;
        }
        if(!timingRecovered){
            NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: standalone frame timing could not be retired after graph rejection"));
            frameTimingTransaction->discard();
        }
        const b8 discarded = transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration());
        if(!recovered || !discarded)
            graphics.requestDeviceRecreation();
        return MakeUnexpected(Failure{});
    }

    const b8 discarded = transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration());
    const b8 timingConfirmed = !frameTimingTransaction || frameTimingTransaction->confirmEndSubmission(terminalToken, discarded && terminalToken.valid());
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<QueueSubmissionToken> GraphicsStandaloneService::submitStandaloneTaskGraph(
    void* const userData,
    const StandaloneTaskGraphDeclaration declareTask,
    const GpuPhysicalQueueId requiredTerminalQueue,
    GpuTimingRecorder* const timingRecorder,
    GpuTimingFrameTransaction* const frameTimingTransaction
)const{
    if(!declareTask)
        return MakeUnexpected(Failure{});

    return GraphicsModuleDetail::SubmitGraphOwnedStandaloneTask(m_graphics, MakeNotNull(declareTask), userData, m_workspaces, requiredTerminalQueue, &m_cpuScheduler, timingRecorder, frameTimingTransaction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

