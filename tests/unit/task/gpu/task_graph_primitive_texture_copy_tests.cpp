// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_primitive_texture_copy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, CopyTextureTaskRequiresTypedTextureImports){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId source = AddTextureMetadata(
        graph,
        Name("tests/task_graph/built_in_copy_source"),
        "Built-In Copy Source"
    );
    const Graphics::GpuGraphResourceId destination = AddTextureMetadata(
        graph,
        Name("tests/task_graph/built_in_copy_destination"),
        "Built-In Copy Destination"
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());

    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/built_in_copy"))
        .setMarkerLabel("Built-In Copy")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const Graphics::GpuCopyTextureTaskRegion region{
        .source = source,
        .sourceSlice = {},
        .destination = destination,
        .destinationSlice = {},
    };
    EXPECT_FALSE(graph.addCopyTextureTask(
        desc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &region,
            .regionCount = 1u,
        }
    ).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_EQ(declarations.taskCount(), 0u);
}

TEST(GpuTaskGraph, CopyTextureTaskPreflightsTypedTextureContract){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const Graphics::TextureDesc validDescription = Graphics::TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setArraySize(2u)
        .setMipLevels(2u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setDimension(Graphics::TextureDimension::Texture2DArray)
        .setInitialState(Graphics::ResourceStates::Common)
    ;

    Graphics::GpuTaskGraph graph(testArena.arena);
    const ImportedTexturePair textures = ImportTexturePair(
        testArena,
        context,
        allocator,
        graph,
        validDescription,
        validDescription
    );
    const Graphics::GpuGraphResourceId sourceResource = textures.source;
    const Graphics::GpuGraphResourceId destinationResource = textures.destination;
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());

    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/copy_contract"))
        .setMarkerLabel("Copy Contract")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    Graphics::GpuCopyTextureTaskRegion region{
        .source = sourceResource,
        .sourceSlice = {},
        .destination = destinationResource,
        .destinationSlice = {},
    };
    Graphics::QueueSubmissionToken acceptedToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    const Graphics::GpuCopyTextureTaskDesc copyDesc{
        .regions = &region,
        .regionCount = 1u,
        .acceptedToken = &acceptedToken,
    };
    const auto expectRejected = [&]{
        acceptedToken = Graphics::QueueSubmissionToken{
            .value = 1u,
            .queue = Graphics::CommandQueue::Graphics,
            .physicalQueueIndex = 0u,
            .deviceGeneration = 1u,
        };
        EXPECT_FALSE(graph.addCopyTextureTask(desc, copyDesc).valid());
        EXPECT_FALSE(acceptedToken.valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.taskCount(), 0u);
    };
    const auto expectDescriptionsRejected = [&](
        const Graphics::TextureDesc& rejectedSourceDescription,
        const Graphics::TextureDesc& rejectedDestinationDescription,
        const Graphics::TextureSlice& rejectedSourceSlice,
        const Graphics::TextureSlice& rejectedDestinationSlice
    ){
        Graphics::GpuTaskGraph rejectedGraph(testArena.arena);
        const ImportedTexturePair rejectedTextures = ImportTexturePair(
            testArena,
            context,
            allocator,
            rejectedGraph,
            rejectedSourceDescription,
            rejectedDestinationDescription
        );
        ASSERT_TRUE(rejectedTextures.source.valid());
        ASSERT_TRUE(rejectedTextures.destination.valid());
        const Graphics::GpuCopyTextureTaskRegion rejectedRegion{
            .source = rejectedTextures.source,
            .sourceSlice = rejectedSourceSlice,
            .destination = rejectedTextures.destination,
            .destinationSlice = rejectedDestinationSlice,
        };
        Graphics::QueueSubmissionToken rejectedToken{
            .value = 1u,
            .queue = Graphics::CommandQueue::Graphics,
            .physicalQueueIndex = 0u,
            .deviceGeneration = 1u,
        };
        EXPECT_FALSE(rejectedGraph.addCopyTextureTask(
            desc,
            Graphics::GpuCopyTextureTaskDesc{
                .regions = &rejectedRegion,
                .regionCount = 1u,
                .acceptedToken = &rejectedToken,
            }
        ).valid());
        EXPECT_FALSE(rejectedToken.valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(rejectedGraph);
        EXPECT_EQ(declarations.taskCount(), 0u);
    };

    Graphics::TextureDesc malformedShape = validDescription;
    malformedShape.setWidth(0u);
    expectDescriptionsRejected(malformedShape, malformedShape, {}, {});

    malformedShape = validDescription;
    malformedShape
        .setDimension(Graphics::TextureDimension::Texture1D)
        .setHeight(2u)
        .setDepth(1u)
        .setArraySize(1u)
    ;
    expectDescriptionsRejected(malformedShape, malformedShape, {}, {});

    malformedShape = validDescription;
    malformedShape
        .setDimension(Graphics::TextureDimension::Texture2D)
        .setDepth(2u)
        .setArraySize(1u)
    ;
    expectDescriptionsRejected(malformedShape, malformedShape, {}, {});

    malformedShape = validDescription;
    malformedShape
        .setDimension(Graphics::TextureDimension::Texture3D)
        .setDepth(4u)
        .setArraySize(2u)
    ;
    expectDescriptionsRejected(malformedShape, malformedShape, {}, {});

    malformedShape = validDescription;
    malformedShape
        .setDimension(Graphics::TextureDimension::TextureCube)
        .setHeight(4u)
        .setDepth(1u)
        .setArraySize(6u)
    ;
    expectDescriptionsRejected(malformedShape, malformedShape, {}, {});

    malformedShape = validDescription;
    malformedShape
        .setDimension(Graphics::TextureDimension::TextureCubeArray)
        .setDepth(1u)
        .setArraySize(7u)
    ;
    expectDescriptionsRejected(malformedShape, malformedShape, {}, {});

    malformedShape = validDescription;
    malformedShape
        .setDimension(Graphics::TextureDimension::Texture2D)
        .setWidth(4u)
        .setHeight(4u)
        .setDepth(1u)
        .setArraySize(1u)
        .setMipLevels(4u)
    ;
    expectDescriptionsRejected(malformedShape, malformedShape, {}, {});

    region.sourceSlice.setMipLevel(2u);
    expectRejected();

    region.sourceSlice = {};
    region.sourceSlice.setArraySlice(2u);
    expectRejected();

    region.sourceSlice = {};
    region.sourceSlice.setOrigin(7u, 0u, 0u).setSize(2u, 1u, 1u);
    expectRejected();

    region.sourceSlice = {};
    Graphics::TextureDesc mismatchedDescription = validDescription;
    mismatchedDescription.setSampleCount(4u);
    expectDescriptionsRejected(validDescription, mismatchedDescription, {}, {});

    mismatchedDescription = validDescription;
    mismatchedDescription.setWidth(16u);
    expectDescriptionsRejected(validDescription, mismatchedDescription, {}, {});

    mismatchedDescription = validDescription;
    mismatchedDescription.setFormat(Graphics::Format::RGBA8_UINT);
    expectDescriptionsRejected(validDescription, mismatchedDescription, {}, {});

    mismatchedDescription = validDescription;
    mismatchedDescription.sampleQuality = 1u;
    expectDescriptionsRejected(validDescription, mismatchedDescription, {}, {});

    Graphics::TextureDesc rejectedDescription = validDescription;
    rejectedDescription
        .setMipLevels(1u)
        .setSampleCount(4u)
        .setFormat(Graphics::Format::BC1_UNORM)
        .setDimension(Graphics::TextureDimension::Texture2DMSArray)
    ;
    expectDescriptionsRejected(rejectedDescription, rejectedDescription, {}, {});

    rejectedDescription = validDescription;
    rejectedDescription
        .setDimension(Graphics::TextureDimension::Texture1D)
        .setHeight(1u)
        .setDepth(1u)
        .setArraySize(1u)
        .setMipLevels(1u)
        .setSampleCount(2u)
    ;
    expectDescriptionsRejected(rejectedDescription, rejectedDescription, {}, {});

    rejectedDescription = validDescription;
    rejectedDescription
        .setDimension(Graphics::TextureDimension::TextureCube)
        .setDepth(1u)
        .setArraySize(6u)
        .setMipLevels(1u)
        .setSampleCount(2u)
    ;
    expectDescriptionsRejected(rejectedDescription, rejectedDescription, {}, {});

    Graphics::TextureDesc maximalDescription = validDescription;
    maximalDescription.setWidth(Limit<u32>::s_Max);
    Graphics::TextureSlice oversizedOriginSlice;
    oversizedOriginSlice
        .setOrigin(static_cast<u32>(Limit<i32>::s_Max) + 1u, 0u, 0u)
        .setSize(1u, 1u, 1u)
    ;
    expectDescriptionsRejected(
        maximalDescription,
        maximalDescription,
        oversizedOriginSlice,
        oversizedOriginSlice
    );

    rejectedDescription = validDescription;
    rejectedDescription.setFormat(Graphics::Format::UNKNOWN);
    expectDescriptionsRejected(rejectedDescription, rejectedDescription, {}, {});

    Graphics::TextureDesc rejectedSourceDescription = validDescription;
    rejectedSourceDescription
        .setDimension(Graphics::TextureDimension::Texture1D)
        .setHeight(1u)
        .setDepth(1u)
        .setArraySize(1u)
    ;
    Graphics::TextureDesc rejectedDestinationDescription = validDescription;
    rejectedDestinationDescription
        .setDimension(Graphics::TextureDimension::Texture2D)
        .setHeight(1u)
        .setDepth(1u)
        .setArraySize(1u)
    ;
    expectDescriptionsRejected(rejectedSourceDescription, rejectedDestinationDescription, {}, {});

    rejectedDescription = Graphics::TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Graphics::Format::BC1_UNORM)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    Graphics::TextureSlice misalignedBlockSlice;
    misalignedBlockSlice.setOrigin(1u, 0u, 0u).setSize(4u, 4u, 1u);
    expectDescriptionsRejected(
        rejectedDescription,
        rejectedDescription,
        misalignedBlockSlice,
        misalignedBlockSlice
    );

    Graphics::TextureDesc compressedEdgeDescription = rejectedDescription;
    compressedEdgeDescription.setWidth(6u).setHeight(6u);
    Graphics::GpuTaskGraph compressedEdgeGraph(testArena.arena);
    const ImportedTexturePair compressedEdgeTextures = ImportTexturePair(
        testArena,
        context,
        allocator,
        compressedEdgeGraph,
        compressedEdgeDescription,
        compressedEdgeDescription
    );
    ASSERT_TRUE(compressedEdgeTextures.source.valid());
    ASSERT_TRUE(compressedEdgeTextures.destination.valid());
    Graphics::TextureSlice compressedEdgeSlice;
    compressedEdgeSlice.setOrigin(4u, 4u, 0u).setSize(2u, 2u, 1u);
    const Graphics::GpuCopyTextureTaskRegion compressedEdgeRegion{
        .source = compressedEdgeTextures.source,
        .sourceSlice = compressedEdgeSlice,
        .destination = compressedEdgeTextures.destination,
        .destinationSlice = compressedEdgeSlice,
    };
    const Graphics::GpuTaskId compressedEdgeTask = compressedEdgeGraph.addCopyTextureTask(
        desc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &compressedEdgeRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(compressedEdgeTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(compressedEdgeGraph);

        EXPECT_EQ(
            declarations.taskAt(compressedEdgeTask.index).queue.requiredCapabilities,
            QueueCapabilities(Graphics::GpuQueueCapability::Transfer, Graphics::GpuQueueCapability::Compute)
        );
    }

    region.sourceSlice = {};
    region.destinationSlice = {};
    Graphics::TextureSlice mipOneSlice;
    mipOneSlice.setMipLevel(1u).setArraySlice(1u);
    const Graphics::GpuCopyTextureTaskRegion validRegions[]{
        Graphics::GpuCopyTextureTaskRegion{
            .source = sourceResource,
            .sourceSlice = {},
            .destination = destinationResource,
            .destinationSlice = {},
        },
        Graphics::GpuCopyTextureTaskRegion{
            .source = sourceResource,
            .sourceSlice = mipOneSlice,
            .destination = destinationResource,
            .destinationSlice = mipOneSlice,
        },
        Graphics::GpuCopyTextureTaskRegion{
            .source = sourceResource,
            .sourceSlice = {},
            .destination = destinationResource,
            .destinationSlice = {},
        },
    };
    const Graphics::GpuTaskId task = graph.addCopyTextureTask(
        desc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = validRegions,
            .regionCount = LengthOf(validRegions),
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(task.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphTaskView taskView = declarations.taskAt(task.index);

        EXPECT_EQ(taskView.queue.requiredCapabilities, Graphics::GpuQueueCapability::Transfer);
        ASSERT_EQ(taskView.resourceUseCount, 6u);
        const Graphics::GpuTaskResourceUse* const uses = taskView.resourceUses;
        ASSERT_NE(uses, nullptr);
        EXPECT_EQ(uses[0u].resource, sourceResource);
        EXPECT_EQ(uses[0u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
        EXPECT_EQ(uses[0u].requiredState, Graphics::ResourceStates::CopySource);
        EXPECT_EQ(uses[0u].access, Graphics::GpuTaskResourceAccess::Read);
        EXPECT_EQ(uses[1u].resource, destinationResource);
        EXPECT_EQ(uses[1u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
        EXPECT_EQ(uses[1u].requiredState, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(uses[1u].access, Graphics::GpuTaskResourceAccess::Write);
        EXPECT_EQ(uses[2u].resource, sourceResource);
        EXPECT_EQ(uses[2u].range.textureSubresources, Graphics::TextureSubresourceSet(1u, 1u, 1u, 1u));
        EXPECT_EQ(uses[2u].requiredState, Graphics::ResourceStates::CopySource);
        EXPECT_EQ(uses[2u].access, Graphics::GpuTaskResourceAccess::Read);
        EXPECT_EQ(uses[3u].resource, destinationResource);
        EXPECT_EQ(uses[3u].range.textureSubresources, Graphics::TextureSubresourceSet(1u, 1u, 1u, 1u));
        EXPECT_EQ(uses[3u].requiredState, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(uses[3u].access, Graphics::GpuTaskResourceAccess::Write);
        EXPECT_EQ(uses[4u].resource, sourceResource);
        EXPECT_EQ(uses[4u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
        EXPECT_EQ(uses[4u].requiredState, Graphics::ResourceStates::CopySource);
        EXPECT_EQ(uses[4u].access, Graphics::GpuTaskResourceAccess::Read);
        EXPECT_EQ(uses[5u].resource, destinationResource);
        EXPECT_EQ(uses[5u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
        EXPECT_EQ(uses[5u].requiredState, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(uses[5u].access, Graphics::GpuTaskResourceAccess::Write);
    }
}

TEST(GpuTaskGraph, ResolveTextureTaskRequiresTypedTextureImports){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId source = AddTextureMetadata(
        graph,
        Name("tests/task_graph/built_in_resolve_source"),
        "Built-In Resolve Source"
    );
    const Graphics::GpuGraphResourceId destination = AddTextureMetadata(
        graph,
        Name("tests/task_graph/built_in_resolve_destination"),
        "Built-In Resolve Destination"
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());

    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/built_in_resolve"))
        .setMarkerLabel("Built-In Resolve")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            true,
            true,
        })
    ;
    const Graphics::GpuResolveTextureTaskRegion region{
        .source = source,
        .destination = destination,
    };
    EXPECT_FALSE(graph.addResolveTextureTask(
        desc,
        Graphics::GpuResolveTextureTaskDesc{
            .regions = &region,
            .regionCount = 1u,
        }
    ).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.taskCount(), 0u);
}

TEST(GpuTaskGraph, ResolveTextureTaskPreflightsTypedTextureContract){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const Graphics::TextureDesc validSourceDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setMipLevels(2u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setSampleCount(4u)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    const Graphics::TextureDesc validDestinationDescription = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setMipLevels(2u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setSampleCount(1u)
        .setInitialState(Graphics::ResourceStates::Common)
    ;

    Graphics::GpuTaskGraph graph(testArena.arena);
    const ImportedTexturePair textures = ImportTexturePair(
        testArena,
        context,
        allocator,
        graph,
        validSourceDescription,
        validDestinationDescription
    );
    const Graphics::GpuGraphResourceId sourceResource = textures.source;
    const Graphics::GpuGraphResourceId destinationResource = textures.destination;
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());

    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/resolve_contract"))
        .setMarkerLabel("Resolve Contract")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            true,
            true,
        })
    ;
    Graphics::GpuResolveTextureTaskRegion region{
        .source = sourceResource,
        .destination = destinationResource,
    };
    Graphics::QueueSubmissionToken acceptedToken{
        .value = 1u,
        .queue = Graphics::CommandQueue::Graphics,
        .physicalQueueIndex = 0u,
        .deviceGeneration = 1u,
    };
    const Graphics::GpuResolveTextureTaskDesc resolveDesc{
        .regions = &region,
        .regionCount = 1u,
        .acceptedToken = &acceptedToken,
    };

    Graphics::GpuTaskDesc transferOnlyDesc = desc;
    transferOnlyDesc
        .setIdentity(Name("tests/task_graph/resolve_contract_transfer_only"))
        .setMarkerLabel("Resolve Contract Transfer Only")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    EXPECT_FALSE(graph.addResolveTextureTask(transferOnlyDesc, resolveDesc).valid());
    EXPECT_FALSE(acceptedToken.valid());

    const auto expectDescriptionsRejected = [&](
        const Graphics::TextureDesc& rejectedSourceDescription,
        const Graphics::TextureDesc& rejectedDestinationDescription,
        const Graphics::TextureSubresourceSet& rejectedSourceSubresources,
        const Graphics::TextureSubresourceSet& rejectedDestinationSubresources
    ){
        Graphics::GpuTaskGraph rejectedGraph(testArena.arena);
        const ImportedTexturePair rejectedTextures = ImportTexturePair(
            testArena,
            context,
            allocator,
            rejectedGraph,
            rejectedSourceDescription,
            rejectedDestinationDescription
        );
        ASSERT_TRUE(rejectedTextures.source.valid());
        ASSERT_TRUE(rejectedTextures.destination.valid());
        const Graphics::GpuResolveTextureTaskRegion rejectedRegion{
            .source = rejectedTextures.source,
            .sourceSubresources = rejectedSourceSubresources,
            .destination = rejectedTextures.destination,
            .destinationSubresources = rejectedDestinationSubresources,
        };
        Graphics::QueueSubmissionToken rejectedToken{
            .value = 1u,
            .queue = Graphics::CommandQueue::Graphics,
            .physicalQueueIndex = 0u,
            .deviceGeneration = 1u,
        };
        EXPECT_FALSE(rejectedGraph.addResolveTextureTask(
            desc,
            Graphics::GpuResolveTextureTaskDesc{
                .regions = &rejectedRegion,
                .regionCount = 1u,
                .acceptedToken = &rejectedToken,
            }
        ).valid());
        EXPECT_FALSE(rejectedToken.valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(rejectedGraph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    };

    Graphics::TextureDesc rejectedSourceDescription = validSourceDescription;
    rejectedSourceDescription.setSampleCount(1u);
    expectDescriptionsRejected(
        rejectedSourceDescription,
        validDestinationDescription,
        {},
        {}
    );

    Graphics::TextureDesc rejectedDestinationDescription = validDestinationDescription;
    rejectedDestinationDescription.setSampleCount(4u);
    expectDescriptionsRejected(
        validSourceDescription,
        rejectedDestinationDescription,
        {},
        {}
    );

    rejectedDestinationDescription = validDestinationDescription;
    rejectedDestinationDescription.setFormat(Graphics::Format::RGBA8_UINT);
    expectDescriptionsRejected(
        validSourceDescription,
        rejectedDestinationDescription,
        {},
        {}
    );

    rejectedSourceDescription = validSourceDescription;
    rejectedDestinationDescription = validDestinationDescription;
    rejectedSourceDescription.setFormat(Graphics::Format::D24S8);
    rejectedDestinationDescription.setFormat(Graphics::Format::D24S8);
    expectDescriptionsRejected(
        rejectedSourceDescription,
        rejectedDestinationDescription,
        {},
        {}
    );

    rejectedSourceDescription = validSourceDescription;
    rejectedDestinationDescription = validDestinationDescription;
    rejectedSourceDescription
        .setDimension(Graphics::TextureDimension::Texture2DMSArray)
        .setArraySize(2u)
    ;
    rejectedDestinationDescription
        .setDimension(Graphics::TextureDimension::Texture2DArray)
        .setArraySize(2u)
    ;
    expectDescriptionsRejected(
        rejectedSourceDescription,
        rejectedDestinationDescription,
        Graphics::TextureSubresourceSet(0u, 1u, 0u, 2u),
        Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u)
    );

    rejectedSourceDescription = validSourceDescription;
    rejectedDestinationDescription = validDestinationDescription;
    rejectedSourceDescription
        .setDimension(Graphics::TextureDimension::Texture2DMS)
        .setMipLevels(1u)
    ;
    rejectedDestinationDescription
        .setDimension(Graphics::TextureDimension::Texture3D)
        .setDepth(1u)
        .setMipLevels(1u)
    ;
    expectDescriptionsRejected(
        rejectedSourceDescription,
        rejectedDestinationDescription,
        {},
        {}
    );

    rejectedDestinationDescription = validDestinationDescription;
    rejectedDestinationDescription.setWidth(8u);
    expectDescriptionsRejected(
        validSourceDescription,
        rejectedDestinationDescription,
        {},
        {}
    );
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    const Graphics::GpuResolveTextureTaskRegion validRegions[]{
        Graphics::GpuResolveTextureTaskRegion{
            .source = sourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .destination = destinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        Graphics::GpuResolveTextureTaskRegion{
            .source = sourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u),
            .destination = destinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u),
        },
    };
    const Graphics::GpuTaskId task = graph.addResolveTextureTask(
        desc,
        Graphics::GpuResolveTextureTaskDesc{
            .regions = validRegions,
            .regionCount = LengthOf(validRegions),
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(task.valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuTaskGraphTaskView taskView = declarations.taskAt(task.index);

    ASSERT_EQ(taskView.resourceUseCount, 4u);
    const Graphics::GpuTaskResourceUse* const uses = taskView.resourceUses;
    ASSERT_NE(uses, nullptr);
    EXPECT_EQ(uses[0u].resource, sourceResource);
    EXPECT_EQ(uses[0u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
    EXPECT_EQ(uses[0u].requiredState, Graphics::ResourceStates::ResolveSource);
    EXPECT_EQ(uses[0u].access, Graphics::GpuTaskResourceAccess::Read);
    EXPECT_EQ(uses[1u].resource, destinationResource);
    EXPECT_EQ(uses[1u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
    EXPECT_EQ(uses[1u].requiredState, Graphics::ResourceStates::ResolveDest);
    EXPECT_EQ(uses[1u].access, Graphics::GpuTaskResourceAccess::Write);
    EXPECT_EQ(uses[2u].resource, sourceResource);
    EXPECT_EQ(uses[2u].range.textureSubresources, Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u));
    EXPECT_EQ(uses[2u].requiredState, Graphics::ResourceStates::ResolveSource);
    EXPECT_EQ(uses[2u].access, Graphics::GpuTaskResourceAccess::Read);
    EXPECT_EQ(uses[3u].resource, destinationResource);
    EXPECT_EQ(uses[3u].range.textureSubresources, Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u));
    EXPECT_EQ(uses[3u].requiredState, Graphics::ResourceStates::ResolveDest);
    EXPECT_EQ(uses[3u].access, Graphics::GpuTaskResourceAccess::Write);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

