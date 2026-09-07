// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "acceptance_observers_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ThrowNativeTaskAcceptance(
    void* const rawContext,
    const QueueSubmissionToken& token
){
    NativeTaskAcceptanceObserver* const context = static_cast<NativeTaskAcceptanceObserver*>(rawContext);
    if(!context || !token.valid())
        return false;
    ++context->acceptedCount;
    context->lastToken = token;
    throw 0xA11CEu;
}


// One recording attempt has one submission-transaction owner. A second transaction cannot split its accepted
// queue frontier, cannot discard it, and cannot invoke a task record callback through a composite execution path.
// Refusing an active reset preserves every accepted token and publication revision until all packets resolve.
TEST_F(DescriptorBufferRoundTripTest, GraphAttemptHasOneSubmissionTransactionOwner){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool firstShouldRecord = true;
    bool firstRecorded = false;
    const GpuTaskId firstTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/transaction_owner_first"))
            .setMarkerLabel("Transaction Owner First")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &firstShouldRecord,
            .attempted = &firstRecorded,
        }
    );
    bool secondShouldRecord = true;
    bool secondRecorded = false;
    const GpuTaskId secondTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/transaction_owner_second"))
            .setMarkerLabel("Transaction Owner Second")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &secondShouldRecord,
            .attempted = &secondRecorded,
        }
    );
    bool thirdShouldRecord = true;
    bool thirdRecorded = false;
    const GpuTaskId thirdTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/transaction_owner_third"))
            .setMarkerLabel("Transaction Owner Third")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &thirdShouldRecord,
            .attempted = &thirdRecorded,
        }
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());
    ASSERT_TRUE(thirdTask.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/transaction_owner_compile_scratch"));
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
    GpuSubmissionPacketId thirdPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        ASSERT_EQ(views.compiled.packetCount(), 3u);
        firstPacket = views.compiled.packetForTask(firstTask);
        secondPacket = views.compiled.packetForTask(secondTask);
        thirdPacket = views.compiled.packetForTask(thirdTask);
        ASSERT_TRUE(firstPacket.valid());
        ASSERT_TRUE(secondPacket.valid());
        ASSERT_TRUE(thirdPacket.valid());
        ASSERT_NE(firstPacket, secondPacket);
        ASSERT_NE(firstPacket, thirdPacket);
        ASSERT_NE(secondPacket, thirdPacket);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        firstTask,
        secondTask,
        recordedGraph
    ));
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);
    EXPECT_FALSE(thirdRecorded);

    GpuGraphSubmissionTransaction ownerTransaction(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction competingTransaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(ownerTransaction.tryReset(compiledGraph));
    ASSERT_TRUE(competingTransaction.tryReset(compiledGraph));
    const GpuTaskGraphSubmitter submitter(device);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    Alloc::ScratchArena firstSubmissionScratch(
        Name("tests/descriptor_buffer/transaction_owner_first_submission_scratch")
    );
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        firstTask,
        firstTask,
        nullptr,
        0u,
        nullptr,
        0u,
        ownerTransaction,
        firstSubmissionScratch
    ));
    ASSERT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    const QueueSubmissionToken firstToken = ownerTransaction.packetToken(firstPacket);
    ASSERT_TRUE(firstToken.valid());
    u64 graphGenerationBeforeRefusedReset = 0u;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        graphGenerationBeforeRefusedReset = declarations.generation();
    }
    EXPECT_FALSE(graph.tryReset());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.generation(), graphGenerationBeforeRefusedReset);
        EXPECT_TRUE(declarations.validTask(firstTask));
        EXPECT_TRUE(declarations.validTask(secondTask));
        EXPECT_TRUE(declarations.validTask(thirdTask));
    }
    u64 planGenerationBeforeRefusedReset = 0u;
    usize packetCountBeforeRefusedReset = 0u;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());

        planGenerationBeforeRefusedReset = views.compiled.planGeneration();
        packetCountBeforeRefusedReset = views.compiled.packetCount();
    }
    EXPECT_FALSE(compiledGraph.tryReset());
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        EXPECT_FALSE(compiler.compile(compilationDeclarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            compileScratch
        ));
    }
    EXPECT_EQ(analysis.diagnostic().status, GpuTaskGraphAnalysisStatus::OutputPlanInUse);
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());

        EXPECT_EQ(views.compiled.planGeneration(), planGenerationBeforeRefusedReset);
        EXPECT_EQ(views.compiled.packetCount(), packetCountBeforeRefusedReset);
        EXPECT_EQ(views.compiled.packetForTask(firstTask), firstPacket);
        EXPECT_EQ(views.compiled.packetForTask(secondTask), secondPacket);
        EXPECT_EQ(views.compiled.packetForTask(thirdTask), thirdPacket);
    }

    Alloc::ScratchArena competingSubmissionScratch(
        Name("tests/descriptor_buffer/transaction_owner_competing_submission_scratch")
    );
    GpuSubmissionPacketId competingFailedPacket;
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        secondTask,
        secondTask,
        nullptr,
        0u,
        nullptr,
        0u,
        competingTransaction,
        competingSubmissionScratch,
        &competingFailedPacket
    ));
    EXPECT_EQ(competingFailedPacket, secondPacket);
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    EXPECT_FALSE(competingTransaction.packetToken(secondPacket).valid());

    Alloc::ScratchArena competingCompositeScratch(
        Name("tests/descriptor_buffer/transaction_owner_competing_composite_scratch")
    );
    EXPECT_FALSE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        thirdTask,
        nullptr,
        competingTransaction,
        competingCompositeScratch
    ));
    EXPECT_FALSE(thirdRecorded);
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    EXPECT_TRUE(competingTransaction.tryReset(compiledGraph));

    QueueSubmissionToken tokensBeforeRefusedReset[3u];
    GpuGraphSubmissionAcceptanceSnapshot snapshotBeforeRefusedReset;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());

        ASSERT_TRUE(ownerTransaction.copyAcceptedPacketTokens(
            views.compiled,
            tokensBeforeRefusedReset,
            LengthOf(tokensBeforeRefusedReset),
            snapshotBeforeRefusedReset
        ));
    }
    ASSERT_TRUE(tokensBeforeRefusedReset[firstPacket.index].valid());
    EXPECT_FALSE(tokensBeforeRefusedReset[secondPacket.index].valid());
    EXPECT_FALSE(tokensBeforeRefusedReset[thirdPacket.index].valid());
    const GpuTaskGraphSubmissionStatistics statisticsBeforeRefusedReset = ownerTransaction.submissionStatistics();
    ASSERT_EQ(statisticsBeforeRefusedReset.acceptedPacketCount, 1u);

    EXPECT_FALSE(ownerTransaction.tryReset(compiledGraph));

    QueueSubmissionToken tokensAfterRefusedReset[3u];
    GpuGraphSubmissionAcceptanceSnapshot snapshotAfterRefusedReset;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());

        ASSERT_TRUE(ownerTransaction.copyAcceptedPacketTokens(
            views.compiled,
            tokensAfterRefusedReset,
            LengthOf(tokensAfterRefusedReset),
            snapshotAfterRefusedReset
        ));
    }
    EXPECT_EQ(snapshotAfterRefusedReset.recordingAttemptGeneration, snapshotBeforeRefusedReset.recordingAttemptGeneration);
    EXPECT_EQ(snapshotAfterRefusedReset.acceptanceRevision, snapshotBeforeRefusedReset.acceptanceRevision);
    EXPECT_EQ(tokensAfterRefusedReset[firstPacket.index].value, firstToken.value);
    EXPECT_EQ(ownerTransaction.submissionStatistics().acceptedPacketCount, 1u);

    Alloc::ScratchArena secondSubmissionScratch(
        Name("tests/descriptor_buffer/transaction_owner_second_submission_scratch")
    );
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        secondTask,
        secondTask,
        nullptr,
        0u,
        nullptr,
        0u,
        ownerTransaction,
        secondSubmissionScratch
    ));
    Alloc::ScratchArena thirdSubmissionScratch(
        Name("tests/descriptor_buffer/transaction_owner_third_submission_scratch")
    );
    ASSERT_TRUE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        thirdTask,
        nullptr,
        ownerTransaction,
        thirdSubmissionScratch
    ));
    EXPECT_TRUE(thirdRecorded);
    EXPECT_FALSE(submissionObserver.overflowed());
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 3u);
    EXPECT_EQ(ownerTransaction.submissionStatistics().acceptedPacketCount, 3u);
    EXPECT_TRUE(ownerTransaction.packetToken(secondPacket).valid());
    EXPECT_TRUE(ownerTransaction.packetToken(thirdPacket).valid());
    EXPECT_FALSE(competingTransaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));

    EXPECT_TRUE(ownerTransaction.tryReset(compiledGraph));
    EXPECT_FALSE(ownerTransaction.packetToken(firstPacket).valid());
    EXPECT_FALSE(ownerTransaction.packetToken(secondPacket).valid());
    EXPECT_FALSE(ownerTransaction.packetToken(thirdPacket).valid());
    EXPECT_EQ(ownerTransaction.submissionStatistics().acceptedPacketCount, 0u);
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
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());

        EXPECT_NE(views.compiled.planGeneration(), planGenerationBeforeRefusedReset);
    }
    EXPECT_TRUE(graph.tryReset());
    EXPECT_TRUE(device.waitForIdle());
}


// An accepted observer is outside normal failure control flow. Its packet must retain the irreversible native token,
// while later work becomes terminal without invoking more user callbacks before the same exception propagates.
TEST_F(DescriptorBufferRoundTripTest, AcceptedObserverExceptionPreservesTokenAndResolvesRemainingAttempt){
    auto& device = DescriptorBufferRoundTripTest::device();
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool shouldRecord = true;
    bool firstRecorded = false;
    const GpuTaskId firstTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/accepted_exception_first"))
            .setMarkerLabel("Accepted Exception First")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &firstRecorded,
        }
    );
    bool secondRecorded = false;
    const GpuTaskId secondTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/accepted_exception_second"))
            .setMarkerLabel("Accepted Exception Second")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &secondRecorded,
        }
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/accepted_exception_compile_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        device.getPhysicalQueueTopology(),
        assignments,
        compiledGraph,
        compileScratch
    ));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(firstPacket, secondPacket);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        firstTask,
        secondTask,
        recordedGraph
    ));
    ASSERT_TRUE(firstRecorded);
    ASSERT_TRUE(secondRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuTaskGraphSubmitter submitter(device);
    Alloc::ScratchArena submissionScratch(Name("tests/descriptor_buffer/accepted_exception_submission_scratch"));
    NativeTaskAcceptanceObserver observer;
    const GpuTaskGraphTaskAcceptedCallback callback{
        .task = firstTask,
        .context = &observer,
        .invoke = ThrowNativeTaskAcceptance,
    };
    GpuSubmissionPacketId failedPacket;
    bool exceptionObserved = false;
    try{
        const bool submitted = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            firstTask,
            secondTask,
            nullptr,
            0u,
            nullptr,
            0u,
            transaction,
            submissionScratch,
            &failedPacket,
            &callback,
            1u
        );
        EXPECT_TRUE(submitted);
    }
    catch(const u32 exceptionValue){
        exceptionObserved = exceptionValue == 0xA11CEu;
    }

    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(failedPacket, firstPacket);
    const QueueSubmissionToken firstToken = transaction.packetToken(firstPacket);
    EXPECT_TRUE(firstToken.valid());
    EXPECT_FALSE(transaction.packetToken(secondPacket).valid());
    EXPECT_EQ(observer.acceptedCount, 1u);
    EXPECT_EQ(observer.lastToken.value, firstToken.value);
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.acceptedPacketCount, 1u);
    EXPECT_EQ(statistics.rejectedPacketCount, 1u);
    EXPECT_EQ(statistics.acceptedTaskCount, 1u);
    EXPECT_EQ(statistics.rejectedTaskCount, 1u);
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(device.waitForIdle());
}


// Fallible timing preparation completes before a composite publishes graph, plan, transaction, or artifact state.
// A rejected recorder therefore leaves no provisional attempt for the caller to clean up.
TEST_F(DescriptorBufferRoundTripTest, CompositeTimingPreflightFailureRetainsCleanupAttempt){
    auto& device = DescriptorBufferRoundTripTest::device();
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool shouldRecord = true;
    bool recorded = false;
    const GpuTaskId task = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/transaction_owner_timing_preflight"))
            .setMarkerLabel("Transaction Owner Timing Preflight")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling)
            .setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::PacketOnly }),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &recorded,
        }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(
        Name("tests/descriptor_buffer/transaction_owner_timing_preflight_compile_scratch")
    );
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        device.getPhysicalQueueTopology(),
        assignments,
        compiledGraph,
        compileScratch
    ));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(views.compiled.packet(packet).plan->recordsTiming);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorderWithoutTiming(device);
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuTaskGraphSubmitter submitter(device);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());
    Alloc::ScratchArena submissionScratch(
        Name("tests/descriptor_buffer/transaction_owner_timing_preflight_submission_scratch")
    );

    EXPECT_FALSE(submitter.recordAndSubmitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recorderWithoutTiming,
        recordedGraph,
        task,
        task,
        transaction,
        submissionScratch
    ));
    EXPECT_FALSE(recorded);
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 0u);
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    EXPECT_FALSE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
}


// Graph lifecycle arbitration is the single winner point even when two fresh transactions begin on separate
// threads. The loser returns before native submission, while the winner retains ownership of the remaining packet.
TEST_F(DescriptorBufferRoundTripTest, ConcurrentTransactionsClaimOneGraphAttempt){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    bool firstShouldRecord = true;
    bool firstRecorded = false;
    const GpuTaskId firstTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/concurrent_transaction_owner_first"))
            .setMarkerLabel("Concurrent Transaction Owner First")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &firstShouldRecord,
            .attempted = &firstRecorded,
        }
    );
    bool secondShouldRecord = true;
    bool secondRecorded = false;
    const GpuTaskId secondTask = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/concurrent_transaction_owner_second"))
            .setMarkerLabel("Concurrent Transaction Owner Second")
            .setQueue(graphicsRequest)
            .setScheduling(scheduling),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &secondShouldRecord,
            .attempted = &secondRecorded,
        }
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(secondTask.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(
        Name("tests/descriptor_buffer/concurrent_transaction_owner_compile_scratch")
    );
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        device.getPhysicalQueueTopology(),
        assignments,
        compiledGraph,
        compileScratch
    ));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId secondPacket = views.compiled.packetForTask(secondTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(firstPacket, secondPacket);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        firstTask,
        secondTask,
        recordedGraph
    ));
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);

    GpuGraphSubmissionTransaction firstTransaction(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction secondTransaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(firstTransaction.tryReset(compiledGraph));
    ASSERT_TRUE(secondTransaction.tryReset(compiledGraph));
    const GpuTaskGraphSubmitter submitter(device);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());

    Latch submissionsReady(2u);
    bool firstSubmissionResult = false;
    bool secondSubmissionResult = false;
    Thread firstSubmissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/concurrent_transaction_owner_first_submission_scratch")
        );
        submissionsReady.count_down();
        submissionsReady.wait();
        firstSubmissionResult = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            firstTask,
            firstTask,
            nullptr,
            0u,
            nullptr,
            0u,
            firstTransaction,
            submissionScratch
        );
    });
    Thread secondSubmissionThread([&](){
        Alloc::ScratchArena submissionScratch(
            Name("tests/descriptor_buffer/concurrent_transaction_owner_second_submission_scratch")
        );
        submissionsReady.count_down();
        submissionsReady.wait();
        secondSubmissionResult = submitter.submitTaskRangeInCompileOrder(
            graph,
            compiledGraph,
            recordedGraph,
            secondTask,
            secondTask,
            nullptr,
            0u,
            nullptr,
            0u,
            secondTransaction,
            submissionScratch
        );
    });
    firstSubmissionThread.join();
    secondSubmissionThread.join();

    EXPECT_NE(firstSubmissionResult, secondSubmissionResult);
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 1u);
    GpuGraphSubmissionTransaction& winnerTransaction = firstSubmissionResult
        ? firstTransaction
        : secondTransaction
    ;
    GpuGraphSubmissionTransaction& loserTransaction = firstSubmissionResult
        ? secondTransaction
        : firstTransaction
    ;
    const GpuTaskId remainingTask = firstSubmissionResult ? secondTask : firstTask;
    const GpuSubmissionPacketId acceptedPacket = firstSubmissionResult ? firstPacket : secondPacket;
    const GpuSubmissionPacketId remainingPacket = firstSubmissionResult ? secondPacket : firstPacket;
    EXPECT_TRUE(winnerTransaction.packetToken(acceptedPacket).valid());
    EXPECT_FALSE(loserTransaction.packetToken(remainingPacket).valid());
    EXPECT_TRUE(loserTransaction.tryReset(compiledGraph));

    Alloc::ScratchArena winnerCompletionScratch(
        Name("tests/descriptor_buffer/concurrent_transaction_owner_completion_scratch")
    );
    ASSERT_TRUE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        remainingTask,
        remainingTask,
        nullptr,
        0u,
        nullptr,
        0u,
        winnerTransaction,
        winnerCompletionScratch
    ));
    EXPECT_EQ(submissionObserver.capturedSubmissionCount(), 2u);
    EXPECT_TRUE(winnerTransaction.packetToken(remainingPacket).valid());
    EXPECT_EQ(winnerTransaction.submissionStatistics().acceptedPacketCount, 2u);
    EXPECT_TRUE(winnerTransaction.tryReset(compiledGraph));
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

