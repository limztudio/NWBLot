// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_clear_task_graph_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TextureClearTaskGraphTestsTag>;
namespace Graphics = Core;

inline constexpr Name s_TextureClearScratchArena("tests/graphics/texture_clear_task_graph_scratch");

struct TextureClearTestContext{
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator;
    Core::Alloc::ThreadPool threadPool;
    Graphics::GraphicsBackend::VulkanContext context;
    Graphics::GraphicsBackend::VulkanAllocator allocator;

    TextureClearTestContext()
        : graphicsAllocator(testArena.arena)
        , threadPool(0u)
        , context(graphicsAllocator, threadPool, 1u)
        , allocator(context)
    {}

    [[nodiscard]] Graphics::TextureHandle createTexture(const Graphics::TextureDesc& description){
        Graphics::Texture* const textureObject = NewArenaObject<Graphics::Texture>(
            testArena.arena,
            context,
            allocator
        );
        if(!textureObject)
            return {};

        Graphics::TextureHandle texture(
            textureObject,
            Graphics::TextureHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        Graphics::TextureDesc& storedDescription = const_cast<Graphics::TextureDesc&>(texture->getDescription());
        storedDescription = description;
        return texture;
    }
};

[[nodiscard]] constexpr Graphics::GpuQueueCapability::Mask QueueCapabilities(
    const Graphics::GpuQueueCapability::Mask first,
    const Graphics::GpuQueueCapability::Mask second = Graphics::GpuQueueCapability::None,
    const Graphics::GpuQueueCapability::Mask third = Graphics::GpuQueueCapability::None
){
    return static_cast<Graphics::GpuQueueCapability::Mask>(
        static_cast<u8>(first)
        | static_cast<u8>(second)
        | static_cast<u8>(third)
    );
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo GraphicsQueue(){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ 0u, 1u },
        .queueClass = Graphics::CommandQueue::Graphics,
        .capabilities = QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueueCapability::Transfer
        ),
        .familyIndex = 0u,
        .queueIndex = 0u,
        .dedicated = false,
    };
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedComputeQueue(){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ 1u, 1u },
        .queueClass = Graphics::CommandQueue::Compute,
        .capabilities = QueueCapabilities(
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueueCapability::Transfer
        ),
        .familyIndex = 1u,
        .queueIndex = 0u,
        .dedicated = true,
    };
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedTransferQueue(){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ 2u, 1u },
        .queueClass = Graphics::CommandQueue::Transfer,
        .capabilities = Graphics::GpuQueueCapability::Transfer,
        .familyIndex = 2u,
        .queueIndex = 0u,
        .dedicated = true,
    };
}

[[nodiscard]] Graphics::GpuGraphResourceId ImportTexture(
    Graphics::GpuTaskGraph& graph,
    const Graphics::TextureHandle& texture,
    const Name& identity,
    const AStringView markerLabel
){
    return graph.importTexture(
        texture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(identity)
            .setMarkerLabel(markerLabel)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
}

[[nodiscard]] Graphics::GpuTaskDesc MakeTransferTaskDesc(const Name& identity, const AStringView markerLabel){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(markerLabel)
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    return desc;
}

[[nodiscard]] bool Compile(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskGraphQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    Graphics::GpuCompiledGraph& compiledGraph,
    Core::Alloc::ScratchArena& scratchArena
){
    Graphics::GpuTaskGraphCompileOptions options;
    options.allowMetadataOnlyTasks = true;
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena, options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, TextureClearNormalizesPartialRegionsAndPreservesGraphicsAlternatives){
    TextureClearTestContext testContext;
    const Graphics::TextureDesc textureDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UINT)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    Graphics::TextureHandle texture = testContext.createTexture(textureDescription);
    ASSERT_TRUE(texture);

    Graphics::GpuTaskGraph partialGraph(testContext.testArena.arena);
    const Graphics::GpuGraphResourceId partialResource = ImportTexture(
        partialGraph,
        texture,
        Name("tests/task_graph/partial_texture_clear"),
        "Partial Texture Clear"
    );
    ASSERT_TRUE(partialResource.valid());
    Graphics::GpuClearTextureRectUIntTaskDesc partialClear;
    partialClear.destination = partialResource;
    partialClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    partialClear.rect = Graphics::Rect(2, 2);
    const Graphics::GpuTaskId partialTask = partialGraph.addClearTextureRectUIntTask(
        MakeTransferTaskDesc(
            Name("tests/task_graph/partial_texture_clear_task"),
            "Partial Texture Clear Task"
        ),
        partialClear
    );
    ASSERT_TRUE(partialTask.valid());
    EXPECT_EQ(
        partialGraph.taskAt(partialTask.index).queue.requiredCapabilities,
        QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Compute)
    );

    {
        const Graphics::GpuPhysicalQueueInfo queue = DedicatedTransferQueue();
        const Graphics::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
        Graphics::GpuTaskGraphAnalysis analysis(testContext.testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testContext.testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testContext.testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TextureClearScratchArena);
        EXPECT_FALSE(Compile(partialGraph, analysis, topology, assignments, compiledGraph, scratchArena));
    }
    {
        const Graphics::GpuPhysicalQueueInfo queue = DedicatedComputeQueue();
        const Graphics::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
        Graphics::GpuTaskGraphAnalysis analysis(testContext.testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testContext.testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testContext.testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TextureClearScratchArena);
        ASSERT_TRUE(Compile(partialGraph, analysis, topology, assignments, compiledGraph, scratchArena));
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(partialTask);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Compute);
    }

    Graphics::GpuTaskGraph graphicsGraph(testContext.testArena.arena);
    const Graphics::GpuGraphResourceId graphicsResource = ImportTexture(
        graphicsGraph,
        texture,
        Name("tests/task_graph/graphics_texture_clear"),
        "Graphics Texture Clear"
    );
    ASSERT_TRUE(graphicsResource.valid());
    Graphics::GpuTaskDesc graphicsFullDesc = MakeTransferTaskDesc(
        Name("tests/task_graph/graphics_full_texture_clear_task"),
        "Graphics Full Texture Clear Task"
    );
    graphicsFullDesc.queue.requiredCapabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueueCapability::Graphics
    );
    Graphics::GpuClearTextureTaskDesc graphicsFullClear;
    graphicsFullClear.destination = graphicsResource;
    graphicsFullClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    graphicsFullClear.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    const Graphics::GpuTaskId graphicsFullTask = graphicsGraph.addClearTextureTask(
        graphicsFullDesc,
        graphicsFullClear
    );
    ASSERT_TRUE(graphicsFullTask.valid());
    EXPECT_EQ(
        graphicsGraph.taskAt(graphicsFullTask.index).queue.requiredCapabilities,
        QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
    );

    Graphics::GpuTaskDesc graphicsRectDesc = MakeTransferTaskDesc(
        Name("tests/task_graph/graphics_partial_texture_clear_task"),
        "Graphics Partial Texture Clear Task"
    );
    graphicsRectDesc.queue.requiredCapabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueueCapability::Graphics
    );
    Graphics::GpuClearTextureRectUIntTaskDesc graphicsRectClear = partialClear;
    graphicsRectClear.destination = graphicsResource;
    const Graphics::GpuTaskId graphicsRectTask = graphicsGraph.addClearTextureRectUIntTask(
        graphicsRectDesc,
        graphicsRectClear
    );
    ASSERT_TRUE(graphicsRectTask.valid());
    EXPECT_EQ(
        graphicsGraph.taskAt(graphicsRectTask.index).queue.requiredCapabilities,
        QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
    );
}

TEST(GpuCommandIrReplay, TextureClearRequiresDeclaredAndPhysicalQueueCapabilities){
    TextureClearTestContext testContext;
    const Graphics::TextureDesc textureDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UINT)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    Graphics::TextureHandle texture = testContext.createTexture(textureDescription);
    ASSERT_TRUE(texture);

    Graphics::GpuTaskGraph fullRectGraph(testContext.testArena.arena);
    const Graphics::GpuGraphResourceId fullRectResource = ImportTexture(
        fullRectGraph,
        texture,
        Name("tests/command_ir_replay/full_rect_texture_clear"),
        "Replay Full Rect Texture Clear"
    );
    ASSERT_TRUE(fullRectResource.valid());
    Graphics::GpuClearTextureRectUIntTaskDesc fullRectClear;
    fullRectClear.destination = fullRectResource;
    fullRectClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    fullRectClear.rect = Graphics::Rect(4, 4);
    const Graphics::GpuTaskId fullRectTask = fullRectGraph.addClearTextureRectUIntTask(
        MakeTransferTaskDesc(
            Name("tests/command_ir_replay/full_rect_texture_clear_task"),
            "Replay Full Rect Texture Clear Task"
        ),
        fullRectClear
    );
    ASSERT_TRUE(fullRectTask.valid());
    ASSERT_EQ(
        fullRectGraph.taskAt(fullRectTask.index).queue.requiredCapabilities,
        Graphics::GpuQueueCapability::Transfer
    );

    const Graphics::GpuPhysicalQueueInfo graphicsQueue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology graphicsTopology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis fullRectAnalysis(testContext.testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments fullRectAssignments(testContext.testArena.arena);
    Graphics::GpuCompiledGraph fullRectCompiledGraph(testContext.testArena.arena);
    {
        Core::Alloc::ScratchArena scratchArena(s_TextureClearScratchArena);
        ASSERT_TRUE(Compile(
            fullRectGraph,
            fullRectAnalysis,
            graphicsTopology,
            fullRectAssignments,
            fullRectCompiledGraph,
            scratchArena
        ));
    }
    const Graphics::GpuSubmissionPacketId fullRectPacket = fullRectCompiledGraph.packetForTask(fullRectTask);
    ASSERT_TRUE(fullRectPacket.valid());
    const Graphics::GpuPhysicalQueueId fullRectQueue = fullRectCompiledGraph.packet(fullRectPacket).queue;

    Graphics::GpuCommandIrCapture validFullRectCapture(testContext.testArena.arena);
    ASSERT_TRUE(validFullRectCapture.captureClearTextureRectUInt(
        fullRectTask,
        fullRectPacket,
        fullRectQueue,
        fullRectResource,
        fullRectClear
    ));
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            validFullRectCapture.commandBytes(),
            fullRectGraph,
            fullRectCompiledGraph,
            fullRectPacket
        ).error,
        Graphics::GpuCommandIrReplayError::None
    );

    Graphics::GpuClearTextureRectUIntTaskDesc undeclaredPartialClear = fullRectClear;
    undeclaredPartialClear.rect = Graphics::Rect(2, 2);
    Graphics::GpuCommandIrCapture undeclaredPartialCapture(testContext.testArena.arena);
    ASSERT_TRUE(undeclaredPartialCapture.captureClearTextureRectUInt(
        fullRectTask,
        fullRectPacket,
        fullRectQueue,
        fullRectResource,
        undeclaredPartialClear
    ));
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            undeclaredPartialCapture.commandBytes(),
            fullRectGraph,
            fullRectCompiledGraph,
            fullRectPacket
        ).error,
        Graphics::GpuCommandIrReplayError::InvalidTextureClear
    );

    Graphics::GpuClearTextureTaskDesc undeclaredFullClear;
    undeclaredFullClear.destination = fullRectResource;
    undeclaredFullClear.subresources = fullRectClear.subresources;
    undeclaredFullClear.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    Graphics::GpuCommandIrCapture undeclaredFullCapture(testContext.testArena.arena);
    ASSERT_TRUE(undeclaredFullCapture.captureClearTexture(
        fullRectTask,
        fullRectPacket,
        fullRectQueue,
        fullRectResource,
        undeclaredFullClear
    ));
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            undeclaredFullCapture.commandBytes(),
            fullRectGraph,
            fullRectCompiledGraph,
            fullRectPacket
        ).error,
        Graphics::GpuCommandIrReplayError::InvalidTextureClear
    );

    Graphics::GpuTaskGraph partialGraph(testContext.testArena.arena);
    const Graphics::GpuGraphResourceId partialResource = ImportTexture(
        partialGraph,
        texture,
        Name("tests/command_ir_replay/partial_texture_clear"),
        "Replay Partial Texture Clear"
    );
    ASSERT_TRUE(partialResource.valid());
    Graphics::GpuClearTextureRectUIntTaskDesc partialClear = undeclaredPartialClear;
    partialClear.destination = partialResource;
    const Graphics::GpuTaskId partialTask = partialGraph.addClearTextureRectUIntTask(
        MakeTransferTaskDesc(
            Name("tests/command_ir_replay/partial_texture_clear_task"),
            "Replay Partial Texture Clear Task"
        ),
        partialClear
    );
    ASSERT_TRUE(partialTask.valid());
    ASSERT_EQ(
        partialGraph.taskAt(partialTask.index).queue.requiredCapabilities,
        QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Compute)
    );

    const Graphics::GpuPhysicalQueueInfo computeQueue = DedicatedComputeQueue();
    const Graphics::GpuTaskGraphQueueTopology computeTopology{
        .queues = &computeQueue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis partialAnalysis(testContext.testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments partialAssignments(testContext.testArena.arena);
    Graphics::GpuCompiledGraph partialCompiledGraph(testContext.testArena.arena);
    {
        Core::Alloc::ScratchArena scratchArena(s_TextureClearScratchArena);
        ASSERT_TRUE(Compile(
            partialGraph,
            partialAnalysis,
            computeTopology,
            partialAssignments,
            partialCompiledGraph,
            scratchArena
        ));
    }
    const Graphics::GpuSubmissionPacketId partialPacket = partialCompiledGraph.packetForTask(partialTask);
    ASSERT_TRUE(partialPacket.valid());
    const Graphics::GpuPhysicalQueueId partialQueueId = partialCompiledGraph.packet(partialPacket).queue;

    Graphics::GpuCommandIrCapture partialCapture(testContext.testArena.arena);
    ASSERT_TRUE(partialCapture.captureClearTextureRectUInt(
        partialTask,
        partialPacket,
        partialQueueId,
        partialResource,
        partialClear
    ));
    Graphics::GpuClearTextureTaskDesc fullClear;
    fullClear.destination = partialResource;
    fullClear.subresources = partialClear.subresources;
    fullClear.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    Graphics::GpuCommandIrCapture fullCapture(testContext.testArena.arena);
    ASSERT_TRUE(fullCapture.captureClearTexture(
        partialTask,
        partialPacket,
        partialQueueId,
        partialResource,
        fullClear
    ));
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            partialCapture.commandBytes(),
            partialGraph,
            partialCompiledGraph,
            partialPacket
        ).error,
        Graphics::GpuCommandIrReplayError::None
    );
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            fullCapture.commandBytes(),
            partialGraph,
            partialCompiledGraph,
            partialPacket
        ).error,
        Graphics::GpuCommandIrReplayError::None
    );

    const Graphics::GpuPhysicalQueueInfo* const compiledQueue = partialCompiledGraph.queueInfo(partialQueueId);
    ASSERT_NE(compiledQueue, nullptr);
    Graphics::GpuPhysicalQueueInfo* const corruptedQueue = const_cast<Graphics::GpuPhysicalQueueInfo*>(compiledQueue);
    const Graphics::GpuQueueCapability::Mask originalCapabilities = corruptedQueue->capabilities;
    corruptedQueue->capabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueueCapability::Graphics
    );
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            partialCapture.commandBytes(),
            partialGraph,
            partialCompiledGraph,
            partialPacket
        ).error,
        Graphics::GpuCommandIrReplayError::InvalidTextureClear
    );
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            fullCapture.commandBytes(),
            partialGraph,
            partialCompiledGraph,
            partialPacket
        ).error,
        Graphics::GpuCommandIrReplayError::InvalidTextureClear
    );
    corruptedQueue->capabilities = originalCapabilities;
}

TEST(GpuTextureClearContract, RejectsUnsupportedStagedFormatsAtDeclarationAndReplay){
    TextureClearTestContext testContext;
    const Graphics::TextureDesc supportedDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::BC1_UNORM)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    Graphics::TextureHandle texture = testContext.createTexture(supportedDescription);
    ASSERT_TRUE(texture);
    Graphics::TextureDesc& textureDescription = const_cast<Graphics::TextureDesc&>(texture->getDescription());

    Graphics::GpuTaskGraph supportedGraph(testContext.testArena.arena);
    const Graphics::GpuGraphResourceId supportedResource = ImportTexture(
        supportedGraph,
        texture,
        Name("tests/texture_clear_contract/supported_bc1"),
        "Supported BC1 Clear"
    );
    ASSERT_TRUE(supportedResource.valid());
    Graphics::GpuClearTextureTaskDesc supportedClear;
    supportedClear.destination = supportedResource;
    supportedClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    supportedClear.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    const Graphics::GpuTaskId supportedTask = supportedGraph.addClearTextureTask(
        MakeTransferTaskDesc(
            Name("tests/texture_clear_contract/supported_bc1_task"),
            "Supported BC1 Clear Task"
        ),
        supportedClear
    );
    ASSERT_TRUE(supportedTask.valid());
    ASSERT_EQ(
        supportedGraph.taskAt(supportedTask.index).queue.requiredCapabilities,
        Graphics::GpuQueueCapability::Transfer
    );

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
    Graphics::GpuTaskGraphAnalysis analysis(testContext.testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testContext.testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testContext.testArena.arena);
    Core::Alloc::ScratchArena scratchArena(s_TextureClearScratchArena);
    ASSERT_TRUE(Compile(supportedGraph, analysis, topology, assignments, compiledGraph, scratchArena));
    const Graphics::GpuSubmissionPacketId packet = compiledGraph.packetForTask(supportedTask);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuPhysicalQueueId queueId = compiledGraph.packet(packet).queue;
    Graphics::GpuCommandIrCapture capture(testContext.testArena.arena);
    ASSERT_TRUE(capture.captureClearTexture(
        supportedTask,
        packet,
        queueId,
        supportedResource,
        supportedClear
    ));
    EXPECT_EQ(
        Graphics::PreflightGpuCommandIrPacket(
            capture.commandBytes(),
            supportedGraph,
            compiledGraph,
            packet
        ).error,
        Graphics::GpuCommandIrReplayError::None
    );

    constexpr Graphics::Format::Enum s_UnsupportedFormats[] = {
        Graphics::Format::BC6H_UFLOAT,
        Graphics::Format::BC6H_SFLOAT,
        Graphics::Format::BC7_UNORM,
        Graphics::Format::BC7_UNORM_SRGB,
        Graphics::Format::ASTC_4x4_UNORM,
        Graphics::Format::ASTC_12x12_FLOAT,
    };
    for(const Graphics::Format::Enum format : s_UnsupportedFormats){
        SCOPED_TRACE(static_cast<u32>(format));
        textureDescription.setFormat(format);
        const Graphics::GpuCommandIrReplayResult replayResult = Graphics::PreflightGpuCommandIrPacket(
            capture.commandBytes(),
            supportedGraph,
            compiledGraph,
            packet
        );
        EXPECT_EQ(replayResult.error, Graphics::GpuCommandIrReplayError::InvalidTextureClear);
        EXPECT_EQ(replayResult.recordIndex, 0u);

        Graphics::GpuTaskGraph rejectedGraph(testContext.testArena.arena);
        const Graphics::GpuGraphResourceId rejectedResource = ImportTexture(
            rejectedGraph,
            texture,
            Name("tests/texture_clear_contract/rejected_compressed"),
            "Rejected Compressed Clear"
        );
        ASSERT_TRUE(rejectedResource.valid());
        Graphics::QueueSubmissionToken acceptedToken{
            .queue = Graphics::CommandQueue::Graphics,
            .value = 1u,
            .physicalQueueIndex = 0u,
            .deviceGeneration = 1u,
        };
        Graphics::GpuClearTextureTaskDesc rejectedClear = supportedClear;
        rejectedClear.destination = rejectedResource;
        rejectedClear.acceptedToken = &acceptedToken;
        EXPECT_FALSE(rejectedGraph.addClearTextureTask(
            MakeTransferTaskDesc(
                Name("tests/texture_clear_contract/rejected_compressed_task"),
                "Rejected Compressed Clear Task"
            ),
            rejectedClear
        ).valid());
        EXPECT_FALSE(acceptedToken.valid());
        EXPECT_EQ(rejectedGraph.taskCount(), 0u);
    }
    textureDescription.setFormat(Graphics::Format::BC1_UNORM);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

