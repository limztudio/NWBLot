// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "acceptance_observers_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, NativePacketStagesHardwareAvboitLightingCompositeSharedTransaction){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto avboitPrefixBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(avboitPrefixBuffer.get(), nullptr);
    auto avboitOutput = CreateConcurrentTestTexture(device);
    ASSERT_NE(avboitOutput.get(), nullptr);
    Texture* const initialTextures[] = { avboitOutput.get() };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/staged_deferred_buffer"))
            .setMarkerLabel("Staged Deferred Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId avboitPrefix = graph.importBuffer(
        avboitPrefixBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/staged_avboit_prefix"))
            .setMarkerLabel("AVBOIT Prefix")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId avboitAccumulation = graph.importTexture(
        avboitOutput,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/staged_avboit_accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(avboitPrefix.valid());
    ASSERT_TRUE(avboitAccumulation.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const GpuTaskResourceUse hardwareUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc hardwareDesc;
    hardwareDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(hardwareUses, LengthOf(hardwareUses))
    ;
    bool hardwareRecorded = false;
    const GpuTaskId hardwareTask = graph.addTask<NativePacketPrefixTask>(
        hardwareDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &hardwareRecorded,
        }
    );
    ASSERT_TRUE(hardwareTask.valid());

    const GpuTaskResourceUse avboitPreUses[] = {
        GpuTaskResourceUse{
            .resource = avboitPrefix,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc avboitPreDesc;
    avboitPreDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(avboitPreUses, LengthOf(avboitPreUses))
    ;
    bool avboitPreRecorded = false;
    const GpuTaskId avboitPreTask = graph.addTask<NativePacketPrefixTask>(
        avboitPreDesc,
        NativePacketPrefixTask::Payload{
            .buffer = avboitPrefixBuffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = avboitOutput.get(),
            .expectedTextureState = ResourceStates::UnorderedAccess,
            .recorded = &avboitPreRecorded,
        }
    );
    ASSERT_TRUE(avboitPreTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(scheduling)
        .setDependencies(&hardwareTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = graph.addTask<NativePacketPrefixTask>(
        lightingDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &lightingRecorded,
        }
    );
    ASSERT_TRUE(lightingTask.valid());

    const GpuTaskResourceUse compositeUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskId compositeTaskDependencies[] = {
        lightingTask,
        avboitPreTask,
    };
    GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(scheduling)
        .setDependencies(compositeTaskDependencies, LengthOf(compositeTaskDependencies))
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    bool compositeRecorded = false;
    const GpuTaskId compositeTask = graph.addTask<NativePacketPrefixTask>(
        compositeDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .texture = avboitOutput.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .recorded = &compositeRecorded,
        }
    );
    ASSERT_TRUE(compositeTask.valid());

    const GpuPhysicalQueueInfo queue{
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
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/staged_deferred_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskQueueAssignment* const hardwareAssignment = assignments.find(hardwareTask);
    const GpuTaskQueueAssignment* const avboitPreAssignment = assignments.find(avboitPreTask);
    const GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lightingTask);
    const GpuTaskQueueAssignment* const compositeAssignment = assignments.find(compositeTask);
    ASSERT_NE(hardwareAssignment, nullptr);
    ASSERT_NE(avboitPreAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    EXPECT_EQ(hardwareAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(hardwareAssignment->reason, GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_EQ(avboitPreAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(avboitPreAssignment->reason, GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_EQ(lightingAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->reason, GpuTaskQueueAssignmentReason::Fallback);
    EXPECT_EQ(compositeAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(compositeAssignment->reason, GpuTaskQueueAssignmentReason::Fallback);

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 4u);
    const GpuSubmissionPacketRange packetRange = views.compiled.allPacketRange();
    ASSERT_TRUE(packetRange.valid());
    ASSERT_EQ(packetRange.packetCount, views.compiled.packetCount());
    const GpuSubmissionPacketId hardwarePacket = views.compiled.packetForTask(hardwareTask);
    const GpuSubmissionPacketId avboitPrePacket = views.compiled.packetForTask(avboitPreTask);
    const GpuSubmissionPacketId lightingPacket = views.compiled.packetForTask(lightingTask);
    const GpuSubmissionPacketId compositePacket = views.compiled.packetForTask(compositeTask);
    ASSERT_TRUE(hardwarePacket.valid());
    ASSERT_TRUE(avboitPrePacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_EQ(views.compiled.packetIdAt(0u), hardwarePacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), avboitPrePacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), lightingPacket);
    EXPECT_EQ(views.compiled.packetIdAt(3u), compositePacket);
    EXPECT_EQ(views.compiled.packet(hardwarePacket).plan->dependencyCount, 0u);
    EXPECT_EQ(views.compiled.packet(avboitPrePacket).plan->dependencyCount, 0u);
    ASSERT_EQ(views.compiled.packet(lightingPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(lightingPacket).dependencies[0u].producer, hardwarePacket);
    ASSERT_EQ(views.compiled.packet(compositePacket).plan->dependencyCount, 2u);
    const GpuPacketDependency* const compositePacketDependencies = views.compiled.packet(compositePacket).dependencies;
    ASSERT_NE(compositePacketDependencies, nullptr);
    bool compositeWaitsForLighting = false;
    bool compositeWaitsForAvboit = false;
    for(usize index = 0u; index < views.compiled.packet(compositePacket).plan->dependencyCount; ++index){
        compositeWaitsForLighting = compositeWaitsForLighting || compositePacketDependencies[index].producer == lightingPacket;
        compositeWaitsForAvboit = compositeWaitsForAvboit || compositePacketDependencies[index].producer == avboitPrePacket;
    }
    EXPECT_TRUE(compositeWaitsForLighting);
    EXPECT_TRUE(compositeWaitsForAvboit);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    const bool allPacketsRecorded = recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        recordedGraph,
        &failedPacket
    );
    ASSERT_TRUE(allPacketsRecorded) << "failed packet index: " << failedPacket.index;
    EXPECT_TRUE(hardwareRecorded);
    EXPECT_TRUE(avboitPreRecorded);
    EXPECT_TRUE(lightingRecorded);
    EXPECT_TRUE(compositeRecorded);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = hardwarePacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken hardwareToken = transaction.packetToken(hardwarePacket);
    ASSERT_TRUE(hardwareToken.valid());
    EXPECT_FALSE(transaction.packetToken(avboitPrePacket).valid());
    EXPECT_FALSE(transaction.packetToken(lightingPacket).valid());
    EXPECT_FALSE(transaction.packetToken(compositePacket).valid());

    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = lightingPacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken lightingToken = transaction.packetToken(lightingPacket);
    ASSERT_TRUE(lightingToken.valid());
    EXPECT_EQ(transaction.packetToken(hardwarePacket).value, hardwareToken.value);
    EXPECT_FALSE(transaction.packetToken(avboitPrePacket).valid());
    EXPECT_FALSE(transaction.packetToken(compositePacket).valid());

    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = avboitPrePacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken avboitPreToken = transaction.packetToken(avboitPrePacket);
    ASSERT_TRUE(avboitPreToken.valid());
    EXPECT_FALSE(transaction.packetToken(compositePacket).valid());

    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        GpuSubmissionPacketRange{ .first = compositePacket, .packetCount = 1u },
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    ASSERT_TRUE(transaction.packetToken(compositePacket).valid());
    EXPECT_EQ(transaction.packetToken(hardwarePacket).value, hardwareToken.value);
    EXPECT_EQ(transaction.packetToken(avboitPrePacket).value, avboitPreToken.value);
    EXPECT_EQ(transaction.packetToken(lightingPacket).value, lightingToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketLateRecordsHistoryTailInSharedTransaction){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto sourceBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto historyBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto presentationBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto callbackFalseBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(sourceBuffer.get(), nullptr);
    ASSERT_NE(historyBuffer.get(), nullptr);
    ASSERT_NE(presentationBuffer.get(), nullptr);
    ASSERT_NE(callbackFalseBuffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId source = graph.importBuffer(
        sourceBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_source"))
            .setMarkerLabel("Late History Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId history = graph.importBuffer(
        historyBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_destination"))
            .setMarkerLabel("Late History Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId presentation = graph.importBuffer(
        presentationBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_presentation"))
            .setMarkerLabel("Late History Presentation")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId callbackFalseResource = graph.importBuffer(
        callbackFalseBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_accepted_callback_false_resource"))
            .setMarkerLabel("Late History Accepted Callback False Resource")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(history.valid());
    ASSERT_TRUE(presentation.valid());
    ASSERT_TRUE(callbackFalseResource.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const GpuTaskResourceUse sourceProducerUses[] = {
        GpuTaskResourceUse{
            .resource = source,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc sourceProducerDesc;
    sourceProducerDesc
        .setIdentity(Name("tests/descriptor_buffer/late_history_source_producer"))
        .setMarkerLabel("Late History Source Producer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(sourceProducerUses, LengthOf(sourceProducerUses))
    ;
    bool sourceProducerRecorded = false;
    QueueSubmissionToken sourceProducerAcceptedToken;
    const GpuTaskId sourceProducerTask = graph.addTask<NativePacketPrefixTask>(
        sourceProducerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = sourceBuffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &sourceProducerRecorded,
            .acceptedToken = &sourceProducerAcceptedToken,
        }
    );
    ASSERT_TRUE(sourceProducerTask.valid());

    const GpuTaskResourceUse presentUses[] = {
        GpuTaskResourceUse{
            .resource = presentation,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskId presentDependencies[] = { sourceProducerTask };
    GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("tests/descriptor_buffer/late_history_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setDependencies(presentDependencies, LengthOf(presentDependencies))
        .setResourceUses(presentUses, LengthOf(presentUses))
    ;
    bool presentRecorded = false;
    QueueSubmissionToken presentAcceptedToken;
    const GpuTaskId presentTask = graph.addTask<NativePacketPrefixTask>(
        presentDesc,
        NativePacketPrefixTask::Payload{
            .buffer = presentationBuffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &presentRecorded,
            .acceptedToken = &presentAcceptedToken,
        }
    );
    ASSERT_TRUE(presentTask.valid());

    const GpuTaskResourceUse historyUses[] = {
        GpuTaskResourceUse{
            .resource = source,
            .range = {},
            .requiredState = ResourceStates::CopySource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = history,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    // The current-frame producer records through normal execution. The late task imports its authoritative packet
    // snapshot through the compiler-produced state seed, with no declaration or runtime compatibility source.
    const GpuTaskId historyDependencies[] = { presentTask };
    GpuTaskDesc historyDesc;
    historyDesc
        .setIdentity(Name("tests/descriptor_buffer/late_history_copy"))
        .setMarkerLabel("Lagged Lighting History Copy")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
        .setDependencies(historyDependencies, LengthOf(historyDependencies))
        .setResourceUses(historyUses, LengthOf(historyUses))
    ;
    bool historyRecorded = false;
    QueueSubmissionToken historyAcceptedToken;
    const GpuTaskId historyTask = graph.addTask<NativePacketPrefixTask>(
        historyDesc,
        NativePacketPrefixTask::Payload{
            .buffer = sourceBuffer.get(),
            .expectedState = ResourceStates::CopySource,
            .recorded = &historyRecorded,
            .acceptedToken = &historyAcceptedToken,
        }
    );
    ASSERT_TRUE(historyTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskAt(historyTask.index).externalStateSourceCount, 0u);
    }

    const GpuTaskId rejectedTailDependencies[] = { historyTask };
    const GpuTaskResourceUse rejectedTailUses[] = {
        GpuTaskResourceUse{
            .resource = history,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool rejectedTailRecorded = false;
    const GpuTaskId rejectedTailTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_record_callback_reject"))
            .setMarkerLabel("Late History Record Callback Reject")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Transfer,
                true,
                true,
            })
            .setScheduling(scheduling)
            .setDependencies(rejectedTailDependencies, LengthOf(rejectedTailDependencies))
            .setResourceUses(rejectedTailUses, LengthOf(rejectedTailUses)),
        NativePacketPrefixTask::Payload{
            .buffer = historyBuffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &rejectedTailRecorded,
        }
    );
    ASSERT_TRUE(rejectedTailTask.valid());

    u32 preflightRejectedDiscardedCount = 0u;
    bool preflightRejectedRecorded = false;
    const GpuTaskId preflightRejectedTailTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_preflight_reject"))
            .setMarkerLabel("Late History Preflight Reject")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Transfer,
                true,
                true,
            })
            .setScheduling(scheduling)
            .setDependencies(rejectedTailDependencies, LengthOf(rejectedTailDependencies))
            .setResourceUses(rejectedTailUses, LengthOf(rejectedTailUses)),
        NativePacketPrefixTask::Payload{
            .buffer = historyBuffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &preflightRejectedRecorded,
            .discardedCount = &preflightRejectedDiscardedCount,
        }
    );
    ASSERT_TRUE(preflightRejectedTailTask.valid());

    u32 invalidAcceptedCallbackDiscardedCount = 0u;
    bool invalidAcceptedCallbackRecorded = false;
    const GpuTaskId invalidAcceptedCallbackTailTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_invalid_accepted_callback"))
            .setMarkerLabel("Late History Invalid Accepted Callback")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Transfer,
                true,
                true,
            })
            .setScheduling(scheduling)
            .setDependencies(rejectedTailDependencies, LengthOf(rejectedTailDependencies))
            .setResourceUses(rejectedTailUses, LengthOf(rejectedTailUses)),
        NativePacketPrefixTask::Payload{
            .buffer = historyBuffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &invalidAcceptedCallbackRecorded,
            .discardedCount = &invalidAcceptedCallbackDiscardedCount,
        }
    );
    ASSERT_TRUE(invalidAcceptedCallbackTailTask.valid());

    u32 mismatchedAcceptedCallbackDiscardedCount = 0u;
    bool mismatchedAcceptedCallbackRecorded = false;
    const GpuTaskId mismatchedAcceptedCallbackTailTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_mismatched_accepted_callback"))
            .setMarkerLabel("Late History Mismatched Accepted Callback")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Transfer,
                true,
                true,
            })
            .setScheduling(scheduling)
            .setDependencies(rejectedTailDependencies, LengthOf(rejectedTailDependencies))
            .setResourceUses(rejectedTailUses, LengthOf(rejectedTailUses)),
        NativePacketPrefixTask::Payload{
            .buffer = historyBuffer.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &mismatchedAcceptedCallbackRecorded,
            .discardedCount = &mismatchedAcceptedCallbackDiscardedCount,
        }
    );
    ASSERT_TRUE(mismatchedAcceptedCallbackTailTask.valid());

    const GpuTaskResourceUse callbackFalseUses[] = {
        GpuTaskResourceUse{
            .resource = callbackFalseResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    u32 callbackFalseDiscardedCount = 0u;
    bool callbackFalseRecorded = false;
    QueueSubmissionToken callbackFalseTypedToken;
    const GpuTaskId callbackFalseTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_accepted_callback_false"))
            .setMarkerLabel("Late History Accepted Callback False")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling)
            .setResourceUses(callbackFalseUses, LengthOf(callbackFalseUses)),
        NativePacketPrefixTask::Payload{
            .buffer = callbackFalseBuffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &callbackFalseRecorded,
            .acceptedToken = &callbackFalseTypedToken,
            .discardedCount = &callbackFalseDiscardedCount,
        }
    );
    ASSERT_TRUE(callbackFalseTask.valid());

    const GpuPhysicalQueueInfo queue{
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
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/late_history_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId sourceProducerPacket;
    GpuSubmissionPacketId presentPacket;
    GpuSubmissionPacketId historyPacket;
    GpuSubmissionPacketId rejectedTailPacket;
    GpuSubmissionPacketId preflightRejectedTailPacket;
    GpuSubmissionPacketId invalidCallbackPacket;
    GpuSubmissionPacketId mismatchedCallbackPacket;
    GpuSubmissionPacketId callbackFalsePacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        sourceProducerPacket = views.compiled.packetForTask(sourceProducerTask);
        presentPacket = views.compiled.packetForTask(presentTask);
        historyPacket = views.compiled.packetForTask(historyTask);
        rejectedTailPacket = views.compiled.packetForTask(rejectedTailTask);
        preflightRejectedTailPacket = views.compiled.packetForTask(preflightRejectedTailTask);
        invalidCallbackPacket = views.compiled.packetForTask(invalidAcceptedCallbackTailTask);
        mismatchedCallbackPacket = views.compiled.packetForTask(mismatchedAcceptedCallbackTailTask);
        callbackFalsePacket = views.compiled.packetForTask(callbackFalseTask);
        ASSERT_TRUE(sourceProducerPacket.valid());
        ASSERT_TRUE(presentPacket.valid());
        ASSERT_TRUE(historyPacket.valid());
        ASSERT_TRUE(rejectedTailPacket.valid());
        ASSERT_TRUE(preflightRejectedTailPacket.valid());
        ASSERT_TRUE(invalidCallbackPacket.valid());
        ASSERT_TRUE(mismatchedCallbackPacket.valid());
        ASSERT_TRUE(callbackFalsePacket.valid());
        ASSERT_EQ(views.compiled.packetCount(), 8u);
        EXPECT_EQ(views.compiled.packetIdAt(0u), sourceProducerPacket);
        EXPECT_EQ(views.compiled.packetIdAt(1u), presentPacket);
        EXPECT_EQ(views.compiled.packetIdAt(2u), historyPacket);
        EXPECT_EQ(views.compiled.packetIdAt(3u), rejectedTailPacket);
        const GpuCompiledTaskView compiledHistoryTask = views.compiled.findTask(historyTask);
        ASSERT_TRUE(compiledHistoryTask.valid());
        ASSERT_EQ(compiledHistoryTask.plan->prologueStateSeedCount, 1u);
        const GpuPacketStateSeed* const historyStateSeeds = compiledHistoryTask.prologueStateSeeds;
        ASSERT_NE(historyStateSeeds, nullptr);
        EXPECT_EQ(historyStateSeeds[0u].resource, source);
        EXPECT_EQ(historyStateSeeds[0u].sourcePacket, sourceProducerPacket);
        const GpuCompiledPacketView historyPacketView = views.compiled.packet(historyPacket);
        ASSERT_TRUE(historyPacketView.valid());
        ASSERT_GE(historyPacketView.plan->dependencyCount, 1u);
        bool historyWaitsForPresent = false;
        for(usize index = 0u; index < historyPacketView.plan->dependencyCount; ++index)
            historyWaitsForPresent = historyWaitsForPresent || historyPacketView.dependencies[index].producer == presentPacket;
        EXPECT_TRUE(historyWaitsForPresent);
        EXPECT_EQ(historyPacketView.plan->externalDependencyCount, 0u);
        const GpuCompiledPacketView rejectedTailPacketView = views.compiled.packet(rejectedTailPacket);
        ASSERT_TRUE(rejectedTailPacketView.valid());
        ASSERT_EQ(rejectedTailPacketView.plan->dependencyCount, 1u);
        EXPECT_EQ(rejectedTailPacketView.dependencies[0u].producer, historyPacket);
        const GpuCompiledPacketView callbackFalsePacketView = views.compiled.packet(callbackFalsePacket);
        ASSERT_TRUE(callbackFalsePacketView.valid());
        EXPECT_EQ(callbackFalsePacketView.plan->dependencyCount, 0u);
    }

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphSubmitter submitter(device);
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = presentTask;
    GpuSubmissionPacketId normalFailedPacket;
    ASSERT_TRUE(submitter.recordAndSubmitNormalGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &normalFailedPacket
    ));
    EXPECT_FALSE(normalFailedPacket.valid());
    EXPECT_TRUE(sourceProducerRecorded);
    EXPECT_TRUE(presentRecorded);
    const QueueSubmissionToken sourceProducerSubmissionToken = transaction.packetToken(sourceProducerPacket);
    const QueueSubmissionToken presentSubmissionToken = transaction.packetToken(presentPacket);
    ASSERT_TRUE(sourceProducerSubmissionToken.valid());
    ASSERT_TRUE(presentSubmissionToken.valid());
    EXPECT_EQ(sourceProducerAcceptedToken.value, sourceProducerSubmissionToken.value);
    EXPECT_EQ(presentAcceptedToken.value, presentSubmissionToken.value);
    EXPECT_FALSE(transaction.packetToken(historyPacket).valid());

    bool historyFinalStateObserved = false;
    const auto observeHistoryFinalState = [](
        void* const rawContext,
        const CommandListResourceStateHandoff* const finalState
    ) -> bool {
        bool* const observed = static_cast<bool*>(rawContext);
        if(!observed || !finalState)
            return false;
        *observed = true;
        return true;
    };
    const GpuTaskGraphTaskRecordedCallback historyRecordedCallback{
        .task = historyTask,
        .context = &historyFinalStateObserved,
        .invoke = observeHistoryFinalState,
    };
    NativeTaskAcceptanceObserver historyAcceptance;
    const GpuTaskGraphTaskAcceptedCallback historyAcceptedCallback{
        .task = historyTask,
        .context = &historyAcceptance,
        .invoke = ObserveNativeTaskAcceptance,
    };
    ASSERT_TRUE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        historyTask,
        &historyRecordedCallback,
        transaction,
        scratchArena,
        nullptr,
        &historyAcceptedCallback
    ));
    EXPECT_TRUE(historyRecorded);
    EXPECT_TRUE(historyFinalStateObserved);
    const QueueSubmissionToken historySubmissionToken = transaction.packetToken(historyPacket);
    ASSERT_TRUE(historySubmissionToken.valid());
    EXPECT_EQ(historyAcceptedToken.value, historySubmissionToken.value);
    EXPECT_EQ(historyAcceptance.acceptedCount, 1u);
    EXPECT_EQ(historyAcceptance.lastToken.queue, historySubmissionToken.queue);
    EXPECT_EQ(historyAcceptance.lastToken.value, historySubmissionToken.value);
    EXPECT_EQ(historyAcceptance.lastToken.physicalQueueIndex, historySubmissionToken.physicalQueueIndex);
    EXPECT_EQ(historyAcceptance.lastToken.deviceGeneration, historySubmissionToken.deviceGeneration);

    NativeTaskAcceptanceObserver callbackFalseAcceptance;
    callbackFalseAcceptance.continueSubmission = false;
    const GpuTaskGraphTaskAcceptedCallback callbackFalseAcceptedCallback{
        .task = callbackFalseTask,
        .context = &callbackFalseAcceptance,
        .invoke = ObserveNativeTaskAcceptance,
    };
    const GpuTaskGraphSubmissionStatistics statisticsBeforeCallbackFalse = transaction.submissionStatistics();
    GpuSubmissionPacketId callbackFalseFailedPacket;
    EXPECT_FALSE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        callbackFalseTask,
        nullptr,
        transaction,
        scratchArena,
        &callbackFalseFailedPacket,
        &callbackFalseAcceptedCallback
    ));
    EXPECT_EQ(callbackFalseFailedPacket, callbackFalsePacket);
    EXPECT_TRUE(callbackFalseRecorded);
    EXPECT_EQ(callbackFalseAcceptance.acceptedCount, 1u);
    const QueueSubmissionToken callbackFalsePacketToken = transaction.packetToken(callbackFalsePacket);
    QueueSubmissionToken callbackFalseTaskToken;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        callbackFalseTaskToken = transaction.taskToken(views.compiled, callbackFalseTask);
    }
    ASSERT_TRUE(callbackFalsePacketToken.valid());
    ASSERT_TRUE(callbackFalseTaskToken.valid());
    EXPECT_EQ(callbackFalseAcceptance.lastToken.value, callbackFalsePacketToken.value);
    EXPECT_EQ(callbackFalseTypedToken.value, callbackFalsePacketToken.value);
    EXPECT_EQ(callbackFalseTaskToken.value, callbackFalsePacketToken.value);
    // The helper's false-result closeout calls rejectTask(), which must leave an already native-accepted packet
    // untouched rather than discarding its payload or rewriting its terminal state.
    EXPECT_EQ(callbackFalseDiscardedCount, 0u);
    const GpuTaskGraphSubmissionStatistics callbackFalseStatistics = transaction.submissionStatistics();
    EXPECT_EQ(
        callbackFalseStatistics.acceptedPacketCount,
        statisticsBeforeCallbackFalse.acceptedPacketCount + 1u
    );
    EXPECT_EQ(callbackFalseStatistics.acceptedTaskCount, statisticsBeforeCallbackFalse.acceptedTaskCount + 1u);
    EXPECT_EQ(callbackFalseStatistics.rejectedPacketCount, statisticsBeforeCallbackFalse.rejectedPacketCount);
    EXPECT_EQ(
        callbackFalseStatistics.nativeSubmissionCount,
        statisticsBeforeCallbackFalse.nativeSubmissionCount + 1u
    );

    // Validation fails before the recorder can prepare its recorded graph. The helper must still establish the
    // current graph attempt for transactional cleanup, rather than leaving the declared tail armed.
    const GpuTaskGraphTaskRecordedCallback invalidPreflightRecordedCallback{
        .task = preflightRejectedTailTask,
    };
    EXPECT_FALSE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        preflightRejectedTailTask,
        &invalidPreflightRecordedCallback,
        transaction,
        scratchArena
    ));
    EXPECT_FALSE(preflightRejectedRecorded);
    EXPECT_EQ(preflightRejectedDiscardedCount, 1u);
    EXPECT_FALSE(transaction.packetToken(preflightRejectedTailPacket).valid());

    const GpuTaskGraphTaskAcceptedCallback invalidAcceptedCallback{
        .task = invalidAcceptedCallbackTailTask,
    };
    const GpuTaskGraphSubmissionStatistics statisticsBeforeInvalidAcceptedCallback =
        transaction.submissionStatistics()
    ;
    EXPECT_FALSE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        invalidAcceptedCallbackTailTask,
        nullptr,
        transaction,
        scratchArena,
        nullptr,
        &invalidAcceptedCallback
    ));
    EXPECT_FALSE(invalidAcceptedCallbackRecorded);
    EXPECT_EQ(invalidAcceptedCallbackDiscardedCount, 1u);
    EXPECT_FALSE(transaction.packetToken(invalidCallbackPacket).valid());
    const GpuTaskGraphSubmissionStatistics invalidAcceptedCallbackStatistics =
        transaction.submissionStatistics()
    ;
    EXPECT_EQ(
        invalidAcceptedCallbackStatistics.acceptedPacketCount,
        statisticsBeforeInvalidAcceptedCallback.acceptedPacketCount
    );
    EXPECT_EQ(
        invalidAcceptedCallbackStatistics.rejectedPacketCount,
        statisticsBeforeInvalidAcceptedCallback.rejectedPacketCount + 1u
    );
    EXPECT_EQ(
        invalidAcceptedCallbackStatistics.nativeSubmissionCount,
        statisticsBeforeInvalidAcceptedCallback.nativeSubmissionCount
    );

    NativeTaskAcceptanceObserver mismatchedAcceptance;
    const GpuTaskGraphTaskAcceptedCallback mismatchedAcceptedCallback{
        .task = historyTask,
        .context = &mismatchedAcceptance,
        .invoke = ObserveNativeTaskAcceptance,
    };
    const GpuTaskGraphSubmissionStatistics statisticsBeforeMismatchedAcceptedCallback =
        transaction.submissionStatistics()
    ;
    EXPECT_FALSE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        mismatchedAcceptedCallbackTailTask,
        nullptr,
        transaction,
        scratchArena,
        nullptr,
        &mismatchedAcceptedCallback
    ));
    EXPECT_FALSE(mismatchedAcceptedCallbackRecorded);
    EXPECT_EQ(mismatchedAcceptedCallbackDiscardedCount, 1u);
    EXPECT_EQ(mismatchedAcceptance.acceptedCount, 0u);
    EXPECT_FALSE(transaction.packetToken(mismatchedCallbackPacket).valid());
    const GpuTaskGraphSubmissionStatistics mismatchedAcceptedCallbackStatistics =
        transaction.submissionStatistics()
    ;
    EXPECT_EQ(
        mismatchedAcceptedCallbackStatistics.acceptedPacketCount,
        statisticsBeforeMismatchedAcceptedCallback.acceptedPacketCount
    );
    EXPECT_EQ(
        mismatchedAcceptedCallbackStatistics.rejectedPacketCount,
        statisticsBeforeMismatchedAcceptedCallback.rejectedPacketCount + 1u
    );
    EXPECT_EQ(
        mismatchedAcceptedCallbackStatistics.nativeSubmissionCount,
        statisticsBeforeMismatchedAcceptedCallback.nativeSubmissionCount
    );

    const auto rejectLateTailAfterRecord = [](
        void* const context,
        const CommandListResourceStateHandoff* const finalState
    ) -> bool {
        static_cast<void>(context);
        static_cast<void>(finalState);
        return false;
    };
    const GpuTaskGraphTaskRecordedCallback rejectedTailRecordedCallback{
        .task = rejectedTailTask,
        .invoke = rejectLateTailAfterRecord,
    };
    EXPECT_FALSE(submitter.recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        rejectedTailTask,
        &rejectedTailRecordedCallback,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(rejectedTailRecorded);
    EXPECT_FALSE(transaction.packetToken(rejectedTailPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
    EXPECT_TRUE(transaction.tryReset(compiledGraph));
    EXPECT_TRUE(graph.tryReset());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

