// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graph_resources_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel's typed resource clears are followed by a resource-free lifecycle callback. Its graph packet must establish
// all four CopyDest states before that callback runs, and the callback may only publish initialization after the
// shared packet accepts.
struct NativePacketSurfelInitializationEntryProbeTask{
    struct Payload{
        Buffer* pool = nullptr;
        Buffer* cellHeads = nullptr;
        Buffer* counter = nullptr;
        Buffer* freeList = nullptr;
        bool* recorded = nullptr;
        bool* needsClear = nullptr;
        bool* clearPending = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.pool
            && payload.cellHeads
            && payload.counter
            && payload.freeList
            && commandList.getBufferState(payload.pool) == ResourceStates::CopyDest
            && commandList.getBufferState(payload.cellHeads) == ResourceStates::CopyDest
            && commandList.getBufferState(payload.counter) == ResourceStates::CopyDest
            && commandList.getBufferState(payload.freeList) == ResourceStates::CopyDest
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        if(ready && payload.clearPending)
            *payload.clearPending = true;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.clearPending && *payload.clearPending){
            *payload.clearPending = false;
            if(payload.needsClear)
                *payload.needsClear = false;
        }
    }

    static void discarded(Payload& payload){
        if(payload.clearPending)
            *payload.clearPending = false;
    }
};


// Age/free must receive its exact descriptor-visible state batch before the graph-owned per-frame cell-head reset.
// Hash build then owns the CopyDest-to-UAV handoff, and the remaining GI callback must observe the compiler's
// same-state UAV ordering without restoring a native bridge.
struct NativePacketSurfelGiAgeFreeProbeTask{
    struct Payload{
        Buffer* constants = nullptr;
        Buffer* pool = nullptr;
        Buffer* counter = nullptr;
        Buffer* freeList = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.constants
            && payload.pool
            && payload.counter
            && payload.freeList
            && commandList.getBufferState(payload.constants) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.pool) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.counter) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.freeList) == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Hash build receives only the descriptors it uses. This getter-only probe makes the cell-head clear-to-hash state
// handoff observable without relying on native renderer setup to repeat it.
struct NativePacketSurfelGiHashBuildProbeTask{
    struct Payload{
        Buffer* constants = nullptr;
        Buffer* pool = nullptr;
        Buffer* cellHeads = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.constants
            && payload.pool
            && payload.cellHeads
            && commandList.getBufferState(payload.constants) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.pool) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.cellHeads) == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Spawn consumes the post-hash persistent field plus the G-buffer source pair. This getter-only probe proves its
// packet prologue owns both the sampled-image state and hash-to-Spawn UAV ordering before native work can run.
struct NativePacketSurfelGiSpawnProbeTask{
    struct Payload{
        Texture* worldPosition = nullptr;
        Texture* normal = nullptr;
        Buffer* constants = nullptr;
        Buffer* pool = nullptr;
        Buffer* cellHeads = nullptr;
        Buffer* counter = nullptr;
        Buffer* freeList = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.worldPosition
            && payload.normal
            && payload.constants
            && payload.pool
            && payload.cellHeads
            && payload.counter
            && payload.freeList
            && commandList.getTextureSubresourceState(payload.worldPosition, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.normal, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.constants) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.pool) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.cellHeads) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.counter) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.freeList) == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Trace-build-args consumes Spawn's counter and writes the indirect argument buffer. This getter-only probe proves
// its graph-owned UAV entry before the renderer callback records the dispatch.
struct NativePacketSurfelGiTraceBuildArgsProbeTask{
    struct Payload{
        Buffer* constants = nullptr;
        Buffer* counter = nullptr;
        Buffer* traceArgs = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.constants
            && payload.counter
            && payload.traceArgs
            && commandList.getBufferState(payload.constants) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.counter) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.traceArgs) == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Trace consumes all descriptor-visible traversal and material inputs, updates the live pool, and reads the indirect
// dispatch arguments. This getter-only probe proves that packet-prologue states arrive before native work records.
struct NativePacketSurfelGiTraceProbeTask{
    static constexpr u32 s_ShaderBufferCount = 9u;
    static constexpr u32 s_ConstantBufferCount = 4u;

    struct Payload{
        Buffer* shaderBuffers[s_ShaderBufferCount] = {};
        Buffer* constantBuffers[s_ConstantBufferCount] = {};
        Buffer* pool = nullptr;
        Buffer* traceArgs = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        bool ready = payload.pool
            && payload.traceArgs
            && commandList.getBufferState(payload.pool) == ResourceStates::UnorderedAccess
            && commandList.getBufferState(payload.traceArgs) == ResourceStates::IndirectArgument
        ;
        for(u32 bufferIndex = 0u; bufferIndex < s_ShaderBufferCount; ++bufferIndex){
            ready = ready
                && payload.shaderBuffers[bufferIndex]
                && commandList.getBufferState(payload.shaderBuffers[bufferIndex]) == ResourceStates::ShaderResource
            ;
        }
        for(u32 bufferIndex = 0u; bufferIndex < s_ConstantBufferCount; ++bufferIndex){
            ready = ready
                && payload.constantBuffers[bufferIndex]
                && commandList.getBufferState(payload.constantBuffers[bufferIndex]) == ResourceStates::ConstantBuffer
            ;
        }
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Resolve receives only its G-buffer, field, constant, and half-resolution output states from the graph. This
// getter-only probe keeps the live-field shader-resource handoff observable before native setup could mask it.
struct NativePacketSurfelGiResolveProbeTask{
    static constexpr u32 s_ShaderBufferCount = 9u;
    static constexpr u32 s_ConstantBufferCount = 4u;
    static constexpr u32 s_ShaderTextureCount = 2u;
    static constexpr u32 s_UavBufferCount = 5u;
    static constexpr u32 s_PoolBufferIndex = 0u;
    static constexpr u32 s_CellHeadsBufferIndex = 1u;
    static constexpr u32 s_TraceArgsBufferIndex = 3u;
    static constexpr u32 s_UavTextureCount = 1u;

    struct Payload{
        Buffer* constants = nullptr;
        Buffer* pool = nullptr;
        Buffer* cellHeads = nullptr;
        Texture* worldPosition = nullptr;
        Texture* normal = nullptr;
        Texture* irradianceHalf = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.constants
            && payload.pool
            && payload.cellHeads
            && payload.worldPosition
            && payload.normal
            && payload.irradianceHalf
            && commandList.getBufferState(payload.constants) == ResourceStates::ConstantBuffer
            && commandList.getBufferState(payload.pool) == ResourceStates::ShaderResource
            && commandList.getBufferState(payload.cellHeads) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.worldPosition, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.normal, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.irradianceHalf, 0u, 0u) == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Upsample consumes only its graph-declared G-buffer and half-resolution input, and writes the final irradiance.
struct NativePacketSurfelGiUpsampleProbeTask{
    struct Payload{
        Texture* worldPosition = nullptr;
        Texture* normal = nullptr;
        Texture* irradianceHalf = nullptr;
        Texture* irradiance = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.worldPosition
            && payload.normal
            && payload.irradianceHalf
            && payload.irradiance
            && commandList.getTextureSubresourceState(payload.worldPosition, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.normal, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.irradianceHalf, 0u, 0u) == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.irradiance, 0u, 0u) == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Lighting observes the final irradiance state before its thunk could restore a native compatibility layout.
struct NativePacketSurfelGiLightingProbeTask{
    struct Payload{
        Texture* irradiance = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.irradiance
            && commandList.getTextureSubresourceState(payload.irradiance, 0u, 0u) == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// First-use Surfel initialization is four typed buffer clears followed by a resource-free lifecycle callback. This
// real-Vulkan probe verifies each `UAV -> CopyDest` entry transition, serial command-IR sequence, accepted CPU
// publication, and rejection retry behavior without a native clear bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSurfelInitializationEntryStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeStorageBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(true)
                .setCpuAccess(CpuAccessMode::Read)
                .setInitialState(ResourceStates::Common)
        );
    };
    const BufferHandle pool = makeStorageBuffer();
    const BufferHandle cellHeads = makeStorageBuffer();
    const BufferHandle counter = makeStorageBuffer();
    const BufferHandle freeList = makeStorageBuffer();
    ASSERT_NE(pool.get(), nullptr);
    ASSERT_NE(cellHeads.get(), nullptr);
    ASSERT_NE(counter.get(), nullptr);
    ASSERT_NE(freeList.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const GpuGraphResourceId poolResource = importBuffer(
        pool,
        Name("tests/descriptor_buffer/surfel_initialize_pool"),
        "Surfel Pool"
    );
    const GpuGraphResourceId cellHeadsResource = importBuffer(
        cellHeads,
        Name("tests/descriptor_buffer/surfel_initialize_cell_heads"),
        "Surfel Cell Heads"
    );
    const GpuGraphResourceId counterResource = importBuffer(
        counter,
        Name("tests/descriptor_buffer/surfel_initialize_counter"),
        "Surfel Counter"
    );
    const GpuGraphResourceId freeListResource = importBuffer(
        freeList,
        Name("tests/descriptor_buffer/surfel_initialize_free_list"),
        "Surfel Free List"
    );
    ASSERT_TRUE(poolResource.valid());
    ASSERT_TRUE(cellHeadsResource.valid());
    ASSERT_TRUE(counterResource.valid());
    ASSERT_TRUE(freeListResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest computeTransferQueue{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        GpuQueuePreference::Compute,
        true,
        false,
    };
    GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const GpuTaskResourceUse prefixUses[] = {
        { .resource = poolResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
        { .resource = cellHeadsResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
        { .resource = counterResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
        { .resource = freeListResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/surfel_initialize_prefix"))
        .setMarkerLabel("Surfel Initialize Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    bool shouldRecord = true;
    bool prefixAttempted = false;
    const GpuTaskId prefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        prefixDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &prefixAttempted,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    GpuTaskSchedulingHint firstClearScheduling;
    firstClearScheduling.cost = GpuTaskCostHint::Medium;
    firstClearScheduling.allowPacketMerge = true;
    GpuTaskSchedulingHint chainedClearScheduling = firstClearScheduling;
    chainedClearScheduling.cost = GpuTaskCostHint::Tiny;
    chainedClearScheduling.mergeWithPrevious = true;
    chainedClearScheduling.allowMergeAcrossConsumerFrontier = true;
    GpuTaskDesc poolClearDesc;
    poolClearDesc
        .setIdentity(Name("tests/descriptor_buffer/surfel_initialize_pool_clear"))
        .setMarkerLabel("Surfel GI Initialize Pool Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(firstClearScheduling)
        .setDependencies(&prefixTask, 1u)
    ;
    const GpuTaskId poolClearTask = graph.addClearBufferTask(
        poolClearDesc,
        GpuClearBufferTaskDesc{
            .destination = poolResource,
            .clearValue = 0u,
        }
    );
    ASSERT_TRUE(poolClearTask.valid());
    const auto addChainedClear = [&](
        const Name& identity,
        const AStringView label,
        const GpuGraphResourceId destination,
        const u32 clearValue,
        const GpuTaskId dependency
    ){
        GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(computeTransferQueue)
            .setScheduling(chainedClearScheduling)
            .setDependencies(&dependency, 1u)
        ;
        return graph.addClearBufferTask(
            clearDesc,
            GpuClearBufferTaskDesc{
                .destination = destination,
                .clearValue = clearValue,
            }
        );
    };
    const GpuTaskId cellHeadClearTask = addChainedClear(
        Name("tests/descriptor_buffer/surfel_initialize_cell_head_clear"),
        "Surfel GI Initialize Cell-Head Clear",
        cellHeadsResource,
        0xffffffffu,
        poolClearTask
    );
    ASSERT_TRUE(cellHeadClearTask.valid());
    const GpuTaskId counterClearTask = addChainedClear(
        Name("tests/descriptor_buffer/surfel_initialize_counter_clear"),
        "Surfel GI Initialize Counter Clear",
        counterResource,
        0u,
        cellHeadClearTask
    );
    ASSERT_TRUE(counterClearTask.valid());
    const GpuTaskId freeListClearTask = addChainedClear(
        Name("tests/descriptor_buffer/surfel_initialize_free_list_clear"),
        "Surfel GI Initialize Free-List Clear",
        freeListResource,
        0u,
        counterClearTask
    );
    ASSERT_TRUE(freeListClearTask.valid());
    GpuTaskDesc lifecycleDesc;
    lifecycleDesc
        .setIdentity(Name("tests/descriptor_buffer/surfel_initialize_lifecycle"))
        .setMarkerLabel("Surfel GI Initialize Lifecycle")
        .setQueue(computeTransferQueue)
        .setScheduling(chainedClearScheduling)
        .setDependencies(&freeListClearTask, 1u)
    ;
    bool initializeRecorded = false;
    bool initializationNeedsClear = true;
    bool initializationClearPending = false;
    const GpuTaskId initializeLifecycleTask = graph.addTask<NativePacketSurfelInitializationEntryProbeTask>(
        lifecycleDesc,
        NativePacketSurfelInitializationEntryProbeTask::Payload{
            .pool = pool.get(),
            .cellHeads = cellHeads.get(),
            .counter = counter.get(),
            .freeList = freeList.get(),
            .recorded = &initializeRecorded,
            .needsClear = &initializationNeedsClear,
            .clearPending = &initializationClearPending,
        }
    );
    ASSERT_TRUE(initializeLifecycleTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/surfel_initialize_entry_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 2u);
    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId initializePacket = views.compiled.packetForTask(poolClearTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(initializePacket.valid());
    EXPECT_EQ(views.compiled.packetForTask(cellHeadClearTask), initializePacket);
    EXPECT_EQ(views.compiled.packetForTask(counterClearTask), initializePacket);
    EXPECT_EQ(views.compiled.packetForTask(freeListClearTask), initializePacket);
    EXPECT_EQ(views.compiled.packetForTask(initializeLifecycleTask), initializePacket);
    ASSERT_EQ(views.compiled.packet(initializePacket).plan->taskCount, 5u);
    const GpuTaskId* const initializeTasks = views.compiled.packet(initializePacket).tasks;
    ASSERT_NE(initializeTasks, nullptr);
    EXPECT_EQ(initializeTasks[0u], poolClearTask);
    EXPECT_EQ(initializeTasks[1u], cellHeadClearTask);
    EXPECT_EQ(initializeTasks[2u], counterClearTask);
    EXPECT_EQ(initializeTasks[3u], freeListClearTask);
    EXPECT_EQ(initializeTasks[4u], initializeLifecycleTask);
    const auto hasInitializeTransition = [&](const GpuTaskId task, const GpuGraphResourceId resource){
        const GpuCompiledTaskView compiledTask = views.compiled.findTask(task);
        if(!compiledTask.valid()){
            ADD_FAILURE() << "missing compiled Surfel initialization clear task";
            return false;
        }
        const GpuCompiledBarrier* const barriers = views.compiled.findTask(task).prologueBarriers;
        if(!barriers){
            ADD_FAILURE() << "missing Surfel initialization clear prologue barriers";
            return false;
        }
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.resource == resource
                && barrier.before == ResourceStates::UnorderedAccess
                && barrier.after == ResourceStates::CopyDest
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasInitializeTransition(poolClearTask, poolResource));
    EXPECT_TRUE(hasInitializeTransition(cellHeadClearTask, cellHeadsResource));
    EXPECT_TRUE(hasInitializeTransition(counterClearTask, counterResource));
    EXPECT_TRUE(hasInitializeTransition(freeListClearTask, freeListResource));
    ASSERT_EQ(views.compiled.packet(initializePacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(initializePacket).dependencies[0u].producer, prefixPacket);

    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    GpuGraphSubmissionTransaction rejectedTransaction(DescriptorBufferRoundTripTest::arena());
    rejectedTransaction.reset(compiledGraph);
    GpuRecordedGraph rejectedRecordedGraph(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        rejectedRecordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(prefixAttempted);
    EXPECT_TRUE(initializeRecorded);
    EXPECT_TRUE(initializationNeedsClear);
    EXPECT_TRUE(initializationClearPending);
    EXPECT_TRUE(rejectedTransaction.discardUnaccepted(
        graph,
        compiledGraph,
        rejectedRecordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(initializationNeedsClear);
    EXPECT_FALSE(initializationClearPending);

    initializeRecorded = false;
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket,
        &commandIrCapture
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(initializeRecorded);
    EXPECT_TRUE(initializationClearPending);
    // The first transaction is permanently bound to its discarded native recording. A retry must not let that
    // transaction discard the retry's pending lifecycle state or submit its stale command lists.
    EXPECT_FALSE(rejectedTransaction.discardUnaccepted(
        graph,
        compiledGraph,
        rejectedRecordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_TRUE(initializationClearPending);
    ASSERT_EQ(commandIrCapture.recordCount(), 4u);
    const GpuCommandIrBuiltinTaskRecord* const poolClearCapture = commandIrCapture.recordAt(0u);
    const GpuCommandIrBuiltinTaskRecord* const cellHeadClearCapture = commandIrCapture.recordAt(1u);
    const GpuCommandIrBuiltinTaskRecord* const counterClearCapture = commandIrCapture.recordAt(2u);
    const GpuCommandIrBuiltinTaskRecord* const freeListClearCapture = commandIrCapture.recordAt(3u);
    ASSERT_NE(poolClearCapture, nullptr);
    ASSERT_NE(cellHeadClearCapture, nullptr);
    ASSERT_NE(counterClearCapture, nullptr);
    ASSERT_NE(freeListClearCapture, nullptr);
    EXPECT_EQ(poolClearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(poolClearCapture->task, poolClearTask);
    EXPECT_EQ(poolClearCapture->destination, poolResource);
    EXPECT_EQ(poolClearCapture->uintClearValue, UIntColor(0u));
    EXPECT_EQ(cellHeadClearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(cellHeadClearCapture->task, cellHeadClearTask);
    EXPECT_EQ(cellHeadClearCapture->destination, cellHeadsResource);
    EXPECT_EQ(cellHeadClearCapture->uintClearValue, UIntColor(0xffffffffu));
    EXPECT_EQ(counterClearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(counterClearCapture->task, counterClearTask);
    EXPECT_EQ(counterClearCapture->destination, counterResource);
    EXPECT_EQ(counterClearCapture->uintClearValue, UIntColor(0u));
    EXPECT_EQ(freeListClearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(freeListClearCapture->task, freeListClearTask);
    EXPECT_EQ(freeListClearCapture->destination, freeListResource);
    EXPECT_EQ(freeListClearCapture->uintClearValue, UIntColor(0u));

    const GpuTaskGraphSubmitter submitter(device);
    GpuGraphSubmissionTransaction staleTransaction(DescriptorBufferRoundTripTest::arena());
    staleTransaction.reset(compiledGraph);
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        rejectedRecordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        staleTransaction,
        scratchArena
    ));
    EXPECT_FALSE(staleTransaction.packetToken(prefixPacket).valid());

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
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
    EXPECT_TRUE(transaction.packetToken(initializePacket).valid());
    EXPECT_FALSE(initializationNeedsClear);
    EXPECT_FALSE(initializationClearPending);
    EXPECT_TRUE(device.waitForIdle());
    const auto expectsClearValue = [&device](const BufferHandle& buffer, const u32 value){
        const u32* const words = static_cast<const u32*>(device.mapBuffer(buffer.get(), CpuAccessMode::Read));
        ASSERT_NE(words, nullptr);
        for(usize wordIndex = 0u; wordIndex < 256u / sizeof(u32); ++wordIndex)
            EXPECT_EQ(words[wordIndex], value);
        device.unmapBuffer(buffer.get());
    };
    expectsClearValue(pool, 0u);
    expectsClearValue(cellHeads, 0xffffffffu);
    expectsClearValue(counter, 0u);
    expectsClearValue(freeList, 0u);
}


// Surfel GI's typed output clear, age/free, per-frame cell-head clear, Hash Build, Spawn, trace-build-args, Trace,
// and Resolve now receive their states from the graph. The test intentionally has no renderer-native state setup:
// Hash Build observes the typed clear transition, Args observes Spawn's counter, Trace receives indirect arguments,
// Resolve receives the compiler-lowered live-field shader-resource state, Upsample receives Resolve's result, and
// the following Lighting probe observes the compiler-owned final irradiance return state.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSurfelGiResolveRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 shaderBufferCount = NativePacketSurfelGiResolveProbeTask::s_ShaderBufferCount;
    constexpr u32 constantBufferCount = NativePacketSurfelGiResolveProbeTask::s_ConstantBufferCount;
    constexpr u32 shaderTextureCount = NativePacketSurfelGiResolveProbeTask::s_ShaderTextureCount;
    constexpr u32 uavBufferCount = NativePacketSurfelGiResolveProbeTask::s_UavBufferCount;
    constexpr u32 uavTextureCount = NativePacketSurfelGiResolveProbeTask::s_UavTextureCount;
    constexpr u32 traceGeometryBufferIndex = 5u;
    const auto makeStorageBuffer = [&device](const bool isDrawIndirectArgs = false){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setCanHaveUAVs(true)
                .setIsDrawIndirectArgs(isDrawIndirectArgs)
                .setCpuAccess(CpuAccessMode::Read)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeConstantBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeUavTexture = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    BufferHandle shaderBuffers[shaderBufferCount];
    BufferHandle constantBuffers[constantBufferCount];
    BufferHandle uavBuffers[uavBufferCount];
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex){
        shaderBuffers[bufferIndex] = makeStorageBuffer();
        ASSERT_NE(shaderBuffers[bufferIndex].get(), nullptr);
    }
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex){
        constantBuffers[bufferIndex] = makeConstantBuffer();
        ASSERT_NE(constantBuffers[bufferIndex].get(), nullptr);
    }
    for(u32 bufferIndex = 0u; bufferIndex < uavBufferCount; ++bufferIndex){
        uavBuffers[bufferIndex] = makeStorageBuffer(
            bufferIndex == NativePacketSurfelGiResolveProbeTask::s_TraceArgsBufferIndex
        );
        ASSERT_NE(uavBuffers[bufferIndex].get(), nullptr);
    }
    TextureHandle shaderTextures[shaderTextureCount];
    for(u32 textureIndex = 0u; textureIndex < shaderTextureCount; ++textureIndex){
        shaderTextures[textureIndex] = device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
        );
        ASSERT_NE(shaderTextures[textureIndex].get(), nullptr);
    }
    TextureHandle uavTextures[uavTextureCount];
    for(u32 textureIndex = 0u; textureIndex < uavTextureCount; ++textureIndex){
        uavTextures[textureIndex] = makeUavTexture();
        ASSERT_NE(uavTextures[textureIndex].get(), nullptr);
    }
    const TextureHandle irradianceHalf = makeUavTexture();
    ASSERT_NE(irradianceHalf.get(), nullptr);
    Texture* const initialTextures[] = {
        shaderTextures[0u].get(),
        shaderTextures[1u].get(),
        uavTextures[0u].get(),
        irradianceHalf.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const BufferHandle& buffer, const Name identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const auto importTexture = [&graph](const TextureHandle& texture, const Name identity, const AStringView label){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
        );
    };
    struct BufferImportDesc{
        Name identity;
        AStringView label;
    };
    const BufferImportDesc shaderBufferImports[shaderBufferCount] = {
        { Name("tests/descriptor_buffer/surfel_gi_instance_materials"), "Shadow Instance Materials" },
        { Name("tests/descriptor_buffer/surfel_gi_material_typed"), "Shadow Typed Materials" },
        { Name("tests/descriptor_buffer/surfel_gi_instances"), "Shadow Instances" },
        { Name("tests/descriptor_buffer/surfel_gi_scene_bvh_nodes"), "Scene BVH Nodes" },
        { Name("tests/descriptor_buffer/surfel_gi_scene_instances"), "Scene Instances" },
        { Name("tests/descriptor_buffer/surfel_gi_mesh_positions"), "Software Mesh Positions" },
        { Name("tests/descriptor_buffer/surfel_gi_pool_snapshot"), "Surfel Pool Snapshot" },
        { Name("tests/descriptor_buffer/surfel_gi_cell_head_snapshot"), "Surfel Cell Head Snapshot" },
        { Name("tests/descriptor_buffer/surfel_gi_lights"), "Deferred Lights" },
    };
    const BufferImportDesc constantBufferImports[constantBufferCount] = {
        { Name("tests/descriptor_buffer/surfel_gi_bindless_slots"), "Deferred Bindless Slots" },
        { Name("tests/descriptor_buffer/surfel_gi_material_context_slots"), "Ray-Trace Material Context Slots" },
        { Name("tests/descriptor_buffer/surfel_gi_constants"), "Surfel Constants" },
        { Name("tests/descriptor_buffer/surfel_gi_scene_shading"), "Scene Shading" },
    };
    const BufferImportDesc uavBufferImports[uavBufferCount] = {
        { Name("tests/descriptor_buffer/surfel_gi_pool"), "Surfel Pool" },
        { Name("tests/descriptor_buffer/surfel_gi_cell_heads"), "Surfel Cell Heads" },
        { Name("tests/descriptor_buffer/surfel_gi_counter"), "Surfel Counter" },
        { Name("tests/descriptor_buffer/surfel_gi_trace_args"), "Surfel Trace Arguments" },
        { Name("tests/descriptor_buffer/surfel_gi_free_list"), "Surfel Free List" },
    };
    GpuGraphResourceId shaderBufferResources[shaderBufferCount] = {};
    GpuGraphResourceId constantBufferResources[constantBufferCount] = {};
    GpuGraphResourceId uavBufferResources[uavBufferCount] = {};
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex){
        shaderBufferResources[bufferIndex] = importBuffer(
            shaderBuffers[bufferIndex],
            shaderBufferImports[bufferIndex].identity,
            shaderBufferImports[bufferIndex].label
        );
        ASSERT_TRUE(shaderBufferResources[bufferIndex].valid());
    }
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex){
        constantBufferResources[bufferIndex] = importBuffer(
            constantBuffers[bufferIndex],
            constantBufferImports[bufferIndex].identity,
            constantBufferImports[bufferIndex].label
        );
        ASSERT_TRUE(constantBufferResources[bufferIndex].valid());
    }
    for(u32 bufferIndex = 0u; bufferIndex < uavBufferCount; ++bufferIndex){
        uavBufferResources[bufferIndex] = importBuffer(
            uavBuffers[bufferIndex],
            uavBufferImports[bufferIndex].identity,
            uavBufferImports[bufferIndex].label
        );
        ASSERT_TRUE(uavBufferResources[bufferIndex].valid());
    }
    const GpuGraphResourceId worldPositionResource = importTexture(
        shaderTextures[0u],
        Name("tests/descriptor_buffer/surfel_gi_world_position"),
        "World Position"
    );
    const GpuGraphResourceId normalResource = importTexture(
        shaderTextures[1u],
        Name("tests/descriptor_buffer/surfel_gi_normal"),
        "Normal"
    );
    const GpuGraphResourceId irradianceResource = importTexture(
        uavTextures[0u],
        Name("tests/descriptor_buffer/surfel_gi_irradiance"),
        "Surfel Irradiance"
    );
    const GpuGraphResourceId irradianceHalfResource = importTexture(
        irradianceHalf,
        Name("tests/descriptor_buffer/surfel_gi_irradiance_half"),
        "Surfel Irradiance Half"
    );
    ASSERT_TRUE(worldPositionResource.valid());
    ASSERT_TRUE(normalResource.valid());
    ASSERT_TRUE(irradianceResource.valid());
    ASSERT_TRUE(irradianceHalfResource.valid());

    const GpuGraphResourceSetId traceGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/surfel_gi_trace_geometry"))
            .setMarkerLabel("Surfel GI Trace Geometry")
            .setMembers(&shaderBufferResources[traceGeometryBufferIndex], 1u)
    );
    ASSERT_TRUE(traceGeometrySet.valid());
    const GpuTaskResourceSetUse traceGeometrySetUses[] = {
        {
            .resourceSet = traceGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest computeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        true,
        true,
    };
    const GpuQueueRequest computeTransferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Compute,
        true,
        false,
    };
    GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Alloc::ScratchArena resourceUseArena(Name("tests/descriptor_buffer/surfel_gi_entry_state_resource_uses"));
    Vector<GpuTaskResourceUse, Alloc::ScratchArena> prefixUses(resourceUseArena);
    prefixUses.reserve(shaderBufferCount + constantBufferCount + uavBufferCount + shaderTextureCount + uavTextureCount);
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        prefixUses.push_back({ .resource = shaderBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        prefixUses.push_back({ .resource = constantBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    for(u32 bufferIndex = 0u; bufferIndex < uavBufferCount; ++bufferIndex)
        prefixUses.push_back({ .resource = uavBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    prefixUses.push_back({ .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::RenderTarget, .access = GpuTaskResourceAccess::Write });
    prefixUses.push_back({ .resource = normalResource, .range = {}, .requiredState = ResourceStates::RenderTarget, .access = GpuTaskResourceAccess::Write });
    prefixUses.push_back({ .resource = irradianceHalfResource, .range = {}, .requiredState = ResourceStates::CopyDest, .access = GpuTaskResourceAccess::Write });
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/surfel_gi_prefix"))
        .setMarkerLabel("Surfel GI Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses.data(), prefixUses.size())
    ;
    bool shouldRecord = true;
    bool prefixAttempted = false;
    const GpuTaskId prefixTask = graph.addTask<NativePacketCaptureRetryTask>(
        prefixDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &prefixAttempted,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.allowPacketMerge = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_output_clear"))
        .setMarkerLabel("Surfel GI Output Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(clearScheduling)
        .setDependencies(&prefixTask, 1u)
    ;
    const GpuTaskId outputClearTask = graph.addClearTextureTask(
        clearDesc,
        GpuClearTextureTaskDesc{
            .destination = irradianceResource,
            .subresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
            .valueType = GpuClearTextureTaskValueType::Float,
            .floatValue = Color(0.f, 0.f, 0.f, 0.f),
        }
    );
    ASSERT_TRUE(outputClearTask.valid());

    GpuTaskSchedulingHint surfelScheduling = boundaryScheduling;
    surfelScheduling.forceSubmissionBoundary = false;
    surfelScheduling.allowPacketMerge = true;
    surfelScheduling.mergeWithPrevious = true;
    const GpuTaskResourceUse ageFreeUses[] = {
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = uavBufferResources[0u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
        { .resource = uavBufferResources[2u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
        { .resource = uavBufferResources[4u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskDesc ageFreeDesc;
    ageFreeDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_age_free"))
        .setMarkerLabel("Surfel GI Age Free")
        .setQueue(computeQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&outputClearTask, 1u)
        .setResourceUses(ageFreeUses, LengthOf(ageFreeUses))
    ;
    bool ageFreeRecorded = false;
    const GpuTaskId ageFreeTask = graph.addTask<NativePacketSurfelGiAgeFreeProbeTask>(
        ageFreeDesc,
        NativePacketSurfelGiAgeFreeProbeTask::Payload{
            .constants = constantBuffers[2u].get(),
            .pool = uavBuffers[0u].get(),
            .counter = uavBuffers[2u].get(),
            .freeList = uavBuffers[4u].get(),
            .recorded = &ageFreeRecorded,
        }
    );
    ASSERT_TRUE(ageFreeTask.valid());

    GpuTaskDesc cellHeadClearDesc;
    cellHeadClearDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_cell_head_clear"))
        .setMarkerLabel("Surfel GI Cell Head Clear")
        .setQueue(computeTransferQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&ageFreeTask, 1u)
    ;
    const GpuTaskId cellHeadClearTask = graph.addClearBufferTask(
        cellHeadClearDesc,
        GpuClearBufferTaskDesc{
            .destination = uavBufferResources[1u],
            .clearValue = 0xffffffffu,
        }
    );
    ASSERT_TRUE(cellHeadClearTask.valid());

    const GpuTaskResourceUse hashBuildUses[] = {
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = uavBufferResources[0u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = uavBufferResources[1u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
    };
    GpuTaskDesc hashBuildDesc;
    hashBuildDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_hash_build"))
        .setMarkerLabel("Surfel GI Hash Build")
        .setQueue(computeQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&cellHeadClearTask, 1u)
        .setResourceUses(hashBuildUses, LengthOf(hashBuildUses))
    ;
    bool hashBuildRecorded = false;
    const GpuTaskId hashBuildTask = graph.addTask<NativePacketSurfelGiHashBuildProbeTask>(
        hashBuildDesc,
        NativePacketSurfelGiHashBuildProbeTask::Payload{
            .constants = constantBuffers[2u].get(),
            .pool = uavBuffers[0u].get(),
            .cellHeads = uavBuffers[1u].get(),
            .recorded = &hashBuildRecorded,
        }
    );
    ASSERT_TRUE(hashBuildTask.valid());

    const GpuTaskResourceUse spawnUses[] = {
        { .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = normalResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = uavBufferResources[0u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = uavBufferResources[1u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = uavBufferResources[2u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
        { .resource = uavBufferResources[4u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite },
    };
    GpuTaskDesc spawnDesc;
    spawnDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_spawn"))
        .setMarkerLabel("Surfel GI Spawn")
        .setQueue(computeQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&hashBuildTask, 1u)
        .setResourceUses(spawnUses, LengthOf(spawnUses))
    ;
    bool spawnRecorded = false;
    const GpuTaskId spawnTask = graph.addTask<NativePacketSurfelGiSpawnProbeTask>(
        spawnDesc,
        NativePacketSurfelGiSpawnProbeTask::Payload{
            .worldPosition = shaderTextures[0u].get(),
            .normal = shaderTextures[1u].get(),
            .constants = constantBuffers[2u].get(),
            .pool = uavBuffers[0u].get(),
            .cellHeads = uavBuffers[1u].get(),
            .counter = uavBuffers[2u].get(),
            .freeList = uavBuffers[4u].get(),
            .recorded = &spawnRecorded,
        }
    );
    ASSERT_TRUE(spawnTask.valid());

    const GpuTaskResourceUse traceBuildArgsUses[] = {
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = uavBufferResources[2u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Read },
        { .resource = uavBufferResources[3u], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskDesc traceBuildArgsDesc;
    traceBuildArgsDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_trace_build_args"))
        .setMarkerLabel("Surfel GI Trace Build Args")
        .setQueue(computeQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&spawnTask, 1u)
        .setResourceUses(traceBuildArgsUses, LengthOf(traceBuildArgsUses))
    ;
    bool traceBuildArgsRecorded = false;
    const GpuTaskId traceBuildArgsTask = graph.addTask<NativePacketSurfelGiTraceBuildArgsProbeTask>(
        traceBuildArgsDesc,
        NativePacketSurfelGiTraceBuildArgsProbeTask::Payload{
            .constants = constantBuffers[2u].get(),
            .counter = uavBuffers[2u].get(),
            .traceArgs = uavBuffers[3u].get(),
            .recorded = &traceBuildArgsRecorded,
        }
    );
    ASSERT_TRUE(traceBuildArgsTask.valid());

    Vector<GpuTaskResourceUse, Alloc::ScratchArena> traceUses(resourceUseArena);
    traceUses.reserve(shaderBufferCount - 1u + constantBufferCount + 2u);
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex){
        if(bufferIndex != traceGeometryBufferIndex)
            traceUses.push_back({ .resource = shaderBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read });
    }
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        traceUses.push_back({ .resource = constantBufferResources[bufferIndex], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read });
    traceUses.push_back({ .resource = uavBufferResources[NativePacketSurfelGiResolveProbeTask::s_PoolBufferIndex], .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::ReadWrite });
    traceUses.push_back({ .resource = uavBufferResources[NativePacketSurfelGiResolveProbeTask::s_TraceArgsBufferIndex], .range = {}, .requiredState = ResourceStates::IndirectArgument, .access = GpuTaskResourceAccess::Read });
    GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_trace"))
        .setMarkerLabel("Surfel GI Trace")
        .setQueue(computeQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&traceBuildArgsTask, 1u)
        .setResourceUses(traceUses.data(), traceUses.size())
        .setResourceSetUses(traceGeometrySetUses, LengthOf(traceGeometrySetUses))
    ;
    NativePacketSurfelGiTraceProbeTask::Payload tracePayload{};
    for(u32 bufferIndex = 0u; bufferIndex < shaderBufferCount; ++bufferIndex)
        tracePayload.shaderBuffers[bufferIndex] = shaderBuffers[bufferIndex].get();
    for(u32 bufferIndex = 0u; bufferIndex < constantBufferCount; ++bufferIndex)
        tracePayload.constantBuffers[bufferIndex] = constantBuffers[bufferIndex].get();
    tracePayload.pool = uavBuffers[NativePacketSurfelGiResolveProbeTask::s_PoolBufferIndex].get();
    tracePayload.traceArgs = uavBuffers[NativePacketSurfelGiResolveProbeTask::s_TraceArgsBufferIndex].get();
    bool traceRecorded = false;
    tracePayload.recorded = &traceRecorded;
    const GpuTaskId traceTask = graph.addTask<NativePacketSurfelGiTraceProbeTask>(
        traceDesc,
        Move(tracePayload)
    );
    ASSERT_TRUE(traceTask.valid());

    const GpuTaskResourceUse resolveUses[] = {
        { .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = normalResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = constantBufferResources[2u], .range = {}, .requiredState = ResourceStates::ConstantBuffer, .access = GpuTaskResourceAccess::Read },
        { .resource = uavBufferResources[NativePacketSurfelGiResolveProbeTask::s_PoolBufferIndex], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = uavBufferResources[NativePacketSurfelGiResolveProbeTask::s_CellHeadsBufferIndex], .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = irradianceHalfResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskDesc resolveDesc;
    resolveDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_resolve"))
        .setMarkerLabel("Surfel GI Resolve")
        .setQueue(computeQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&traceTask, 1u)
        .setResourceUses(resolveUses, LengthOf(resolveUses))
    ;
    bool resolveRecorded = false;
    const GpuTaskId resolveTask = graph.addTask<NativePacketSurfelGiResolveProbeTask>(
        resolveDesc,
        NativePacketSurfelGiResolveProbeTask::Payload{
            .constants = constantBuffers[2u].get(),
            .pool = uavBuffers[NativePacketSurfelGiResolveProbeTask::s_PoolBufferIndex].get(),
            .cellHeads = uavBuffers[NativePacketSurfelGiResolveProbeTask::s_CellHeadsBufferIndex].get(),
            .worldPosition = shaderTextures[0u].get(),
            .normal = shaderTextures[1u].get(),
            .irradianceHalf = irradianceHalf.get(),
            .recorded = &resolveRecorded,
        }
    );
    ASSERT_TRUE(resolveTask.valid());

    const GpuTaskResourceUse surfelUses[] = {
        { .resource = worldPositionResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = normalResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = irradianceHalfResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
        { .resource = irradianceResource, .range = {}, .requiredState = ResourceStates::UnorderedAccess, .access = GpuTaskResourceAccess::Write },
    };
    GpuTaskDesc surfelDesc;
    surfelDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(computeQueue)
        .setScheduling(surfelScheduling)
        .setDependencies(&resolveTask, 1u)
        .setResourceUses(surfelUses, LengthOf(surfelUses))
    ;
    bool surfelRecorded = false;
    const GpuTaskId surfelTask = graph.addTask<NativePacketSurfelGiUpsampleProbeTask>(
        surfelDesc,
        NativePacketSurfelGiUpsampleProbeTask::Payload{
            .worldPosition = shaderTextures[0u].get(),
            .normal = shaderTextures[1u].get(),
            .irradianceHalf = irradianceHalf.get(),
            .irradiance = uavTextures[0u].get(),
            .recorded = &surfelRecorded,
        }
    );
    ASSERT_TRUE(surfelTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        { .resource = irradianceResource, .range = {}, .requiredState = ResourceStates::ShaderResource, .access = GpuTaskResourceAccess::Read },
    };
    GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/descriptor_buffer/graph_owned_surfel_gi_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeQueue)
        .setScheduling(boundaryScheduling)
        .setDependencies(&surfelTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = graph.addTask<NativePacketSurfelGiLightingProbeTask>(
        lightingDesc,
        NativePacketSurfelGiLightingProbeTask::Payload{
            .irradiance = uavTextures[0u].get(),
            .recorded = &lightingRecorded,
        }
    );
    ASSERT_TRUE(lightingTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/surfel_gi_entry_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 3u);
    const GpuSubmissionPacketId prefixPacket = views.compiled.packetForTask(prefixTask);
    const GpuSubmissionPacketId clearPacket = views.compiled.packetForTask(outputClearTask);
    const GpuSubmissionPacketId ageFreePacket = views.compiled.packetForTask(ageFreeTask);
    const GpuSubmissionPacketId cellHeadClearPacket = views.compiled.packetForTask(cellHeadClearTask);
    const GpuSubmissionPacketId hashBuildPacket = views.compiled.packetForTask(hashBuildTask);
    const GpuSubmissionPacketId spawnPacket = views.compiled.packetForTask(spawnTask);
    const GpuSubmissionPacketId traceBuildArgsPacket = views.compiled.packetForTask(traceBuildArgsTask);
    const GpuSubmissionPacketId tracePacket = views.compiled.packetForTask(traceTask);
    const GpuSubmissionPacketId resolvePacket = views.compiled.packetForTask(resolveTask);
    const GpuSubmissionPacketId surfelPacket = views.compiled.packetForTask(surfelTask);
    const GpuSubmissionPacketId lightingPacket = views.compiled.packetForTask(lightingTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(ageFreePacket.valid());
    ASSERT_TRUE(cellHeadClearPacket.valid());
    ASSERT_TRUE(hashBuildPacket.valid());
    ASSERT_TRUE(spawnPacket.valid());
    ASSERT_TRUE(traceBuildArgsPacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    ASSERT_TRUE(resolvePacket.valid());
    ASSERT_TRUE(surfelPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(clearPacket, surfelPacket);
    EXPECT_EQ(ageFreePacket, surfelPacket);
    EXPECT_EQ(cellHeadClearPacket, surfelPacket);
    EXPECT_EQ(hashBuildPacket, surfelPacket);
    EXPECT_EQ(spawnPacket, surfelPacket);
    EXPECT_EQ(traceBuildArgsPacket, surfelPacket);
    EXPECT_EQ(tracePacket, surfelPacket);
    EXPECT_EQ(resolvePacket, surfelPacket);
    EXPECT_NE(lightingPacket, surfelPacket);
    EXPECT_TRUE(analysis.hasExplicitEdge(ageFreeTask, cellHeadClearTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(cellHeadClearTask, hashBuildTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(hashBuildTask, spawnTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(spawnTask, traceBuildArgsTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(traceBuildArgsTask, traceTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(traceTask, resolveTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(resolveTask, surfelTask));
    EXPECT_TRUE(analysis.hasExplicitEdge(surfelTask, lightingTask));
    ASSERT_NE(views.compiled.packet(surfelPacket).tasks, nullptr);
    ASSERT_EQ(views.compiled.packet(surfelPacket).plan->taskCount, 9u);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[0u], outputClearTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[1u], ageFreeTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[2u], cellHeadClearTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[3u], hashBuildTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[4u], spawnTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[5u], traceBuildArgsTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[6u], traceTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[7u], resolveTask);
    EXPECT_EQ(views.compiled.packet(surfelPacket).tasks[8u], surfelTask);
    const GpuCompiledTaskView compiledAgeFree = views.compiled.findTask(ageFreeTask);
    ASSERT_TRUE(compiledAgeFree.valid());
    const GpuCompiledBarrier* const ageFreeBarriers = views.compiled.findTask(ageFreeTask).prologueBarriers;
    ASSERT_NE(ageFreeBarriers, nullptr);
    const auto hasAgeFreeTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledAgeFree.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = ageFreeBarriers[barrierIndex];
            if(
                barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasAgeFreeTransition(constantBufferResources[2u], ResourceStates::CopyDest, ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasAgeFreeTransition(uavBufferResources[0u], ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasAgeFreeTransition(uavBufferResources[2u], ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasAgeFreeTransition(uavBufferResources[4u], ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    const GpuCompiledTaskView compiledHashBuild = views.compiled.findTask(hashBuildTask);
    ASSERT_TRUE(compiledHashBuild.valid());
    const GpuCompiledBarrier* const hashBuildBarriers = views.compiled.findTask(hashBuildTask).prologueBarriers;
    ASSERT_NE(hashBuildBarriers, nullptr);
    const auto hasHashBuildTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledHashBuild.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = hashBuildBarriers[barrierIndex];
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
    EXPECT_TRUE(hasHashBuildTransition(
        uavBufferResources[1u],
        ResourceStates::CopyDest,
        ResourceStates::UnorderedAccess
    ));
    const GpuCompiledTaskView compiledSpawn = views.compiled.findTask(spawnTask);
    ASSERT_TRUE(compiledSpawn.valid());
    const GpuCompiledBarrier* const spawnBarriers = views.compiled.findTask(spawnTask).prologueBarriers;
    ASSERT_NE(spawnBarriers, nullptr);
    const auto hasSpawnBarrier = [&](const GpuCompiledBarrierType::Enum type, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSpawn.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = spawnBarriers[barrierIndex];
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
    EXPECT_TRUE(hasSpawnBarrier(
        GpuCompiledBarrierType::TextureTransition,
        worldPositionResource,
        ResourceStates::RenderTarget,
        ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasSpawnBarrier(
        GpuCompiledBarrierType::BufferUav,
        uavBufferResources[1u],
        ResourceStates::UnorderedAccess,
        ResourceStates::UnorderedAccess
    ));
    const GpuCompiledTaskView compiledTraceBuildArgs = views.compiled.findTask(traceBuildArgsTask);
    ASSERT_TRUE(compiledTraceBuildArgs.valid());
    const GpuCompiledBarrier* const traceBuildArgsBarriers = views.compiled.findTask(traceBuildArgsTask).prologueBarriers;
    ASSERT_NE(traceBuildArgsBarriers, nullptr);
    const auto hasTraceBuildArgsBarrier = [&](const GpuCompiledBarrierType::Enum type, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTraceBuildArgs.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = traceBuildArgsBarriers[barrierIndex];
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
    EXPECT_TRUE(hasTraceBuildArgsBarrier(
        GpuCompiledBarrierType::BufferUav,
        uavBufferResources[2u],
        ResourceStates::UnorderedAccess,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTraceBuildArgsBarrier(
        GpuCompiledBarrierType::BufferTransition,
        uavBufferResources[3u],
        ResourceStates::CopyDest,
        ResourceStates::UnorderedAccess
    ));
    const GpuCompiledTaskView compiledTrace = views.compiled.findTask(traceTask);
    ASSERT_TRUE(compiledTrace.valid());
    const GpuCompiledBarrier* const traceBarriers = views.compiled.findTask(traceTask).prologueBarriers;
    ASSERT_NE(traceBarriers, nullptr);
    const auto hasTraceBarrier = [&](const GpuCompiledBarrierType::Enum type, const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTrace.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
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
    EXPECT_TRUE(hasTraceBarrier(
        GpuCompiledBarrierType::BufferUav,
        uavBufferResources[0u],
        ResourceStates::UnorderedAccess,
        ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTraceBarrier(
        GpuCompiledBarrierType::BufferTransition,
        uavBufferResources[3u],
        ResourceStates::UnorderedAccess,
        ResourceStates::IndirectArgument
    ));
    const GpuCompiledTaskView compiledResolve = views.compiled.findTask(resolveTask);
    ASSERT_TRUE(compiledResolve.valid());
    const GpuCompiledBarrier* const resolveBarriers = views.compiled.findTask(resolveTask).prologueBarriers;
    ASSERT_NE(resolveBarriers, nullptr);
    const auto hasResolveTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledResolve.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = resolveBarriers[barrierIndex];
            if(
                barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasResolveTransition(uavBufferResources[0u], ResourceStates::UnorderedAccess, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasResolveTransition(uavBufferResources[1u], ResourceStates::UnorderedAccess, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasResolveTransition(irradianceHalfResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    const GpuCompiledTaskView compiledSurfel = views.compiled.findTask(surfelTask);
    ASSERT_TRUE(compiledSurfel.valid());
    const GpuCompiledBarrier* const surfelBarriers = views.compiled.findTask(surfelTask).prologueBarriers;
    ASSERT_NE(surfelBarriers, nullptr);
    const auto hasSurfelTransition = [&](const GpuGraphResourceId resource, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSurfel.plan->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = surfelBarriers[barrierIndex];
            if(
                barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasSurfelTransition(irradianceHalfResource, ResourceStates::UnorderedAccess, ResourceStates::ShaderResource));
    EXPECT_TRUE(hasSurfelTransition(irradianceResource, ResourceStates::CopyDest, ResourceStates::UnorderedAccess));
    ASSERT_EQ(views.compiled.packet(surfelPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(surfelPacket).dependencies[0u].producer, prefixPacket);
    const GpuCompiledTaskView compiledLighting = views.compiled.findTask(lightingTask);
    ASSERT_TRUE(compiledLighting.valid());
    const GpuCompiledBarrier* const lightingBarriers = views.compiled.findTask(lightingTask).prologueBarriers;
    ASSERT_NE(lightingBarriers, nullptr);
    bool lightingTransitionsIrradiance = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledLighting.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = lightingBarriers[barrierIndex];
        lightingTransitionsIrradiance = lightingTransitionsIrradiance
            || (
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == irradianceResource
                && barrier.before == ResourceStates::UnorderedAccess
                && barrier.after == ResourceStates::ShaderResource
            )
        ;
    }
    EXPECT_TRUE(lightingTransitionsIrradiance);
    ASSERT_EQ(views.compiled.packet(lightingPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(views.compiled.packet(lightingPacket).dependencies[0u].producer, surfelPacket);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket,
        &commandIrCapture
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(prefixAttempted);
    EXPECT_TRUE(ageFreeRecorded);
    EXPECT_TRUE(hashBuildRecorded);
    EXPECT_TRUE(spawnRecorded);
    EXPECT_TRUE(traceBuildArgsRecorded);
    EXPECT_TRUE(traceRecorded);
    EXPECT_TRUE(resolveRecorded);
    EXPECT_TRUE(surfelRecorded);
    EXPECT_TRUE(lightingRecorded);
    ASSERT_EQ(commandIrCapture.recordCount(), 2u);
    const GpuCommandIrBuiltinTaskRecord* const outputClearCapture = commandIrCapture.recordAt(0u);
    const GpuCommandIrBuiltinTaskRecord* const cellHeadClearCapture = commandIrCapture.recordAt(1u);
    ASSERT_NE(outputClearCapture, nullptr);
    ASSERT_NE(cellHeadClearCapture, nullptr);
    EXPECT_EQ(outputClearCapture->opcode, GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(outputClearCapture->task, outputClearTask);
    EXPECT_EQ(cellHeadClearCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(cellHeadClearCapture->task, cellHeadClearTask);
    EXPECT_EQ(cellHeadClearCapture->packet, cellHeadClearPacket);
    EXPECT_EQ(cellHeadClearCapture->destination, uavBufferResources[1u]);
    EXPECT_EQ(cellHeadClearCapture->uintClearValue, UIntColor(0xffffffffu));

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
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
    EXPECT_TRUE(transaction.packetToken(surfelPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
    const u32* const cellHeadWords = static_cast<const u32*>(
        device.mapBuffer(uavBuffers[1u].get(), CpuAccessMode::Read)
    );
    ASSERT_NE(cellHeadWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < 256u / sizeof(u32); ++wordIndex)
        EXPECT_EQ(cellHeadWords[wordIndex], 0xffffffffu);
    device.unmapBuffer(uavBuffers[1u].get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

