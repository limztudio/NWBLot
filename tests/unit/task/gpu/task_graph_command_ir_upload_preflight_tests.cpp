// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_upload_preflight_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UploadPreflightFixture final{
    UploadPreflightFixture()
        : graphicsAllocator(testArena.arena)
        , cpuScheduler(0u)
        , context(graphicsAllocator, cpuScheduler, 1u)
        , allocator(context)
        , graph(testArena.arena)
        , compilation(testArena)
    {}


    [[nodiscard]] Graphics::GpuGraphResourceId importBuffer(const Graphics::BufferDesc& description, const Name& identity){
        Graphics::Buffer* const object = NewMetadataOnlyBuffer(testArena.arena, context, allocator, description);
        if(!object)
            return {};
        Graphics::BufferHandle handle(object, Graphics::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
        return graph.importBuffer(
            handle,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Command IR Upload Buffer")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(description.initialState)
        );
    }

    [[nodiscard]] Graphics::GpuGraphResourceId importTexture(const Graphics::TextureDesc& description, const Name& identity){
        Graphics::Texture* const object = NewMetadataOnlyTexture(testArena.arena, context, allocator, description);
        if(!object)
            return {};
        Graphics::TextureHandle handle(object, Graphics::TextureHandle::deleter_type(&testArena.arena), AdoptRef);
        return graph.importTexture(
            handle,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Command IR Upload Texture")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(description.initialState)
        );
    }


    TaskGraphTestUtils::TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator;
    Core::CpuTaskScheduler cpuScheduler;
    Graphics::GraphicsBackend::VulkanContext context;
    Graphics::GraphicsBackend::VulkanAllocator allocator;
    Graphics::GpuTaskGraph graph;
    SingleQueueCompile compilation;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuCommandIrUploadPreflight, BufferChecksSourceRangeAndTerminalStateAgainstDeclaration){
    UploadPreflightFixture fixture;
    const Graphics::BufferDesc description = Graphics::BufferDesc()
        .setByteSize(64u)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    const Graphics::GpuGraphResourceId destination = fixture.importBuffer(
        description, Name("tests/command_ir_upload_preflight/buffer")
    );
    ASSERT_TRUE(destination.valid());
    const u8 bytes[]{ 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    const Graphics::GpuUploadBlobId source = fixture.graph.copyUploadData(bytes, sizeof(bytes), alignof(u32));
    ASSERT_TRUE(source.valid());
    Graphics::GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/command_ir_upload_preflight/buffer_task"))
        .setMarkerLabel("Command IR Buffer Upload")
    ;
    const Graphics::GpuTaskId task = fixture.graph.addUploadBufferTask(
        taskDesc,
        Graphics::GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destination,
            .destinationOffsetBytes = 16u,
            .finalState = Graphics::ResourceStates::ShaderResource,
        }
    );
    ASSERT_TRUE(task.valid());
    ASSERT_TRUE(fixture.compilation.compile(fixture.graph));
    const GpuTaskGraphReadViews reads(fixture.graph, fixture.compilation.compiledGraph);
    const Graphics::GpuSubmissionPacketId packet = reads.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuPhysicalQueueId queue = reads.compiled.packet(packet).plan->queue;
    const auto preflight = [&](const Graphics::GpuCommandIrCapture& capture){
        return Graphics::PreflightGpuCommandIrPacket(capture.commandBytes(), reads.declarations, reads.compiled, packet);
    };

    Graphics::GpuCommandIrCapture accepted(fixture.testArena.arena);
    ASSERT_TRUE(accepted.captureUploadBuffer(
        task, packet, queue, source, destination, 16u, BinaryByteView{ bytes, sizeof(bytes) },
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(accepted).error, Graphics::GpuCommandIrReplayError::None);

    Graphics::GpuCommandIrCapture undeclaredRange(fixture.testArena.arena);
    ASSERT_TRUE(undeclaredRange.captureUploadBuffer(
        task, packet, queue, source, destination, 0u, BinaryByteView{ bytes, sizeof(bytes) },
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(undeclaredRange).error, Graphics::GpuCommandIrReplayError::ResourceUseMismatch);

    Graphics::GpuCommandIrCapture foreignSource(fixture.testArena.arena);
    const Graphics::GpuUploadBlobId missingSource{ .generation = source.generation, .index = source.index + 1u };
    ASSERT_TRUE(foreignSource.captureUploadBuffer(
        task, packet, queue, missingSource, destination, 16u, BinaryByteView{ bytes, sizeof(bytes) },
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(foreignSource).error, Graphics::GpuCommandIrReplayError::InvalidBufferUpload);

    const u8 alteredBytes[]{ 9u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    Graphics::GpuCommandIrCapture alteredPayload(fixture.testArena.arena);
    ASSERT_TRUE(alteredPayload.captureUploadBuffer(
        task, packet, queue, source, destination, 16u, BinaryByteView{ alteredBytes, sizeof(alteredBytes) },
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(alteredPayload).error, Graphics::GpuCommandIrReplayError::InvalidBufferUpload);

    Graphics::GpuCommandIrCapture wrongFinalState(fixture.testArena.arena);
    ASSERT_TRUE(wrongFinalState.captureUploadBuffer(
        task, packet, queue, source, destination, 16u, BinaryByteView{ bytes, sizeof(bytes) },
        Graphics::ResourceStates::CopyDest
    ));
    EXPECT_EQ(preflight(wrongFinalState).error, Graphics::GpuCommandIrReplayError::ResourceUseMismatch);
}

TEST(GpuCommandIrUploadPreflight, PitchedTextureChecksFullMipExtentBytesSubresourceAndTerminalState){
    UploadPreflightFixture fixture;
    const Graphics::TextureDesc description = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(3u)
        .setDimension(Graphics::TextureDimension::Texture2DArray)
        .setArraySize(2u)
        .setMipLevels(2u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    const Graphics::GpuGraphResourceId destination = fixture.importTexture(
        description, Name("tests/command_ir_upload_preflight/texture")
    );
    ASSERT_TRUE(destination.valid());
    u8 bytes[96u]{};
    for(usize index = 0u; index < LengthOf(bytes); ++index)
        bytes[index] = static_cast<u8>(index);
    const Graphics::GpuUploadBlobId source = fixture.graph.copyUploadData(bytes, sizeof(bytes), alignof(u32));
    ASSERT_TRUE(source.valid());
    Graphics::GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/command_ir_upload_preflight/texture_task"))
        .setMarkerLabel("Command IR Texture Upload")
    ;
    const Graphics::GpuTaskId task = fixture.graph.addUploadTextureTask(
        taskDesc,
        Graphics::GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destination,
            .arraySlice = 1u,
            .mipLevel = 0u,
            .rowPitch = 32u,
            .depthPitch = 96u,
            .finalState = Graphics::ResourceStates::ShaderResource,
            .aspect = Graphics::TextureUploadAspect::Color,
        }
    );
    ASSERT_TRUE(task.valid());
    ASSERT_TRUE(fixture.compilation.compile(fixture.graph));
    const GpuTaskGraphReadViews reads(fixture.graph, fixture.compilation.compiledGraph);
    const Graphics::GpuSubmissionPacketId packet = reads.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuPhysicalQueueId queue = reads.compiled.packet(packet).plan->queue;
    Graphics::TextureSlice fullSlice;
    fullSlice.arraySlice = 1u;
    fullSlice = fullSlice.resolve(description);
    ASSERT_EQ(fullSlice.width, 4u);
    ASSERT_EQ(fullSlice.height, 3u);
    ASSERT_EQ(fullSlice.depth, 1u);
    const auto preflight = [&](const Graphics::GpuCommandIrCapture& capture){
        return Graphics::PreflightGpuCommandIrPacket(capture.commandBytes(), reads.declarations, reads.compiled, packet);
    };
    const auto captureTexture = [&](Graphics::GpuCommandIrCapture& capture, const Graphics::TextureSlice& slice,
        const BinaryByteView sourceBytes, const Graphics::ResourceStates::Mask finalState){
        return capture.captureUploadTexture(
            task, packet, queue, source, destination, slice, 32u, 96u, Graphics::TextureUploadAspect::Color,
            sourceBytes, finalState
        );
    };

    Graphics::GpuCommandIrCapture accepted(fixture.testArena.arena);
    ASSERT_TRUE(captureTexture(
        accepted, fullSlice, BinaryByteView{ bytes, sizeof(bytes) }, Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(accepted).error, Graphics::GpuCommandIrReplayError::None);

    Graphics::TextureSlice partialSlice = fullSlice;
    partialSlice.width = 3u;
    Graphics::GpuCommandIrCapture partial(fixture.testArena.arena);
    ASSERT_TRUE(captureTexture(
        partial, partialSlice, BinaryByteView{ bytes, sizeof(bytes) }, Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(partial).error, Graphics::GpuCommandIrReplayError::InvalidTextureUpload);

    Graphics::GpuCommandIrCapture shortBytes(fixture.testArena.arena);
    ASSERT_TRUE(captureTexture(
        shortBytes, fullSlice, BinaryByteView{ bytes, sizeof(bytes) - 4u }, Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(shortBytes).error, Graphics::GpuCommandIrReplayError::InvalidTextureUpload);

    Graphics::TextureSlice undeclaredSlice = fullSlice;
    undeclaredSlice.arraySlice = 0u;
    Graphics::GpuCommandIrCapture undeclared(fixture.testArena.arena);
    ASSERT_TRUE(captureTexture(
        undeclared, undeclaredSlice, BinaryByteView{ bytes, sizeof(bytes) }, Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_EQ(preflight(undeclared).error, Graphics::GpuCommandIrReplayError::ResourceUseMismatch);

    Graphics::GpuCommandIrCapture wrongFinalState(fixture.testArena.arena);
    ASSERT_TRUE(captureTexture(
        wrongFinalState, fullSlice, BinaryByteView{ bytes, sizeof(bytes) }, Graphics::ResourceStates::CopyDest
    ));
    EXPECT_EQ(preflight(wrongFinalState).error, Graphics::GpuCommandIrReplayError::ResourceUseMismatch);
}

TEST(GpuCommandIrUploadPreflight, LateUndeclaredOperationRejectsWholePacketBeforeLowering){
    UploadPreflightFixture fixture;
    const Graphics::BufferDesc description = Graphics::BufferDesc()
        .setByteSize(64u)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    const Graphics::GpuGraphResourceId destination = fixture.importBuffer(
        description, Name("tests/command_ir_upload_preflight/late_buffer")
    );
    ASSERT_TRUE(destination.valid());
    const u8 bytes[]{ 1u, 2u, 3u, 4u };
    const Graphics::GpuUploadBlobId source = fixture.graph.copyUploadData(bytes, sizeof(bytes), alignof(u32));
    ASSERT_TRUE(source.valid());
    Graphics::GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/command_ir_upload_preflight/late_task"))
        .setMarkerLabel("Command IR Late Upload")
    ;
    const Graphics::GpuTaskId task = fixture.graph.addUploadBufferTask(
        taskDesc,
        Graphics::GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destination,
            .destinationOffsetBytes = 16u,
            .finalState = Graphics::ResourceStates::CopyDest,
        }
    );
    ASSERT_TRUE(task.valid());
    ASSERT_TRUE(fixture.compilation.compile(fixture.graph));
    const GpuTaskGraphReadViews reads(fixture.graph, fixture.compilation.compiledGraph);
    const Graphics::GpuSubmissionPacketId packet = reads.compiled.packetForTask(task);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuPhysicalQueueId queue = reads.compiled.packet(packet).plan->queue;

    Graphics::GpuCommandIrCapture capture(fixture.testArena.arena);
    ASSERT_TRUE(capture.captureUploadBuffer(
        task, packet, queue, source, destination, 16u, BinaryByteView{ bytes, sizeof(bytes) },
        Graphics::ResourceStates::CopyDest
    ));
    ASSERT_TRUE(capture.captureUploadBuffer(
        task, packet, queue, source, destination, 32u, BinaryByteView{ bytes, sizeof(bytes) },
        Graphics::ResourceStates::CopyDest
    ));
    ASSERT_TRUE(Graphics::ValidateGpuCommandIrStream(capture.commandBytes()).valid());
    const Graphics::GpuCommandIrReplayResult result = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(), reads.declarations, reads.compiled, packet
    );
    EXPECT_EQ(result.error, Graphics::GpuCommandIrReplayError::ResourceUseMismatch);
    EXPECT_TRUE(result.streamValidation.valid());
    EXPECT_EQ(result.recordIndex, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

