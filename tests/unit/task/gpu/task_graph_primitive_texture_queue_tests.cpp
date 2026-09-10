// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_primitive_texture_queue_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, TextureClearNormalizesQueueCapabilities){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const auto createTexture = [&](const Graphics::TextureDesc& sourceDescription){
        Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
            testArena.arena,
            context,
            allocator,
            sourceDescription
        );
        if(!textureObject)
            return Graphics::TextureHandle{};

        Graphics::TextureHandle texture(
            textureObject,
            Graphics::TextureHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        return texture;
    };
    const Graphics::TextureDesc colorDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UINT)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    const Graphics::TextureDesc compressedDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::BC1_UNORM)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    const Graphics::TextureDesc depthDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::D24S8)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    const Graphics::TextureDesc multisampleColorDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setDimension(Graphics::TextureDimension::Texture2DMS)
        .setFormat(Graphics::Format::RGBA8_UINT)
        .setSampleCount(4u)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    const Graphics::TextureDesc multisampleDepthDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setDimension(Graphics::TextureDimension::Texture2DMS)
        .setFormat(Graphics::Format::D24S8)
        .setSampleCount(4u)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    const Graphics::TextureDesc multisampleCompressedDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setDimension(Graphics::TextureDimension::Texture2DMS)
        .setFormat(Graphics::Format::BC1_UNORM)
        .setSampleCount(4u)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    Graphics::TextureHandle colorTexture = createTexture(colorDescription);
    Graphics::TextureHandle compressedTexture = createTexture(compressedDescription);
    Graphics::TextureHandle depthTexture = createTexture(depthDescription);
    Graphics::TextureHandle multisampleColorTexture = createTexture(multisampleColorDescription);
    Graphics::TextureHandle multisampleDepthTexture = createTexture(multisampleDepthDescription);
    Graphics::TextureHandle multisampleCompressedTexture = createTexture(multisampleCompressedDescription);
    ASSERT_NE(colorTexture.get(), nullptr);
    ASSERT_NE(compressedTexture.get(), nullptr);
    ASSERT_NE(depthTexture.get(), nullptr);
    ASSERT_NE(multisampleColorTexture.get(), nullptr);
    ASSERT_NE(multisampleDepthTexture.get(), nullptr);
    ASSERT_NE(multisampleCompressedTexture.get(), nullptr);

    Graphics::GpuTaskDesc transferDesc;
    transferDesc
        .setIdentity(Name("tests/task_graph/full_clear_transfer_input"))
        .setMarkerLabel("Full Clear Transfer Input")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;

    Graphics::GpuTaskGraph colorGraph(testArena.arena);
    const Graphics::GpuGraphResourceId colorResource = colorGraph.importTexture(
        colorTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/full_clear_color"))
            .setMarkerLabel("Full Clear Color")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(colorResource.valid());
    Graphics::GpuClearTextureTaskDesc colorClear;
    colorClear.destination = colorResource;
    colorClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    colorClear.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    const Graphics::GpuTaskId colorTask = colorGraph.addClearTextureTask(transferDesc, colorClear);
    ASSERT_TRUE(colorTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(colorGraph);

        EXPECT_EQ(
            declarations.taskAt(colorTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Compute)
        );
    }

    Graphics::GpuClearTextureRectUIntTaskDesc colorRectClear;
    colorRectClear.destination = colorResource;
    colorRectClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    colorRectClear.rect = Graphics::Rect(4, 4);
    Graphics::GpuTaskDesc rectTransferDesc = transferDesc;
    rectTransferDesc
        .setIdentity(Name("tests/task_graph/rect_clear_transfer_input"))
        .setMarkerLabel("Rect Clear Transfer Input")
    ;
    const Graphics::GpuTaskId colorRectTask = colorGraph.addClearTextureRectUIntTask(
        rectTransferDesc,
        colorRectClear
    );
    ASSERT_TRUE(colorRectTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(colorGraph);

        EXPECT_EQ(
            declarations.taskAt(colorRectTask.index).queue.requiredCapabilities,
            Graphics::GpuQueueCapability::Transfer
        );
    }

    Graphics::GpuTaskGraph rectGraph(testArena.arena);
    const Graphics::GpuGraphResourceId rectResource = rectGraph.importTexture(
        colorTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/rect_clear_color"))
            .setMarkerLabel("Rect Clear Color")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(rectResource.valid());
    Graphics::GpuClearTextureRectUIntTaskDesc isolatedRectClear = colorRectClear;
    isolatedRectClear.destination = rectResource;
    const Graphics::GpuTaskId isolatedRectTask = rectGraph.addClearTextureRectUIntTask(
        rectTransferDesc,
        isolatedRectClear
    );
    ASSERT_TRUE(isolatedRectTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(rectGraph);

        EXPECT_EQ(
            declarations.taskAt(isolatedRectTask.index).queue.requiredCapabilities,
            Graphics::GpuQueueCapability::Transfer
        );
    }

    Graphics::GpuTaskGraph compressedGraph(testArena.arena);
    const Graphics::GpuGraphResourceId compressedResource = compressedGraph.importTexture(
        compressedTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/full_clear_compressed"))
            .setMarkerLabel("Full Clear Compressed")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(compressedResource.valid());
    Graphics::GpuClearTextureTaskDesc compressedClear;
    compressedClear.destination = compressedResource;
    compressedClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    compressedClear.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    const Graphics::GpuTaskId compressedTask = compressedGraph.addClearTextureTask(
        transferDesc,
        compressedClear
    );
    ASSERT_TRUE(compressedTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(compressedGraph);

        EXPECT_EQ(
            declarations.taskAt(compressedTask.index).queue.requiredCapabilities,
            Graphics::GpuQueueCapability::Transfer
        );
    }

    Graphics::GpuTaskGraph depthGraph(testArena.arena);
    const Graphics::GpuGraphResourceId depthResource = depthGraph.importTexture(
        depthTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/full_clear_depth"))
            .setMarkerLabel("Full Clear Depth")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(depthResource.valid());
    Graphics::GpuClearTextureTaskDesc depthClear;
    depthClear.destination = depthResource;
    depthClear.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    depthClear.valueType = Graphics::GpuClearTextureTaskValueType::DepthStencil;
    depthClear.clearDepth = true;
    const Graphics::GpuTaskId depthTask = depthGraph.addClearTextureTask(transferDesc, depthClear);
    ASSERT_TRUE(depthTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(depthGraph);

        EXPECT_EQ(
            declarations.taskAt(depthTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
        );
    }

    Graphics::GpuTaskGraph multisampleColorGraph(testArena.arena);
    const Graphics::GpuGraphResourceId multisampleColorResource = multisampleColorGraph.importTexture(
        multisampleColorTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/full_clear_multisample_color"))
            .setMarkerLabel("Full Clear Multisample Color")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(multisampleColorResource.valid());
    Graphics::GpuClearTextureTaskDesc multisampleColorClear = colorClear;
    multisampleColorClear.destination = multisampleColorResource;
    const Graphics::GpuTaskId multisampleColorTask = multisampleColorGraph.addClearTextureTask(
        transferDesc,
        multisampleColorClear
    );
    ASSERT_TRUE(multisampleColorTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(multisampleColorGraph);

        EXPECT_EQ(
            declarations.taskAt(multisampleColorTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Compute)
        );
    }

    Graphics::GpuTaskGraph multisampleDepthGraph(testArena.arena);
    const Graphics::GpuGraphResourceId multisampleDepthResource = multisampleDepthGraph.importTexture(
        multisampleDepthTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/full_clear_multisample_depth"))
            .setMarkerLabel("Full Clear Multisample Depth")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(multisampleDepthResource.valid());
    Graphics::GpuClearTextureTaskDesc multisampleDepthClear = depthClear;
    multisampleDepthClear.destination = multisampleDepthResource;
    const Graphics::GpuTaskId multisampleDepthTask = multisampleDepthGraph.addClearTextureTask(
        transferDesc,
        multisampleDepthClear
    );
    ASSERT_TRUE(multisampleDepthTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(multisampleDepthGraph);

        EXPECT_EQ(
            declarations.taskAt(multisampleDepthTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
        );
    }

    Graphics::GpuTaskGraph multisampleCompressedGraph(testArena.arena);
    const Graphics::GpuGraphResourceId multisampleCompressedResource = multisampleCompressedGraph.importTexture(
        multisampleCompressedTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/full_clear_multisample_compressed"))
            .setMarkerLabel("Full Clear Multisample Compressed")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(multisampleCompressedResource.valid());
    Graphics::QueueSubmissionToken compressedRejectedToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    Graphics::GpuClearTextureTaskDesc multisampleCompressedClear;
    multisampleCompressedClear.acceptedToken = &compressedRejectedToken;
    multisampleCompressedClear.destination = multisampleCompressedResource;
    multisampleCompressedClear.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    EXPECT_FALSE(multisampleCompressedGraph.addClearTextureTask(
        transferDesc,
        multisampleCompressedClear
    ).valid());
    EXPECT_FALSE(compressedRejectedToken.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(multisampleCompressedGraph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    Graphics::GpuTaskGraph multisampleRectGraph(testArena.arena);
    const Graphics::GpuGraphResourceId multisampleRectResource = multisampleRectGraph.importTexture(
        multisampleColorTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/rect_clear_multisample_color"))
            .setMarkerLabel("Rect Clear Multisample Color")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(multisampleRectResource.valid());
    Graphics::QueueSubmissionToken rectRejectedToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    Graphics::GpuClearTextureRectUIntTaskDesc multisampleRectClear;
    multisampleRectClear.destination = multisampleRectResource;
    multisampleRectClear.rect = Graphics::Rect(4, 4);
    multisampleRectClear.acceptedToken = &rectRejectedToken;
    EXPECT_FALSE(multisampleRectGraph.addClearTextureRectUIntTask(
        rectTransferDesc,
        multisampleRectClear
    ).valid());
    EXPECT_FALSE(rectRejectedToken.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(multisampleRectGraph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    const auto expectCompileRejected = [&](const Graphics::GpuTaskGraph& graph, const Graphics::GpuPhysicalQueueInfo& queue){
        const Graphics::GpuPhysicalQueueInfo queues[] = { queue };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    };
    const auto expectCompiledOn = [&](
        const Graphics::GpuTaskGraph& graph,
        const Graphics::GpuTaskId task,
        const Graphics::GpuPhysicalQueueInfo& queue,
        const Graphics::CommandQueue::Enum expectedQueue
    ){
        const Graphics::GpuPhysicalQueueInfo queues[] = { queue };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queueClass, expectedQueue);
    };

    expectCompileRejected(colorGraph, DedicatedTransferQueue());
    expectCompiledOn(colorGraph, colorTask, DedicatedComputeQueue(), Graphics::CommandQueue::Compute);
    expectCompiledOn(colorGraph, colorRectTask, DedicatedComputeQueue(), Graphics::CommandQueue::Compute);
    expectCompiledOn(colorGraph, colorTask, GraphicsQueue(), Graphics::CommandQueue::Graphics);
    expectCompiledOn(colorGraph, colorRectTask, GraphicsQueue(), Graphics::CommandQueue::Graphics);
    expectCompiledOn(rectGraph, isolatedRectTask, DedicatedTransferQueue(), Graphics::CommandQueue::Transfer);
    expectCompiledOn(compressedGraph, compressedTask, DedicatedTransferQueue(), Graphics::CommandQueue::Transfer);
    expectCompileRejected(depthGraph, DedicatedTransferQueue());
    expectCompileRejected(depthGraph, DedicatedComputeQueue());
    expectCompiledOn(depthGraph, depthTask, GraphicsQueue(), Graphics::CommandQueue::Graphics);
    expectCompileRejected(multisampleColorGraph, DedicatedTransferQueue());
    expectCompiledOn(
        multisampleColorGraph,
        multisampleColorTask,
        DedicatedComputeQueue(),
        Graphics::CommandQueue::Compute
    );
    expectCompiledOn(
        multisampleColorGraph,
        multisampleColorTask,
        GraphicsQueue(),
        Graphics::CommandQueue::Graphics
    );
    expectCompileRejected(multisampleDepthGraph, DedicatedTransferQueue());
    expectCompileRejected(multisampleDepthGraph, DedicatedComputeQueue());
    expectCompiledOn(
        multisampleDepthGraph,
        multisampleDepthTask,
        GraphicsQueue(),
        Graphics::CommandQueue::Graphics
    );
}

TEST(GpuTaskGraph, DepthTextureUploadsAndMultisampleCopiesPromoteExactQueueCapabilities){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const auto createTexture = [&](const Graphics::TextureDesc& sourceDescription){
        Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
            testArena.arena,
            context,
            allocator,
            sourceDescription
        );
        if(!textureObject)
            return Graphics::TextureHandle{};

        Graphics::TextureHandle texture(
            textureObject,
            Graphics::TextureHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        return texture;
    };

    Graphics::GpuTaskDesc transferDesc;
    transferDesc
        .setIdentity(Name("tests/task_graph/depth_upload_exact_queue"))
        .setMarkerLabel("Depth Upload Exact Queue")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;

    Graphics::TextureDesc uploadDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::D32)
        .setInitialState(Graphics::ResourceStates::CopyDest)
    ;
    Graphics::TextureHandle uploadTexture = createTexture(uploadDescription);
    ASSERT_TRUE(uploadTexture);
    Graphics::GpuTaskGraph uploadGraph(testArena.arena);
    const Graphics::GpuGraphResourceId uploadDestination = uploadGraph.importTexture(
        uploadTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/depth_upload_destination"))
            .setMarkerLabel("Depth Upload Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(uploadDestination.valid());
    const u32 uploadTexels[16u]{};
    const Graphics::GpuUploadBlobId uploadSource = uploadGraph.copyUploadData(
        uploadTexels,
        sizeof(uploadTexels),
        alignof(u32)
    );
    ASSERT_TRUE(uploadSource.valid());
    const Graphics::GpuTaskId uploadTask = uploadGraph.addUploadTextureTask(
        transferDesc,
        Graphics::GpuUploadTextureTaskDesc{
            .source = uploadSource,
            .destination = uploadDestination,
            .finalState = Graphics::ResourceStates::CopyDest,
        }
    );
    ASSERT_TRUE(uploadTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(uploadGraph);

        EXPECT_EQ(
            declarations.taskAt(uploadTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
        );
    }

    Graphics::TextureDesc multisampleDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setDimension(Graphics::TextureDimension::Texture2DMS)
        .setSampleCount(4u)
        .setFormat(Graphics::Format::D32)
        .setInitialState(Graphics::ResourceStates::CopySource)
    ;
    Graphics::TextureHandle copySourceTexture = createTexture(multisampleDescription);
    multisampleDescription.setInitialState(Graphics::ResourceStates::CopyDest);
    Graphics::TextureHandle copyDestinationTexture = createTexture(multisampleDescription);
    ASSERT_TRUE(copySourceTexture);
    ASSERT_TRUE(copyDestinationTexture);
    Graphics::GpuTaskGraph copyGraph(testArena.arena);
    const Graphics::GpuGraphResourceId copySource = copyGraph.importTexture(
        copySourceTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/multisample_depth_copy_source"))
            .setMarkerLabel("Multisample Depth Copy Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    const Graphics::GpuGraphResourceId copyDestination = copyGraph.importTexture(
        copyDestinationTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/multisample_depth_copy_destination"))
            .setMarkerLabel("Multisample Depth Copy Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(copySource.valid());
    ASSERT_TRUE(copyDestination.valid());
    const Graphics::GpuCopyTextureTaskRegion copyRegion{
        .source = copySource,
        .sourceSlice = {},
        .destination = copyDestination,
        .destinationSlice = {},
    };
    Graphics::GpuTaskDesc copyTaskDesc = transferDesc;
    copyTaskDesc
        .setIdentity(Name("tests/task_graph/multisample_depth_copy_exact_queue"))
        .setMarkerLabel("Multisample Depth Copy Exact Queue")
    ;
    const Graphics::GpuTaskId copyTask = copyGraph.addCopyTextureTask(
        copyTaskDesc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &copyRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(copyTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(copyGraph);

        EXPECT_EQ(
            declarations.taskAt(copyTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
        );
    }

    const auto expectCompiledOn = [&](
        const Graphics::GpuTaskGraph& graph,
        const Graphics::GpuTaskId task,
        const Graphics::GpuPhysicalQueueInfo& queue,
        const Graphics::CommandQueue::Enum expectedQueueClass,
        const bool expected
    ){
        const Graphics::GpuPhysicalQueueInfo queues[] = { queue };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_EQ(Compile(graph, analysis, topology, assignments, compiledGraph), expected);
        if(expected){
            const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
            ASSERT_NE(assignment, nullptr);
            EXPECT_EQ(assignment->queueClass, expectedQueueClass);
        }
    };
    expectCompiledOn(uploadGraph, uploadTask, DedicatedTransferQueue(), Graphics::CommandQueue::Transfer, false);
    expectCompiledOn(uploadGraph, uploadTask, GraphicsQueue(), Graphics::CommandQueue::Graphics, true);
    expectCompiledOn(copyGraph, copyTask, DedicatedTransferQueue(), Graphics::CommandQueue::Transfer, false);
    expectCompiledOn(copyGraph, copyTask, GraphicsQueue(), Graphics::CommandQueue::Graphics, true);

    Graphics::TextureDesc partialDescription = Graphics::TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    Graphics::TextureHandle partialSourceTexture = createTexture(partialDescription);
    Graphics::TextureHandle partialDestinationTexture = createTexture(partialDescription);
    ASSERT_TRUE(partialSourceTexture);
    ASSERT_TRUE(partialDestinationTexture);
    Graphics::GpuTaskGraph partialGraph(testArena.arena);
    const Graphics::GpuGraphResourceId partialSource = partialGraph.importTexture(
        partialSourceTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/partial_copy_source"))
            .setMarkerLabel("Partial Copy Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    const Graphics::GpuGraphResourceId partialDestination = partialGraph.importTexture(
        partialDestinationTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/partial_copy_destination"))
            .setMarkerLabel("Partial Copy Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_TRUE(partialSource.valid());
    ASSERT_TRUE(partialDestination.valid());
    Graphics::TextureSlice partialSlice;
    partialSlice.setSize(4u, 4u, 1u);
    const Graphics::GpuCopyTextureTaskRegion partialRegion{
        .source = partialSource,
        .sourceSlice = partialSlice,
        .destination = partialDestination,
        .destinationSlice = partialSlice,
    };
    Graphics::GpuTaskDesc partialTaskDesc = transferDesc;
    partialTaskDesc
        .setIdentity(Name("tests/task_graph/partial_copy_exact_queue"))
        .setMarkerLabel("Partial Copy Exact Queue")
    ;
    const Graphics::GpuTaskId partialTask = partialGraph.addCopyTextureTask(
        partialTaskDesc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &partialRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(partialTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(partialGraph);

        EXPECT_EQ(
            declarations.taskAt(partialTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Compute)
        );
    }
    expectCompiledOn(partialGraph, partialTask, DedicatedTransferQueue(), Graphics::CommandQueue::Transfer, false);
    expectCompiledOn(partialGraph, partialTask, DedicatedComputeQueue(), Graphics::CommandQueue::Compute, true);
    expectCompiledOn(partialGraph, partialTask, GraphicsQueue(), Graphics::CommandQueue::Graphics, true);

    Graphics::GpuTaskGraph partialGraphicsGraph(testArena.arena);
    const Graphics::GpuGraphResourceId partialGraphicsSource = partialGraphicsGraph.importTexture(
        partialSourceTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/partial_graphics_copy_source"))
            .setMarkerLabel("Partial Graphics Copy Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    const Graphics::GpuGraphResourceId partialGraphicsDestination = partialGraphicsGraph.importTexture(
        partialDestinationTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/partial_graphics_copy_destination"))
            .setMarkerLabel("Partial Graphics Copy Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_TRUE(partialGraphicsSource.valid());
    ASSERT_TRUE(partialGraphicsDestination.valid());
    Graphics::GpuCopyTextureTaskRegion partialGraphicsRegion = partialRegion;
    partialGraphicsRegion.source = partialGraphicsSource;
    partialGraphicsRegion.destination = partialGraphicsDestination;
    Graphics::GpuTaskDesc partialGraphicsTaskDesc = partialTaskDesc;
    partialGraphicsTaskDesc.queue.requiredCapabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueueCapability::Graphics
    );
    const Graphics::GpuTaskId partialGraphicsTask = partialGraphicsGraph.addCopyTextureTask(
        partialGraphicsTaskDesc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &partialGraphicsRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(partialGraphicsTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(partialGraphicsGraph);

        EXPECT_EQ(
            declarations.taskAt(partialGraphicsTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
        );
    }
    expectCompiledOn(
        partialGraphicsGraph,
        partialGraphicsTask,
        GraphicsQueue(
            0u,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Graphics)
        ),
        Graphics::CommandQueue::Graphics,
        true
    );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

