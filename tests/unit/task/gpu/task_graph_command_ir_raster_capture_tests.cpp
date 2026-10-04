// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"

#include <core/task/gpu/capture/command_ir_raster.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_raster_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;


struct OwnerReleaseProbe{
    u32* releases = nullptr;

    explicit OwnerReleaseProbe(u32& value)noexcept : releases(&value){}
    OwnerReleaseProbe(const OwnerReleaseProbe&) = delete;
    OwnerReleaseProbe(OwnerReleaseProbe&& other)noexcept : releases(Exchange(other.releases, nullptr)){}
    ~OwnerReleaseProbe()noexcept{
        if(releases)
            ++*releases;
    }
};


Graphics::GpuCommandIrRasterStateDesc StateForBuffer(
    const Graphics::GpuGraphResourceId vertex,
    const Graphics::BufferHandle& buffer){
    Graphics::GpuCommandIrRasterStateDesc state;
    state.pipeline = Graphics::GpuGraphPipelineId{ .generation = vertex.generation, .index = 2u };
    state.colorAttachment = Graphics::GpuGraphResourceId{ .generation = vertex.generation, .index = 6u };
    state.viewport = Graphics::Viewport(64.f, 48.f);
    state.vertexBuffers.push_back(Graphics::GpuCommandIrRasterVertexOwner{
        .resource = vertex,
        .buffer = buffer,
        .slot = 0u,
        .offset = 8u,
    });
    return state;
}

Graphics::BufferHandle MetadataVertexBuffer(
    TaskGraphTestUtils::TestArena& testArena,
    Graphics::GraphicsBackend::VulkanContext& context,
    Graphics::GraphicsBackend::VulkanAllocator& allocator){
    Graphics::Buffer* const object = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setByteSize(256u).setInitialState(Graphics::ResourceStates::VertexBuffer)
    );
    return Graphics::BufferHandle(object, Graphics::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
}

TEST(GpuCommandIrRasterCapture, ExportRetainsExactBufferAfterCallerGraphAndCaptureReset){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Graphics::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::BufferHandle vertex = MetadataVertexBuffer(testArena, context, allocator);
    ASSERT_TRUE(vertex);
    Graphics::Buffer* const original = vertex.get();
    const Graphics::GpuGraphResourceId vertexId = graph.importBuffer(
        vertex,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_raster/retained_vertex"))
            .setMarkerLabel("Retained Vertex")
            .setType(Graphics::GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(vertexId.valid());
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuCommandIrOwnedStream owned(testArena.arena);
    const Graphics::GpuTaskId task{ .generation = vertexId.generation, .index = 4u };
    {
        const Graphics::GpuCommandIrRasterStateDesc state = StateForBuffer(vertexId, vertex);
        ASSERT_TRUE(capture.captureSetGraphicsState(task, s_CommandIrPacket, s_CommandIrQueue, state));
    }
    ASSERT_TRUE(capture.exportOwned(owned));
    const auto* const owner = owned.rasterStateOwner(0u);
    ASSERT_NE(owner, nullptr);
    ASSERT_EQ(owner->vertexBuffers.size(), 1u);
    EXPECT_EQ(owner->vertexBuffers[0].get(), original);

    graph.reset();
    vertex.reset();
    capture.reset();
    EXPECT_EQ(original->getReferenceCount(), 1u);
    EXPECT_EQ(owner->vertexBuffers[0].get(), original);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(owned.bytes()).valid());
}

TEST(GpuCommandIrRasterCapture, RollbackRestoresOwnedBlobAndOwnerPrefix){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Graphics::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::BufferHandle first = MetadataVertexBuffer(testArena, context, allocator);
    Graphics::BufferHandle second = MetadataVertexBuffer(testArena, context, allocator);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const Graphics::GpuGraphResourceId firstId{ .generation = s_CommandIrTask.generation, .index = 5u };
    const Graphics::GpuGraphResourceId secondId{ .generation = s_CommandIrTask.generation, .index = 7u };
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuCommandIrCapture foreign(testArena.arena);
    Graphics::GpuCommandIrOwnedStream prefix(testArena.arena);
    Graphics::GpuCommandIrOwnedStream rolledBack(testArena.arena);
    const Graphics::GpuCommandIrRasterStateDesc firstState = StateForBuffer(firstId, first);
    const Graphics::GpuCommandIrRasterStateDesc secondState = StateForBuffer(secondId, second);
    const u8 prefixPush[] = { 0x10u, 0x20u, 0x30u, 0x40u };
    const u8 tentativePush[] = { 0x50u, 0x60u, 0x70u, 0x80u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, firstState));
    ASSERT_TRUE(capture.capturePushConstants(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue,
        BinaryByteView{ prefixPush, sizeof(prefixPush) }));
    const Graphics::GpuCommandIrCaptureCheckpoint checkpoint = capture.checkpoint();
    ASSERT_TRUE(capture.exportOwned(prefix));
    const u32 secondBaseReferences = second->getReferenceCount();
    ASSERT_TRUE(capture.captureSetGraphicsState(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, secondState));
    ASSERT_TRUE(capture.capturePushConstants(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue,
        BinaryByteView{ tentativePush, sizeof(tentativePush) }));
    EXPECT_GT(second->getReferenceCount(), secondBaseReferences);
    EXPECT_FALSE(foreign.rollback(checkpoint));
    ASSERT_TRUE(capture.rollback(checkpoint));
    EXPECT_EQ(second->getReferenceCount(), secondBaseReferences);
    EXPECT_EQ(capture.recordCount(), 2u);
    ASSERT_TRUE(capture.exportOwned(rolledBack));
    ASSERT_EQ(rolledBack.bytes().size(), prefix.bytes().size());
    EXPECT_EQ(GLB_MEMCMP(rolledBack.bytes().data(), prefix.bytes().data(), prefix.bytes().size()), 0);
    EXPECT_EQ(rolledBack.rasterStateOwner(1u), nullptr);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(rolledBack.bytes()).valid());

    capture.reset();
    EXPECT_FALSE(capture.rollback(checkpoint));
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(prefix.bytes()).valid());
}

TEST(GpuCommandIrRasterCapture, NativeBufferAddressesStayOutOfWireBytes){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Graphics::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::BufferHandle first = MetadataVertexBuffer(testArena, context, allocator);
    Graphics::BufferHandle second = MetadataVertexBuffer(testArena, context, allocator);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_NE(first.get(), second.get());
    Graphics::GpuCommandIrCapture firstCapture(testArena.arena);
    Graphics::GpuCommandIrCapture secondCapture(testArena.arena);
    const Graphics::GpuGraphResourceId vertex{ .generation = s_CommandIrTask.generation, .index = 5u };
    ASSERT_TRUE(firstCapture.captureSetGraphicsState(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, StateForBuffer(vertex, first)));
    ASSERT_TRUE(secondCapture.captureSetGraphicsState(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, StateForBuffer(vertex, second)));
    const BinaryByteView firstBytes = firstCapture.commandBytes();
    const BinaryByteView secondBytes = secondCapture.commandBytes();
    ASSERT_EQ(firstBytes.size(), secondBytes.size());
    EXPECT_EQ(GLB_MEMCMP(firstBytes.data(), secondBytes.data(), firstBytes.size()), 0);
}

TEST(GpuCommandIrRasterCapture, RetainedOwnerAnchorOutlivesIndependentCaptureArena){
    TaskGraphTestUtils::TestArena ownerArena;
    u32 releases = 0u;
    Graphics::GpuCommandIrOwnerAnchor exported;
    {
        TaskGraphTestUtils::TestArena captureArena;
        Graphics::GpuCommandIrCapture capture(captureArena.arena);
        Graphics::GpuCommandIrOwnerAnchor source = Graphics::MakeGpuCommandIrOwnerAnchor(
            ownerArena.arena, OwnerReleaseProbe(releases));
        ASSERT_TRUE(source.valid());
        exported = source;
        capture.reset();
        EXPECT_EQ(releases, 0u);
    }
    EXPECT_TRUE(exported.valid());
    EXPECT_EQ(releases, 0u);
    exported = Graphics::GpuCommandIrOwnerAnchor{};
    EXPECT_EQ(releases, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

