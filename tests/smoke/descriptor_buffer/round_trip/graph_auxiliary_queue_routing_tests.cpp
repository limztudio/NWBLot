// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A physical-queue ID is more precise than its broad CommandQueue class. When a Graphics family exposes a second
// queue, explicitly opted-in graph work may cross between them; Vulkan requires a timeline wait but no queue-family
// ownership transfer. Exercise the complete compiler -> recorder -> submitter path on a real Device.
TEST_F(DescriptorBufferRoundTripTest, SameClassGraphicsQueuesRouteGraphPacketsAndStateHandoffs){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Same-class queue routing: no usable headless Vulkan device on this host.";

    auto& device = multiQueueScope.graphics().getDevice();
    if(!device.getDescriptorBufferManager().isEnabled())
        GTEST_SKIP() << "Same-class queue routing: VK_EXT_descriptor_buffer is unavailable on this device.";

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* secondaryGraphicsQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass == CommandQueue::Graphics
            && candidate.id != primaryGraphicsQueue
            && candidate.familyIndex == device.getQueueFamilyIndex(primaryGraphicsQueue)
        ){
            secondaryGraphicsQueue = &candidate;
            break;
        }
    }
    if(!secondaryGraphicsQueue){
        GTEST_SKIP() << "Same-class queue routing: adapter exposes only one Graphics queue.";
    }

    static constexpr u32 s_SourceWords[] = {
        0x1e35a7c9u,
        0x73b4d2f0u,
        0x0badbeefu,
        0x9e3779b9u,
    };
    auto source = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_SourceWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    auto destination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_SourceWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);

    GpuTaskGraph graph(multiQueueScope.arena());
    const GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/same_class_source"))
            .setMarkerLabel("Same Class Source")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/same_class_destination"))
            .setMarkerLabel("Same Class Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuUploadBlobId sourceBlob = graph.copyUploadData(s_SourceWords, sizeof(s_SourceWords), alignof(u32));
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());
    ASSERT_TRUE(sourceBlob.valid());

    GpuQueueRequest graphicsTransferRequest;
    graphicsTransferRequest.requiredCapabilities = GpuQueueCapability::Transfer;
    graphicsTransferRequest.preferredQueue = GpuQueuePreference::Graphics;
    graphicsTransferRequest.allowFallback = false;
    graphicsTransferRequest.compilerMayOverridePreference = false;

    GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = GpuTaskCostHint::Large;
    uploadScheduling.forceSubmissionBoundary = true;
    uploadScheduling.allowPacketMerge = false;
    uploadScheduling.allowSameClassQueueRouting = true;
    GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/descriptor_buffer/same_class_upload"))
        .setMarkerLabel("Same Class Upload")
        .setQueue(graphicsTransferRequest)
        .setScheduling(uploadScheduling)
    ;
    QueueSubmissionToken uploadToken;
    const GpuTaskId uploadTask = graph.addUploadBufferTask(
        uploadDesc,
        GpuUploadBufferTaskDesc{
            .source = sourceBlob,
            .destination = sourceResource,
            .finalState = ResourceStates::CopySource,
            .acceptedToken = &uploadToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());

    GpuTaskSchedulingHint copyScheduling;
    copyScheduling.cost = GpuTaskCostHint::Medium;
    copyScheduling.forceSubmissionBoundary = true;
    copyScheduling.allowPacketMerge = false;
    copyScheduling.allowSameClassQueueRouting = true;
    const GpuCopyBufferTaskRegion copyRegion{
        .source = sourceResource,
        .sourceOffsetBytes = 0u,
        .destination = destinationResource,
        .destinationOffsetBytes = 0u,
        .dataSizeBytes = sizeof(s_SourceWords),
    };
    GpuTaskDesc copyDesc;
    copyDesc
        .setIdentity(Name("tests/descriptor_buffer/same_class_copy"))
        .setMarkerLabel("Same Class Copy")
        .setQueue(graphicsTransferRequest)
        .setScheduling(copyScheduling)
    ;
    QueueSubmissionToken copyToken;
    const GpuTaskId copyTask = graph.addCopyBufferTask(
        copyDesc,
        GpuCopyBufferTaskDesc{
            .regions = &copyRegion,
            .regionCount = 1u,
            .acceptedToken = &copyToken,
        }
    );
    ASSERT_TRUE(copyTask.valid());

    GpuTaskGraphAnalysis analysis(multiQueueScope.arena());
    GpuTaskGraphQueueAssignments assignments(multiQueueScope.arena());
    GpuCompiledGraph compiledGraph(multiQueueScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/same_class_queue_routing_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskQueueAssignment* const uploadAssignment = assignments.find(uploadTask);
    const GpuTaskQueueAssignment* const copyAssignment = assignments.find(copyTask);
    ASSERT_NE(uploadAssignment, nullptr);
    ASSERT_NE(copyAssignment, nullptr);
    EXPECT_EQ(uploadAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(copyAssignment->queue, secondaryGraphicsQueue->id);
    EXPECT_EQ(copyAssignment->reason, GpuTaskQueueAssignmentReason::PreferredQueue);
    EXPECT_TRUE(copyAssignment->modifiers & GpuTaskQueueAssignmentModifier::SameClassLoadBalance);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId uploadPacket = views.compiled.packetForTask(uploadTask);
    const GpuSubmissionPacketId copyPacket = views.compiled.packetForTask(copyTask);
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(copyPacket.valid());
    ASSERT_EQ(views.compiled.packet(copyPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(copyPacket).dependencies[0u].producer, uploadPacket);

    GpuRecordedGraph recordedGraph(multiQueueScope.arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    ASSERT_TRUE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, uploadTask));
    ASSERT_TRUE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, copyTask));

    GpuGraphSubmissionTransaction transaction(multiQueueScope.arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    ASSERT_TRUE(uploadToken.matchesPhysicalQueue(primaryGraphicsQueue.index, primaryGraphicsQueue.deviceGeneration));
    ASSERT_TRUE(copyToken.matchesPhysicalQueue(
        secondaryGraphicsQueue->id.index,
        secondaryGraphicsQueue->id.deviceGeneration
    ));
    ASSERT_TRUE(device.waitForIdle());

    const GpuTaskGraphCompileStatistics& compileStatistics = views.compiled.compileStatistics();
    ASSERT_TRUE(compileStatistics.valid());
    EXPECT_EQ(compileStatistics.crossQueuePacketDependencyCount, 1u);
    EXPECT_EQ(compileStatistics.crossFamilyPacketDependencyCount, 0u);
    const GpuTaskGraphRecordingStatistics recordingStatistics = recordedGraph.recordingStatistics(
        compiledGraph,
        views.compiled
    );
    ASSERT_TRUE(recordingStatistics.valid());
    EXPECT_EQ(recordingStatistics.packetCount, 2u);
    EXPECT_EQ(recordingStatistics.commandListCount, 2u);
    const GpuTaskGraphPhysicalQueueRecordingStatistics primaryQueueRecordingStatistics =
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, views.compiled, primaryGraphicsQueue)
    ;
    const GpuTaskGraphPhysicalQueueRecordingStatistics secondaryQueueRecordingStatistics =
        recordedGraph.physicalQueueRecordingStatistics(compiledGraph, views.compiled, secondaryGraphicsQueue->id)
    ;
    ASSERT_TRUE(primaryQueueRecordingStatistics.valid());
    ASSERT_TRUE(secondaryQueueRecordingStatistics.valid());
    EXPECT_EQ(primaryQueueRecordingStatistics.graphGeneration, views.compiled.generation());
    EXPECT_EQ(primaryQueueRecordingStatistics.planGeneration, views.compiled.planGeneration());
    EXPECT_EQ(primaryQueueRecordingStatistics.recordingAttemptGeneration, recordedGraph.recordingAttemptGeneration());
    EXPECT_EQ(primaryQueueRecordingStatistics.deviceGeneration, views.compiled.deviceGeneration());
    EXPECT_EQ(primaryQueueRecordingStatistics.queue, primaryGraphicsQueue);
    EXPECT_EQ(secondaryQueueRecordingStatistics.queue, secondaryGraphicsQueue->id);
    EXPECT_NE(primaryQueueRecordingStatistics.queue, secondaryQueueRecordingStatistics.queue);
    EXPECT_EQ(primaryQueueRecordingStatistics.queueClass, CommandQueue::Graphics);
    EXPECT_EQ(secondaryQueueRecordingStatistics.queueClass, CommandQueue::Graphics);
    EXPECT_EQ(primaryQueueRecordingStatistics.packetCount, 1u);
    EXPECT_EQ(primaryQueueRecordingStatistics.taskCount, 1u);
    EXPECT_EQ(primaryQueueRecordingStatistics.commandListCount, 1u);
    EXPECT_EQ(primaryQueueRecordingStatistics.workerRoutedPacketCount, 0u);
    EXPECT_EQ(primaryQueueRecordingStatistics.parallelPacketCount, 0u);
    EXPECT_EQ(secondaryQueueRecordingStatistics.packetCount, 1u);
    EXPECT_EQ(secondaryQueueRecordingStatistics.taskCount, 1u);
    EXPECT_EQ(secondaryQueueRecordingStatistics.commandListCount, 1u);
    EXPECT_EQ(secondaryQueueRecordingStatistics.workerRoutedPacketCount, 0u);
    EXPECT_EQ(secondaryQueueRecordingStatistics.parallelPacketCount, 0u);
    // The recorder completed synchronously before these immutable snapshots, so the two exact physical queues sum
    // to the aggregate without an in-flight ready-frontier worker or reset/recompile mutation window.
    EXPECT_EQ(
        primaryQueueRecordingStatistics.packetCount + secondaryQueueRecordingStatistics.packetCount,
        recordingStatistics.packetCount
    );
    EXPECT_EQ(
        primaryQueueRecordingStatistics.taskCount + secondaryQueueRecordingStatistics.taskCount,
        recordingStatistics.taskCount
    );
    EXPECT_EQ(
        primaryQueueRecordingStatistics.commandListCount + secondaryQueueRecordingStatistics.commandListCount,
        recordingStatistics.commandListCount
    );
    EXPECT_EQ(
        primaryQueueRecordingStatistics.barrierCount + secondaryQueueRecordingStatistics.barrierCount,
        recordingStatistics.barrierCount
    );
    EXPECT_EQ(
        primaryQueueRecordingStatistics.parallelPacketCount + secondaryQueueRecordingStatistics.parallelPacketCount,
        recordingStatistics.parallelPacketCount
    );
    EXPECT_EQ(recordingStatistics.workerRoutedPacketCount, 0u);
    EXPECT_EQ(recordingStatistics.parallelPacketCount, 0u);
    EXPECT_NEAR(
        primaryQueueRecordingStatistics.commandListAcquisitionSeconds
            + secondaryQueueRecordingStatistics.commandListAcquisitionSeconds,
        recordingStatistics.commandListAcquisitionSeconds,
        0.000001
    );
    EXPECT_NEAR(
        primaryQueueRecordingStatistics.graphBarrierRecordingSeconds
            + secondaryQueueRecordingStatistics.graphBarrierRecordingSeconds,
        recordingStatistics.graphBarrierRecordingSeconds,
        0.000001
    );
    EXPECT_NEAR(
        primaryQueueRecordingStatistics.taskRecordSeconds + secondaryQueueRecordingStatistics.taskRecordSeconds,
        recordingStatistics.taskRecordSeconds,
        0.000001
    );
    EXPECT_NEAR(
        primaryQueueRecordingStatistics.recordingSeconds + secondaryQueueRecordingStatistics.recordingSeconds,
        recordingStatistics.recordingSeconds,
        0.000001
    );
    EXPECT_GE(recordingStatistics.recordingElapsedSeconds, recordingStatistics.recordingSeconds);
    EXPECT_EQ(recordingStatistics.readyFrontierElapsedSeconds, 0.0);
    EXPECT_EQ(recordingStatistics.readyFrontierWorkerBusySeconds, 0.0);
    EXPECT_EQ(recordingStatistics.readyFrontierWorkerCapacitySeconds, 0.0);
    EXPECT_EQ(recordingStatistics.readyFrontierWorkerUtilization(), 0.0);
    const GpuTaskGraphSubmissionStatistics submissionStatistics = transaction.submissionStatistics();
    ASSERT_TRUE(submissionStatistics.valid());
    EXPECT_EQ(submissionStatistics.acceptedTaskCount, 2u);
    EXPECT_EQ(submissionStatistics.nativeSubmissionCount, 2u);
    EXPECT_EQ(submissionStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(submissionStatistics.sameQueueWaitElisionCount, 0u);
    EXPECT_EQ(submissionStatistics.timelineWaitCount, 1u);
    EXPECT_EQ(submissionStatistics.mergedTimelineWaitCount, 0u);
    EXPECT_EQ(submissionStatistics.nativeSubmissionCountByQueueClass[CommandQueue::Graphics], 2u);
    EXPECT_EQ(submissionStatistics.timelineWaitCountByQueueClass[CommandQueue::Graphics], 1u);

    const GpuTaskGraphPhysicalQueueSubmissionStatistics primaryQueueStatistics =
        transaction.physicalQueueSubmissionStatistics(
            views.compiled,
            primaryGraphicsQueue
        );
    const GpuTaskGraphPhysicalQueueSubmissionStatistics secondaryQueueStatistics =
        transaction.physicalQueueSubmissionStatistics(
            views.compiled,
            secondaryGraphicsQueue->id
        );
    ASSERT_TRUE(primaryQueueStatistics.valid());
    ASSERT_TRUE(secondaryQueueStatistics.valid());
    EXPECT_EQ(primaryQueueStatistics.graphGeneration, views.compiled.generation());
    EXPECT_EQ(primaryQueueStatistics.planGeneration, views.compiled.planGeneration());
    EXPECT_EQ(primaryQueueStatistics.recordingAttemptGeneration, recordedGraph.recordingAttemptGeneration());
    EXPECT_EQ(primaryQueueStatistics.deviceGeneration, views.compiled.deviceGeneration());
    EXPECT_EQ(primaryQueueStatistics.queue, primaryGraphicsQueue);
    EXPECT_EQ(secondaryQueueStatistics.queue, secondaryGraphicsQueue->id);
    EXPECT_NE(primaryQueueStatistics.queue, secondaryQueueStatistics.queue);
    EXPECT_EQ(primaryQueueStatistics.queueClass, CommandQueue::Graphics);
    EXPECT_EQ(secondaryQueueStatistics.queueClass, CommandQueue::Graphics);
    EXPECT_EQ(primaryQueueStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(primaryQueueStatistics.acceptedTaskCount, 1u);
    EXPECT_EQ(primaryQueueStatistics.rejectedPacketCount, 0u);
    EXPECT_EQ(primaryQueueStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(primaryQueueStatistics.nativeCommandListCount, 1u);
    EXPECT_EQ(primaryQueueStatistics.plannedWaitTokenCount, 0u);
    EXPECT_EQ(primaryQueueStatistics.sameQueueWaitElisionCount, 0u);
    EXPECT_EQ(primaryQueueStatistics.timelineWaitCount, 0u);
    EXPECT_EQ(primaryQueueStatistics.mergedTimelineWaitCount, 0u);
    EXPECT_EQ(primaryQueueStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_GE(primaryQueueStatistics.submissionSeconds, 0.0);
    EXPECT_EQ(secondaryQueueStatistics.acceptedPacketCount, 1u);
    EXPECT_EQ(secondaryQueueStatistics.acceptedTaskCount, 1u);
    EXPECT_EQ(secondaryQueueStatistics.rejectedPacketCount, 0u);
    EXPECT_EQ(secondaryQueueStatistics.nativeSubmissionCount, 1u);
    EXPECT_EQ(secondaryQueueStatistics.nativeCommandListCount, 1u);
    EXPECT_EQ(secondaryQueueStatistics.plannedWaitTokenCount, 1u);
    EXPECT_EQ(secondaryQueueStatistics.sameQueueWaitElisionCount, 0u);
    EXPECT_EQ(secondaryQueueStatistics.timelineWaitCount, 1u);
    EXPECT_EQ(secondaryQueueStatistics.mergedTimelineWaitCount, 0u);
    EXPECT_EQ(secondaryQueueStatistics.acceptedFrontierSubmissionCount, 0u);
    EXPECT_GE(secondaryQueueStatistics.submissionSeconds, 0.0);
    // These snapshots are taken after synchronous submission has returned, so their sum matches this aggregate
    // snapshot without a concurrent transaction mutation window.
    EXPECT_EQ(
        primaryQueueStatistics.acceptedPacketCount + secondaryQueueStatistics.acceptedPacketCount,
        submissionStatistics.acceptedPacketCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.acceptedTaskCount + secondaryQueueStatistics.acceptedTaskCount,
        submissionStatistics.acceptedTaskCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.rejectedPacketCount + secondaryQueueStatistics.rejectedPacketCount,
        submissionStatistics.rejectedPacketCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.rejectedTaskCount + secondaryQueueStatistics.rejectedTaskCount,
        submissionStatistics.rejectedTaskCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.nativeSubmissionCount + secondaryQueueStatistics.nativeSubmissionCount,
        submissionStatistics.nativeSubmissionCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.rejectedSubmissionCount + secondaryQueueStatistics.rejectedSubmissionCount,
        submissionStatistics.rejectedSubmissionCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.nativeCommandListCount + secondaryQueueStatistics.nativeCommandListCount,
        submissionStatistics.nativeCommandListCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.plannedWaitTokenCount + secondaryQueueStatistics.plannedWaitTokenCount,
        submissionStatistics.plannedWaitTokenCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.sameQueueWaitElisionCount + secondaryQueueStatistics.sameQueueWaitElisionCount,
        submissionStatistics.sameQueueWaitElisionCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.timelineWaitCount + secondaryQueueStatistics.timelineWaitCount,
        submissionStatistics.timelineWaitCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.mergedTimelineWaitCount + secondaryQueueStatistics.mergedTimelineWaitCount,
        submissionStatistics.mergedTimelineWaitCount
    );
    EXPECT_EQ(
        primaryQueueStatistics.acceptedFrontierSubmissionCount
            + secondaryQueueStatistics.acceptedFrontierSubmissionCount,
        submissionStatistics.acceptedFrontierSubmissionCount
    );
    EXPECT_NEAR(
        primaryQueueStatistics.submissionSeconds + secondaryQueueStatistics.submissionSeconds,
        submissionStatistics.submissionSeconds,
        0.000001
    );
    EXPECT_EQ(
        primaryQueueStatistics.nativeSubmissionCount + secondaryQueueStatistics.nativeSubmissionCount,
        submissionStatistics.nativeSubmissionCountByQueueClass[CommandQueue::Graphics]
    );
    EXPECT_EQ(
        primaryQueueStatistics.nativeCommandListCount + secondaryQueueStatistics.nativeCommandListCount,
        submissionStatistics.nativeCommandListCountByQueueClass[CommandQueue::Graphics]
    );
    EXPECT_EQ(
        primaryQueueStatistics.timelineWaitCount + secondaryQueueStatistics.timelineWaitCount,
        submissionStatistics.timelineWaitCountByQueueClass[CommandQueue::Graphics]
    );

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(copiedWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// Debug queue forcing is a public compiler input, but its selected physical queue must still survive native packet
// recording and Vulkan submission. Exercise that complete route when the adapter exposes an auxiliary Graphics queue.
TEST_F(DescriptorBufferRoundTripTest, ForcedTimingQueueOverrideRoutesNativeGraphSubmission){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Forced queue override: no usable headless Vulkan device on this host.";

    auto& device = multiQueueScope.graphics().getDevice();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* auxiliaryGraphicsQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass == CommandQueue::Graphics
            && candidate.id != primaryGraphicsQueue
            && candidate.familyIndex == device.getQueueFamilyIndex(primaryGraphicsQueue)
        ){
            auxiliaryGraphicsQueue = &candidate;
            break;
        }
    }
    if(!auxiliaryGraphicsQueue)
        GTEST_SKIP() << "Forced queue override: adapter exposes only one Graphics queue.";

    static constexpr u32 s_ClearValue = 0xa17e5c3du;
    static constexpr usize s_WordCount = 4u;
    auto destination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32) * s_WordCount)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(destination.get(), nullptr);

    GpuTaskGraph graph(multiQueueScope.arena());
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/forced_queue_override_destination"))
            .setMarkerLabel("Forced Queue Override Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(destinationResource.valid());

    GpuQueueRequest graphicsTransferRequest;
    graphicsTransferRequest.requiredCapabilities = GpuQueueCapability::Transfer;
    graphicsTransferRequest.preferredQueue = GpuQueuePreference::Graphics;
    graphicsTransferRequest.allowFallback = false;
    graphicsTransferRequest.compilerMayOverridePreference = false;

    GpuTaskSchedulingHint scheduling;
    scheduling.allowSameClassQueueRouting = true;
    scheduling.allowTimingFeedbackRouting = true;
    const Name taskIdentity("tests/descriptor_buffer/forced_queue_override_clear");
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(taskIdentity)
        .setMarkerLabel("Forced Queue Override Clear")
        .setQueue(graphicsTransferRequest)
        .setScheduling(scheduling)
    ;
    QueueSubmissionToken acceptedToken;
    const GpuTaskId task = graph.addClearBufferTask(
        taskDesc,
        GpuClearBufferTaskDesc{
            .destination = destinationResource,
            .clearValue = s_ClearValue,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuTaskTimingQueueOverride timingOverrides[]{
        GpuTaskTimingQueueOverride{
            .key = GpuTaskTimingKey{
                .task = taskIdentity,
                .queue = CommandQueue::Graphics,
            },
            .queue = auxiliaryGraphicsQueue->id,
        },
    };
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.queueAssignmentOptions.timingQueueOverrides = timingOverrides;
    compileOptions.queueAssignmentOptions.timingQueueOverrideCount = LengthOf(timingOverrides);

    GpuTaskGraphAnalysis analysis(multiQueueScope.arena());
    GpuTaskGraphQueueAssignments assignments(multiQueueScope.arena());
    GpuCompiledGraph compiledGraph(multiQueueScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/forced_queue_override_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        topology,
        assignments,
        compiledGraph,
        scratchArena,
        compileOptions
    ));

    const GpuTaskQueueAssignment* const assignment = assignments.find(task);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->initialQueue, primaryGraphicsQueue);
    EXPECT_NE(assignment->initialQueue, assignment->queue);
    EXPECT_EQ(assignment->queue, auxiliaryGraphicsQueue->id);
    EXPECT_TRUE(assignment->modifiers & GpuTaskQueueAssignmentModifier::DebugTimingOverride);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packet(packet).plan->queue, auxiliaryGraphicsQueue->id);

    GpuRecordedGraph recordedGraph(multiQueueScope.arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));

    GpuGraphSubmissionTransaction transaction(multiQueueScope.arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    ASSERT_TRUE(acceptedToken.matchesPhysicalQueue(
        auxiliaryGraphicsQueue->id.index,
        auxiliaryGraphicsQueue->id.deviceGeneration
    ));
    EXPECT_EQ(acceptedToken.queue, CommandQueue::Graphics);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const clearedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(clearedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < s_WordCount; ++wordIndex)
        EXPECT_EQ(clearedWords[wordIndex], s_ClearValue);
    device.unmapBuffer(destination.get());
}


// A cross-family same-class route must still preserve the broad Graphics API while lowering exclusive resources
// through a physical-owner release/acquire pair. This is topology-gated because most adapters expose one Graphics
// family; when present, exercise the complete compiler -> recorder -> Vulkan submitter path.
TEST_F(DescriptorBufferRoundTripTest, CrossFamilySameClassGraphicsQueuesRouteWithOwnershipTransfers){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    ASSERT_TRUE(multiQueueScope.setCrossFamilySameClassQueueRoutingEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Cross-family same-class queue routing: no usable headless Vulkan device on this host.";

    auto& device = multiQueueScope.graphics().getDevice();
    if(!device.getDescriptorBufferManager().isEnabled())
        GTEST_SKIP() << "Cross-family same-class queue routing: VK_EXT_descriptor_buffer is unavailable on this device.";

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const u32 primaryGraphicsFamily = device.getQueueFamilyIndex(primaryGraphicsQueue);
    const GpuPhysicalQueueInfo* secondaryGraphicsQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass == CommandQueue::Graphics
            && candidate.id != primaryGraphicsQueue
            && candidate.familyIndex != primaryGraphicsFamily
        ){
            secondaryGraphicsQueue = &candidate;
            break;
        }
    }
    if(!secondaryGraphicsQueue){
        GTEST_SKIP() << "Cross-family same-class queue routing: adapter exposes no alternate Graphics family.";
    }

    static constexpr u32 s_SourceWords[] = {
        0x90d412f3u,
        0x2a5c7e19u,
        0x4c9b80d6u,
        0xf10e6a42u,
    };
    auto source = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_SourceWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    auto destination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_SourceWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);

    GpuTaskGraph graph(multiQueueScope.arena());
    const GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/cross_family_same_class_source"))
            .setMarkerLabel("Cross Family Same Class Source")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/cross_family_same_class_destination"))
            .setMarkerLabel("Cross Family Same Class Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuUploadBlobId sourceBlob = graph.copyUploadData(s_SourceWords, sizeof(s_SourceWords), alignof(u32));
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());
    ASSERT_TRUE(sourceBlob.valid());

    GpuQueueRequest graphicsTransferRequest;
    graphicsTransferRequest.requiredCapabilities = GpuQueueCapability::Transfer;
    graphicsTransferRequest.preferredQueue = GpuQueuePreference::Graphics;
    graphicsTransferRequest.allowFallback = false;
    graphicsTransferRequest.compilerMayOverridePreference = false;

    GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = GpuTaskCostHint::Large;
    uploadScheduling.forceSubmissionBoundary = true;
    uploadScheduling.allowPacketMerge = false;
    uploadScheduling.allowSameClassQueueRouting = true;
    uploadScheduling.allowCrossFamilySameClassQueueRouting = true;
    GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/descriptor_buffer/cross_family_same_class_upload"))
        .setMarkerLabel("Cross Family Same Class Upload")
        .setQueue(graphicsTransferRequest)
        .setScheduling(uploadScheduling)
    ;
    QueueSubmissionToken uploadToken;
    const GpuTaskId uploadTask = graph.addUploadBufferTask(
        uploadDesc,
        GpuUploadBufferTaskDesc{
            .source = sourceBlob,
            .destination = sourceResource,
            .finalState = ResourceStates::CopySource,
            .acceptedToken = &uploadToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());

    GpuTaskSchedulingHint copyScheduling;
    copyScheduling.cost = GpuTaskCostHint::Medium;
    copyScheduling.forceSubmissionBoundary = true;
    copyScheduling.allowPacketMerge = false;
    copyScheduling.allowSameClassQueueRouting = true;
    copyScheduling.allowCrossFamilySameClassQueueRouting = true;
    const GpuCopyBufferTaskRegion copyRegion{
        .source = sourceResource,
        .sourceOffsetBytes = 0u,
        .destination = destinationResource,
        .destinationOffsetBytes = 0u,
        .dataSizeBytes = sizeof(s_SourceWords),
    };
    GpuTaskDesc copyDesc;
    copyDesc
        .setIdentity(Name("tests/descriptor_buffer/cross_family_same_class_copy"))
        .setMarkerLabel("Cross Family Same Class Copy")
        .setQueue(graphicsTransferRequest)
        .setScheduling(copyScheduling)
    ;
    QueueSubmissionToken copyToken;
    const GpuTaskId copyTask = graph.addCopyBufferTask(
        copyDesc,
        GpuCopyBufferTaskDesc{
            .regions = &copyRegion,
            .regionCount = 1u,
            .acceptedToken = &copyToken,
        }
    );
    ASSERT_TRUE(copyTask.valid());

    GpuTaskGraphAnalysis analysis(multiQueueScope.arena());
    GpuTaskGraphQueueAssignments assignments(multiQueueScope.arena());
    GpuCompiledGraph compiledGraph(multiQueueScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/cross_family_same_class_queue_routing_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskQueueAssignment* const uploadAssignment = assignments.find(uploadTask);
    const GpuTaskQueueAssignment* const copyAssignment = assignments.find(copyTask);
    ASSERT_NE(uploadAssignment, nullptr);
    ASSERT_NE(copyAssignment, nullptr);
    EXPECT_EQ(uploadAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(copyAssignment->queue, secondaryGraphicsQueue->id);
    EXPECT_EQ(copyAssignment->reason, GpuTaskQueueAssignmentReason::PreferredQueue);
    EXPECT_TRUE(copyAssignment->modifiers & GpuTaskQueueAssignmentModifier::SameClassLoadBalance);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId uploadPacket = views.compiled.packetForTask(uploadTask);
    const GpuSubmissionPacketId copyPacket = views.compiled.packetForTask(copyTask);
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(copyPacket.valid());
    ASSERT_EQ(views.compiled.packet(copyPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(copyPacket).dependencies[0u].producer, uploadPacket);

    const GpuCompiledTaskView compiledUpload = views.compiled.findTask(uploadTask);
    const GpuCompiledTaskView compiledCopy = views.compiled.findTask(copyTask);
    ASSERT_TRUE(compiledUpload.valid());
    ASSERT_TRUE(compiledCopy.valid());
    bool hasOwnershipRelease = false;
    const GpuCompiledBarrier* const uploadBarriers = views.compiled.findTask(uploadTask).epilogueBarriers;
    for(u32 barrierIndex = 0u; barrierIndex < compiledUpload.plan->epilogueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = uploadBarriers[barrierIndex];
        hasOwnershipRelease = hasOwnershipRelease || (
            barrier.type == GpuCompiledBarrierType::BufferOwnershipRelease
            && barrier.resource == sourceResource
            && barrier.sourceQueue == primaryGraphicsQueue
            && barrier.destinationQueue == secondaryGraphicsQueue->id
        );
    }
    bool hasOwnershipAcquire = false;
    const GpuCompiledBarrier* const copyBarriers = views.compiled.findTask(copyTask).prologueBarriers;
    for(u32 barrierIndex = 0u; barrierIndex < compiledCopy.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = copyBarriers[barrierIndex];
        hasOwnershipAcquire = hasOwnershipAcquire || (
            barrier.type == GpuCompiledBarrierType::BufferOwnershipAcquire
            && barrier.resource == sourceResource
            && barrier.sourceQueue == primaryGraphicsQueue
            && barrier.destinationQueue == secondaryGraphicsQueue->id
        );
    }
    EXPECT_TRUE(hasOwnershipRelease);
    EXPECT_TRUE(hasOwnershipAcquire);

    GpuRecordedGraph recordedGraph(multiQueueScope.arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    ASSERT_TRUE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, uploadTask));
    ASSERT_TRUE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, copyTask));

    GpuGraphSubmissionTransaction transaction(multiQueueScope.arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    ASSERT_TRUE(uploadToken.matchesPhysicalQueue(primaryGraphicsQueue.index, primaryGraphicsQueue.deviceGeneration));
    ASSERT_TRUE(copyToken.matchesPhysicalQueue(
        secondaryGraphicsQueue->id.index,
        secondaryGraphicsQueue->id.deviceGeneration
    ));
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(copiedWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// The device registry must expose alternate dedicated Compute/Transfer families with their concrete queue identity
// before the compiler can use its class-generic cross-family ownership path. Most adapters expose only one such
// family, so this remains a topology-qualified probe; graph-unit coverage exercises the ownership plan either way.
TEST_F(DescriptorBufferRoundTripTest, CrossFamilySameClassComputeAndTransferQueuesRegisterWhenAvailable){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setAsyncComputeLaneEnabled(true));
    ASSERT_TRUE(multiQueueScope.setTransferQueueEnabled(true));
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    ASSERT_TRUE(multiQueueScope.setCrossFamilySameClassQueueRoutingEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Cross-family same-class Compute/Transfer routing: no usable headless Vulkan device on this host.";

    auto& device = multiQueueScope.graphics().getDevice();
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    const auto findAuxiliary = [&](const CommandQueue::Enum queueClass){
        const GpuPhysicalQueueId primary = device.getPrimaryPhysicalQueue(queueClass);
        const u32 primaryFamily = device.getQueueFamilyIndex(primary);
        const GpuPhysicalQueueInfo* auxiliary = static_cast<const GpuPhysicalQueueInfo*>(nullptr);
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
            if(
                candidate.queueClass == queueClass
                && candidate.id != primary
                && candidate.familyIndex != primaryFamily
            ){
                auxiliary = &candidate;
                break;
            }
        }
        return auxiliary;
    };

    const GpuPhysicalQueueInfo* const auxiliaryCompute = findAuxiliary(CommandQueue::Compute);
    const GpuPhysicalQueueInfo* const auxiliaryTransfer = findAuxiliary(CommandQueue::Transfer);
    if(!auxiliaryCompute && !auxiliaryTransfer){
        GTEST_SKIP() << "Cross-family same-class Compute/Transfer routing: adapter exposes no alternate dedicated family.";
    }

    if(auxiliaryCompute){
        const GpuPhysicalQueueId primaryCompute = device.getPrimaryPhysicalQueue(CommandQueue::Compute);
        EXPECT_TRUE(primaryCompute.valid());
        EXPECT_TRUE(device.matchesPhysicalQueueIdentity(auxiliaryCompute->id));
        EXPECT_NE(auxiliaryCompute->familyIndex, device.getQueueFamilyIndex(primaryCompute));
        EXPECT_TRUE(static_cast<u8>(auxiliaryCompute->capabilities) & static_cast<u8>(GpuQueueCapability::Compute));
    }
    if(auxiliaryTransfer){
        const GpuPhysicalQueueId primaryTransfer = device.getPrimaryPhysicalQueue(CommandQueue::Transfer);
        EXPECT_TRUE(primaryTransfer.valid());
        EXPECT_TRUE(device.matchesPhysicalQueueIdentity(auxiliaryTransfer->id));
        EXPECT_NE(auxiliaryTransfer->familyIndex, device.getQueueFamilyIndex(primaryTransfer));
        EXPECT_TRUE(static_cast<u8>(auxiliaryTransfer->capabilities) & static_cast<u8>(GpuQueueCapability::Transfer));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

