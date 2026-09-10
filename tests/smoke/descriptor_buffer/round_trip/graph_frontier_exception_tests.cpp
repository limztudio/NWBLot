// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "parallel_recording_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ParallelRecordingException = 0xE101u;


struct ParallelRecordingExceptionState{
    CpuTaskScheduler& workers;
    Latch recordingStarted{ 3u };
    Atomic<u32> discardCount{ 0u };
    Atomic<u32> activeCallbacks{ 0u };
    GpuSubmissionPacketId callerPacket;
    GpuSubmissionPacketId successfulWorkerPacket;
    GpuSubmissionPacketId failedWorkerPacket;
    bool throwOnWorker = false;

    explicit ParallelRecordingExceptionState(CpuTaskScheduler& recordingWorkers, const bool workerThrows = false)noexcept
        : workers(recordingWorkers)
        , throwOnWorker(workerThrows)
    {}
};


struct ParallelRecordingExceptionTask{
    struct Payload{
        ParallelRecordingExceptionState* state = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(!payload.state || !commandList.isRecording())
            return false;
        ParallelRecordingExceptionState& state = *payload.state;
        state.activeCallbacks.fetch_add(1u, MemoryOrder::relaxed);
        ScopeExit finishCallback([&]()noexcept{ state.activeCallbacks.fetch_sub(1u, MemoryOrder::relaxed); });

        const usize workerIndex = state.workers.currentWorkerIndex();
        if(workerIndex == 0u)
            state.callerPacket = context.packet;
        else if(workerIndex == 1u)
            state.successfulWorkerPacket = context.packet;
        else
            state.failedWorkerPacket = context.packet;
        state.recordingStarted.count_down();
        state.recordingStarted.wait();
        if(workerIndex == (state.throwOnWorker ? 1u : 0u))
            throw s_ParallelRecordingException;
        return workerIndex == 1u && commandList.isRecording();
    }

    static void discarded(Payload& payload)noexcept{
        if(payload.state)
            payload.state->discardCount.fetch_add(1u, MemoryOrder::relaxed);
    }
};


inline constexpr u32 s_RecordedCallbackException = 0xE106u;


// The accepted-frontier helper must resolve the target even when its transaction has no accepted prefix. This probe
// records whether an invalid implementation accidentally advances into the native recording path.
struct NativeAcceptedFrontierWithoutPrefixTask{
    struct Payload{
        bool* recorded = nullptr;
        u32* discardedCount = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext&
    ){
        if(payload.recorded)
            *payload.recorded = commandList.isRecording();
        return commandList.isRecording();
    }

    static void discarded(Payload& payload)noexcept{
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierCallerExceptionDrainsTimedClaimsWithoutInvokingDiscardObservers){
    auto& device = DescriptorBufferRoundTripTest::device();
    CpuTaskScheduler recordingWorkers(2u);
    ParallelRecordingExceptionState state(recordingWorkers);
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;
    const GpuQueueRequest queueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const auto addTask = [&](const Name& identity, const AStringView label){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queueRequest)
            .setScheduling(scheduling)
            .setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::PacketOnly })
        ;
        return graph.addTask<ParallelRecordingExceptionTask>(
            desc,
            ParallelRecordingExceptionTask::Payload{
                .state = &state,
            }
        );
    };
    const GpuTaskId firstTask = addTask(
        Name("tests/descriptor_buffer/parallel_recording_exception_first"),
        "Parallel Recording Exception First"
    );
    const GpuTaskId secondTask = addTask(
        Name("tests/descriptor_buffer/parallel_recording_exception_second"),
        "Parallel Recording Exception Second"
    );
    const GpuTaskId thirdTask = addTask(
        Name("tests/descriptor_buffer/parallel_recording_exception_third"),
        "Parallel Recording Exception Third"
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());
    ASSERT_TRUE(thirdTask.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(GpuQueueCapability::Graphics),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/parallel_recording_exception_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId firstPacket;
    GpuSubmissionPacketId failedPacket;
    GpuSubmissionPacketId thirdPacket;
    GpuSubmissionPacketRange packetRange;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        ASSERT_EQ(views.compiled.packetCount(), 3u);
        firstPacket = views.compiled.packetForTask(firstTask);
        failedPacket = views.compiled.packetForTask(secondTask);
        thirdPacket = views.compiled.packetForTask(thirdTask);
        ASSERT_TRUE(firstPacket.valid());
        ASSERT_TRUE(failedPacket.valid());
        ASSERT_TRUE(thirdPacket.valid());
        const GpuCompiledPacketView firstPacketView = views.compiled.packet(firstPacket);
        const GpuCompiledPacketView failedPacketView = views.compiled.packet(failedPacket);
        const GpuCompiledPacketView thirdPacketView = views.compiled.packet(thirdPacket);
        ASSERT_TRUE(firstPacketView.valid());
        ASSERT_TRUE(failedPacketView.valid());
        ASSERT_TRUE(thirdPacketView.valid());
        ASSERT_TRUE(firstPacketView.plan->recordsTiming);
        ASSERT_TRUE(failedPacketView.plan->recordsTiming);
        ASSERT_TRUE(thirdPacketView.plan->recordsTiming);
        ASSERT_EQ(firstPacketView.plan->recordingFrontier, 0u);
        ASSERT_EQ(failedPacketView.plan->recordingFrontier, 0u);
        ASSERT_EQ(thirdPacketView.plan->recordingFrontier, 0u);
        packetRange = views.compiled.allPacketRange();
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    s_scope->setGpuTimingEnabled(true);
    const GpuNativePacketRecorder recorder(device, s_scope->graphics().gpuTiming());
    GpuSubmissionPacketId reportedFailedPacket;
    bool exceptionObserved = false;
    try{
        const bool frontierRecorded = recorder.recordPacketRangeInReadyFrontiers(
            graph,
            compiledGraph,
            packetRange,
            recordedGraph,
            recordingWorkers,
            &reportedFailedPacket
        );
        EXPECT_FALSE(frontierRecorded);
    }
    catch(const u32 exception){
        exceptionObserved = exception == s_ParallelRecordingException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_FALSE(reportedFailedPacket.valid());
    EXPECT_EQ(state.discardCount.load(MemoryOrder::relaxed), 0u);
    EXPECT_EQ(state.activeCallbacks.load(MemoryOrder::relaxed), 0u);
    ASSERT_TRUE(state.callerPacket.valid());
    ASSERT_TRUE(state.successfulWorkerPacket.valid());
    ASSERT_TRUE(state.failedWorkerPacket.valid());
    EXPECT_TRUE(recordedGraph.packetSnapshot(state.successfulWorkerPacket).has_value());
    EXPECT_FALSE(recordedGraph.packetSnapshot(state.failedWorkerPacket).has_value());
    EXPECT_FALSE(recordedGraph.packetSnapshot(state.callerPacket).has_value());

    EXPECT_FALSE(recordedGraph.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
    EXPECT_EQ(state.discardCount.load(MemoryOrder::relaxed), 1u);
    recordedGraph.reset(compiledGraph);
    s_scope->setGpuTimingEnabled(false);
}


// A composite owns the complete graph attempt, including the caller claim that throws. Its unwind path
// must consume successful, false, and throwing packet states without invoking any discard observer.
TEST_F(DescriptorBufferRoundTripTest, CompositeReadyFrontierCallerExceptionResolvesWholeAttemptWithoutDiscardCallbacks){
    auto& device = DescriptorBufferRoundTripTest::device();
    CpuTaskScheduler recordingWorkers(2u);
    ParallelRecordingExceptionState state(recordingWorkers);
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;
    const GpuQueueRequest queueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const auto addTask = [&](const Name& identity, const AStringView label){
        return graph.addTask<ParallelRecordingExceptionTask>(
            GpuTaskDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setQueue(queueRequest)
                .setScheduling(scheduling),
            ParallelRecordingExceptionTask::Payload{
                .state = &state,
            }
        );
    };
    const GpuTaskId firstTask = addTask(
        Name("tests/descriptor_buffer/composite_parallel_exception_first"),
        "Composite Parallel Exception First"
    );
    const GpuTaskId secondTask = addTask(
        Name("tests/descriptor_buffer/composite_parallel_exception_second"),
        "Composite Parallel Exception Second"
    );
    const GpuTaskId thirdTask = addTask(
        Name("tests/descriptor_buffer/composite_parallel_exception_third"),
        "Composite Parallel Exception Third"
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());
    ASSERT_TRUE(thirdTask.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(GpuQueueCapability::Graphics),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/composite_parallel_exception_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, compileScratch));
    }
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        ASSERT_EQ(views.compiled.packetCount(), 3u);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskScheduler submitter(device);
    Alloc::ScratchArena submissionScratch(Name("tests/descriptor_buffer/composite_parallel_exception_submission"));
    GpuSubmissionPacketId failedPacket;
    bool exceptionObserved = false;
    try{
        const bool submitted = submitter.recordAndSubmitTaskRangeInReadyFrontiers(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            recordingWorkers,
            firstTask,
            thirdTask,
            transaction,
            submissionScratch,
            &failedPacket
        );
        EXPECT_FALSE(submitted);
    }
    catch(const u32 exception){
        exceptionObserved = exception == s_ParallelRecordingException;
    }

    EXPECT_TRUE(exceptionObserved);
    EXPECT_TRUE(failedPacket.valid());
    EXPECT_EQ(state.activeCallbacks.load(MemoryOrder::relaxed), 0u);
    EXPECT_EQ(state.discardCount.load(MemoryOrder::relaxed), 0u);
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.rejectedPacketCount, 3u);
    EXPECT_EQ(statistics.rejectedTaskCount, 3u);
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        for(usize packetIndex = 0u; packetIndex < views.compiled.packetCount(); ++packetIndex)
            EXPECT_FALSE(transaction.packetToken(views.compiled.packetIdAt(packetIndex)).valid());
    }

    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    recordedGraph.reset(compiledGraph);
    EXPECT_TRUE(graph.tryReset());
    EXPECT_EQ(state.discardCount.load(MemoryOrder::relaxed), 0u);
}


// The child constructs its own graph and recording pool. The three-way barrier makes the throwing callback belong
// to native worker one, so this proves process termination without relying on caller-side exception forwarding.
TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierWorkerExceptionIsTerminal){
    const auto recordWithFailingWorker = [](){
        auto& device = DescriptorBufferRoundTripTest::device();
        CpuTaskScheduler recordingWorkers(2u);
        ParallelRecordingExceptionState state(recordingWorkers, true);
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        GpuTaskSchedulingHint scheduling;
        scheduling.cost = GpuTaskCostHint::Medium;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        scheduling.allowParallelRecording = true;
        const GpuQueueRequest queueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        };
        const auto addTask = [&](const Name& identity, const AStringView label){
            return graph.addTask<ParallelRecordingExceptionTask>(
                GpuTaskDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(label)
                    .setQueue(queueRequest)
                    .setScheduling(scheduling),
                ParallelRecordingExceptionTask::Payload{ .state = &state }
            );
        };
        const GpuTaskId firstTask = addTask(
            Name("tests/descriptor_buffer/terminal_worker_first"),
            "Terminal Worker First"
        );
        const GpuTaskId secondTask = addTask(
            Name("tests/descriptor_buffer/terminal_worker_second"),
            "Terminal Worker Second"
        );
        const GpuTaskId thirdTask = addTask(
            Name("tests/descriptor_buffer/terminal_worker_third"),
            "Terminal Worker Third"
        );
        ASSERT_TRUE(firstTask.valid());
        ASSERT_TRUE(secondTask.valid());
        ASSERT_TRUE(thirdTask.valid());

        const GpuPhysicalQueueInfo graphicsQueue{
            .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
            .queueIndex = 0u,
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(GpuQueueCapability::Graphics),
            .dedicated = false,
        };
        const GpuTaskGraphQueueTopology topology{
            .queues = &graphicsQueue,
            .queueCount = 1u,
        };
        GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
        GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
        GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
        Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/terminal_worker_scratch"));
        const GpuTaskGraphCompiler compiler;
        {
            const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
            ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
        }
        GpuSubmissionPacketRange packetRange;
        {
            const GpuTaskGraphReadViews views(graph, compiledGraph);
            ASSERT_EQ(views.compiled.packetCount(), 3u);
            for(usize packetIndex = 0u; packetIndex < views.compiled.packetCount(); ++packetIndex){
                const GpuCompiledPacketView packet = views.compiled.packet(views.compiled.packetIdAt(packetIndex));
                ASSERT_TRUE(packet.valid());
                ASSERT_EQ(packet.plan->recordingFrontier, 0u);
            }
            packetRange = views.compiled.allPacketRange();
        }

        GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
        const GpuNativePacketRecorder recorder(device);
        const bool recorded = recorder.recordPacketRangeInReadyFrontiers(
            graph,
            compiledGraph,
            packetRange,
            recordedGraph,
            recordingWorkers
        );
        EXPECT_FALSE(recorded);
    };
    EXPECT_DEATH_IF_SUPPORTED(recordWithFailingWorker(), "");
}


// A recorded callback runs after every packet is published but before native submission. Throwing there must
// terminalize the complete attempt without replacing the callback exception with either task's discard observer.
TEST_F(DescriptorBufferRoundTripTest, CompositeRecordedCallbackExceptionResolvesWholeAttemptWithoutObserverReplay){
    auto& device = DescriptorBufferRoundTripTest::device();
    DiscardObserverExceptionState state;
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest queueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskId firstTask = graph.addTask<DiscardObserverExceptionTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recorded_callback_exception_first"))
            .setMarkerLabel("Recorded Callback Exception First")
            .setQueue(queueRequest)
            .setScheduling(scheduling),
        DiscardObserverExceptionTask::Payload(state)
    );
    const GpuTaskId secondTask = graph.addTask<DiscardObserverExceptionTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recorded_callback_exception_second"))
            .setMarkerLabel("Recorded Callback Exception Second")
            .setQueue(queueRequest)
            .setScheduling(scheduling),
        DiscardObserverExceptionTask::Payload(state)
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/recorded_callback_exception_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId firstPacket;
    GpuSubmissionPacketId secondPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        ASSERT_EQ(views.compiled.packetCount(), 2u);
        firstPacket = views.compiled.packetForTask(firstTask);
        secondPacket = views.compiled.packetForTask(secondTask);
        ASSERT_TRUE(firstPacket.valid());
        ASSERT_TRUE(secondPacket.valid());
    }

    const auto throwRecordedCallback = [](void*, const CommandListResourceStateHandoff*) -> bool {
        throw s_RecordedCallbackException;
    };
    const GpuTaskGraphTaskRecordedCallback callback{
        .task = firstTask,
        .invoke = throwRecordedCallback,
    };
    GpuTaskGraphNormalExecutionDesc execution;
    execution.taskRecordedCallbacks = &callback;
    execution.taskRecordedCallbackCount = 1u;
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskScheduler submitter(device);
    Alloc::ScratchArena submissionScratch(Name("tests/descriptor_buffer/recorded_callback_exception_submission"));
    GpuSubmissionPacketId failedPacket;
    bool exceptionObserved = false;
    try{
        const bool submitted = submitter.submit(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            execution,
            transaction,
            submissionScratch,
            &failedPacket
        );
        EXPECT_FALSE(submitted);
    }
    catch(const u32 exception){
        exceptionObserved = exception == s_RecordedCallbackException;
    }

    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(failedPacket, firstPacket);
    EXPECT_TRUE(recordedGraph.packetSnapshot(firstPacket).has_value());
    EXPECT_TRUE(recordedGraph.packetSnapshot(secondPacket).has_value());
    EXPECT_EQ(state.discardCount, 0u);
    EXPECT_EQ(state.destructionCount, 0u);
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.rejectedPacketCount, 2u);
    EXPECT_EQ(statistics.rejectedTaskCount, 2u);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    recordedGraph.reset(compiledGraph);
    EXPECT_TRUE(graph.tryReset());
    EXPECT_EQ(state.discardCount, 0u);
    EXPECT_EQ(state.destructionCount, 2u);
}


// An accepted-frontier request without an accepted prefix is invalid, but preparing the helper already binds a graph
// recording attempt. The false result must reject that task and close the binding before returning to its caller.
TEST_F(DescriptorBufferRoundTripTest, AcceptedFrontierWithoutAcceptedPrefixRejectsBoundAttempt){
    auto& device = DescriptorBufferRoundTripTest::device();
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.joinsAcceptedQueueFrontier = true;
    scheduling.isRecoverySubmission = true;
    bool recorded = false;
    u32 discardedCount = 0u;
    const GpuTaskId task = graph.addTask<NativeAcceptedFrontierWithoutPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/frontier_without_accepted_prefix"))
            .setMarkerLabel("Frontier Without Accepted Prefix")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling),
        NativeAcceptedFrontierWithoutPrefixTask::Payload{
            .recorded = &recorded,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/frontier_without_accepted_prefix_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(
            compilationDeclarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            scratchArena
        ));
    }

    GpuSubmissionPacketId packet;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 1u);
        packet = views.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());
        EXPECT_TRUE(views.compiled.packet(packet).plan->joinsAcceptedQueueFrontier);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskScheduler submitter(device);
    GpuSubmissionPacketId failedPacket;
    EXPECT_FALSE(submitter.recordAndSubmitAcceptedFrontierTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        task,
        transaction,
        scratchArena,
        &failedPacket
    ));

    EXPECT_EQ(failedPacket, packet);
    EXPECT_FALSE(recorded);
    EXPECT_EQ(discardedCount, 1u);
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.acceptedPacketCount, 0u);
    EXPECT_EQ(statistics.nativeSubmissionCount, 0u);
    EXPECT_EQ(statistics.rejectedPacketCount, 1u);
    EXPECT_EQ(statistics.rejectedTaskCount, 1u);
    EXPECT_EQ(statistics.rejectedSubmissionCount, 0u);
    EXPECT_EQ(statistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_EQ(statistics.recoverySubmissionCount, 0u);
    bool transactionReset = transaction.tryReset(compiledGraph);
    EXPECT_TRUE(transactionReset);
    if(!transactionReset){
        EXPECT_TRUE(transaction.discardUnaccepted(graph, compiledGraph, recordedGraph.recordingAttemptGeneration()));
        transactionReset = transaction.tryReset(compiledGraph);
        EXPECT_TRUE(transactionReset);
    }
    recordedGraph.reset(compiledGraph);
    EXPECT_TRUE(graph.tryReset());
}


// The accepted-frontier helper may reject before it initializes a recorded artifact. Once its discard observer
// throws, the composite still owns the bound attempt and must callback-free close every other declared packet.
TEST_F(DescriptorBufferRoundTripTest, AcceptedFrontierRejectionExceptionResolvesOtherDeclaredPackets){
    auto& device = DescriptorBufferRoundTripTest::device();
    DiscardObserverExceptionState state;
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest queueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskId rejectedTask = graph.addTask<DiscardObserverExceptionTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/frontier_rejection_exception_target"))
            .setMarkerLabel("Frontier Rejection Exception Target")
            .setQueue(queueRequest)
            .setScheduling(scheduling),
        DiscardObserverExceptionTask::Payload(state)
    );
    const GpuTaskId remainingTask = graph.addTask<DiscardObserverExceptionTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/frontier_rejection_exception_remaining"))
            .setMarkerLabel("Frontier Rejection Exception Remaining")
            .setQueue(queueRequest)
            .setScheduling(scheduling),
        DiscardObserverExceptionTask::Payload(state)
    );
    ASSERT_TRUE(rejectedTask.valid());
    ASSERT_TRUE(remainingTask.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/frontier_rejection_exception_compile"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    GpuSubmissionPacketId rejectedPacket;
    GpuSubmissionPacketId remainingPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        ASSERT_EQ(views.compiled.packetCount(), 2u);
        rejectedPacket = views.compiled.packetForTask(rejectedTask);
        remainingPacket = views.compiled.packetForTask(remainingTask);
        ASSERT_TRUE(rejectedPacket.valid());
        ASSERT_TRUE(remainingPacket.valid());
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskScheduler submitter(device);
    Alloc::ScratchArena submissionScratch(Name("tests/descriptor_buffer/frontier_rejection_exception_submission"));
    GpuSubmissionPacketId failedPacket;
    bool exceptionObserved = false;
    try{
        const bool submitted = submitter.recordAndSubmitAcceptedFrontierTask(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            rejectedTask,
            transaction,
            submissionScratch,
            &failedPacket
        );
        EXPECT_FALSE(submitted);
    }
    catch(const u32 exception){
        exceptionObserved = exception == s_DiscardObserverException;
    }

    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(failedPacket, rejectedPacket);
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_EQ(state.discardCount, 1u);
    EXPECT_EQ(state.destructionCount, 0u);
    EXPECT_FALSE(transaction.packetToken(rejectedPacket).valid());
    EXPECT_FALSE(transaction.packetToken(remainingPacket).valid());
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.rejectedPacketCount, 2u);
    EXPECT_EQ(statistics.rejectedTaskCount, 2u);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    recordedGraph.reset(compiledGraph);
    EXPECT_TRUE(graph.tryReset());
    EXPECT_EQ(state.discardCount, 1u);
    EXPECT_EQ(state.destructionCount, 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

