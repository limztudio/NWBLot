// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "recording_capability_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The public marker API can only close the current top entry; it cannot transfer ownership of the recorder's exact
// task/packet marker lease. Consuming that entry makes the recorder reject and abort the packet at scope closure.
struct NativePacketMarkerLeaseInterferenceTask{
    struct Payload{
        bool* attempted = nullptr;
        u32* discardedCount = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.attempted)
            *payload.attempted = true;
        commandList.endMarker();
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


inline constexpr u32 s_GraphRecordingOwnershipException = 0xE103u;


// A task thunk is not allowed to replace, end, or publish the recorder-owned native command-buffer lease. The
// ordinary variants claim success so the packet boundary must reject them; the submission variants prove neither a
// false result nor an exception can let the borrowed list escape through Device submission preflight.
struct NativePacketRecordingBoundaryViolationTask{
    enum class Operation : u8{
        Close,
        CloseAndOpen,
        CloseSubmitReturnFalse,
        CloseSubmitThrow,
        RetainReturnFalse,
        RetainReturnTrue,
        RetainThrow,
    };

    struct Payload{
        Operation operation = Operation::Close;
        bool* attempted = nullptr;
        bool* submissionAccepted = nullptr;
        u32* discardedCount = nullptr;
        NativePacketSubmissionHookObserver* submissionObserver = nullptr;
        CommandListHandle::deleter_type commandListDeleter;
        CommandListHandle* retainedCommandList = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.attempted)
            *payload.attempted = true;

        if(
            payload.operation == Operation::RetainReturnFalse
            || payload.operation == Operation::RetainReturnTrue
            || payload.operation == Operation::RetainThrow
        ){
            if(!payload.retainedCommandList || !payload.commandListDeleter.arena)
                return false;
            *payload.retainedCommandList = CommandListHandle(&commandList, payload.commandListDeleter);
            if(payload.operation == Operation::RetainThrow)
                throw s_GraphRecordingOwnershipException;
            return payload.operation == Operation::RetainReturnTrue;
        }

        commandList.close();
        if(payload.operation == Operation::CloseAndOpen)
            commandList.open();
        if(
            payload.operation == Operation::CloseSubmitReturnFalse
            || payload.operation == Operation::CloseSubmitThrow
        ){
            CommandList* const borrowedLists[]{ &commandList };
            const QueueSubmissionDesc submission{
                .preSubmitHook = QueueSubmissionPreSubmitHook{
                    .context = payload.submissionObserver,
                    .invoke = RejectNativePacketSubmissionHook,
                },
            };
            const bool submissionAccepted = commandList.getDevice().executeCommandLists(
                borrowedLists,
                LengthOf(borrowedLists),
                commandList.getDescription().physicalQueue,
                submission
            ).valid();
            if(payload.submissionAccepted)
                *payload.submissionAccepted = submissionAccepted;
            if(payload.operation == Operation::CloseSubmitThrow)
                throw s_GraphRecordingOwnershipException;
            return false;
        }
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecorderRejectsTaskRecordingLeaseReplacementInAllBuilds){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    struct Case{
        NativePacketRecordingBoundaryViolationTask::Operation operation;
        const char* identity;
        const char* label;
    };
    const Case cases[]{
        {
            NativePacketRecordingBoundaryViolationTask::Operation::Close,
            "tests/descriptor_buffer/task_closes_recording_lease",
            "Task Closes Recording Lease",
        },
        {
            NativePacketRecordingBoundaryViolationTask::Operation::CloseAndOpen,
            "tests/descriptor_buffer/task_replaces_recording_lease",
            "Task Replaces Recording Lease",
        },
    };

    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        bool attempted = false;
        u32 discardedCount = 0u;
        GpuTaskSchedulingHint scheduling;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(Name(testCase.identity))
            .setMarkerLabel(testCase.label)
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling)
        ;
        const GpuTaskId task = graph.addTask<NativePacketRecordingBoundaryViolationTask>(
            taskDesc,
            NativePacketRecordingBoundaryViolationTask::Payload{
                .operation = testCase.operation,
                .attempted = &attempted,
                .discardedCount = &discardedCount,
                .commandListDeleter = {},
                .retainedCommandList = nullptr,
            }
        );
        ASSERT_TRUE(task.valid());

        GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
        GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
        GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
        Alloc::ScratchArena scratchArena(Name(testCase.identity));
        const GpuTaskGraphCompiler compiler;
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());

        const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
        ASSERT_TRUE(packet.valid());

        GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
        const GpuNativePacketRecorder recorder(device);
        EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
            recordedGraph
        ));
        EXPECT_TRUE(attempted);
        EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
        EXPECT_EQ(discardedCount, 1u);
    }
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecorderRejectsConsumedOwnedTaskMarker){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    bool attempted = false;
    u32 discardedCount = 0u;
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuTaskId task = graph.addTask<NativePacketMarkerLeaseInterferenceTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/task_consumes_owned_marker"))
            .setMarkerLabel("Task Consumes Recorder Owned Marker")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling),
        NativePacketMarkerLeaseInterferenceTask::Payload{
            .attempted = &attempted,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/task_consumes_owned_marker"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph
    ));
    EXPECT_TRUE(attempted);
    EXPECT_EQ(discardedCount, 1u);
    EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecorderBlocksBorrowedGraphListSubmissionBeforePreSubmitHook){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    struct Case{
        NativePacketRecordingBoundaryViolationTask::Operation operation;
        StringView identity;
        AStringView label;
        bool shouldThrow = false;
    };
    const Case cases[]{
        {
            NativePacketRecordingBoundaryViolationTask::Operation::CloseSubmitReturnFalse,
            "tests/descriptor_buffer/task_closes_submits_and_returns_false",
            "Task Closes Submits And Returns False",
            false,
        },
        {
            NativePacketRecordingBoundaryViolationTask::Operation::CloseSubmitThrow,
            "tests/descriptor_buffer/task_closes_submits_and_throws",
            "Task Closes Submits And Throws",
            true,
        },
    };

    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        NativePacketSubmissionHookObserver submissionObserver;
        bool attempted = false;
        bool submissionAccepted = false;
        u32 discardedCount = 0u;
        GpuTaskSchedulingHint scheduling;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        const GpuTaskId task = graph.addTask<NativePacketRecordingBoundaryViolationTask>(
            GpuTaskDesc{}
                .setIdentity(Name(testCase.identity))
                .setMarkerLabel(testCase.label)
                .setQueue(GpuQueueRequest{
                    GpuQueueCapability::Graphics,
                    GpuQueuePreference::Graphics,
                    false,
                    false,
                })
                .setScheduling(scheduling),
            NativePacketRecordingBoundaryViolationTask::Payload{
                .operation = testCase.operation,
                .attempted = &attempted,
                .submissionAccepted = &submissionAccepted,
                .discardedCount = &discardedCount,
                .submissionObserver = &submissionObserver,
                .commandListDeleter = {},
                .retainedCommandList = nullptr,
            }
        );
        ASSERT_TRUE(task.valid());

        GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
        GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
        GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
        Alloc::ScratchArena scratchArena(Name(testCase.identity));
        const GpuTaskGraphCompiler compiler;
        {
            const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
            ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
        }
        GpuSubmissionPacketId packet;
        {
            const GpuTaskGraphReadViews views(graph, compiledGraph);
            ASSERT_TRUE(views.valid());
            packet = views.compiled.packetForTask(task);
        }
        ASSERT_TRUE(packet.valid());

        GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
        const GpuNativePacketRecorder recorder(device);
        bool exceptionObserved = false;
        try{
            const bool recorded = recorder.recordPacketRangeInCompileOrder(
                graph,
                compiledGraph,
                GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
                recordedGraph
            );
            EXPECT_FALSE(recorded);
        }
        catch(const u32 exception){
            exceptionObserved = exception == s_GraphRecordingOwnershipException;
        }
        EXPECT_EQ(exceptionObserved, testCase.shouldThrow);
        EXPECT_TRUE(attempted);
        EXPECT_FALSE(submissionAccepted);
        EXPECT_EQ(submissionObserver.invocationCount, 0u);
        EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
        EXPECT_EQ(discardedCount, testCase.shouldThrow ? 0u : 1u);

        recordedGraph.reset(compiledGraph);
        EXPECT_TRUE(graph.tryReset());
    }
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecorderAbandonsRetainedGraphListOnFalseAndThrow){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    CommandListHandle deleterSource = device.createCommandList();
    ASSERT_TRUE(deleterSource);
    const CommandListHandle::deleter_type commandListDeleter = deleterSource.get_deleter();
    deleterSource.reset();

    struct Case{
        NativePacketRecordingBoundaryViolationTask::Operation operation;
        StringView identity;
        AStringView label;
        bool shouldThrow = false;
    };
    const Case cases[]{
        {
            NativePacketRecordingBoundaryViolationTask::Operation::RetainReturnFalse,
            "tests/descriptor_buffer/task_retains_recording_lease_and_returns_false",
            "Task Retains Recording Lease And Returns False",
            false,
        },
        {
            NativePacketRecordingBoundaryViolationTask::Operation::RetainThrow,
            "tests/descriptor_buffer/task_retains_recording_lease_and_throws",
            "Task Retains Recording Lease And Throws",
            true,
        },
    };

    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        CommandListHandle retainedCommandList;
        bool attempted = false;
        u32 discardedCount = 0u;
        GpuTaskSchedulingHint scheduling;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        const GpuTaskId task = graph.addTask<NativePacketRecordingBoundaryViolationTask>(
            GpuTaskDesc{}
                .setIdentity(Name(testCase.identity))
                .setMarkerLabel(testCase.label)
                .setQueue(GpuQueueRequest{
                    GpuQueueCapability::Graphics,
                    GpuQueuePreference::Graphics,
                    false,
                    false,
                })
                .setScheduling(scheduling),
            NativePacketRecordingBoundaryViolationTask::Payload{
                .operation = testCase.operation,
                .attempted = &attempted,
                .discardedCount = &discardedCount,
                .commandListDeleter = commandListDeleter,
                .retainedCommandList = &retainedCommandList,
            }
        );
        ASSERT_TRUE(task.valid());

        GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
        GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
        GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
        Alloc::ScratchArena scratchArena(Name(testCase.identity));
        const GpuTaskGraphCompiler compiler;
        {
            const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
            ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
        }
        GpuSubmissionPacketId packet;
        {
            const GpuTaskGraphReadViews views(graph, compiledGraph);
            ASSERT_TRUE(views.valid());
            packet = views.compiled.packetForTask(task);
        }
        ASSERT_TRUE(packet.valid());

        GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
        const GpuNativePacketRecorder recorder(device);
        bool exceptionObserved = false;
        try{
            const bool recorded = recorder.recordPacketRangeInCompileOrder(
                graph,
                compiledGraph,
                GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
                recordedGraph
            );
            EXPECT_FALSE(recorded);
        }
        catch(const u32 exception){
            exceptionObserved = exception == s_GraphRecordingOwnershipException;
        }
        EXPECT_EQ(exceptionObserved, testCase.shouldThrow);
        EXPECT_TRUE(attempted);
        EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
        EXPECT_EQ(discardedCount, testCase.shouldThrow ? 0u : 1u);
        ASSERT_TRUE(retainedCommandList);
        EXPECT_FALSE(retainedCommandList->isRecording());
        EXPECT_FALSE(retainedCommandList->hasCommandBuffer());
        EXPECT_TRUE(retainedCommandList->commandRecordingFailed());

        retainedCommandList.reset();
        recordedGraph.reset(compiledGraph);
        EXPECT_TRUE(graph.tryReset());
    }
}


TEST_F(DescriptorBufferRoundTripTest, RecordedGraphRevokesRejectedRetainedCommandListOnResetAndDestruction){
    auto& device = DescriptorBufferRoundTripTest::device();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    CommandListHandle deleterSource = device.createCommandList();
    ASSERT_TRUE(deleterSource);
    const CommandListHandle::deleter_type commandListDeleter = deleterSource.get_deleter();
    deleterSource.reset();

    struct Case{
        StringView identity;
        AStringView label;
        bool resetRecordedGraph = false;
        bool rejectNativeSubmission = false;
        bool acceptNativeSubmission = false;
    };
    const Case cases[]{
        {
            "tests/descriptor_buffer/rejected_retained_graph_list_reset",
            "Rejected Retained Graph List Reset",
            true,
            false,
            false,
        },
        {
            "tests/descriptor_buffer/rejected_retained_graph_list_destruction",
            "Rejected Retained Graph List Destruction",
            false,
            false,
            false,
        },
        {
            "tests/descriptor_buffer/native_rejected_retained_graph_list_reset",
            "Native Rejected Retained Graph List Reset",
            true,
            true,
            false,
        },
        {
            "tests/descriptor_buffer/accepted_retained_graph_list_reset",
            "Accepted Retained Graph List Reset",
            true,
            false,
            true,
        },
    };

    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
        CommandListHandle retainedCommandList;
        bool attempted = false;
        u32 discardedCount = 0u;
        GpuTaskSchedulingHint scheduling;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        const GpuTaskId task = graph.addTask<NativePacketRecordingBoundaryViolationTask>(
            GpuTaskDesc{}
                .setIdentity(Name(testCase.identity))
                .setMarkerLabel(testCase.label)
                .setQueue(GpuQueueRequest{
                    GpuQueueCapability::Graphics,
                    GpuQueuePreference::Graphics,
                    false,
                    false,
                })
                .setScheduling(scheduling),
            NativePacketRecordingBoundaryViolationTask::Payload{
                .operation = NativePacketRecordingBoundaryViolationTask::Operation::RetainReturnTrue,
                .attempted = &attempted,
                .discardedCount = &discardedCount,
                .commandListDeleter = commandListDeleter,
                .retainedCommandList = &retainedCommandList,
            }
        );
        ASSERT_TRUE(task.valid());

        GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
        GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
        GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
        Alloc::ScratchArena scratchArena(Name(testCase.identity));
        const GpuTaskGraphCompiler compiler;
        {
            const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
            ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
        }
        GpuSubmissionPacketId packet;
        GpuPhysicalQueueId packetQueue;
        {
            const GpuTaskGraphReadViews views(graph, compiledGraph);
            ASSERT_TRUE(views.valid());
            packet = views.compiled.packetForTask(task);
            const GpuCompiledPacketView packetView = views.compiled.packet(packet);
            ASSERT_TRUE(packetView.valid());
            packetQueue = packetView.plan->queue;
        }
        ASSERT_TRUE(packet.valid());

        {
            GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
            const GpuNativePacketRecorder recorder(device);
            ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
                graph,
                compiledGraph,
                GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
                recordedGraph
            ));
            EXPECT_TRUE(attempted);
            const Optional<GpuRecordedPacket> recordedPacket = recordedGraph.packetSnapshot(packet);
            ASSERT_TRUE(recordedPacket.has_value());
            ASSERT_EQ(recordedPacket->commandListCount, 1u);
            ASSERT_TRUE(retainedCommandList);
            EXPECT_FALSE(retainedCommandList->isRecording());
            EXPECT_TRUE(retainedCommandList->hasCommandBuffer());
            EXPECT_FALSE(retainedCommandList->commandRecordingFailed());

            GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
            transaction.reset(compiledGraph);
            if(testCase.acceptNativeSubmission){
                const GpuTaskScheduler submitter(device);
                ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
                    graph,
                    compiledGraph,
                    recordedGraph,
                    GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
                    nullptr,
                    0u,
                    nullptr,
                    0u,
                    transaction,
                    scratchArena
                ));
                EXPECT_TRUE(transaction.packetToken(packet).valid());
            }
            else if(testCase.rejectNativeSubmission){
                const GpuPhysicalQueueId rejectedQueue = packetQueue;
                ASSERT_TRUE(rejectedQueue.valid());
                const VkQueue nativeRejectedQueue = static_cast<VkQueue>(
                    device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, rejectedQueue).pointer()
                );
                ASSERT_NE(nativeRejectedQueue, VK_NULL_HANDLE);
                VulkanTestQueueSubmit2Observer submissionObserver(device);
                ASSERT_TRUE(submissionObserver.valid());
                ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeRejectedQueue));
                const GpuTaskScheduler submitter(device);
                EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
                    graph,
                    compiledGraph,
                    recordedGraph,
                    GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
                    nullptr,
                    0u,
                    nullptr,
                    0u,
                    transaction,
                    scratchArena
                ));
                EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
                EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
            }
            else{
                ASSERT_TRUE(transaction.discardUnaccepted(
                    graph,
                    compiledGraph,
                    recordedGraph.recordingAttemptGeneration()
                ));
            }
            EXPECT_EQ(discardedCount, testCase.acceptNativeSubmission ? 0u : 1u);
            EXPECT_EQ(
                retainedCommandList->hasCommandBuffer(),
                !testCase.rejectNativeSubmission && !testCase.acceptNativeSubmission
            );

            if(testCase.resetRecordedGraph){
                recordedGraph.reset(compiledGraph);
                EXPECT_FALSE(recordedGraph.packetSnapshot(packet).has_value());
            }
        }

        ASSERT_TRUE(retainedCommandList);
        EXPECT_FALSE(retainedCommandList->isRecording());
        EXPECT_FALSE(retainedCommandList->hasCommandBuffer());
        EXPECT_EQ(retainedCommandList->commandRecordingFailed(), !testCase.acceptNativeSubmission);
        retainedCommandList->open();
        EXPECT_TRUE(retainedCommandList->isRecording());
        EXPECT_TRUE(retainedCommandList->hasCommandBuffer());
        EXPECT_FALSE(retainedCommandList->commandRecordingFailed());
        retainedCommandList->close();
        EXPECT_FALSE(retainedCommandList->isRecording());
        EXPECT_TRUE(retainedCommandList->hasCommandBuffer());
        EXPECT_FALSE(retainedCommandList->commandRecordingFailed());

        if(testCase.acceptNativeSubmission)
            ASSERT_TRUE(device.waitForIdle());

        retainedCommandList.reset();
        EXPECT_TRUE(graph.tryReset());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

