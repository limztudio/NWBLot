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


// Pure software Shadow Preparation can begin after a forced-emulation frame inherits an accepted hardware build-input
// seed. The same two-callback state shape is used by a fully frozen hybrid BLAS -> software-tail handoff. Its frozen
// SW-BVH recorder only observes the graph-established position/index ShaderResource state; it must not need to issue
// a duplicate native transition before dispatching the build/refit kernels.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedPreparedSoftwareBvhInputStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AccelStructBuildInput seed: ray tracing acceleration structures are not enabled on this device.";

    const auto makeInputBuffer = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsAccelStructBuildInput(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const BufferHandle position = makeInputBuffer();
    const BufferHandle index = makeInputBuffer();
    ASSERT_NE(position.get(), nullptr);
    ASSERT_NE(index.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importInput = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId positionResource = importInput(
        position,
        Name("tests/descriptor_buffer/prepared_sw_bvh_position"),
        "Prepared SW-BVH Position"
    );
    const GpuGraphResourceId indexResource = importInput(
        index,
        Name("tests/descriptor_buffer/prepared_sw_bvh_index"),
        "Prepared SW-BVH Index"
    );
    ASSERT_TRUE(positionResource.valid());
    ASSERT_TRUE(indexResource.valid());

    const GpuGraphResourceId traceGeometryMembers[] = { positionResource, indexResource };
    const GpuGraphResourceSetId traceGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/prepared_sw_bvh_trace_geometry"))
            .setMarkerLabel("Prepared SW-BVH Trace Geometry")
            .setMembers(traceGeometryMembers, LengthOf(traceGeometryMembers))
    );
    ASSERT_TRUE(traceGeometrySet.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint precursorScheduling;
    precursorScheduling.cost = GpuTaskCostHint::Medium;
    precursorScheduling.forceSubmissionBoundary = false;
    precursorScheduling.allowPacketMerge = true;
    const GpuTaskResourceUse precursorUses[] = {
        {
            .resource = positionResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructBuildInput,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = indexResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructBuildInput,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc precursorDesc;
    precursorDesc
        .setIdentity(Name("tests/descriptor_buffer/prepared_sw_bvh_build_input_precursor"))
        .setMarkerLabel("Prepared SW-BVH Build-Input Precursor")
        .setQueue(graphicsQueue)
        .setScheduling(precursorScheduling)
        .setResourceUses(precursorUses, LengthOf(precursorUses))
    ;
    bool precursorRecorded = false;
    const GpuTaskId precursorTask = graph.addTask<NativePacketPrefixTask>(
        precursorDesc,
        NativePacketPrefixTask::Payload{
            .buffer = position.get(),
            .expectedState = ResourceStates::AccelStructBuildInput,
            .additionalBuffer = index.get(),
            .expectedAdditionalBufferState = ResourceStates::AccelStructBuildInput,
            .recorded = &precursorRecorded,
        }
    );
    ASSERT_TRUE(precursorTask.valid());

    GpuTaskSchedulingHint prepareScheduling = precursorScheduling;
    prepareScheduling.mergeWithPrevious = true;
    const GpuTaskResourceSetUse traceGeometrySetUses[] = {
        {
            .resourceSet = traceGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/descriptor_buffer/prepared_sw_bvh_shadow_prepare"))
        .setMarkerLabel("Prepared Software BVH Shadow Preparation")
        .setQueue(graphicsQueue)
        .setScheduling(prepareScheduling)
        .setDependencies(&precursorTask, 1u)
        .setResourceSetUses(traceGeometrySetUses, LengthOf(traceGeometrySetUses))
    ;
    bool prepareRecorded = false;
    const GpuTaskId prepareTask = graph.addTask<NativePacketPrefixTask>(
        prepareDesc,
        NativePacketPrefixTask::Payload{
            .buffer = position.get(),
            .expectedState = ResourceStates::ShaderResource,
            .additionalBuffer = index.get(),
            .expectedAdditionalBufferState = ResourceStates::ShaderResource,
            .recorded = &prepareRecorded,
        }
    );
    ASSERT_TRUE(prepareTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/prepared_sw_bvh_input_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(precursorTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(prepareTask), packet);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 2u);

    const GpuCompiledTaskView compiledPrecursor = views.compiled.findTask(precursorTask);
    const GpuCompiledTaskView compiledPrepare = views.compiled.findTask(prepareTask);
    ASSERT_TRUE(compiledPrecursor.valid());
    ASSERT_TRUE(compiledPrepare.valid());
    const auto hasTransition = [&](const GpuTaskId task, const GpuCompiledTask& compiledTask, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(compiledTask.prologueBarrierCount != 0u && !barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(
        precursorTask,
        *compiledPrecursor.plan,
        positionResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        precursorTask,
        *compiledPrecursor.plan,
        indexResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        prepareTask,
        *compiledPrepare.plan,
        positionResource,
        ResourceStates::AccelStructBuildInput,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTransition(
        prepareTask,
        *compiledPrepare.plan,
        indexResource,
        ResourceStates::AccelStructBuildInput,
        ResourceStates::ShaderResource
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
    EXPECT_TRUE(precursorRecorded);
    EXPECT_TRUE(prepareRecorded);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, precursorTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;

    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(position.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(index.get()), ResourceStates::ShaderResource);
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


// The frozen hybrid compatibility tail consumes position/index geometry only after the hardware BLAS recorder has
// established its build-input state. Its exact immutable Read set must lower the ShaderResource handoff before the
// native software callback records, without reintroducing a record-time transition bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedHybridSoftwareTailInputSetRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AccelStructBuildInput seed: ray tracing acceleration structures are not enabled on this device.";

    const auto makeInputBuffer = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsAccelStructBuildInput(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const BufferHandle position = makeInputBuffer();
    const BufferHandle index = makeInputBuffer();
    ASSERT_NE(position.get(), nullptr);
    ASSERT_NE(index.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importInput = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId positionResource = importInput(
        position,
        Name("tests/descriptor_buffer/shadow_prepare_hybrid_tail_position"),
        "Shadow Prepare Hybrid Tail Position"
    );
    const GpuGraphResourceId indexResource = importInput(
        index,
        Name("tests/descriptor_buffer/shadow_prepare_hybrid_tail_index"),
        "Shadow Prepare Hybrid Tail Index"
    );
    ASSERT_TRUE(positionResource.valid());
    ASSERT_TRUE(indexResource.valid());

    const GpuGraphResourceId hybridTailInputMembers[] = { positionResource, indexResource };
    const GpuGraphResourceSetId hybridTailInputSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shadow_prepare_hybrid_tail_inputs"))
            .setMarkerLabel("Shadow Prepare Hybrid Software Tail Inputs")
            .setMembers(hybridTailInputMembers, LengthOf(hybridTailInputMembers))
    );
    ASSERT_TRUE(hybridTailInputSet.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint precursorScheduling;
    precursorScheduling.cost = GpuTaskCostHint::Medium;
    precursorScheduling.forceSubmissionBoundary = false;
    precursorScheduling.allowPacketMerge = true;
    const GpuTaskResourceUse precursorUses[] = {
        {
            .resource = positionResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructBuildInput,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = indexResource,
            .range = {},
            .requiredState = ResourceStates::AccelStructBuildInput,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc precursorDesc;
    precursorDesc
        .setIdentity(Name("tests/descriptor_buffer/shadow_prepare_hybrid_tail_precursor"))
        .setMarkerLabel("Shadow Prepare Hybrid Tail Build-Input Precursor")
        .setQueue(graphicsQueue)
        .setScheduling(precursorScheduling)
        .setResourceUses(precursorUses, LengthOf(precursorUses))
    ;
    bool precursorRecorded = false;
    const GpuTaskId precursorTask = graph.addTask<NativePacketPrefixTask>(
        precursorDesc,
        NativePacketPrefixTask::Payload{
            .buffer = position.get(),
            .expectedState = ResourceStates::AccelStructBuildInput,
            .additionalBuffer = index.get(),
            .expectedAdditionalBufferState = ResourceStates::AccelStructBuildInput,
            .recorded = &precursorRecorded,
        }
    );
    ASSERT_TRUE(precursorTask.valid());

    GpuTaskSchedulingHint hybridTailScheduling = precursorScheduling;
    hybridTailScheduling.mergeWithPrevious = true;
    const GpuTaskResourceSetUse hybridTailInputSetUses[] = {
        {
            .resourceSet = hybridTailInputSet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc hybridTailDesc;
    hybridTailDesc
        .setIdentity(Name("tests/descriptor_buffer/shadow_prepare_hybrid_tail"))
        .setMarkerLabel("Shadow Prepare Hybrid Software Tail")
        .setQueue(graphicsQueue)
        .setScheduling(hybridTailScheduling)
        .setDependencies(&precursorTask, 1u)
        .setResourceSetUses(hybridTailInputSetUses, LengthOf(hybridTailInputSetUses))
    ;
    bool hybridTailRecorded = false;
    const GpuTaskId hybridTailTask = graph.addTask<NativePacketPrefixTask>(
        hybridTailDesc,
        NativePacketPrefixTask::Payload{
            .buffer = position.get(),
            .expectedState = ResourceStates::ShaderResource,
            .additionalBuffer = index.get(),
            .expectedAdditionalBufferState = ResourceStates::ShaderResource,
            .recorded = &hybridTailRecorded,
        }
    );
    ASSERT_TRUE(hybridTailTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shadow_prepare_hybrid_tail_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(precursorTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(hybridTailTask), packet);
    ASSERT_EQ(views.compiled.packet(packet).plan->taskCount, 2u);

    const GpuCompiledTaskView compiledPrecursor = views.compiled.findTask(precursorTask);
    const GpuCompiledTaskView compiledHybridTail = views.compiled.findTask(hybridTailTask);
    ASSERT_TRUE(compiledPrecursor.valid());
    ASSERT_TRUE(compiledHybridTail.valid());
    const auto hasTransition = [&](const GpuTaskId task, const GpuCompiledTask& compiledTask, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(compiledTask.prologueBarrierCount != 0u && !barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(
        precursorTask,
        *compiledPrecursor.plan,
        positionResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        precursorTask,
        *compiledPrecursor.plan,
        indexResource,
        ResourceStates::Common,
        ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        hybridTailTask,
        *compiledHybridTail.plan,
        positionResource,
        ResourceStates::AccelStructBuildInput,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTransition(
        hybridTailTask,
        *compiledHybridTail.plan,
        indexResource,
        ResourceStates::AccelStructBuildInput,
        ResourceStates::ShaderResource
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
    EXPECT_TRUE(precursorRecorded);
    EXPECT_TRUE(hybridTailRecorded);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, precursorTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;

    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(position.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getBufferState(index.get()), ResourceStates::ShaderResource);
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


// Shadow Preparation keeps parent links and shared sort/counter scratch as UAV state for a later SW-BVH build.
// Its graph declaration must expand that exact frozen set before the native callback records, without a local
// transition bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedShadowPrepareSoftwareBvhBuildStateSetRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeBuildStateBuffer = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const BufferHandle parent = makeBuildStateBuffer();
    const BufferHandle sortScratch = makeBuildStateBuffer();
    ASSERT_NE(parent.get(), nullptr);
    ASSERT_NE(sortScratch.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuildState = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId parentResource = importBuildState(
        parent,
        Name("tests/descriptor_buffer/shadow_prepare_sw_bvh_parent"),
        "Shadow Prepare Software BVH Parent"
    );
    const GpuGraphResourceId sortScratchResource = importBuildState(
        sortScratch,
        Name("tests/descriptor_buffer/shadow_prepare_sw_bvh_sort_scratch"),
        "Shadow Prepare Software BVH Sort Scratch"
    );
    ASSERT_TRUE(parentResource.valid());
    ASSERT_TRUE(sortScratchResource.valid());

    const GpuGraphResourceId buildStateMembers[] = { parentResource, sortScratchResource };
    const GpuGraphResourceSetId buildStateSet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shadow_prepare_software_bvh_build_state"))
            .setMarkerLabel("Shadow Prepare Software BVH Build State")
            .setMembers(buildStateMembers, LengthOf(buildStateMembers))
    );
    ASSERT_TRUE(buildStateSet.valid());
    const GpuTaskResourceSetUse buildStateSetUses[] = {
        {
            .resourceSet = buildStateSet,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.allowPacketMerge = true;
    GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/descriptor_buffer/shadow_prepare_software_bvh_build_state"))
        .setMarkerLabel("Shadow Prepare Software BVH Build State")
        .setQueue(graphicsQueue)
        .setScheduling(scheduling)
        .setResourceSetUses(buildStateSetUses, LengthOf(buildStateSetUses))
    ;
    bool recorded = false;
    const GpuTaskId shadowPrepareTask = graph.addTask<NativePacketPrefixTask>(
        shadowPrepareDesc,
        NativePacketPrefixTask::Payload{
            .buffer = parent.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .additionalBuffer = sortScratch.get(),
            .expectedAdditionalBufferState = ResourceStates::UnorderedAccess,
            .recorded = &recorded,
        }
    );
    ASSERT_TRUE(shadowPrepareTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shadow_prepare_sw_bvh_build_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(shadowPrepareTask);
    ASSERT_TRUE(packet.valid());
    const GpuCompiledTaskView compiledTask = views.compiled.findTask(shadowPrepareTask);
    ASSERT_TRUE(compiledTask.valid());
    const GpuCompiledBarrier* const barriers = views.compiled.findTask(shadowPrepareTask).prologueBarriers;
    ASSERT_NE(barriers, nullptr);
    const auto hasUavTransition = [&](const GpuGraphResourceId resource){
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == ResourceStates::Common
                && barrier.after == ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasUavTransition(parentResource));
    EXPECT_TRUE(hasUavTransition(sortScratchResource));

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
    EXPECT_TRUE(recorded);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        shadowPrepareTask,
        finalStateStorage
    ));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(parent.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(stateProbe->getBufferState(sortScratch.get()), ResourceStates::UnorderedAccess);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

