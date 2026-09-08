// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Upload blobs copy caller bytes at declaration, then record through the ordinary CommandList staging allocator.
// Mutating the original stack storage before late packet recording must therefore not affect the submitted upload.
TEST_F(DescriptorBufferRoundTripTest, BuiltInUploadBufferTaskCopiesGraphOwnedBlobAndPublishesAcceptedToken){
    auto& device = DescriptorBufferRoundTripTest::device();
    u32 sourceWords[] = {
        0x13c0ffeeu,
        0x4a7b12d3u,
        0x9e3779b9u,
        0xfeedfaceu,
    };
    static constexpr u32 s_ExpectedWords[] = {
        0x13c0ffeeu,
        0x4a7b12d3u,
        0x9e3779b9u,
        0xfeedfaceu,
    };
    auto destination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(sourceWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(destination.get(), nullptr);
    auto keepInitialDestination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(sourceWords))
            .enableAutomaticStateTracking(ResourceStates::Common)
    );
    ASSERT_NE(keepInitialDestination.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_buffer_destination"))
            .setMarkerLabel("Built-In Upload Buffer Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(destinationResource.valid());
    const GpuGraphResourceId keepInitialDestinationResource = graph.importBuffer(
        keepInitialDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_buffer_keep_initial_destination"))
            .setMarkerLabel("Built-In Upload Buffer Keep Initial Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(keepInitialDestinationResource.valid());
    const GpuUploadBlobId source = graph.copyUploadData(sourceWords, sizeof(sourceWords), alignof(u32));
    ASSERT_TRUE(source.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.validUploadBlob(source));
        ASSERT_EQ(declarations.uploadBlobCount(), 1u);
    }
    for(u32& word : sourceWords)
        word = 0u;

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_upload_buffer"))
        .setMarkerLabel("Built-In Buffer Upload")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
    ;
    const QueueSubmissionToken staleToken{
        .queue = CommandQueue::Graphics,
        .value = 7u,
    };
    ASSERT_TRUE(staleToken.valid());
    QueueSubmissionToken acceptedToken = staleToken;
    const GpuTaskId uploadTask = graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destinationResource,
            .finalState = ResourceStates::ShaderResource,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());
    // Declaration clears a stale caller token. It is populated only once the packet reaches queue submission.
    EXPECT_FALSE(acceptedToken.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.taskAt(uploadTask.index).hasPayload);
        const GpuTaskGraphTaskView uploadTaskView = declarations.taskAt(uploadTask.index);
        ASSERT_EQ(uploadTaskView.resourceUseCount, 2u);
        ASSERT_NE(uploadTaskView.resourceUses, nullptr);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].resource, destinationResource);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].requiredState, ResourceStates::CopyDest);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].access, GpuTaskResourceAccess::Write);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].resource, destinationResource);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].requiredState, ResourceStates::ShaderResource);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].access, GpuTaskResourceAccess::Write);
    }
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destinationResource,
            .destinationOffsetBytes = sizeof(sourceWords),
        }
    ).valid());
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destinationResource,
            .destinationOffsetBytes = 1u,
        }
    ).valid());
    const u8 unalignedByte = 0xabu;
    const GpuUploadBlobId unalignedSource = graph.copyUploadData(&unalignedByte, sizeof(unalignedByte));
    ASSERT_TRUE(unalignedSource.valid());
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = unalignedSource,
            .destination = destinationResource,
        }
    ).valid());
    // Automatic state tracking restores the backend descriptor state when a command list closes. A different
    // graph-visible final state would leave the next packet with an incorrect compiler seed, so reject it early.
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = keepInitialDestinationResource,
            .finalState = ResourceStates::ShaderResource,
        }
    ).valid());

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .dedicated = true,
        };
        ++queueCount;
    }
    const GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = queueCount,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_upload_buffer_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskQueueAssignment* const assignment = assignments.find(uploadTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, dedicatedTransfer ? CommandQueue::Transfer : CommandQueue::Graphics);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId packet = views.compiled.packetForTask(uploadTask);
    ASSERT_TRUE(packet.valid());

    const GpuNativePacketRecorder recorder(device);
    // Upload blobs deliberately have no command-IR encoding. Capture failure terminalizes the single-task attempt
    // and clears its output token before any submission transaction can bind that exact attempt.
    GpuRecordedGraph rejectedRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction rejectedTransaction(DescriptorBufferRoundTripTest::arena());
    rejectedTransaction.reset(compiledGraph);
    GpuCommandIrCapture rejectedCapture(DescriptorBufferRoundTripTest::arena());
    acceptedToken = staleToken;
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        rejectedRecordedGraph,
        nullptr,
        &rejectedCapture
    ));
    const u64 rejectedRecordingAttemptGeneration = rejectedRecordedGraph.recordingAttemptGeneration();
    ASSERT_NE(rejectedRecordingAttemptGeneration, 0u);
    EXPECT_FALSE(acceptedToken.valid());
    EXPECT_FALSE(rejectedTransaction.discardUnaccepted(
        graph,
        compiledGraph,
        rejectedRecordingAttemptGeneration
    ));
    EXPECT_FALSE(acceptedToken.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    ASSERT_TRUE(recordedGraph.hasTaskFinalStateSeed(compiledGraph, views.compiled, uploadTask));

    // A successfully recorded packet reserves its graph work for this attempt. A second output artifact cannot
    // record an identical native command list before the first one reaches submission.
    GpuRecordedGraph duplicateRecordedGraph(DescriptorBufferRoundTripTest::arena());
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        duplicateRecordedGraph
    ));

    const GpuTaskScheduler submitter(device);
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
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.queue, packetToken.queue);
    EXPECT_EQ(acceptedToken.value, packetToken.value);

    // Reusing a valid recorded packet through a fresh transaction must fail before Device::executeCommandLists().
    // Its task already accepted the first token, so the graph-owned readiness preflight prevents double execution.
    GpuGraphSubmissionTransaction duplicateTransaction(DescriptorBufferRoundTripTest::arena());
    duplicateTransaction.reset(compiledGraph);
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        duplicateTransaction,
        scratchArena
    ));
    EXPECT_FALSE(duplicateTransaction.packetToken(packet).valid());
    EXPECT_EQ(acceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const uploadedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(uploadedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_ExpectedWords); ++wordIndex)
        EXPECT_EQ(uploadedWords[wordIndex], s_ExpectedWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// Occupancy, Extinction, and Accumulation deliberately overwrite the same material-instance buffer at separate
// AVBOIT write points. Each source must remain an immutable graph blob through late recording, and each phase's
// accepted token must remain invalid when its packet is discarded.
TEST_F(DescriptorBufferRoundTripTest, AvboitPhaseUploadsKeepImmutableSnapshotsIsolated){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_OccupancyWords[] = {
        0x0cc0a001u,
        0x0cc0a002u,
        0x0cc0a003u,
        0x0cc0a004u,
    };
    static constexpr u32 s_ExtinctionWords[] = {
        0x0e710001u,
        0x0e710002u,
        0x0e710003u,
        0x0e710004u,
    };
    static constexpr u32 s_AccumulationWords[] = {
        0xacce0001u,
        0xacce0002u,
        0xacce0003u,
        0xacce0004u,
    };
    u32 phaseWords[LengthOf(s_OccupancyWords)] = {};
    const auto copyWords = [](u32* const destination, const u32* const source){
        for(usize wordIndex = 0u; wordIndex < LengthOf(s_OccupancyWords); ++wordIndex)
            destination[wordIndex] = source[wordIndex];
    };

    auto materialInstances = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(phaseWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(materialInstances.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId materialInstancesResource = graph.importBuffer(
        materialInstances,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_phase_material_instances"))
            .setMarkerLabel("AVBOIT Phase Material Instances")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(materialInstancesResource.valid());

    copyWords(phaseWords, s_OccupancyWords);
    const GpuUploadBlobId occupancyBlob = graph.copyUploadData(phaseWords, sizeof(phaseWords), alignof(u32));
    copyWords(phaseWords, s_ExtinctionWords);
    const GpuUploadBlobId extinctionBlob = graph.copyUploadData(phaseWords, sizeof(phaseWords), alignof(u32));
    copyWords(phaseWords, s_AccumulationWords);
    const GpuUploadBlobId accumulationBlob = graph.copyUploadData(phaseWords, sizeof(phaseWords), alignof(u32));
    ASSERT_TRUE(occupancyBlob.valid());
    ASSERT_TRUE(extinctionBlob.valid());
    ASSERT_TRUE(accumulationBlob.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_EQ(declarations.uploadBlobCount(), 3u);
    }
    for(u32& word : phaseWords)
        word = 0u;

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest graphicsUploadQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const QueueSubmissionToken staleToken{
        .queue = CommandQueue::Graphics,
        .value = 17u,
    };
    QueueSubmissionToken occupancyAcceptedToken = staleToken;
    QueueSubmissionToken extinctionAcceptedToken = staleToken;
    QueueSubmissionToken accumulationAcceptedToken = staleToken;

    GpuTaskDesc occupancyDesc;
    occupancyDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_occupancy_material_instances_upload"))
        .setMarkerLabel("AVBOIT Occupancy Material Instances Upload")
        .setQueue(graphicsUploadQueue)
        .setScheduling(scheduling)
    ;
    const GpuTaskId occupancyUpload = graph.addUploadBufferTask(
        occupancyDesc,
        GpuUploadBufferTaskDesc{
            .source = occupancyBlob,
            .destination = materialInstancesResource,
            .finalState = ResourceStates::Common,
            .acceptedToken = &occupancyAcceptedToken,
        }
    );
    ASSERT_TRUE(occupancyUpload.valid());

    GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_material_instances_upload"))
        .setMarkerLabel("AVBOIT Extinction Material Instances Upload")
        .setQueue(graphicsUploadQueue)
        .setScheduling(scheduling)
        .setDependencies(&occupancyUpload, 1u)
    ;
    const GpuTaskId extinctionUpload = graph.addUploadBufferTask(
        extinctionDesc,
        GpuUploadBufferTaskDesc{
            .source = extinctionBlob,
            .destination = materialInstancesResource,
            .finalState = ResourceStates::Common,
            .acceptedToken = &extinctionAcceptedToken,
        }
    );
    ASSERT_TRUE(extinctionUpload.valid());

    GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_material_instances_upload"))
        .setMarkerLabel("AVBOIT Accumulation Material Instances Upload")
        .setQueue(graphicsUploadQueue)
        .setScheduling(scheduling)
        .setDependencies(&extinctionUpload, 1u)
    ;
    const GpuTaskId accumulationUpload = graph.addUploadBufferTask(
        accumulationDesc,
        GpuUploadBufferTaskDesc{
            .source = accumulationBlob,
            .destination = materialInstancesResource,
            .finalState = ResourceStates::Common,
            .acceptedToken = &accumulationAcceptedToken,
        }
    );
    ASSERT_TRUE(accumulationUpload.valid());
    EXPECT_FALSE(occupancyAcceptedToken.valid());
    EXPECT_FALSE(extinctionAcceptedToken.valid());
    EXPECT_FALSE(accumulationAcceptedToken.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/avboit_phase_upload_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 3u);
    const GpuSubmissionPacketId occupancyPacket = views.compiled.packetForTask(occupancyUpload);
    const GpuSubmissionPacketId extinctionPacket = views.compiled.packetForTask(extinctionUpload);
    const GpuSubmissionPacketId accumulationPacket = views.compiled.packetForTask(accumulationUpload);
    ASSERT_TRUE(occupancyPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    EXPECT_LT(occupancyPacket.index, extinctionPacket.index);
    EXPECT_LT(extinctionPacket.index, accumulationPacket.index);

    const GpuNativePacketRecorder recorder(device);
    GpuRecordedGraph rejectedRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction rejectedTransaction(DescriptorBufferRoundTripTest::arena());
    rejectedTransaction.reset(compiledGraph);
    GpuCommandIrCapture rejectedCapture(DescriptorBufferRoundTripTest::arena());
    occupancyAcceptedToken = staleToken;
    extinctionAcceptedToken = staleToken;
    accumulationAcceptedToken = staleToken;
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = occupancyPacket, .packetCount = 1u },
        rejectedRecordedGraph,
        nullptr,
        &rejectedCapture
    ));
    EXPECT_TRUE(rejectedTransaction.discardUnaccepted(
        graph,
        compiledGraph,
        rejectedRecordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_FALSE(occupancyAcceptedToken.valid());
    EXPECT_FALSE(extinctionAcceptedToken.valid());
    EXPECT_FALSE(accumulationAcceptedToken.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    const auto submitAndVerify = [&](const GpuSubmissionPacketId packet, QueueSubmissionToken& acceptedToken, const u32* const expectedWords){
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
        const QueueSubmissionToken packetToken = transaction.packetToken(packet);
        ASSERT_TRUE(packetToken.valid());
        EXPECT_TRUE(acceptedToken.valid());
        EXPECT_EQ(acceptedToken.queue, packetToken.queue);
        EXPECT_EQ(acceptedToken.value, packetToken.value);
        ASSERT_TRUE(device.waitForIdle());

        const u32* const uploadedWords = static_cast<const u32*>(device.mapBuffer(materialInstances.get(), CpuAccessMode::Read));
        ASSERT_NE(uploadedWords, nullptr);
        for(usize wordIndex = 0u; wordIndex < LengthOf(s_OccupancyWords); ++wordIndex)
            EXPECT_EQ(uploadedWords[wordIndex], expectedWords[wordIndex]);
        device.unmapBuffer(materialInstances.get());
    };
    submitAndVerify(occupancyPacket, occupancyAcceptedToken, s_OccupancyWords);
    submitAndVerify(extinctionPacket, extinctionAcceptedToken, s_ExtinctionWords);
    submitAndVerify(accumulationPacket, accumulationAcceptedToken, s_AccumulationWords);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

