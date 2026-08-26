// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture/acceleration-structure readiness, typed packet preflight, and Texture handoff-ingress coverage.


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


struct PacketPreflightProbeTask{
    struct Payload{
        bool* recorded = nullptr;
        u32* discardedCount = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        if(payload.recorded)
            *payload.recorded = true;
        return true;
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


class GpuResourceReadinessTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "GPU resource readiness: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled GPU resource-readiness smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static Core::Alloc::GlobalArena& arena(){
        return s_scope->arena();
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool GpuResourceReadinessTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> GpuResourceReadinessTest::s_scope;
Optional<CapturingLogger> GpuResourceReadinessTest::s_logger;
Optional<Common::LoggerRegistrationGuard> GpuResourceReadinessTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(GpuResourceReadinessTest, TexturePredicateTracksVirtualMemoryBinding){
    auto& device = GpuResourceReadinessTest::device();
    EXPECT_FALSE(device.isTextureReadyForGpuUse(nullptr));

    const TextureDesc ordinaryDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureHandle ordinary = device.createTexture(ordinaryDesc);
    ASSERT_TRUE(ordinary);
    EXPECT_TRUE(device.isTextureReadyForGpuUse(ordinary.get()));

    TextureDesc virtualDesc = ordinaryDesc;
    virtualDesc.isVirtual = true;
    TextureHandle placed = device.createTexture(virtualDesc);
    ASSERT_TRUE(placed);
    EXPECT_FALSE(device.isTextureReadyForGpuUse(placed.get()));

    const MemoryRequirements requirements = device.getTextureMemoryRequirements(placed.get());
    ASSERT_GT(requirements.size, 0u);
    HeapHandle heap = device.createHeap(HeapDesc{
        .capacity = requirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/gpu_readiness/texture_heap"),
    });
    ASSERT_TRUE(heap);
    if(!device.bindTextureMemory(placed.get(), heap.get(), 0u))
        GTEST_SKIP() << "GPU readiness: DeviceLocal heap is incompatible with virtual textures.";
    EXPECT_TRUE(device.isTextureReadyForGpuUse(placed.get()));
}


TEST_F(GpuResourceReadinessTest, PacketPreflightRejectsUnboundTextureBeforeThunkAndCapture){
    auto& device = GpuResourceReadinessTest::device();
    BufferHandle readyBuffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    TextureDesc virtualDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    virtualDesc.isVirtual = true;
    TextureHandle unboundTexture = device.createTexture(virtualDesc);
    ASSERT_TRUE(readyBuffer);
    ASSERT_TRUE(unboundTexture);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(readyBuffer.get()));
    ASSERT_FALSE(device.isTextureReadyForGpuUse(unboundTexture.get()));

    GpuTaskGraph graph(GpuResourceReadinessTest::arena());
    const GpuGraphResourceId bufferResource = graph.importBuffer(
        readyBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/gpu_readiness/ready_packet_buffer"))
            .setMarkerLabel("Ready Packet Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    const GpuGraphResourceId textureResource = graph.importTexture(
        unboundTexture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/gpu_readiness/unbound_packet_texture"))
            .setMarkerLabel("Unbound Packet Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(bufferResource.valid());
    ASSERT_TRUE(textureResource.valid());
    GpuTaskResourceRange range;
    range.textureSubresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse prefixUse{
        .resource = bufferResource,
        .range = {},
        .requiredState = ResourceStates::Common,
        .access = GpuTaskResourceAccess::Read,
    };
    const GpuTaskResourceUse tailUse{
        .resource = textureResource,
        .range = range,
        .requiredState = ResourceStates::Common,
        .access = GpuTaskResourceAccess::Read,
    };
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/gpu_readiness/ready_packet_prefix"))
        .setMarkerLabel("Ready Packet Prefix")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setResourceUses(&prefixUse, 1u)
    ;
    bool prefixRecorded = false;
    u32 prefixDiscardedCount = 0u;
    const GpuTaskId prefixTask = graph.addTask<PacketPreflightProbeTask>(
        prefixDesc,
        PacketPreflightProbeTask::Payload{
            .recorded = &prefixRecorded,
            .discardedCount = &prefixDiscardedCount,
        }
    );
    ASSERT_TRUE(prefixTask.valid());
    GpuTaskSchedulingHint tailScheduling;
    tailScheduling.mergeWithPrevious = true;
    GpuTaskDesc tailDesc = prefixDesc;
    tailDesc
        .setIdentity(Name("tests/gpu_readiness/unbound_packet_tail"))
        .setMarkerLabel("Unbound Packet Tail")
        .setDependencies(&prefixTask, 1u)
        .setScheduling(tailScheduling)
        .setResourceUses(&tailUse, 1u)
    ;
    bool tailRecorded = false;
    u32 tailDiscardedCount = 0u;
    const GpuTaskId tailTask = graph.addTask<PacketPreflightProbeTask>(
        tailDesc,
        PacketPreflightProbeTask::Payload{
            .recorded = &tailRecorded,
            .discardedCount = &tailDiscardedCount,
        }
    );
    ASSERT_TRUE(tailTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(GpuResourceReadinessTest::arena());
    GpuTaskGraphQueueAssignments assignments(GpuResourceReadinessTest::arena());
    GpuCompiledGraph compiledGraph(GpuResourceReadinessTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/gpu_readiness/unbound_packet_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(prefixTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(compiledGraph.packetForTask(tailTask), packet);
    ASSERT_TRUE(compiledGraph.taskPrecedesInSamePacket(prefixTask, tailTask));

    GpuRecordedGraph recordedGraph(GpuResourceReadinessTest::arena());
    GpuCommandIrCapture capture(GpuResourceReadinessTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph,
        &capture
    ));
    EXPECT_FALSE(prefixRecorded);
    EXPECT_FALSE(tailRecorded);
    EXPECT_EQ(prefixDiscardedCount, 1u);
    EXPECT_EQ(tailDiscardedCount, 1u);
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 0u);
    EXPECT_EQ(recordedGraph.find(packet), nullptr);
}

TEST_F(GpuResourceReadinessTest, PacketPreflightRejectsInvalidGraphInitialTextureState){
    auto& device = GpuResourceReadinessTest::device();
    const TextureDesc desc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureHandle texture = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        Object(static_cast<u64>(0x71a00001u)),
        desc
    );
    ASSERT_TRUE(texture);
    ASSERT_TRUE(device.isTextureReadyForGpuUse(texture.get(), VK_IMAGE_USAGE_SAMPLED_BIT));
    ASSERT_FALSE(device.isTextureReadyForGpuUse(texture.get(), VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

    GpuTaskGraph graph(GpuResourceReadinessTest::arena());
    const GpuGraphResourceId resource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/gpu_readiness/unsupported_initial_texture"))
            .setMarkerLabel("Unsupported Initial Texture")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::VertexBuffer)
    );
    ASSERT_TRUE(resource.valid());
    GpuTaskResourceRange range;
    range.textureSubresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse use{
        .resource = resource,
        .range = range,
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/gpu_readiness/unsupported_initial_texture_task"))
        .setMarkerLabel("Unsupported Initial Texture Task")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setResourceUses(&use, 1u)
    ;
    bool recorded = false;
    u32 discardedCount = 0u;
    const GpuTaskId task = graph.addTask<PacketPreflightProbeTask>(
        taskDesc,
        PacketPreflightProbeTask::Payload{
            .recorded = &recorded,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(GpuResourceReadinessTest::arena());
    GpuTaskGraphQueueAssignments assignments(GpuResourceReadinessTest::arena());
    GpuCompiledGraph compiledGraph(GpuResourceReadinessTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/gpu_readiness/unsupported_initial_texture_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
    ASSERT_NE(compiledTask, nullptr);
    const GpuCompiledBarrier* const barriers = compiledGraph.taskPrologueBarriers(task);
    ASSERT_NE(barriers, nullptr);
    ASSERT_EQ(compiledTask->prologueBarrierCount, 1u);
    EXPECT_EQ(barriers[0u].type, GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(barriers[0u].before, ResourceStates::VertexBuffer);
    EXPECT_EQ(barriers[0u].after, ResourceStates::ShaderResource);
    EXPECT_TRUE(barriers[0u].isGraphInitialState);

    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    GpuRecordedGraph recordedGraph(GpuResourceReadinessTest::arena());
    GpuCommandIrCapture capture(GpuResourceReadinessTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph,
        &capture
    ));
    EXPECT_FALSE(recorded);
    EXPECT_EQ(discardedCount, 1u);
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 0u);
    EXPECT_EQ(recordedGraph.find(packet), nullptr);
}


TEST_F(GpuResourceReadinessTest, OrderedUploadCopyDestConflictRejectsMergedPacketBeforePrefixAndStaging){
    auto& device = GpuResourceReadinessTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(buffer);

    CommandListResourceStateHandoff permanentState(GpuResourceReadinessTest::arena());
    CommandListHandle producer = device.createCommandList();
    ASSERT_TRUE(producer);
    producer->open();
    producer->setPermanentBufferState(buffer.get(), ResourceStates::ShaderResource);
    producer->close(&permanentState);
    ASSERT_FALSE(producer->commandRecordingFailed());
    ASSERT_TRUE(producer->hasCommandBuffer());
    ASSERT_TRUE(permanentState.valid());

    GpuTaskGraph graph(GpuResourceReadinessTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/gpu_readiness/permanent_upload_buffer"))
            .setMarkerLabel("Permanent Upload Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::ShaderResource)
    );
    ASSERT_TRUE(resource.valid());
    const GpuTaskResourceUse prefixUse{
        .resource = resource,
        .range = {},
        .requiredState = ResourceStates::ShaderResource,
        .access = GpuTaskResourceAccess::Read,
    };
    const GpuTaskExternalStateSource externalSources[] = {
        GpuTaskExternalStateSource{ .states = &permanentState },
    };
    const GpuQueueRequest queueRequest{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/gpu_readiness/permanent_upload_prefix"))
        .setMarkerLabel("Permanent Upload Prefix")
        .setQueue(queueRequest)
        .setExternalStateSources(externalSources, LengthOf(externalSources))
        .setResourceUses(&prefixUse, 1u)
    ;
    bool prefixRecorded = false;
    u32 prefixDiscardedCount = 0u;
    const GpuTaskId prefixTask = graph.addTask<PacketPreflightProbeTask>(
        prefixDesc,
        PacketPreflightProbeTask::Payload{
            .recorded = &prefixRecorded,
            .discardedCount = &prefixDiscardedCount,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    const u32 uploadWords[] = { 0x10293847u, 0x55667788u, 0xa5a5c3c3u, 0xdeadbeefu };
    const GpuUploadBlobId uploadBlob = graph.copyUploadData(
        uploadWords,
        sizeof(uploadWords),
        alignof(u32)
    );
    ASSERT_TRUE(uploadBlob.valid());
    GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.mergeWithPrevious = true;
    GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/gpu_readiness/permanent_upload_tail"))
        .setMarkerLabel("Permanent Upload Tail")
        .setQueue(queueRequest)
        .setDependencies(&prefixTask, 1u)
        .setScheduling(uploadScheduling)
    ;
    QueueSubmissionToken acceptedToken{
        .queue = CommandQueue::Graphics,
        .value = 7u,
    };
    const GpuTaskId uploadTask = graph.addUploadBufferTask(
        uploadDesc,
        GpuUploadBufferTaskDesc{
            .source = uploadBlob,
            .destination = resource,
            .finalState = ResourceStates::ShaderResource,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());
    EXPECT_FALSE(acceptedToken.valid());
    const GpuTaskGraphTaskView uploadView = graph.taskAt(uploadTask.index);
    ASSERT_EQ(uploadView.resourceUseCount, 2u);
    EXPECT_EQ(uploadView.resourceUses[0u].requiredState, ResourceStates::CopyDest);
    EXPECT_EQ(uploadView.resourceUses[1u].requiredState, ResourceStates::ShaderResource);

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(GpuResourceReadinessTest::arena());
    GpuTaskGraphQueueAssignments assignments(GpuResourceReadinessTest::arena());
    GpuCompiledGraph compiledGraph(GpuResourceReadinessTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/gpu_readiness/permanent_upload_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(prefixTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_EQ(compiledGraph.packetForTask(uploadTask), packet);
    ASSERT_TRUE(compiledGraph.taskPrecedesInSamePacket(prefixTask, uploadTask));

    GpuRecordedGraph recordedGraph(GpuResourceReadinessTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph
    ));
    EXPECT_FALSE(prefixRecorded);
    EXPECT_EQ(prefixDiscardedCount, 1u);
    EXPECT_FALSE(acceptedToken.valid());
    EXPECT_EQ(recordedGraph.find(packet), nullptr);
}


TEST_F(GpuResourceReadinessTest, PacketPreflightRejectsBufferSpanBeyondImportedDescriptor){
    auto& device = GpuResourceReadinessTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(16u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);

    GpuTaskGraph graph(GpuResourceReadinessTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/gpu_readiness/out_of_bounds_buffer"))
            .setMarkerLabel("Out-Of-Bounds Buffer")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(resource.valid());
    GpuTaskResourceRange range;
    range.bufferRange = BufferRange(8u, 16u);
    const GpuTaskResourceUse use{
        .resource = resource,
        .range = range,
        .requiredState = ResourceStates::Common,
        .access = GpuTaskResourceAccess::Read,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/gpu_readiness/out_of_bounds_buffer_task"))
        .setMarkerLabel("Out-Of-Bounds Buffer Task")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setResourceUses(&use, 1u)
    ;
    bool recorded = false;
    u32 discardedCount = 0u;
    const GpuTaskId task = graph.addTask<PacketPreflightProbeTask>(
        taskDesc,
        PacketPreflightProbeTask::Payload{
            .recorded = &recorded,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(GpuResourceReadinessTest::arena());
    GpuTaskGraphQueueAssignments assignments(GpuResourceReadinessTest::arena());
    GpuCompiledGraph compiledGraph(GpuResourceReadinessTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/gpu_readiness/out_of_bounds_buffer_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(GpuResourceReadinessTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph
    ));
    EXPECT_FALSE(recorded);
    EXPECT_EQ(discardedCount, 1u);
    EXPECT_EQ(recordedGraph.find(packet), nullptr);
}


TEST_F(GpuResourceReadinessTest, PacketPreflightAcceptsBackendlessHazardDomainWithUnknownState){
    auto& device = GpuResourceReadinessTest::device();
    GpuTaskGraph graph(GpuResourceReadinessTest::arena());
    const GpuGraphResourceId resource = graph.importHazardDomain(
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/gpu_readiness/hazard_domain"))
            .setMarkerLabel("Hazard Domain")
            .setType(GpuGraphResourceType::HazardDomain)
    );
    ASSERT_TRUE(resource.valid());
    const GpuTaskResourceUse use{
        .resource = resource,
        .range = {},
        .requiredState = ResourceStates::Unknown,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/gpu_readiness/hazard_domain_task"))
        .setMarkerLabel("Hazard Domain Task")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setResourceUses(&use, 1u)
    ;
    bool recorded = false;
    u32 discardedCount = 0u;
    const GpuTaskId task = graph.addTask<PacketPreflightProbeTask>(
        taskDesc,
        PacketPreflightProbeTask::Payload{
            .recorded = &recorded,
            .discardedCount = &discardedCount,
        }
    );
    ASSERT_TRUE(task.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    GpuTaskGraphAnalysis analysis(GpuResourceReadinessTest::arena());
    GpuTaskGraphQueueAssignments assignments(GpuResourceReadinessTest::arena());
    GpuCompiledGraph compiledGraph(GpuResourceReadinessTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/gpu_readiness/hazard_domain_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(GpuResourceReadinessTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph
    ));
    EXPECT_TRUE(recorded);
    EXPECT_EQ(discardedCount, 0u);
    EXPECT_NE(recordedGraph.find(packet), nullptr);
}


TEST_F(GpuResourceReadinessTest, HandoffProducerRejectsUnboundTextureBeforePublishingReadyBufferState){
    auto& device = GpuResourceReadinessTest::device();
    BufferHandle readyBuffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    TextureDesc virtualDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    virtualDesc.isVirtual = true;
    TextureHandle unboundTexture = device.createTexture(virtualDesc);
    ASSERT_TRUE(readyBuffer);
    ASSERT_TRUE(unboundTexture);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(readyBuffer.get()));
    ASSERT_FALSE(device.isTextureReadyForGpuUse(unboundTexture.get()));

    CommandListResourceStateHandoff invalidSeed(GpuResourceReadinessTest::arena());
    CommandListHandle producer = device.createCommandList();
    ASSERT_TRUE(producer);
    producer->open();
    producer->beginTrackingTextureState(
        unboundTexture.get(),
        TextureSubresourceSet(0u, 1u, 0u, 1u),
        ResourceStates::Common
    );
    producer->beginTrackingBufferState(readyBuffer.get(), ResourceStates::Common);
    producer->close(&invalidSeed);
    EXPECT_TRUE(producer->commandRecordingFailed());
    EXPECT_FALSE(producer->hasCommandBuffer());
    EXPECT_FALSE(invalidSeed.valid());

#if !defined(NWB_FINAL)
    const Object nativeImage(static_cast<u64>(0x63b00001u));
    const TextureDesc unmanagedDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    TextureHandle readyTexture = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        unmanagedDesc
    );
    ASSERT_TRUE(readyTexture);

    CommandListResourceStateHandoff revokedSeed(GpuResourceReadinessTest::arena());
    CommandListHandle readyProducer = device.createCommandList();
    ASSERT_TRUE(readyProducer);
    readyProducer->open();
    readyProducer->beginTrackingTextureState(
        readyTexture.get(),
        TextureSubresourceSet(0u, 1u, 0u, 1u),
        ResourceStates::Common
    );
    readyProducer->beginTrackingBufferState(readyBuffer.get(), ResourceStates::Common);
    readyProducer->close(&revokedSeed);
    ASSERT_FALSE(readyProducer->commandRecordingFailed());
    ASSERT_TRUE(revokedSeed.valid());
    ASSERT_TRUE(device.revokeUnmanagedNativeTextureForTesting(readyTexture.get(), nativeImage));

    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    consumer->open(&revokedSeed);
    EXPECT_TRUE(consumer->commandRecordingFailed());
    EXPECT_FALSE(consumer->hasCommandBuffer());
    EXPECT_FALSE(consumer->hasExplicitBufferState(readyBuffer.get()));
    consumer->close();

    device.releaseRevokedNativeTextureIdentityForTesting(readyTexture.get(), nativeImage);
    TextureHandle replacement = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        unmanagedDesc
    );
    ASSERT_TRUE(replacement);
    CommandListResourceStateHandoff restoredSeed(GpuResourceReadinessTest::arena());
    CommandListHandle restoredProducer = device.createCommandList();
    ASSERT_TRUE(restoredProducer);
    restoredProducer->open();
    restoredProducer->beginTrackingTextureState(
        replacement.get(),
        TextureSubresourceSet(0u, 1u, 0u, 1u),
        ResourceStates::Common
    );
    restoredProducer->beginTrackingBufferState(readyBuffer.get(), ResourceStates::Common);
    restoredProducer->close(&restoredSeed);
    ASSERT_TRUE(restoredSeed.valid());

    consumer->open(&restoredSeed);
    EXPECT_FALSE(consumer->commandRecordingFailed());
    EXPECT_TRUE(consumer->hasExplicitBufferState(readyBuffer.get()));
    consumer->close();
#endif
}


TEST_F(GpuResourceReadinessTest, AccelStructPredicateTracksVirtualMemoryBinding){
    auto& device = GpuResourceReadinessTest::device();
    EXPECT_FALSE(device.isAccelStructReadyForGpuUse(nullptr));
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "GPU readiness: ray-tracing acceleration structures are unavailable.";

    RayTracingAccelStructDesc desc(GpuResourceReadinessTest::arena());
    desc
        .setTopLevelMaxInstances(1u)
        .setIsVirtual(true)
        .setDebugName(Name("tests/gpu_readiness/virtual_accel_struct"))
    ;
    RayTracingAccelStructHandle accelStruct = device.createAccelStruct(desc);
    ASSERT_TRUE(accelStruct);
    EXPECT_FALSE(device.isAccelStructReadyForGpuUse(accelStruct.get()));

    const MemoryRequirements requirements = device.getAccelStructMemoryRequirements(accelStruct.get());
    ASSERT_GT(requirements.size, 0u);
    HeapHandle heap = device.createHeap(HeapDesc{
        .capacity = requirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/gpu_readiness/accel_struct_heap"),
    });
    ASSERT_TRUE(heap);
    if(!device.bindAccelStructMemory(accelStruct.get(), heap.get(), 0u))
        GTEST_SKIP() << "GPU readiness: DeviceLocal heap is incompatible with virtual acceleration structures.";
    EXPECT_TRUE(device.isAccelStructReadyForGpuUse(accelStruct.get()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

