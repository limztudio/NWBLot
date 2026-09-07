// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "parallel_recording_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_DiscardObserverOuterException = 0xE105u;


// A transaction-owned rejection publishes both graph and transaction terminal state before calling an extensible
// discard observer. The exact exception may escape, but it cannot leave a binding behind or replay the observer.
TEST_F(DescriptorBufferRoundTripTest, TransactionDiscardObserverExceptionConsumesExactBindingBeforePropagation){
    auto& device = DescriptorBufferRoundTripTest::device();
    DiscardObserverExceptionState state;
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuTaskId task = graph.addTask<DiscardObserverExceptionTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/submission_discard_observer_exception"))
            .setMarkerLabel("Submission Discard Observer Exception")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling),
        DiscardObserverExceptionTask::Payload(state)
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena compileScratch(Name("tests/descriptor_buffer/submission_discard_observer_exception_compile"));
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
    GpuSubmissionPacketId packet;
    GpuSubmissionPacketRange packetRange;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        packet = views.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());
        packetRange = views.compiled.allPacketRange();
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        recordedGraph
    ));

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    bool exceptionObserved = false;
    try{
        const bool discarded = transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        );
        EXPECT_FALSE(discarded);
    }
    catch(const u32 exception){
        exceptionObserved = exception == s_DiscardObserverException;
    }

    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(state.discardCount, 1u);
    EXPECT_EQ(state.destructionCount, 0u);
    EXPECT_FALSE(transaction.packetToken(packet).valid());
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    EXPECT_EQ(statistics.rejectedPacketCount, 1u);
    EXPECT_EQ(statistics.rejectedTaskCount, 1u);
    EXPECT_EQ(statistics.rejectedSubmissionCount, 0u);

    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    recordedGraph.reset(compiledGraph);
    bool graphReset = false;
    EXPECT_NO_THROW(graphReset = graph.tryReset());
    EXPECT_TRUE(graphReset);
    EXPECT_EQ(state.discardCount, 1u);
    EXPECT_EQ(state.destructionCount, 1u);
}


// Reset and invalid-declaration cleanup both destroy the owned payload before the exact discard exception reaches
// the caller. Reset additionally clears the graph during unwind, so no observer can be replayed on a later reset.
TEST_F(DescriptorBufferRoundTripTest, DiscardObserverExceptionLeavesPayloadCleanupFailureAtomic){
    {
        DiscardObserverExceptionState state;
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        const GpuTaskId task = graph.addTask<DiscardObserverExceptionTask>(
            GpuTaskDesc{}
                .setIdentity(Name("tests/descriptor_buffer/reset_discard_observer_exception"))
                .setMarkerLabel("Reset Discard Observer Exception"),
            DiscardObserverExceptionTask::Payload(state)
        );
        ASSERT_TRUE(task.valid());

        bool exceptionObserved = false;
        bool reset = false;
        try{
            reset = graph.tryReset();
        }
        catch(const u32 exception){
            exceptionObserved = exception == s_DiscardObserverException;
        }
        EXPECT_TRUE(exceptionObserved);
        EXPECT_FALSE(reset);
        EXPECT_EQ(state.discardCount, 1u);
        EXPECT_EQ(state.destructionCount, 1u);
        {
            const GpuTaskGraph::DeclarationReadView declarations(graph);

            EXPECT_EQ(declarations.taskCount(), 0u);
            EXPECT_FALSE(declarations.validTask(task));
        }
        EXPECT_TRUE(graph.tryReset());
        EXPECT_EQ(state.discardCount, 1u);
    }

    {
        DiscardObserverExceptionState state;
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        GpuTaskId task;
        bool exceptionObserved = false;
        try{
            task = graph.addTask<DiscardObserverExceptionTask>(
                GpuTaskDesc{},
                DiscardObserverExceptionTask::Payload(state)
            );
        }
        catch(const u32 exception){
            exceptionObserved = exception == s_DiscardObserverException;
        }
        EXPECT_TRUE(exceptionObserved);
        EXPECT_FALSE(task.valid());
        EXPECT_EQ(state.discardCount, 1u);
        EXPECT_EQ(state.destructionCount, 1u);
        {
            const GpuTaskGraph::DeclarationReadView declarations(graph);

            EXPECT_EQ(declarations.taskCount(), 0u);
        }
        EXPECT_TRUE(graph.tryReset());
    }
}


// Ordinary destruction exposes a discard observer failure to the entry boundary. During an existing unwind, graph
// destruction is callback-free so the original exception remains the only active failure while payloads still die.
TEST_F(DescriptorBufferRoundTripTest, TaskGraphDestructorSkipsDiscardObserversOnlyDuringExistingUnwind){
    DiscardObserverExceptionState ordinaryState;
    bool discardExceptionObserved = false;
    try{
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        const GpuTaskId task = graph.addTask<DiscardObserverExceptionTask>(
            GpuTaskDesc{}
                .setIdentity(Name("tests/descriptor_buffer/destructor_discard_observer_exception"))
                .setMarkerLabel("Destructor Discard Observer Exception"),
            DiscardObserverExceptionTask::Payload(ordinaryState)
        );
        EXPECT_TRUE(task.valid());
    }
    catch(const u32 exception){
        discardExceptionObserved = exception == s_DiscardObserverException;
    }
    EXPECT_TRUE(discardExceptionObserved);
    EXPECT_EQ(ordinaryState.discardCount, 1u);
    EXPECT_EQ(ordinaryState.destructionCount, 1u);

    DiscardObserverExceptionState unwindState;
    bool outerExceptionObserved = false;
    try{
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        const GpuTaskId task = graph.addTask<DiscardObserverExceptionTask>(
            GpuTaskDesc{}
                .setIdentity(Name("tests/descriptor_buffer/destructor_existing_unwind"))
                .setMarkerLabel("Destructor Existing Unwind"),
            DiscardObserverExceptionTask::Payload(unwindState)
        );
        EXPECT_TRUE(task.valid());
        throw s_DiscardObserverOuterException;
    }
    catch(const u32 exception){
        outerExceptionObserved = exception == s_DiscardObserverOuterException;
    }
    EXPECT_TRUE(outerExceptionObserved);
    EXPECT_EQ(unwindState.discardCount, 0u);
    EXPECT_EQ(unwindState.destructionCount, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

