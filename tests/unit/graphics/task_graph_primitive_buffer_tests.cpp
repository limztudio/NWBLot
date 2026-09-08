// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_primitive_buffer_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, UploadBufferTaskPreflightsNativeAlignmentContract){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::Alloc::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const Graphics::BufferDesc destinationCreationDesc = Graphics::BufferDesc()
        .setByteSize(8u)
        .setInitialState(Graphics::ResourceStates::Common)
    ;
    Graphics::Buffer* const destinationObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        destinationCreationDesc
    );
    ASSERT_NE(destinationObject, nullptr);
    Graphics::BufferHandle destination(
        destinationObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/upload_buffer_alignment_destination"))
            .setMarkerLabel("Upload Buffer Alignment Destination")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_TRUE(destinationResource.valid());

    const u8 uploadBytes[] = { 0u, 1u, 2u, 3u };
    const Graphics::GpuUploadBlobId alignedBlob = graph.copyUploadData(uploadBytes, sizeof(uploadBytes), alignof(u32));
    const Graphics::GpuUploadBlobId unalignedBlob = graph.copyUploadData(
        uploadBytes,
        sizeof(uploadBytes) - 1u,
        alignof(u8)
    );
    ASSERT_TRUE(alignedBlob.valid());
    ASSERT_TRUE(unalignedBlob.valid());

    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/upload_buffer_alignment"))
        .setMarkerLabel("Upload Buffer Alignment")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    EXPECT_FALSE(graph.addUploadBufferTask(
        desc,
        Graphics::GpuUploadBufferTaskDesc{
            .source = alignedBlob,
            .destination = destinationResource,
            .destinationOffsetBytes = 1u,
            .finalState = Graphics::ResourceStates::CopyDest,
        }
    ).valid());
    EXPECT_FALSE(graph.addUploadBufferTask(
        desc,
        Graphics::GpuUploadBufferTaskDesc{
            .source = unalignedBlob,
            .destination = destinationResource,
            .destinationOffsetBytes = 0u,
            .finalState = Graphics::ResourceStates::CopyDest,
        }
    ).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    const Graphics::GpuTaskId copyDestTask = graph.addUploadBufferTask(
        desc,
        Graphics::GpuUploadBufferTaskDesc{
            .source = alignedBlob,
            .destination = destinationResource,
            .destinationOffsetBytes = 4u,
            .finalState = Graphics::ResourceStates::CopyDest,
        }
    );
    ASSERT_TRUE(copyDestTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphTaskView copyDestView = declarations.taskAt(copyDestTask.index);

        EXPECT_EQ(declarations.taskCount(), 1u);
        ASSERT_EQ(copyDestView.resourceUseCount, 1u);
        ASSERT_NE(copyDestView.resourceUses, nullptr);
        EXPECT_EQ(copyDestView.resourceUses[0u].requiredState, Graphics::ResourceStates::CopyDest);
    }

    Graphics::GpuTaskDesc finalStateDesc = desc;
    finalStateDesc
        .setIdentity(Name("tests/task_graph/upload_buffer_final_state"))
        .setMarkerLabel("Upload Buffer Final State")
    ;
    const Graphics::GpuTaskId finalStateTask = graph.addUploadBufferTask(
        finalStateDesc,
        Graphics::GpuUploadBufferTaskDesc{
            .source = alignedBlob,
            .destination = destinationResource,
            .destinationOffsetBytes = 0u,
            .finalState = Graphics::ResourceStates::ShaderResource,
        }
    );
    ASSERT_TRUE(finalStateTask.valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    const Graphics::GpuTaskGraphTaskView finalStateView = declarations.taskAt(finalStateTask.index);

    ASSERT_EQ(finalStateView.resourceUseCount, 2u);
    ASSERT_NE(finalStateView.resourceUses, nullptr);
    EXPECT_EQ(finalStateView.resourceUses[0u].resource, destinationResource);
    EXPECT_EQ(finalStateView.resourceUses[0u].requiredState, Graphics::ResourceStates::CopyDest);
    EXPECT_EQ(finalStateView.resourceUses[0u].access, Graphics::GpuTaskResourceAccess::Write);
    EXPECT_EQ(finalStateView.resourceUses[1u].resource, destinationResource);
    EXPECT_EQ(finalStateView.resourceUses[1u].requiredState, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(finalStateView.resourceUses[1u].access, Graphics::GpuTaskResourceAccess::Write);
}

TEST(GpuTaskGraph, RejectsRetainedInitialStateMismatchesForBufferPrimitives){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::Alloc::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const auto createBuffer = [&](const Graphics::BufferDesc& sourceDescription){
        Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
            testArena.arena,
            context,
            allocator,
            sourceDescription
        );
        if(!bufferObject)
            return Graphics::BufferHandle{};

        Graphics::BufferHandle buffer(
            bufferObject,
            Graphics::BufferHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        return buffer;
    };

    const Graphics::BufferDesc copySourceDescription = Graphics::BufferDesc()
        .setByteSize(sizeof(u32))
        .setInitialState(Graphics::ResourceStates::CopySource)
        .setKeepInitialState(true)
    ;
    const Graphics::BufferDesc copyDestinationDescription = Graphics::BufferDesc()
        .setByteSize(sizeof(u32))
        .setInitialState(Graphics::ResourceStates::CopyDest)
        .setKeepInitialState(true)
    ;
    const Graphics::BufferDesc commonDescription = Graphics::BufferDesc()
        .setByteSize(sizeof(u32))
        .setInitialState(Graphics::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    Graphics::BufferHandle source = createBuffer(copySourceDescription);
    Graphics::BufferHandle destination = createBuffer(copyDestinationDescription);
    Graphics::BufferHandle mismatchedSource = createBuffer(commonDescription);
    Graphics::BufferHandle mismatchedDestination = createBuffer(commonDescription);
    Graphics::BufferHandle laterSource = createBuffer(copySourceDescription);
    Graphics::BufferHandle laterDestination = createBuffer(copyDestinationDescription);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);
    ASSERT_NE(mismatchedSource.get(), nullptr);
    ASSERT_NE(mismatchedDestination.get(), nullptr);
    ASSERT_NE(laterSource.get(), nullptr);
    ASSERT_NE(laterDestination.get(), nullptr);

    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_buffer_source"))
            .setMarkerLabel("Retained Copy Buffer Source")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    const Graphics::GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_buffer_destination"))
            .setMarkerLabel("Retained Copy Buffer Destination")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    const Graphics::GpuGraphResourceId mismatchedSourceResource = graph.importBuffer(
        mismatchedSource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_buffer_later_source"))
            .setMarkerLabel("Retained Copy Buffer Later Source")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    const Graphics::GpuGraphResourceId mismatchedDestinationResource = graph.importBuffer(
        mismatchedDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_buffer_later_destination"))
            .setMarkerLabel("Retained Copy Buffer Later Destination")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());
    ASSERT_TRUE(mismatchedSourceResource.valid());
    ASSERT_TRUE(mismatchedDestinationResource.valid());
    const Graphics::GpuGraphResourceId laterSourceResource = graph.importBuffer(
        laterSource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_buffer_valid_later_source"))
            .setMarkerLabel("Retained Copy Buffer Valid Later Source")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    const Graphics::GpuGraphResourceId laterDestinationResource = graph.importBuffer(
        laterDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_buffer_valid_later_destination"))
            .setMarkerLabel("Retained Copy Buffer Valid Later Destination")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(laterSourceResource.valid());
    ASSERT_TRUE(laterDestinationResource.valid());

    Graphics::GpuTaskDesc desc;
    desc.setQueue(Graphics::GpuQueueRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    });
    const Graphics::GpuCopyBufferTaskRegion badSourceRegions[]{
        Graphics::GpuCopyBufferTaskRegion{
            .source = sourceResource,
            .sourceOffsetBytes = 0u,
            .destination = destinationResource,
            .destinationOffsetBytes = 0u,
            .dataSizeBytes = sizeof(u32),
        },
        Graphics::GpuCopyBufferTaskRegion{
            .source = mismatchedSourceResource,
            .sourceOffsetBytes = 0u,
            .destination = destinationResource,
            .destinationOffsetBytes = 0u,
            .dataSizeBytes = sizeof(u32),
        },
    };
    desc
        .setIdentity(Name("tests/task_graph/retained_copy_buffer_bad_source"))
        .setMarkerLabel("Retained Copy Buffer Bad Source")
    ;
    EXPECT_FALSE(graph.addCopyBufferTask(
        desc,
        Graphics::GpuCopyBufferTaskDesc{
            .regions = badSourceRegions,
            .regionCount = LengthOf(badSourceRegions),
        }
    ).valid());

    const Graphics::GpuCopyBufferTaskRegion badDestinationRegions[]{
        Graphics::GpuCopyBufferTaskRegion{
            .source = sourceResource,
            .sourceOffsetBytes = 0u,
            .destination = destinationResource,
            .destinationOffsetBytes = 0u,
            .dataSizeBytes = sizeof(u32),
        },
        Graphics::GpuCopyBufferTaskRegion{
            .source = sourceResource,
            .sourceOffsetBytes = 0u,
            .destination = mismatchedDestinationResource,
            .destinationOffsetBytes = 0u,
            .dataSizeBytes = sizeof(u32),
        },
    };
    desc
        .setIdentity(Name("tests/task_graph/retained_copy_buffer_bad_destination"))
        .setMarkerLabel("Retained Copy Buffer Bad Destination")
    ;
    EXPECT_FALSE(graph.addCopyBufferTask(
        desc,
        Graphics::GpuCopyBufferTaskDesc{
            .regions = badDestinationRegions,
            .regionCount = LengthOf(badDestinationRegions),
        }
    ).valid());

    Graphics::GpuClearBufferTaskDesc clearDesc;
    clearDesc.destination = mismatchedDestinationResource;
    desc
        .setIdentity(Name("tests/task_graph/retained_clear_buffer_bad_destination"))
        .setMarkerLabel("Retained Clear Buffer Bad Destination")
    ;
    EXPECT_FALSE(graph.addClearBufferTask(desc, clearDesc).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    // A retained descriptor may start in Common while the graph explicitly declares that same state. The compiler
    // then owns the Common -> CopyDest transition instead of requiring primitive callers to create a native bridge.
    Graphics::GpuTaskGraph transitionedGraph(testArena.arena);
    const Graphics::GpuGraphResourceId transitionedDestination = transitionedGraph.importBuffer(
        mismatchedDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_clear_buffer_graph_transition"))
            .setMarkerLabel("Retained Clear Buffer Graph Transition")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_TRUE(transitionedDestination.valid());
    Graphics::GpuClearBufferTaskDesc transitionedClearDesc;
    transitionedClearDesc.destination = transitionedDestination;
    desc
        .setIdentity(Name("tests/task_graph/retained_clear_buffer_graph_transition"))
        .setMarkerLabel("Retained Clear Buffer Graph Transition")
    ;
    EXPECT_TRUE(transitionedGraph.addClearBufferTask(desc, transitionedClearDesc).valid());

    // Keep-initial-state restoration happens before a graph packet publishes its terminal handoff. Reject the
    // incompatible typed import itself without mutating graph storage or its declaration revision.
    Graphics::GpuTaskGraph externalFinalGraph(testArena.arena);
    u64 externalFinalRevision = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(externalFinalGraph);

        externalFinalRevision = declarations.declarationRevision();
    }
    const Graphics::GpuGraphResourceId externalFinalDestination = externalFinalGraph.importBuffer(
        mismatchedDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_clear_buffer_external_final"))
            .setMarkerLabel("Retained Clear Buffer External Final")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    );
    EXPECT_FALSE(externalFinalDestination.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(externalFinalGraph);

        EXPECT_EQ(declarations.resourceCount(), 0u);
        EXPECT_EQ(declarations.declarationRevision(), externalFinalRevision);
    }

    // An automatic retained resource needs one concrete descriptor state. Unknown cannot be restored by the
    // native tracker or published as a graph initial state, so do not admit a primitive solely because both
    // declarations happen to name Unknown.
    const Graphics::BufferDesc unknownInitialDescription = Graphics::BufferDesc()
        .setByteSize(sizeof(u32))
        .setInitialState(Graphics::ResourceStates::Unknown)
        .setKeepInitialState(true)
    ;
    Graphics::BufferHandle unknownInitialDestination = createBuffer(unknownInitialDescription);
    ASSERT_NE(unknownInitialDestination.get(), nullptr);
    Graphics::GpuTaskGraph unknownInitialGraph(testArena.arena);
    const Graphics::GpuGraphResourceId unknownInitialResource = unknownInitialGraph.importBuffer(
        unknownInitialDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_clear_buffer_unknown_initial"))
            .setMarkerLabel("Retained Clear Buffer Unknown Initial")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Unknown)
    );
    ASSERT_TRUE(unknownInitialResource.valid());
    Graphics::GpuClearBufferTaskDesc unknownInitialClearDesc;
    unknownInitialClearDesc.destination = unknownInitialResource;
    desc
        .setIdentity(Name("tests/task_graph/retained_clear_buffer_unknown_initial"))
        .setMarkerLabel("Retained Clear Buffer Unknown Initial")
    ;
    EXPECT_FALSE(unknownInitialGraph.addClearBufferTask(desc, unknownInitialClearDesc).valid());

    const Graphics::GpuCopyBufferTaskRegion validRegions[]{
        Graphics::GpuCopyBufferTaskRegion{
            .source = sourceResource,
            .sourceOffsetBytes = 0u,
            .destination = destinationResource,
            .destinationOffsetBytes = 0u,
            .dataSizeBytes = sizeof(u32),
        },
        Graphics::GpuCopyBufferTaskRegion{
            .source = laterSourceResource,
            .sourceOffsetBytes = 0u,
            .destination = laterDestinationResource,
            .destinationOffsetBytes = 0u,
            .dataSizeBytes = sizeof(u32),
        },
    };
    desc
        .setIdentity(Name("tests/task_graph/retained_copy_buffer_valid"))
        .setMarkerLabel("Retained Copy Buffer Valid")
    ;
    EXPECT_TRUE(graph.addCopyBufferTask(
        desc,
        Graphics::GpuCopyBufferTaskDesc{
            .regions = validRegions,
            .regionCount = LengthOf(validRegions),
        }
    ).valid());
    desc
        .setIdentity(Name("tests/task_graph/retained_clear_buffer_valid"))
        .setMarkerLabel("Retained Clear Buffer Valid")
    ;
    clearDesc.destination = destinationResource;
    EXPECT_TRUE(graph.addClearBufferTask(desc, clearDesc).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.taskCount(), 2u);
}

TEST(GpuTaskGraph, CopyBufferTaskRequiresTypedBufferImports){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId source = AddBufferMetadata(
        graph,
        Name("tests/task_graph/built_in_buffer_copy_source"),
        "Built-In Buffer Copy Source"
    );
    const Graphics::GpuGraphResourceId destination = AddBufferMetadata(
        graph,
        Name("tests/task_graph/built_in_buffer_copy_destination"),
        "Built-In Buffer Copy Destination"
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());

    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/built_in_buffer_copy"))
        .setMarkerLabel("Built-In Buffer Copy")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const Graphics::GpuCopyBufferTaskRegion region{
        .source = source,
        .destination = destination,
        .dataSizeBytes = sizeof(u32),
    };
    EXPECT_FALSE(graph.addCopyBufferTask(
        desc,
        Graphics::GpuCopyBufferTaskDesc{
            .regions = &region,
            .regionCount = 1u,
        }
    ).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.taskCount(), 0u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

