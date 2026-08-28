// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Immutable command-list lease identity, recording-ingress, replay, and submission coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <core/graphics/task_graph/task_graph.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_list_provenance_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool DescriptionsMatch(
    const CommandListParameters& left,
    const CommandListParameters& right
){
    return
        left.queueType == right.queueType
        && left.physicalQueue == right.physicalQueue
        && left.recordingWorkerDomain == right.recordingWorkerDomain
        && left.recordingWorkerIndex == right.recordingWorkerIndex
    ;
}

[[nodiscard]] static CommandListParameters ForgeRecordingWorker(CommandList& commandList){
    CommandListParameters& publicDescription = const_cast<CommandListParameters&>(commandList.getDescription());
    const CommandListParameters savedDescription = publicDescription;
    publicDescription.recordingWorkerDomain = 0x7c11d9a54183b6e2ull;
    publicDescription.recordingWorkerIndex = savedDescription.recordingWorkerIndex == 1u ? 2u : 1u;
    return savedDescription;
}

[[nodiscard]] static CommandListParameters ForgeExecutionQueue(
    CommandList& commandList,
    const GpuPhysicalQueueInfo& targetQueue
){
    CommandListParameters& publicDescription = const_cast<CommandListParameters&>(commandList.getDescription());
    const CommandListParameters savedDescription = publicDescription;
    publicDescription.physicalQueue = targetQueue.id;
    publicDescription.queueType = targetQueue.queueClass;
    return savedDescription;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindDistinctTransferQueue(
    const GraphicsBackend::Device& device,
    const GpuPhysicalQueueId& excludedQueue
){
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id != excludedQueue
            && (candidate.capabilities & GpuQueueCapability::Transfer) != GpuQueueCapability::None
        )
            return &candidate;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ForgedDescriptionTask{
    struct Payload{
        Buffer* destination = nullptr;
        bool* attack = nullptr;
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
        if(!payload.destination)
            return false;
        if(!payload.attack || !*payload.attack)
            return true;

        const CommandListParameters savedDescription = ForgeRecordingWorker(commandList);
        commandList.clearBufferUInt(payload.destination, 0x5a17c3e9u);
        const_cast<CommandListParameters&>(commandList.getDescription()) = savedDescription;
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LeaseProvenanceGraph{
    GraphicsBackend::Device& m_device;
    BufferHandle m_source;
    BufferHandle m_destination;
    GpuTaskGraph m_graph;
    GpuTaskGraphAnalysis m_analysis;
    GpuTaskGraphQueueAssignments m_assignments;
    GpuCompiledGraph m_compiledGraph;
    GpuGraphResourceId m_sourceResource;
    GpuGraphResourceId m_destinationResource;
    GpuTaskId m_task;
    GpuSubmissionPacketId m_packet;
    GpuPhysicalQueueId m_queue;

    LeaseProvenanceGraph(GraphicsBackend::Device& device, GraphicsArena& arena)
        : m_device(device)
        , m_graph(arena)
        , m_analysis(arena)
        , m_assignments(arena)
        , m_compiledGraph(arena)
    {}

    [[nodiscard]] bool initialize(
        bool& attack,
        bool& attempted,
        u32& discardedCount,
        Alloc::ScratchArena& scratchArena
    ){
        m_source = m_device.createBuffer(
            BufferDesc()
                .setByteSize(64u)
                .setInitialState(ResourceStates::Common)
        );
        m_destination = m_device.createBuffer(
            BufferDesc()
                .setByteSize(64u)
                .setInitialState(ResourceStates::Common)
        );
        if(
            !m_source
            || !m_destination
            || !m_device.isBufferReadyForGpuUse(m_source.get())
            || !m_device.isBufferReadyForGpuUse(m_destination.get())
        )
            return false;

        m_sourceResource = m_graph.importBuffer(
            m_source,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_list_provenance/source"))
                .setMarkerLabel("Command List Provenance Source")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        m_destinationResource = m_graph.importBuffer(
            m_destination,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_list_provenance/destination"))
                .setMarkerLabel("Command List Provenance Destination")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        if(!m_sourceResource.valid() || !m_destinationResource.valid())
            return false;

        GpuTaskResourceRange sourceRange;
        sourceRange.bufferRange = BufferRange(0u, 16u);
        GpuTaskResourceRange destinationRange;
        destinationRange.bufferRange = BufferRange(0u, 16u);
        const GpuTaskResourceUse uses[]{
            {
                .resource = m_sourceResource,
                .range = sourceRange,
                .requiredState = ResourceStates::CopySource,
                .access = GpuTaskResourceAccess::Read,
            },
            {
                .resource = m_destinationResource,
                .range = destinationRange,
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
        };
        GpuTaskSchedulingHint scheduling;
        scheduling.forceSubmissionBoundary = true;
        scheduling.allowPacketMerge = false;
        GpuTaskDesc taskDescription;
        taskDescription
            .setIdentity(Name("tests/command_list_provenance/forged_record"))
            .setMarkerLabel("Forged Command List Description")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(scheduling)
            .setResourceUses(uses, LengthOf(uses))
        ;
        m_task = m_graph.addTask<ForgedDescriptionTask>(
            taskDescription,
            ForgedDescriptionTask::Payload{
                .destination = m_destination.get(),
                .attack = &attack,
                .attempted = &attempted,
                .discardedCount = &discardedCount,
            }
        );
        if(!m_task.valid())
            return false;

        m_queue = m_device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
        const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_queue);
        if(
            !queueInfo
            || (queueInfo->capabilities & GpuQueueCapability::Transfer) == GpuQueueCapability::None
        )
            return false;

        const GpuTaskGraphQueueTopology topology{
            .queues = queueInfo,
            .queueCount = 1u,
        };
        const GpuTaskGraphCompiler compiler;
        if(!compiler.compile(m_graph, m_analysis, topology, m_assignments, m_compiledGraph, scratchArena))
            return false;

        m_packet = m_compiledGraph.packetForTask(m_task);
        return m_packet.valid() && m_compiledGraph.packet(m_packet).queue == m_queue;
    }

    [[nodiscard]] bool capture(GpuCommandIrCapture& capture)const{
        return capture.captureCopyBuffer(
            m_task,
            m_packet,
            m_queue,
            m_sourceResource,
            0u,
            m_destinationResource,
            0u,
            16u
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CommandListProvenanceTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(
            !s_scope->setTransferQueueEnabled(true)
            || !s_scope->setSameClassMultiQueueEnabled(true)
            || !s_scope->initialize()
        ){
            GTEST_SKIP() << "Command-list provenance: no validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "command-list provenance tests emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static Core::Alloc::GlobalArena& arena(){ return s_scope->arena(); }


protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool CommandListProvenanceTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> CommandListProvenanceTest::s_scope;
Optional<CapturingLogger> CommandListProvenanceTest::s_logger;
Optional<Common::LoggerRegistrationGuard> CommandListProvenanceTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CommandListProvenanceTest, WorkerForgeryRejectsPreOpenAndActiveRecordingWithoutResourceMutation){
    using namespace __hidden_command_list_provenance_tests;

    BufferHandle buffer = device().createBuffer(
        BufferDesc()
            .setByteSize(64u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(device().isBufferReadyForGpuUse(buffer.get()));
    CommandListParameters parameters;
    parameters.setRecordingWorker(0x5dc8493ab26710e4ull, 3u);
    CommandListHandle commandList = device().createCommandList(parameters);
    ASSERT_TRUE(commandList);
    const CommandListParameters resolvedDescription = commandList->getResolvedDescription();

    CommandListParameters savedDescription = ForgeRecordingWorker(*commandList);
    commandList->open();
    EXPECT_FALSE(commandList->isRecording());
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_TRUE(DescriptionsMatch(commandList->getResolvedDescription(), resolvedDescription));

    const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    ASSERT_FALSE(commandList->commandRecordingFailed());
    const u64 recordingLease = commandList->recordingLeaseSerial();
    const u32 baselineReferences = buffer->getReferenceCount();

    savedDescription = ForgeRecordingWorker(*commandList);
    EXPECT_FALSE(commandList->isRecording());
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_FALSE(commandList->matchesRecordingLease(recordingLease));
    constexpr u32 s_UploadWord = 0x4f25c891u;
    EXPECT_FALSE(commandList->tryWriteBuffer(buffer.get(), &s_UploadWord, sizeof(s_UploadWord)));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitBufferState(buffer.get()));
    EXPECT_EQ(buffer->getReferenceCount(), baselineReferences);

    const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
    EXPECT_TRUE(commandList->isRecording());
    EXPECT_TRUE(commandList->matchesRecordingLease(recordingLease));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());

    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    commandList->clearBufferUInt(buffer.get(), 0u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    CommandList* const commandLists[]{ commandList.get() };
    EXPECT_TRUE(device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        resolvedDescription.physicalQueue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(CommandListProvenanceTest, QueueClassForgeryRejectsDirectRecordingIngressAndRecovers){
    using namespace __hidden_command_list_provenance_tests;

    BufferHandle buffer = device().createBuffer(
        BufferDesc()
            .setByteSize(64u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(device().isBufferReadyForGpuUse(buffer.get()));
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(commandList->isRecording());

    CommandListParameters& publicDescription = const_cast<CommandListParameters&>(commandList->getDescription());
    const CommandListParameters savedDescription = publicDescription;
    publicDescription.queueType = savedDescription.queueType == CommandQueue::Graphics
        ? CommandQueue::Compute
        : CommandQueue::Graphics
    ;
    constexpr u32 s_UploadWord = 0x91b8e34cu;
    EXPECT_FALSE(commandList->tryWriteBuffer(buffer.get(), &s_UploadWord, sizeof(s_UploadWord)));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitBufferState(buffer.get()));

    publicDescription = savedDescription;
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    EXPECT_TRUE(commandList->tryWriteBuffer(buffer.get(), &s_UploadWord, sizeof(s_UploadWord)));
    commandList->close();
    CommandList* const commandLists[]{ commandList.get() };
    ASSERT_TRUE(device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        savedDescription.physicalQueue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(CommandListProvenanceTest, ForgedIngressCannotEndRenderMarkerStateOrPublishHandoff){
    using namespace __hidden_command_list_provenance_tests;

    TextureHandle renderTarget = device().createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setInRenderTarget(true)
    );
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(renderTarget.get())
    );
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(framebuffer);

    {
        SCOPED_TRACE("endRenderPass");
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setGraphicsState(GraphicsState{}.setFramebuffer(framebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        const CommandListParameters savedDescription = ForgeRecordingWorker(*commandList);
        commandList->endRenderPass();
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->isRenderPassActive());
        const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
    }

    {
        SCOPED_TRACE("clearState");
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setGraphicsState(GraphicsState{}.setFramebuffer(framebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        const CommandListParameters savedDescription = ForgeRecordingWorker(*commandList);
        commandList->clearState();
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->isRenderPassActive());
        const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
    }

    {
        SCOPED_TRACE("endMarker");
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->beginMarker("Command List Provenance Marker");
        ASSERT_FALSE(commandList->commandRecordingFailed());
        const CommandListParameters savedDescription = ForgeRecordingWorker(*commandList);
        commandList->endMarker();
        EXPECT_TRUE(commandList->commandRecordingFailed());
        const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
    }

    {
        SCOPED_TRACE("close with final handoff");
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setGraphicsState(GraphicsState{}.setFramebuffer(framebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        CommandListResourceStateHandoff handoff(arena());
        const CommandListParameters savedDescription = ForgeRecordingWorker(*commandList);
        commandList->close(&handoff);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_FALSE(handoff.valid());

        const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
        EXPECT_FALSE(commandList->hasCommandBuffer());
        commandList->open();
        EXPECT_TRUE(commandList->isRecording());
        EXPECT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        EXPECT_TRUE(commandList->hasCommandBuffer());
    }
}


TEST_F(CommandListProvenanceTest, NativePacketRecorderDiscardsForgeRestoreThunkAndRetriesSamePacket){
    using namespace __hidden_command_list_provenance_tests;

    bool attack = true;
    bool attempted = false;
    u32 discardedCount = 0u;
    LeaseProvenanceGraph resources(device(), arena());
    Alloc::ScratchArena scratchArena(Name("tests/command_list_provenance/record_compiler_scratch"));
    ASSERT_TRUE(resources.initialize(attack, attempted, discardedCount, scratchArena));
    const GpuNativePacketRecorder recorder(device());

    GpuRecordedGraph rejectedGraph(arena());
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        resources.m_graph,
        resources.m_compiledGraph,
        GpuSubmissionPacketRange{ .first = resources.m_packet, .packetCount = 1u },
        rejectedGraph
    ));
    EXPECT_TRUE(attempted);
    EXPECT_EQ(rejectedGraph.find(resources.m_packet), nullptr);
    EXPECT_EQ(discardedCount, 1u);

    attack = false;
    attempted = false;
    EXPECT_TRUE(recorder.recordPacketRangeInCompileOrder(
        resources.m_graph,
        resources.m_compiledGraph,
        GpuSubmissionPacketRange{ .first = resources.m_packet, .packetCount = 1u },
        rejectedGraph
    ));
    EXPECT_TRUE(attempted);
    EXPECT_NE(rejectedGraph.find(resources.m_packet), nullptr);
    EXPECT_EQ(discardedCount, 1u);
}


TEST_F(CommandListProvenanceTest, CommandIrReplayRejectsForgedPublicQueueWithoutMutatingTheLease){
    using namespace __hidden_command_list_provenance_tests;

    bool attack = false;
    bool attempted = false;
    u32 discardedCount = 0u;
    LeaseProvenanceGraph resources(device(), arena());
    Alloc::ScratchArena scratchArena(Name("tests/command_list_provenance/replay_compiler_scratch"));
    ASSERT_TRUE(resources.initialize(attack, attempted, discardedCount, scratchArena));
    GpuCommandIrCapture capture(arena());
    ASSERT_TRUE(resources.capture(capture));
    ASSERT_EQ(capture.recordCount(), 1u);

    const GpuPhysicalQueueInfo* const otherQueue = FindDistinctTransferQueue(device(), resources.m_queue);
    if(!otherQueue)
        GTEST_SKIP() << "Command-list provenance: no distinct transfer-capable exact queue for replay.";

    CommandListParameters parameters;
    parameters.setPhysicalQueue(otherQueue->id);
    CommandListHandle commandList = device().createCommandList(parameters);
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    const u64 recordingLease = commandList->recordingLeaseSerial();
    const CommandListParameters resolvedDescription = commandList->getResolvedDescription();
    const GpuPhysicalQueueInfo* const packetQueue = device().getPhysicalQueueInfo(resources.m_queue);
    ASSERT_NE(packetQueue, nullptr);

    const CommandListParameters savedDescription = ForgeExecutionQueue(*commandList, *packetQueue);
    EXPECT_FALSE(commandList->isRecording());
    EXPECT_TRUE(DescriptionsMatch(commandList->getResolvedDescription(), resolvedDescription));
    const GpuCommandIrReplayResult forgedReplay = ReplayGpuCommandIrPacket(
        capture.commandBytes(),
        resources.m_graph,
        resources.m_compiledGraph,
        resources.m_packet,
        *commandList
    );
    EXPECT_EQ(forgedReplay.error, GpuCommandIrReplayError::CommandListNotRecording);
    EXPECT_TRUE(forgedReplay.streamValidation.valid());
    const GpuCommandIrReplayResult forgedDirectReplay = ReplayGpuCommandIrPacketDirectVulkan(
        capture.commandBytes(),
        resources.m_graph,
        resources.m_compiledGraph,
        resources.m_packet,
        *commandList
    );
    EXPECT_EQ(forgedDirectReplay.error, GpuCommandIrReplayError::CommandListNotRecording);
    EXPECT_TRUE(forgedDirectReplay.streamValidation.valid());
    EXPECT_FALSE(commandList->commandRecordingFailed());

    const_cast<CommandListParameters&>(commandList->getDescription()) = savedDescription;
    EXPECT_TRUE(commandList->isRecording());
    EXPECT_TRUE(commandList->matchesRecordingLease(recordingLease));
    const GpuCommandIrReplayResult actualQueueReplay = ReplayGpuCommandIrPacket(
        capture.commandBytes(),
        resources.m_graph,
        resources.m_compiledGraph,
        resources.m_packet,
        *commandList
    );
    EXPECT_EQ(actualQueueReplay.error, GpuCommandIrReplayError::CommandListQueueMismatch);
    EXPECT_TRUE(actualQueueReplay.streamValidation.valid());
    const GpuCommandIrReplayResult actualQueueDirectReplay = ReplayGpuCommandIrPacketDirectVulkan(
        capture.commandBytes(),
        resources.m_graph,
        resources.m_compiledGraph,
        resources.m_packet,
        *commandList
    );
    EXPECT_EQ(actualQueueDirectReplay.error, GpuCommandIrReplayError::CommandListQueueMismatch);
    EXPECT_TRUE(actualQueueDirectReplay.streamValidation.valid());
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_TRUE(commandList->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

