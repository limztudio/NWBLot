// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_RecordingDeclarationMutationException = 0xE102u;


struct RecordingDeclarationMutationState{
    GpuTaskGraph* graph = nullptr;
    u64 expectedRevision = 0u;
    usize expectedTaskCount = 0u;
    bool mutationRejected = false;
    bool storageUnchanged = false;
    bool shouldThrow = false;
};


struct RecordingDeclarationMutationTask{
    struct Payload{
        RecordingDeclarationMutationState* state = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(!payload.state || !payload.state->graph || !commandList.isRecording())
            return false;

        GpuTaskDesc attemptedDesc;
        attemptedDesc
            .setIdentity(Name("tests/descriptor_buffer/recording_declaration_mutation_attempt"))
            .setMarkerLabel("Recording Declaration Mutation Attempt")
        ;
        const GpuTaskId attemptedTask = payload.state->graph->addTask(attemptedDesc);
        payload.state->mutationRejected = !attemptedTask.valid();
        payload.state->storageUnchanged = context.declarations.taskCount() == payload.state->expectedTaskCount
            && context.declarations.declarationRevision() == payload.state->expectedRevision
        ;
        if(payload.state->shouldThrow)
            throw s_RecordingDeclarationMutationException;
        return false;
    }
};


struct DeclarationMutationRecorderPreflightState{
    const GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    const GpuNativePacketRecorder* recorder = nullptr;
    GpuRecordedGraph* recordedGraph = nullptr;
    GpuSubmissionPacketRange range;
    GpuSubmissionPacketId failedPacket;
    bool attempted = false;
    bool succeeded = true;
};


struct DeclarationMutationRecorderPreflightTask{
    struct Payload{
        DeclarationMutationRecorderPreflightState* state = nullptr;

        explicit Payload(DeclarationMutationRecorderPreflightState& value)noexcept
            : state(&value)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other)
            : state(other.state)
        {
            other.state = nullptr;
            if(
                !state
                || !state->graph
                || !state->compiledGraph
                || !state->recorder
                || !state->recordedGraph
            )
                return;

            state->attempted = true;
            state->succeeded = state->recorder->recordPacketRangeInCompileOrder(
                *state->graph,
                *state->compiledGraph,
                state->range,
                *state->recordedGraph,
                &state->failedPacket
            );
        }
    };
};


struct DeclarationMutationRangeSubmissionPreflightState{
    GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    const GpuRecordedGraph* recordedGraph = nullptr;
    const GpuTaskScheduler* submitter = nullptr;
    GpuGraphSubmissionTransaction* transaction = nullptr;
    Alloc::ScratchArena* scratchArena = nullptr;
    GpuSubmissionPacketRange range;
    GpuSubmissionPacketId failedPacket;
    bool attempted = false;
    bool succeeded = true;
};


struct DeclarationMutationRangeSubmissionPreflightTask{
    struct Payload{
        DeclarationMutationRangeSubmissionPreflightState* state = nullptr;

        explicit Payload(DeclarationMutationRangeSubmissionPreflightState& value)noexcept
            : state(&value)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other)
            : state(other.state)
        {
            other.state = nullptr;
            if(
                !state
                || !state->graph
                || !state->compiledGraph
                || !state->recordedGraph
                || !state->submitter
                || !state->transaction
                || !state->scratchArena
            )
                return;

            state->attempted = true;
            state->succeeded = state->submitter->submitPacketRangeInCompileOrder(
                *state->graph,
                *state->compiledGraph,
                *state->recordedGraph,
                state->range,
                nullptr,
                0u,
                nullptr,
                0u,
                *state->transaction,
                *state->scratchArena,
                &state->failedPacket
            );
        }
    };
};


struct DeclarationMutationSubmitterPreflightState{
    GpuTaskGraph* graph = nullptr;
    const GpuCompiledGraph* compiledGraph = nullptr;
    const GpuNativePacketRecorder* recorder = nullptr;
    GpuRecordedGraph* recordedGraph = nullptr;
    const GpuTaskScheduler* submitter = nullptr;
    GpuGraphSubmissionTransaction* transaction = nullptr;
    Alloc::ScratchArena* scratchArena = nullptr;
    GpuTaskGraphNormalExecutionDesc execution;
    GpuSubmissionPacketId failedPacket;
    bool attempted = false;
    bool succeeded = true;
};


struct DeclarationMutationSubmitterPreflightTask{
    struct Payload{
        DeclarationMutationSubmitterPreflightState* state = nullptr;

        explicit Payload(DeclarationMutationSubmitterPreflightState& value)noexcept
            : state(&value)
        {}
        Payload(const Payload&) = delete;
        Payload(Payload&& other)
            : state(other.state)
        {
            other.state = nullptr;
            if(
                !state
                || !state->graph
                || !state->compiledGraph
                || !state->recorder
                || !state->recordedGraph
                || !state->submitter
                || !state->transaction
                || !state->scratchArena
            )
                return;

            state->attempted = true;
            state->succeeded = state->submitter->recordAndSubmitNormalGraph(
                *state->graph,
                *state->compiledGraph,
                *state->recorder,
                *state->recordedGraph,
                state->execution,
                *state->transaction,
                *state->scratchArena,
                &state->failedPacket
            );
        }
    };
};


struct DiscardDeclarationMutationState{
    GpuTaskGraph* graph = nullptr;
    u64 expectedRevision = 0u;
    usize expectedTaskCount = 0u;
    bool recorded = false;
    bool callbackInvoked = false;
    bool mutationRejected = false;
    bool storageUnchanged = false;
};


struct DiscardDeclarationMutationTask{
    struct Payload{
        DiscardDeclarationMutationState* state = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext&
    ){
        if(payload.state)
            payload.state->recorded = commandList.isRecording();
        return commandList.isRecording();
    }

    static void discarded(Payload& payload){
        if(!payload.state || !payload.state->graph)
            return;

        payload.state->callbackInvoked = true;
        GpuTaskDesc attemptedDesc;
        attemptedDesc
            .setIdentity(Name("tests/descriptor_buffer/discard_declaration_mutation_attempt"))
            .setMarkerLabel("Discard Declaration Mutation Attempt")
        ;
        const GpuTaskId attemptedTask = payload.state->graph->addTask(attemptedDesc);
        payload.state->mutationRejected = !attemptedTask.valid();
        const GpuTaskGraph::DeclarationReadView declarations(*payload.state->graph);
        payload.state->storageUnchanged = declarations.taskCount() == payload.state->expectedTaskCount
            && declarations.declarationRevision() == payload.state->expectedRevision
        ;
    }
};


TEST_F(DescriptorBufferRoundTripTest, RecordingAttemptFreezesTaskGraphDeclarationMutation){
    auto& device = DescriptorBufferRoundTripTest::device();
    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    RecordingDeclarationMutationState failedState{
        .graph = &graph,
        .shouldThrow = false,
    };
    RecordingDeclarationMutationState throwingState{
        .graph = &graph,
        .shouldThrow = true,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest queueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const auto addMutationTask = [
        &graph,
        &queueRequest,
        &scheduling
    ](
        const Name& identity,
        const AStringView label,
        RecordingDeclarationMutationState& state
    ){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queueRequest)
            .setScheduling(scheduling)
        ;
        return graph.addTask<RecordingDeclarationMutationTask>(
            desc,
            RecordingDeclarationMutationTask::Payload{ .state = &state }
        );
    };
    const GpuTaskId failedTask = addMutationTask(
        Name("tests/descriptor_buffer/recording_declaration_mutation_false"),
        "Recording Declaration Mutation False",
        failedState
    );
    const GpuTaskId throwingTask = addMutationTask(
        Name("tests/descriptor_buffer/recording_declaration_mutation_throw"),
        "Recording Declaration Mutation Throw",
        throwingState
    );
    ASSERT_TRUE(failedTask.valid());
    ASSERT_TRUE(throwingTask.valid());

    u64 declarationRevision = 0u;
    usize taskCount = 0u;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        declarationRevision = declarations.declarationRevision();
        taskCount = declarations.taskCount();
    }
    failedState.expectedRevision = declarationRevision;
    failedState.expectedTaskCount = taskCount;
    throwingState.expectedRevision = declarationRevision;
    throwingState.expectedTaskCount = taskCount;

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/recording_declaration_mutation_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            scratchArena
        ));
    }
    GpuSubmissionPacketId failedPacket;
    GpuSubmissionPacketId throwingPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        failedPacket = views.compiled.packetForTask(failedTask);
        throwingPacket = views.compiled.packetForTask(throwingTask);
        ASSERT_TRUE(failedPacket.valid());
        ASSERT_TRUE(throwingPacket.valid());
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = failedPacket, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(failedState.mutationRejected);
    EXPECT_TRUE(failedState.storageUnchanged);

    bool exceptionObserved = false;
    try{
        const bool recorded = recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            GpuSubmissionPacketRange{ .first = throwingPacket, .packetCount = 1u },
            recordedGraph
        );
        EXPECT_FALSE(recorded);
    }
    catch(const u32 exception){
        exceptionObserved = exception == s_RecordingDeclarationMutationException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_TRUE(throwingState.mutationRejected);
    EXPECT_TRUE(throwingState.storageUnchanged);
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), taskCount);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
    }

    recordedGraph.reset(compiledGraph);
    EXPECT_TRUE(graph.tryReset());
}


// Runtime preflight takes a nonblocking declaration-read claim before it traverses the graph or prepares timing.
// Reentry from a declaration payload move therefore rejects without publishing an attempt or semantic failure.
TEST_F(DescriptorBufferRoundTripTest, DeclarationMutationRejectsRuntimeReadPreflightBeforeSideEffects){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.joinsAcceptedQueueFrontier = true;
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    bool shouldRecord = true;
    bool recordAttempted = false;
    const GpuTaskId task = graph.addTask<NativePacketCaptureRetryTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/declaration_mutation_runtime_preflight"))
            .setMarkerLabel("Declaration Mutation Runtime Preflight")
            .setQueue(graphicsQueue)
            .setScheduling(scheduling)
            .setTimingMetadata(GpuTaskTimingMetadata{ .policy = GpuTaskTimingPolicy::PacketOnly }),
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &recordAttempted,
        }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/declaration_mutation_runtime_preflight_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
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

        packet = views.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());
        const GpuCompiledPacketView packetView = views.compiled.packet(packet);
        ASSERT_TRUE(packetView.valid());
        ASSERT_TRUE(packetView.plan->recordsTiming);
        ASSERT_TRUE(packetView.plan->joinsAcceptedQueueFrontier);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    const GpuNativePacketRecorder recorder(device, timing);
    const GpuTaskScheduler submitter(device);
    const GpuTimingRecorderStatistics initialTimingStatistics = timing.statistics(device);
    u64 initialRevision = 0u;
    usize initialTaskCount = 0u;
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        initialRevision = declarations.declarationRevision();
        initialTaskCount = declarations.taskCount();
    }

    DeclarationMutationRecorderPreflightState recorderState{
        .graph = &graph,
        .compiledGraph = &compiledGraph,
        .recorder = &recorder,
        .recordedGraph = &recordedGraph,
        .range = GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        .failedPacket = {},
        .attempted = false,
        .succeeded = true,
    };
    const GpuTaskId recorderProbe = graph.addTask<DeclarationMutationRecorderPreflightTask>(
        GpuTaskDesc{},
        DeclarationMutationRecorderPreflightTask::Payload(recorderState)
    );
    EXPECT_FALSE(recorderProbe.valid());
    EXPECT_TRUE(recorderState.attempted);
    EXPECT_FALSE(recorderState.succeeded);
    EXPECT_FALSE(recorderState.failedPacket.valid());
    EXPECT_FALSE(recordAttempted);
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.declarationRevision(), initialRevision);
        EXPECT_EQ(declarations.taskCount(), initialTaskCount);
    }
    const GpuTimingRecorderStatistics recorderTimingStatistics = timing.statistics(device);
    EXPECT_EQ(recorderTimingStatistics.preparedScopeCount, initialTimingStatistics.preparedScopeCount);
    EXPECT_EQ(recorderTimingStatistics.requestedQueryCount, initialTimingStatistics.requestedQueryCount);

    const ArenaMemoryStats initialRangeScratchStatistics = scratchArena.memoryStats();
    DeclarationMutationRangeSubmissionPreflightState rangeSubmissionState{
        .graph = &graph,
        .compiledGraph = &compiledGraph,
        .recordedGraph = &recordedGraph,
        .submitter = &submitter,
        .transaction = &transaction,
        .scratchArena = &scratchArena,
        .range = GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        .failedPacket = {},
        .attempted = false,
        .succeeded = true,
    };
    const GpuTaskId rangeSubmissionProbe = graph.addTask<DeclarationMutationRangeSubmissionPreflightTask>(
        GpuTaskDesc{},
        DeclarationMutationRangeSubmissionPreflightTask::Payload(rangeSubmissionState)
    );
    EXPECT_FALSE(rangeSubmissionProbe.valid());
    EXPECT_TRUE(rangeSubmissionState.attempted);
    EXPECT_FALSE(rangeSubmissionState.succeeded);
    EXPECT_FALSE(rangeSubmissionState.failedPacket.valid());
    EXPECT_FALSE(recordAttempted);
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

        EXPECT_TRUE(transaction.validFor(views.compiled));
        EXPECT_EQ(views.declarations.declarationRevision(), initialRevision);
        EXPECT_EQ(views.declarations.taskCount(), initialTaskCount);
    }
    const ArenaMemoryStats rejectedRangeScratchStatistics = scratchArena.memoryStats();
    EXPECT_EQ(rejectedRangeScratchStatistics.reservedBytes, initialRangeScratchStatistics.reservedBytes);
    EXPECT_EQ(rejectedRangeScratchStatistics.usedBytes, initialRangeScratchStatistics.usedBytes);
    EXPECT_EQ(rejectedRangeScratchStatistics.peakUsedBytes, initialRangeScratchStatistics.peakUsedBytes);
    EXPECT_EQ(rejectedRangeScratchStatistics.allocationCount, initialRangeScratchStatistics.allocationCount);
    EXPECT_EQ(rejectedRangeScratchStatistics.reallocationCount, initialRangeScratchStatistics.reallocationCount);
    EXPECT_EQ(rejectedRangeScratchStatistics.deallocationCount, initialRangeScratchStatistics.deallocationCount);
    const GpuTimingRecorderStatistics rangeSubmitterTimingStatistics = timing.statistics(device);
    EXPECT_EQ(rangeSubmitterTimingStatistics.preparedScopeCount, initialTimingStatistics.preparedScopeCount);
    EXPECT_EQ(rangeSubmitterTimingStatistics.requestedQueryCount, initialTimingStatistics.requestedQueryCount);

    GpuTaskGraphNormalExecutionDesc execution;
    execution.terminalTask = task;
    DeclarationMutationSubmitterPreflightState submitterState{
        .graph = &graph,
        .compiledGraph = &compiledGraph,
        .recorder = &recorder,
        .recordedGraph = &recordedGraph,
        .submitter = &submitter,
        .transaction = &transaction,
        .scratchArena = &scratchArena,
        .execution = execution,
        .failedPacket = {},
        .attempted = false,
        .succeeded = true,
    };
    const GpuTaskId submitterProbe = graph.addTask<DeclarationMutationSubmitterPreflightTask>(
        GpuTaskDesc{},
        DeclarationMutationSubmitterPreflightTask::Payload(submitterState)
    );
    EXPECT_FALSE(submitterProbe.valid());
    EXPECT_TRUE(submitterState.attempted);
    EXPECT_FALSE(submitterState.succeeded);
    EXPECT_FALSE(submitterState.failedPacket.valid());
    EXPECT_FALSE(recordAttempted);
    EXPECT_EQ(recordedGraph.recordingAttemptGeneration(), 0u);
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.declarationRevision(), initialRevision);
        EXPECT_EQ(declarations.taskCount(), initialTaskCount);
    }
    const GpuTimingRecorderStatistics submitterTimingStatistics = timing.statistics(device);
    EXPECT_EQ(submitterTimingStatistics.preparedScopeCount, initialTimingStatistics.preparedScopeCount);
    EXPECT_EQ(submitterTimingStatistics.requestedQueryCount, initialTimingStatistics.requestedQueryCount);
    EXPECT_TRUE(graph.tryReset());
}


// Public discard owns a declaration-read claim through observer delivery and exact terminal binding resolution.
// A discarded payload therefore cannot mutate the declaration containers that the outer operation still reads.
TEST_F(DescriptorBufferRoundTripTest, DiscardCallbackReentryRejectsDeclarationMutationUntilTerminalResolution){
    auto& device = DescriptorBufferRoundTripTest::device();

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    DiscardDeclarationMutationState state{
        .graph = &graph,
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuTaskId task = graph.addTask<DiscardDeclarationMutationTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/discard_declaration_mutation"))
            .setMarkerLabel("Discard Declaration Mutation")
            .setQueue(graphicsQueue),
        DiscardDeclarationMutationTask::Payload{ .state = &state }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/discard_declaration_mutation_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations,
            analysis,
            device.getPhysicalQueueTopology(),
            assignments,
            compiledGraph,
            scratchArena
        ));
    }
    GpuSubmissionPacketRange packetRange;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);

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
    ASSERT_TRUE(state.recorded);
    ASSERT_NE(recordedGraph.recordingAttemptGeneration(), 0u);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(transaction.tryReset(compiledGraph));
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        state.expectedRevision = declarations.declarationRevision();
        state.expectedTaskCount = declarations.taskCount();
    }
    const GpuTaskGraphSubmissionStatistics beforeDiscard = transaction.submissionStatistics();
    ASSERT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));

    EXPECT_TRUE(state.callbackInvoked);
    EXPECT_TRUE(state.mutationRejected);
    EXPECT_TRUE(state.storageUnchanged);
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.declarationRevision(), state.expectedRevision);
        EXPECT_EQ(declarations.taskCount(), state.expectedTaskCount);
    }
    const GpuTaskGraphSubmissionStatistics afterDiscard = transaction.submissionStatistics();
    EXPECT_EQ(afterDiscard.acceptedPacketCount, beforeDiscard.acceptedPacketCount);
    EXPECT_EQ(afterDiscard.acceptedTaskCount, beforeDiscard.acceptedTaskCount);
    EXPECT_EQ(afterDiscard.nativeSubmissionCount, beforeDiscard.nativeSubmissionCount);
    EXPECT_EQ(afterDiscard.rejectedPacketCount, beforeDiscard.rejectedPacketCount + 1u);
    EXPECT_EQ(afterDiscard.rejectedTaskCount, beforeDiscard.rejectedTaskCount + 1u);
    EXPECT_EQ(afterDiscard.rejectedSubmissionCount, beforeDiscard.rejectedSubmissionCount);

    recordedGraph.reset(compiledGraph);
    EXPECT_TRUE(graph.tryReset());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

