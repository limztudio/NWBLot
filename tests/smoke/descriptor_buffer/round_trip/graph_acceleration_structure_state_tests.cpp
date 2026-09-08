// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Prepared acceleration-structure recording owns no native entry or exit transition on the normal graph path.
// These getter-only callbacks use real Vulkan TLAS/BLAS objects and frozen position/index build inputs, but import
// no backing Buffer as a second graph resource. The compiler must lower the immutable build-input set plus typed
// Write -> Read finalization handoffs in one accepting Graphics packet without a callback-local transition bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedPreparedAccelStructStateFinalizeRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "Ray tracing acceleration structures are not enabled on this device.";

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/prepared_tlas_state_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};
    RayTracingAccelStructDesc tlasDesc(descArena);
    tlasDesc
        .setTopLevelMaxInstances(1u)
        .setDebugName(Name{"tests/descriptor_buffer/prepared_tlas_state"})
    ;
    auto tlas = device.createAccelStruct(tlasDesc);
    ASSERT_NE(tlas.get(), nullptr);
    const BufferHandle tlasBacking = tlas->getBackingBufferHandle();
    ASSERT_NE(tlasBacking.get(), nullptr);
    ASSERT_EQ(tlas->getBackingBuffer(), tlasBacking.get());

    auto blasPosition = device.createBuffer(
        BufferDesc()
            .setByteSize(3u * 3u * sizeof(f32))
            .setStructStride(3u * sizeof(f32))
            .setIsAccelStructBuildInput(true)
            .setInitialState(ResourceStates::Common)
    );
    auto blasIndex = device.createBuffer(
        BufferDesc()
            .setByteSize(3u * sizeof(u32))
            .setStructStride(sizeof(u32))
            .setIsAccelStructBuildInput(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(blasPosition.get(), nullptr);
    ASSERT_NE(blasIndex.get(), nullptr);

    RayTracingGeometryTriangles blasTriangles;
    blasTriangles
        .setVertexBuffer(blasPosition.get())
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(3u * sizeof(f32))
        .setVertexCount(3u)
        .setIndexBuffer(blasIndex.get())
        .setIndexFormat(Format::R32_UINT)
        .setIndexCount(3u)
    ;
    RayTracingGeometryDesc blasGeometry;
    blasGeometry
        .setTriangles(blasTriangles)
        .setFlags(RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
    ;
    RayTracingAccelStructDesc blasDesc(descArena);
    blasDesc
        .addBottomLevelGeometry(blasGeometry)
        .setBuildFlags(RayTracingAccelStructBuildFlags::PreferFastTrace)
        .setDebugName(Name{"tests/descriptor_buffer/prepared_blas_state"})
    ;
    auto blas = device.createAccelStruct(blasDesc);
    ASSERT_NE(blas.get(), nullptr);
    const BufferHandle blasBacking = blas->getBackingBufferHandle();
    ASSERT_NE(blasBacking.get(), nullptr);
    ASSERT_EQ(blas->getBackingBuffer(), blasBacking.get());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId tlasResource = graph.importAccelStruct(
        tlas,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/prepared_tlas_state"))
            .setMarkerLabel("Prepared TLAS")
            .setType(GpuGraphResourceType::AccelStruct)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId blasResource = graph.importAccelStruct(
        blas,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/prepared_blas_state"))
            .setMarkerLabel("Prepared BLAS")
            .setType(GpuGraphResourceType::AccelStruct)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId blasPositionResource = graph.importBuffer(
        blasPosition,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/prepared_blas_position_input_state"))
            .setMarkerLabel("Prepared BLAS Position Input")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId blasIndexResource = graph.importBuffer(
        blasIndex,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/prepared_blas_index_input_state"))
            .setMarkerLabel("Prepared BLAS Index Input")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(tlasResource.valid());
    ASSERT_TRUE(blasResource.valid());
    ASSERT_TRUE(blasPositionResource.valid());
    ASSERT_TRUE(blasIndexResource.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(declarations.findImportedBuffer(tlasBacking).valid());
        EXPECT_FALSE(declarations.findImportedBuffer(blasBacking).valid());
    }

    const GpuGraphResourceId geometryBuildInputMembers[] = {
        blasPositionResource,
        blasIndexResource,
    };
    const GpuGraphResourceSetId geometryBuildInputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/prepared_blas_geometry_build_inputs"))
            .setMarkerLabel("Prepared BLAS Geometry Build Inputs")
            .setMembers(geometryBuildInputMembers, LengthOf(geometryBuildInputMembers))
    );
    ASSERT_TRUE(geometryBuildInputSet.valid());

    const GpuGraphResourceId finalizeMembers[] = {
        tlasResource,
        blasResource,
    };
    const GpuGraphResourceSetId finalizeSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/prepared_accel_struct_finalize_resources"))
            .setMarkerLabel("Prepared Accel-Struct Finalize Resources")
            .setMembers(finalizeMembers, LengthOf(finalizeMembers))
    );
    ASSERT_TRUE(finalizeSet.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint buildScheduling;
    buildScheduling.cost = GpuTaskCostHint::Medium;
    buildScheduling.forceSubmissionBoundary = false;
    buildScheduling.allowPacketMerge = true;
    const GpuTaskResourceSetUse geometryBuildInputSetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = geometryBuildInputSet,
            .range = {},
            .requiredState = ResourceStates::AccelStructBuildInput,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc geometryBuildInputDesc;
    geometryBuildInputDesc
        .setIdentity(Name("tests/descriptor_buffer/prepared_blas_geometry_build_inputs"))
        .setMarkerLabel("Prepared BLAS Geometry Build Inputs")
        .setQueue(graphicsQueue)
        .setScheduling(buildScheduling)
        .setResourceSetUses(geometryBuildInputSetUses, LengthOf(geometryBuildInputSetUses))
    ;
    bool geometryBuildInputRecorded = false;
    const GpuTaskId geometryBuildInputTask = graph.addTask<NativePacketPrefixTask>(
        geometryBuildInputDesc,
        NativePacketPrefixTask::Payload{
            .buffer = blasPosition.get(),
            .expectedState = ResourceStates::AccelStructBuildInput,
            .additionalBuffer = blasIndex.get(),
            .expectedAdditionalBufferState = ResourceStates::AccelStructBuildInput,
            .recorded = &geometryBuildInputRecorded,
        }
    );
    ASSERT_TRUE(geometryBuildInputTask.valid());

    buildScheduling.mergeWithPrevious = true;
    const GpuTaskResourceUse buildUses[] = {
        GpuTaskResourceUse{
            .resource = tlasResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructWrite,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = blasResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructWrite,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc buildDesc;
    buildDesc
        .setIdentity(Name("tests/descriptor_buffer/prepared_accel_struct_build"))
        .setMarkerLabel("Prepared Accel-Struct Build")
        .setQueue(graphicsQueue)
        .setScheduling(buildScheduling)
        .setDependencies(&geometryBuildInputTask, 1u)
        .setResourceUses(buildUses, LengthOf(buildUses))
    ;
    bool buildRecorded = false;
    const GpuTaskId buildTask = graph.addTask<NativePacketPrefixTask>(
        buildDesc,
        NativePacketPrefixTask::Payload{
            .buffer = tlasBacking.get(),
            .expectedState = ResourceStates::AccelStructWrite,
            .additionalBuffer = blasBacking.get(),
            .expectedAdditionalBufferState = ResourceStates::AccelStructWrite,
            .recorded = &buildRecorded,
        }
    );
    ASSERT_TRUE(buildTask.valid());

    GpuTaskSchedulingHint finalizeScheduling;
    finalizeScheduling.cost = GpuTaskCostHint::Tiny;
    finalizeScheduling.forceSubmissionBoundary = false;
    finalizeScheduling.allowPacketMerge = true;
    finalizeScheduling.mergeWithPrevious = true;
    finalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    const GpuTaskResourceSetUse finalizeSetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = finalizeSet,
            .range = {},
            .requiredState = ResourceStates::AccelStructRead,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc finalizeDesc;
    finalizeDesc
        .setIdentity(Name("tests/descriptor_buffer/prepared_accel_struct_finalize"))
        .setMarkerLabel("Prepared Accel-Struct Finalize")
        .setQueue(graphicsQueue)
        .setScheduling(finalizeScheduling)
        .setDependencies(&buildTask, 1u)
        .setResourceSetUses(finalizeSetUses, LengthOf(finalizeSetUses))
    ;
    bool finalizeRecorded = false;
    const GpuTaskId finalizeTask = graph.addTask<NativePacketPrefixTask>(
        finalizeDesc,
        NativePacketPrefixTask::Payload{
            .buffer = tlasBacking.get(),
            .expectedState = ResourceStates::AccelStructRead,
            .additionalBuffer = blasBacking.get(),
            .expectedAdditionalBufferState = ResourceStates::AccelStructRead,
            .recorded = &finalizeRecorded,
        }
    );
    ASSERT_TRUE(finalizeTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/prepared_tlas_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(geometryBuildInputTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(buildTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(finalizeTask), packet);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 3u);

    const GpuCompiledTaskView compiledGeometryBuildInputs = views.compiled.findTask(geometryBuildInputTask);
    const GpuCompiledTaskView compiledBuild = views.compiled.findTask(buildTask);
    const GpuCompiledTaskView compiledFinalize = views.compiled.findTask(finalizeTask);
    ASSERT_TRUE(compiledGeometryBuildInputs.valid());
    ASSERT_TRUE(compiledBuild.valid());
    ASSERT_TRUE(compiledFinalize.valid());
    const auto hasTransition = [&](
        const GpuTaskId task,
        const GpuCompiledTask& compiledTask,
        const GpuCompiledBarrierType::Enum type,
        const GpuGraphResourceId resource,
        const ResourceStates::Mask before,
        const ResourceStates::Mask after
    ){
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(compiledTask.prologueBarrierCount != 0u && !barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(
        geometryBuildInputTask,
        *compiledGeometryBuildInputs.plan,
        GpuCompiledBarrierType::BufferTransition,
        blasPositionResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        geometryBuildInputTask,
        *compiledGeometryBuildInputs.plan,
        GpuCompiledBarrierType::BufferTransition,
        blasIndexResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        buildTask,
        *compiledBuild.plan,
        GpuCompiledBarrierType::AccelStructTransition,
        tlasResource,
        ResourceStates::Common,
        ResourceStates::AccelStructWrite
    ));
    EXPECT_TRUE(hasTransition(
        buildTask,
        *compiledBuild.plan,
        GpuCompiledBarrierType::AccelStructTransition,
        blasResource,
        ResourceStates::Common,
        ResourceStates::AccelStructWrite
    ));
    EXPECT_TRUE(hasTransition(
        finalizeTask,
        *compiledFinalize.plan,
        GpuCompiledBarrierType::AccelStructTransition,
        tlasResource,
        ResourceStates::AccelStructWrite,
        ResourceStates::AccelStructRead
    ));
    EXPECT_TRUE(hasTransition(
        finalizeTask,
        *compiledFinalize.plan,
        GpuCompiledBarrierType::AccelStructTransition,
        blasResource,
        ResourceStates::AccelStructWrite,
        ResourceStates::AccelStructRead
    ));

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(geometryBuildInputRecorded);
    EXPECT_TRUE(buildRecorded);
    EXPECT_TRUE(finalizeRecorded);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        geometryBuildInputTask,
        finalStateStorage
    ));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;

    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(blasPosition.get()), ResourceStates::AccelStructBuildInput);
    EXPECT_EQ(stateProbe->getBufferState(blasIndex.get()), ResourceStates::AccelStructBuildInput);
    EXPECT_EQ(stateProbe->getBufferState(tlasBacking.get()), ResourceStates::AccelStructRead);
    EXPECT_EQ(stateProbe->getBufferState(blasBacking.get()), ResourceStates::AccelStructRead);
    stateProbe->close();

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
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
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Typed acceleration-structure declarations now carry the backing allocation through packet state seeds and
// terminal exports. This deliberately imports no backing Buffer: if either path falls back to the former manual
// buffer declaration, the second packet observes Common instead of the producer's AccelStructWrite state.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnsAccelStructPacketStateAndExternalExportWithoutBackingImport){
    auto& device = DescriptorBufferRoundTripTest::device();
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "Acceleration structures are not enabled on this device.";

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/typed_accel_struct_handoff_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};
    RayTracingAccelStructDesc tlasDesc(descArena);
    tlasDesc
        .setTopLevelMaxInstances(1u)
        .setDebugName(Name{"tests/descriptor_buffer/typed_accel_struct_handoff"})
    ;
    RayTracingAccelStructHandle tlas = device.createAccelStruct(tlasDesc);
    ASSERT_NE(tlas.get(), nullptr);
    Buffer* const backingBuffer = tlas->getBackingBuffer();
    ASSERT_NE(backingBuffer, nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId accelStruct = graph.importAccelStruct(
        tlas,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/typed_accel_struct_handoff"))
            .setMarkerLabel("Typed Accel-Struct Handoff")
            .setType(GpuGraphResourceType::AccelStruct)
            .setInitialState(ResourceStates::Common)
            .setExternalFinalState(ResourceStates::AccelStructRead)
    );
    ASSERT_TRUE(accelStruct.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(declarations.findImportedBuffer(tlas->getBackingBufferHandle()).valid());
    }

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
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const GpuTaskResourceUse producerUse{
        .resource = accelStruct,
        .range = {},
        .requiredState = ResourceStates::AccelStructWrite,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/descriptor_buffer/typed_accel_struct_handoff_producer"))
        .setMarkerLabel("Typed Accel-Struct Handoff Producer")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    bool producerRecorded = false;
    const GpuTaskId producer = graph.addTask<NativePacketPrefixTask>(
        producerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = backingBuffer,
            .expectedState = ResourceStates::AccelStructWrite,
            .recorded = &producerRecorded,
        }
    );
    ASSERT_TRUE(producer.valid());

    const GpuTaskId consumerDependencies[] = { producer };
    const GpuTaskResourceUse consumerUse{
        .resource = accelStruct,
        .range = {},
        .requiredState = ResourceStates::AccelStructWrite,
        .access = GpuTaskResourceAccess::Read,
    };
    GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/descriptor_buffer/typed_accel_struct_handoff_consumer"))
        .setMarkerLabel("Typed Accel-Struct Handoff Consumer")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    bool consumerRecorded = false;
    const GpuTaskId consumer = graph.addTask<NativePacketPrefixTask>(
        consumerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = backingBuffer,
            .expectedState = ResourceStates::AccelStructWrite,
            .recorded = &consumerRecorded,
        }
    );
    ASSERT_TRUE(consumer.valid());

    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/typed_accel_struct_handoff_scratch"));
    const GpuTaskGraphCompiler compiler;
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    GpuSubmissionPacketId producerPacket;
    GpuSubmissionPacketId consumerPacket;
    {
        const GpuTaskGraphReadViews views(graph, compiledGraph);
        ASSERT_TRUE(views.valid());
        ASSERT_EQ(views.compiled.packetCount(), 2u);

        const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producer);
        const GpuCompiledTaskView compiledConsumer = views.compiled.findTask(consumer);
        ASSERT_TRUE(compiledProducer.valid());
        ASSERT_TRUE(compiledConsumer.valid());
        producerPacket = compiledProducer.plan->packet;
        consumerPacket = compiledConsumer.plan->packet;
        ASSERT_NE(producerPacket, consumerPacket);
        ASSERT_EQ(compiledConsumer.plan->prologueStateSeedCount, 1u);
        ASSERT_NE(compiledConsumer.prologueStateSeeds, nullptr);
        EXPECT_EQ(compiledConsumer.prologueStateSeeds[0u].resource, accelStruct);
        EXPECT_EQ(compiledConsumer.prologueStateSeeds[0u].sourcePacket, producerPacket);
        ASSERT_EQ(compiledConsumer.plan->epilogueBarrierCount, 1u);
        ASSERT_NE(compiledConsumer.epilogueBarriers, nullptr);
        EXPECT_EQ(compiledConsumer.epilogueBarriers[0u].type, GpuCompiledBarrierType::AccelStructStateExport);
        EXPECT_EQ(compiledConsumer.epilogueBarriers[0u].resource, accelStruct);
        EXPECT_EQ(compiledConsumer.epilogueBarriers[0u].before, ResourceStates::AccelStructWrite);
        EXPECT_EQ(compiledConsumer.epilogueBarriers[0u].after, ResourceStates::AccelStructRead);
    }

    GpuTaskGraph driftGraph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId driftAccelStruct = driftGraph.importAccelStruct(
        tlas,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/typed_accel_struct_handoff_drift"))
            .setMarkerLabel("Typed Accel-Struct Handoff Drift")
            .setType(GpuGraphResourceType::AccelStruct)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(driftAccelStruct.valid());
    const GpuTaskResourceUse driftUse{
        .resource = driftAccelStruct,
        .range = {},
        .requiredState = ResourceStates::AccelStructWrite,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc driftTaskDesc;
    driftTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/typed_accel_struct_handoff_drift_task"))
        .setMarkerLabel("Typed Accel-Struct Handoff Drift Task")
        .setQueue(graphicsQueue)
        .setResourceUses(&driftUse, 1u)
    ;
    bool driftTaskRecorded = false;
    const GpuTaskId driftTask = driftGraph.addTask<NativePacketPrefixTask>(
        driftTaskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = backingBuffer,
            .expectedState = ResourceStates::AccelStructWrite,
            .recorded = &driftTaskRecorded,
        }
    );
    ASSERT_TRUE(driftTask.valid());
    GpuTaskGraphAnalysis driftAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments driftAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph driftCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena driftScratchArena(Name("tests/descriptor_buffer/typed_accel_struct_handoff_drift_scratch"));
    {
        const GpuTaskGraph::DeclarationReadView compilationDeclarations(driftGraph);
        ASSERT_TRUE(compiler.compile(compilationDeclarations, driftAnalysis, topology, driftAssignments, driftCompiledGraph, driftScratchArena));
    }
    const GpuNativePacketRecorder recorder(device);
    RayTracingAccelStructDesc& mutableTlasDesc = const_cast<RayTracingAccelStructDesc&>(tlas->getDescription());
    mutableTlasDesc.queueSharing = ResourceQueueSharing::GraphicsAndAsyncCompute;
    EXPECT_FALSE(tlas->queueSharingMatchesCreation());
    {
        const GpuTaskGraphReadViews driftViews(driftGraph, driftCompiledGraph);
        ASSERT_TRUE(driftViews.valid());
        ASSERT_EQ(driftViews.compiled.packetCount(), 1u);
        const GpuSubmissionPacketId driftPacket = driftViews.compiled.packetForTask(driftTask);
        ASSERT_TRUE(driftPacket.valid());
        GpuRecordedGraph driftRecordedGraph(DescriptorBufferRoundTripTest::arena());
        GpuSubmissionPacketId driftFailedPacket;
        EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
            driftGraph,
            driftCompiledGraph,
            driftViews.compiled.allPacketRange(),
            driftRecordedGraph,
            &driftFailedPacket
        ));
        EXPECT_EQ(driftFailedPacket, driftPacket);
        EXPECT_FALSE(driftTaskRecorded);
        EXPECT_FALSE(driftRecordedGraph.packetSnapshot(driftPacket).has_value());
    }
    mutableTlasDesc.queueSharing = tlas->getCreationQueueSharing();
    EXPECT_TRUE(tlas->queueSharingMatchesCreation());

    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(consumerRecorded);

    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        consumer,
        finalStateStorage
    ));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(backingBuffer), ResourceStates::AccelStructRead);
    stateProbe->close();

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
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
    EXPECT_TRUE(transaction.packetToken(producerPacket).valid());
    EXPECT_TRUE(transaction.packetToken(consumerPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

