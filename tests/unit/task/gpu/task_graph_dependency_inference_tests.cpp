// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_dependency_inference_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, ExpandsImmutableResourceSetsIntoConcreteHazards){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId textureA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/resource_set_texture_a"),
        "Resource Set Texture A"
    );
    const Graphics::GpuGraphResourceId textureB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/resource_set_texture_b"),
        "Resource Set Texture B"
    );
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/resource_set_buffer"),
        "Resource Set Buffer"
    );
    ASSERT_TRUE(textureA.valid());
    ASSERT_TRUE(textureB.valid());
    ASSERT_TRUE(buffer.valid());

    const Graphics::GpuGraphResourceId members[] = { textureA, textureB, buffer };
    const Graphics::GpuGraphResourceSetDesc resourceSetDesc = Graphics::GpuGraphResourceSetDesc{}
        .setIdentity(Name("tests/task_graph/visible_resources"))
        .setMarkerLabel("Visible Resources")
        .setMembers(members, LengthOf(members))
    ;
    const Graphics::GpuGraphResourceSetId resourceSet = graph.importResourceSet(resourceSetDesc);
    ASSERT_TRUE(resourceSet.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.resourceSetCount(), 1u);
    }
    EXPECT_EQ(graph.importResourceSet(resourceSetDesc), resourceSet);

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphResourceSetView resourceSetView = declarations.resourceSetAt(resourceSet.index);

        ASSERT_EQ(resourceSetView.id, resourceSet);
        ASSERT_EQ(resourceSetView.memberCount, LengthOf(members));
        for(usize memberIndex = 0u; memberIndex < LengthOf(members); ++memberIndex)
            EXPECT_EQ(resourceSetView.members[memberIndex], members[memberIndex]);
    }

    const Graphics::GpuGraphResourceId duplicateMembers[] = { textureA, textureA };
    EXPECT_FALSE(graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/duplicate_resources"))
            .setMarkerLabel("Duplicate Resources")
            .setMembers(duplicateMembers, LengthOf(duplicateMembers))
    ).valid());

    const Graphics::GpuTaskResourceSetUse writeSetUse{
        .resourceSet = resourceSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceSetUse readSetUse{
        .resourceSet = resourceSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId writer = AddTask(
        graph,
        Name("tests/task_graph/resource_set_writer"),
        "Resource Set Writer",
        nullptr,
        0u,
        nullptr,
        0u,
        &writeSetUse,
        1u
    );
    const Graphics::GpuTaskId reader = AddTask(
        graph,
        Name("tests/task_graph/resource_set_reader"),
        "Resource Set Reader",
        nullptr,
        0u,
        nullptr,
        0u,
        &readSetUse,
        1u
    );
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(reader.valid());

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphTaskView writerView = declarations.taskAt(writer.index);

        ASSERT_EQ(writerView.resourceUseCount, LengthOf(members));
        for(usize memberIndex = 0u; memberIndex < LengthOf(members); ++memberIndex){
            EXPECT_EQ(writerView.resourceUses[memberIndex].resource, members[memberIndex]);
            EXPECT_EQ(writerView.resourceUses[memberIndex].requiredState, Graphics::ResourceStates::UnorderedAccess);
            EXPECT_EQ(writerView.resourceUses[memberIndex].access, Graphics::GpuTaskResourceAccess::Write);
        }
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 1u);
    const Graphics::GpuTaskDependencyEdge* const edge = FindEdge(analysis, writer, reader);
    ASSERT_NE(edge, nullptr);
    EXPECT_EQ(edge->hazard, Graphics::GpuTaskHazardType::ReadAfterWrite);
    ASSERT_EQ(analysis.inferredEdges().size(), LengthOf(members));
    for(const Graphics::GpuGraphResourceId member : members){
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            writer,
            reader,
            member,
            Graphics::GpuTaskHazardType::ReadAfterWrite
        ));
    }

    graph.reset();
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_FALSE(declarations.validResourceSet(resourceSet));
    }
    const Graphics::GpuTaskResourceSetUse staleSetUse{
        .resourceSet = resourceSet,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    EXPECT_FALSE(AddTask(
        graph,
        Name("tests/task_graph/stale_resource_set"),
        "Stale Resource Set",
        nullptr,
        0u,
        nullptr,
        0u,
        &staleSetUse,
        1u
    ).valid());
}

TEST(GpuTaskGraph, RejectsMalformedBufferRangesAndDetectsTextureOverlap){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuGraphResourceDesc bufferDesc;
    bufferDesc
        .setIdentity(Name("tests/task_graph/range_buffer"))
        .setMarkerLabel("Range Buffer")
        .setType(Graphics::GpuGraphResourceType::Buffer)
    ;
    const Graphics::GpuGraphResourceId buffer = graph.importResource(bufferDesc);
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuTaskResourceRange zeroBufferRange;
    zeroBufferRange.bufferRange = Graphics::BufferRange(0u, 0u);
    const Graphics::GpuTaskResourceUse zeroBufferUse{
        .resource = buffer,
        .range = zeroBufferRange,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/zero_buffer_range"),
        "Zero Buffer Range",
        nullptr,
        0u,
        &zeroBufferUse,
        1u
    ).valid());
    Graphics::GpuTaskGraphAnalysis zeroRangeAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, zeroRangeAnalysis));
    EXPECT_EQ(zeroRangeAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);

    graph.reset();
    const Graphics::GpuGraphResourceId overflowBuffer = graph.importResource(bufferDesc);
    ASSERT_TRUE(overflowBuffer.valid());
    Graphics::GpuTaskResourceRange overflowBufferRange;
    overflowBufferRange.bufferRange = Graphics::BufferRange(Limit<u64>::s_Max - 3u, 4u);
    const Graphics::GpuTaskResourceUse overflowBufferUse{
        .resource = overflowBuffer,
        .range = overflowBufferRange,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/overflow_buffer_range"),
        "Overflow Buffer Range",
        nullptr,
        0u,
        &overflowBufferUse,
        1u
    ).valid());
    Graphics::GpuTaskGraphAnalysis overflowRangeAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, overflowRangeAnalysis));
    EXPECT_EQ(overflowRangeAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);

    graph.reset();
    Graphics::GpuGraphResourceDesc textureDesc;
    textureDesc
        .setIdentity(Name("tests/task_graph/overlap_texture"))
        .setMarkerLabel("Overlap Texture")
        .setType(Graphics::GpuGraphResourceType::Texture)
    ;
    const Graphics::GpuGraphResourceId texture = graph.importResource(textureDesc);
    ASSERT_TRUE(texture.valid());
    Graphics::GpuTaskResourceRange firstTextureRange;
    firstTextureRange.textureSubresources = Graphics::TextureSubresourceSet(0u, 2u, 0u, 1u);
    Graphics::GpuTaskResourceRange secondTextureRange;
    secondTextureRange.textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse firstTextureUse{
        .resource = texture,
        .range = firstTextureRange,
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse secondTextureUse{
        .resource = texture,
        .range = secondTextureRange,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/overlap_texture_writer"),
        "Overlap Texture Writer",
        nullptr,
        0u,
        &firstTextureUse,
        1u
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/overlap_texture_reader"),
        "Overlap Texture Reader",
        nullptr,
        0u,
        &secondTextureUse,
        1u
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    Graphics::GpuTaskGraphAnalysis overlapAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, overlapAnalysis));
    EXPECT_NE(FindEdge(overlapAnalysis, first, second), nullptr);
}

TEST(GpuTaskGraph, InfersRawWarAndWawDependenciesWithoutReadReadEdges){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId rawResource = AddHazardDomain(graph, Name("tests/task_graph/raw"), "RAW");
    const Graphics::GpuGraphResourceId warResource = AddHazardDomain(graph, Name("tests/task_graph/war"), "WAR");
    const Graphics::GpuGraphResourceId wawResource = AddHazardDomain(graph, Name("tests/task_graph/waw"), "WAW");
    const Graphics::GpuGraphResourceId readResource = AddHazardDomain(graph, Name("tests/task_graph/read"), "Read");
    ASSERT_TRUE(rawResource.valid());
    ASSERT_TRUE(warResource.valid());
    ASSERT_TRUE(wawResource.valid());
    ASSERT_TRUE(readResource.valid());

    const Graphics::GpuTaskResourceUse firstUses[] = {
        Graphics::GpuTaskResourceUse{ .resource = rawResource, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        Graphics::GpuTaskResourceUse{ .resource = warResource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        Graphics::GpuTaskResourceUse{ .resource = wawResource, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        Graphics::GpuTaskResourceUse{ .resource = readResource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    const Graphics::GpuTaskResourceUse secondUses[] = {
        Graphics::GpuTaskResourceUse{ .resource = rawResource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        Graphics::GpuTaskResourceUse{ .resource = warResource, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        Graphics::GpuTaskResourceUse{ .resource = wawResource, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        Graphics::GpuTaskResourceUse{ .resource = readResource, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/first"),
        "First",
        nullptr,
        0u,
        firstUses,
        LengthOf(firstUses)
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/second"),
        "Second",
        nullptr,
        0u,
        secondUses,
        LengthOf(secondUses)
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 1u);
    const Graphics::GpuTaskDependencyEdge* const edge = FindEdge(analysis, first, second);
    ASSERT_NE(edge, nullptr);
    // A task pair has one execution edge even when three resource hazards establish it. The task/resource telemetry
    // retains the individual resource uses; the edge preserves the first deterministic reason.
    EXPECT_EQ(edge->hazard, Graphics::GpuTaskHazardType::ReadAfterWrite);
    EXPECT_EQ(edge->resource, rawResource);
    EXPECT_EQ(analysis.inferredEdgeCount(), 1u);
    ASSERT_EQ(analysis.inferredEdges().size(), 3u);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        first,
        second,
        rawResource,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        first,
        second,
        warResource,
        Graphics::GpuTaskHazardType::WriteAfterRead
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        first,
        second,
        wawResource,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
}

TEST(GpuTaskGraph, KeepsTextureSubresourcesAndBufferRangesIndependent){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuGraphResourceDesc textureDesc;
    textureDesc
        .setIdentity(Name("tests/task_graph/texture"))
        .setMarkerLabel("Texture")
        .setType(Graphics::GpuGraphResourceType::Texture)
    ;
    const Graphics::GpuGraphResourceId texture = graph.importResource(textureDesc);
    ASSERT_TRUE(texture.valid());

    Graphics::GpuTaskResourceRange firstTextureRange;
    firstTextureRange.textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    Graphics::GpuTaskResourceRange secondTextureRange;
    secondTextureRange.textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse firstTextureUse{
        .resource = texture,
        .range = firstTextureRange,
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse secondTextureUse{
        .resource = texture,
        .range = secondTextureRange,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    ASSERT_TRUE(AddTask(graph, Name("tests/task_graph/mip_zero"), "Mip Zero", nullptr, 0u, &firstTextureUse, 1u).valid());
    ASSERT_TRUE(AddTask(graph, Name("tests/task_graph/mip_one"), "Mip One", nullptr, 0u, &secondTextureUse, 1u).valid());

    Graphics::GpuTaskGraphAnalysis textureAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, textureAnalysis));
    EXPECT_TRUE(textureAnalysis.edges().empty());

    graph.reset();
    Graphics::GpuGraphResourceDesc bufferDesc;
    bufferDesc
        .setIdentity(Name("tests/task_graph/buffer"))
        .setMarkerLabel("Buffer")
        .setType(Graphics::GpuGraphResourceType::Buffer)
    ;
    const Graphics::GpuGraphResourceId buffer = graph.importResource(bufferDesc);
    ASSERT_TRUE(buffer.valid());
    Graphics::GpuTaskResourceRange firstBufferRange;
    firstBufferRange.bufferRange = Graphics::BufferRange(0u, 16u);
    Graphics::GpuTaskResourceRange secondBufferRange;
    secondBufferRange.bufferRange = Graphics::BufferRange(16u, 16u);
    const Graphics::GpuTaskResourceUse firstBufferUse{
        .resource = buffer,
        .range = firstBufferRange,
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse secondBufferUse{
        .resource = buffer,
        .range = secondBufferRange,
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId first = AddTask(graph, Name("tests/task_graph/buffer_first"), "Buffer First", nullptr, 0u, &firstBufferUse, 1u);
    const Graphics::GpuTaskId second = AddTask(graph, Name("tests/task_graph/buffer_second"), "Buffer Second", nullptr, 0u, &secondBufferUse, 1u);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskGraphAnalysis bufferAnalysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, bufferAnalysis));
    EXPECT_EQ(FindEdge(bufferAnalysis, first, second), nullptr);
}

TEST(GpuTaskGraph, DeduplicatesExplicitAndInferredEdgesAndProducesStableOrder){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddHazardDomain(graph, Name("tests/task_graph/dedup_resource"), "Dedup Resource");
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceUse writerUse{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse readerUse{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId first = AddTask(graph, Name("tests/task_graph/ordered_first"), "Ordered First", nullptr, 0u, &writerUse, 1u);
    const Graphics::GpuTaskId independent = AddTask(graph, Name("tests/task_graph/ordered_independent"), "Ordered Independent");
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/ordered_second"),
        "Ordered Second",
        &first,
        1u,
        &readerUse,
        1u
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(independent.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 1u);
    EXPECT_EQ(analysis.explicitEdgeCount(), 1u);
    EXPECT_EQ(analysis.inferredEdgeCount(), 1u);
    ASSERT_EQ(analysis.inferredEdges().size(), 1u);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        first,
        second,
        resource,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0], first);
    EXPECT_EQ(analysis.topologicalOrder()[1], independent);
    EXPECT_EQ(analysis.topologicalOrder()[2], second);
}

TEST(GpuTaskGraph, ReducesSchedulingDagWithoutLosingRawDependencyDiagnostics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId handoff = AddHazardDomain(
        graph,
        Name("tests/task_graph/transitive_reduction_handoff"),
        "Transitive Reduction Handoff"
    );
    ASSERT_TRUE(handoff.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Name mergeDomain("tests/task_graph/transitive_reduction_merge_domain");

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    firstScheduling.frontierScoredMergeDomain = mergeDomain;
    const Graphics::GpuTaskResourceUse firstUse{
        .resource = handoff,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/transitive_reduction_first"))
        .setMarkerLabel("Transitive Reduction First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
        .setResourceUses(&firstUse, 1u)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint secondScheduling;
    secondScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    secondScheduling.frontierScoredMergeDomain = mergeDomain;
    Graphics::GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(Name("tests/task_graph/transitive_reduction_second"))
        .setMarkerLabel("Transitive Reduction Second")
        .setQueue(graphicsRequest)
        .setScheduling(secondScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId second = graph.addTask(secondDesc);
    ASSERT_TRUE(second.valid());

    const Graphics::GpuTaskResourceUse thirdUse{
        .resource = handoff,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc thirdDesc;
    thirdDesc
        .setIdentity(Name("tests/task_graph/transitive_reduction_third"))
        .setMarkerLabel("Transitive Reduction Third")
        .setQueue(computeRequest)
        .setDependencies(&second, 1u)
        .setResourceUses(&thirdUse, 1u)
    ;
    const Graphics::GpuTaskId third = graph.addTask(thirdDesc);
    ASSERT_TRUE(third.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    EXPECT_EQ(analysis.edges()[0u].producer, first);
    EXPECT_EQ(analysis.edges()[0u].consumer, second);
    EXPECT_EQ(analysis.edges()[1u].producer, second);
    EXPECT_EQ(analysis.edges()[1u].consumer, third);
    EXPECT_EQ(analysis.edges()[2u].producer, first);
    EXPECT_EQ(analysis.edges()[2u].consumer, third);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);
    EXPECT_EQ(analysis.schedulingEdges()[0u].producer, first);
    EXPECT_EQ(analysis.schedulingEdges()[0u].consumer, second);
    EXPECT_EQ(analysis.schedulingEdges()[1u].producer, second);
    EXPECT_EQ(analysis.schedulingEdges()[1u].consumer, third);
    ASSERT_EQ(analysis.inferredEdges().size(), 1u);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        first,
        third,
        handoff,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis separateAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments separateAssignments(testArena.arena);
    Graphics::GpuCompiledGraph separateGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, separateAnalysis, topology, separateAssignments, separateGraph));
    {
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(separateGraph);
        ASSERT_EQ(compiledPlan.packetCount(), 3u);
        const Graphics::GpuSubmissionPacketId separateFirstPacket = compiledPlan.packetForTask(first);
        const Graphics::GpuSubmissionPacketId separateSecondPacket = compiledPlan.packetForTask(second);
        const Graphics::GpuSubmissionPacketId separateThirdPacket = compiledPlan.packetForTask(third);
        ASSERT_TRUE(separateFirstPacket.valid());
        ASSERT_TRUE(separateSecondPacket.valid());
        ASSERT_TRUE(separateThirdPacket.valid());
        EXPECT_EQ(compiledPlan.packet(separateFirstPacket).plan->dependencyCount, 0u);
        ASSERT_EQ(compiledPlan.packet(separateSecondPacket).plan->dependencyCount, 1u);
        EXPECT_EQ(compiledPlan.packet(separateSecondPacket).dependencies[0u].producer, separateFirstPacket);
        ASSERT_EQ(compiledPlan.packet(separateThirdPacket).plan->dependencyCount, 1u);
        EXPECT_EQ(compiledPlan.packet(separateThirdPacket).dependencies[0u].producer, separateSecondPacket);
    }

    Graphics::GpuTaskGraphCompileOptions scoredOptions;
    scoredOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis scoredAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments scoredAssignments(testArena.arena);
    Graphics::GpuCompiledGraph scoredGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, scoredAnalysis, topology, scoredAssignments, scoredGraph, scoredOptions));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(scoredGraph);
    ASSERT_EQ(compiledPlan.packetCount(), 2u);
    const Graphics::GpuSubmissionPacketId scoredFirstPacket = compiledPlan.packetForTask(first);
    const Graphics::GpuSubmissionPacketId scoredSecondPacket = compiledPlan.packetForTask(second);
    const Graphics::GpuSubmissionPacketId scoredThirdPacket = compiledPlan.packetForTask(third);
    ASSERT_TRUE(scoredFirstPacket.valid());
    ASSERT_TRUE(scoredThirdPacket.valid());
    EXPECT_EQ(scoredFirstPacket, scoredSecondPacket);
    EXPECT_NE(scoredFirstPacket, scoredThirdPacket);
    EXPECT_EQ(
        compiledPlan.packetizationDecisionForTask(second),
        Graphics::GpuTaskPacketizationDecision::MergedFrontierScored
    );
    ASSERT_EQ(compiledPlan.packet(scoredThirdPacket).plan->dependencyCount, 1u);
    EXPECT_EQ(compiledPlan.packet(scoredThirdPacket).dependencies[0u].producer, scoredFirstPacket);
}

TEST(GpuTaskGraph, ReducesDenseLayeredDagAndPreservesStableTopologicalOrder){
    constexpr usize s_LayerCount = 3u;
    constexpr usize s_LayerWidth = 16u;
    constexpr usize s_TaskCount = s_LayerCount * s_LayerWidth;
    constexpr usize s_RawEdgeCount = 3u * s_LayerWidth * s_LayerWidth;
    constexpr usize s_SchedulingEdgeCount = 2u * s_LayerWidth * s_LayerWidth;

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Name taskBaseName("tests/task_graph/dense_layered_task_");
    Graphics::GpuTaskId tasks[s_TaskCount] = {};
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        char taskIndexBuffer[32u] = {};
        const usize layerIndex = taskIndex / s_LayerWidth;
        const usize dependencyCount = layerIndex * s_LayerWidth;
        tasks[taskIndex] = AddTask(
            graph,
            DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer)),
            "Dense Layered DAG Task",
            tasks,
            dependencyCount
        );
        ASSERT_TRUE(tasks[taskIndex].valid());
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_TRUE(analysis.validFor(declarations));
    }
    ASSERT_EQ(analysis.edges().size(), s_RawEdgeCount);
    EXPECT_EQ(analysis.explicitEdgeCount(), s_RawEdgeCount);
    EXPECT_EQ(analysis.inferredEdgeCount(), 0u);
    EXPECT_TRUE(analysis.inferredEdges().empty());
    ASSERT_EQ(analysis.topologicalOrder().size(), s_TaskCount);
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex)
        EXPECT_EQ(analysis.topologicalOrder()[taskIndex], tasks[taskIndex]);

    ASSERT_EQ(analysis.schedulingEdges().size(), s_SchedulingEdgeCount);
    usize schedulingEdgeIndex = 0u;
    for(usize destinationLayer = 1u; destinationLayer < s_LayerCount; ++destinationLayer){
        for(usize consumerOffset = 0u; consumerOffset < s_LayerWidth; ++consumerOffset){
            for(usize producerOffset = 0u; producerOffset < s_LayerWidth; ++producerOffset){
                const Graphics::GpuTaskDependencyEdge& edge = analysis.schedulingEdges()[schedulingEdgeIndex++];
                EXPECT_EQ(edge.producer, tasks[(destinationLayer - 1u) * s_LayerWidth + producerOffset]);
                EXPECT_EQ(edge.consumer, tasks[destinationLayer * s_LayerWidth + consumerOffset]);
                EXPECT_EQ(edge.hazard, Graphics::GpuTaskHazardType::Explicit);
                EXPECT_FALSE(edge.resource.valid());
                EXPECT_FALSE(edge.resourceVersion.valid());
            }
        }
    }
    EXPECT_EQ(schedulingEdgeIndex, s_SchedulingEdgeCount);

    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        const usize layerIndex = taskIndex / s_LayerWidth;
        const Graphics::GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(tasks[taskIndex]);
        const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(tasks[taskIndex]);
        const usize expectedConsumerCount = layerIndex + 1u < s_LayerCount ? s_LayerWidth : 0u;
        const usize expectedProducerCount = layerIndex == 0u ? 0u : s_LayerWidth;
        ASSERT_EQ(consumers.taskCount, expectedConsumerCount);
        ASSERT_EQ(producers.taskCount, expectedProducerCount);
        for(usize endpointOffset = 0u; endpointOffset < consumers.taskCount; ++endpointOffset){
            EXPECT_EQ(
                consumers[endpointOffset],
                tasks[(layerIndex + 1u) * s_LayerWidth + endpointOffset].index
            );
        }
        for(usize endpointOffset = 0u; endpointOffset < producers.taskCount; ++endpointOffset){
            EXPECT_EQ(
                producers[endpointOffset],
                tasks[(layerIndex - 1u) * s_LayerWidth + endpointOffset].index
            );
        }
    }
}

TEST(GpuTaskGraph, SchedulingAdjacencyRejectsStaleIdsAndClearsOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/scheduling_adjacency_first"),
        "Scheduling Adjacency First"
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/scheduling_adjacency_second"),
        "Scheduling Adjacency Second",
        &first,
        1u
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(first);
    const Graphics::GpuTaskGraphSchedulingTaskIndexView producers = analysis.schedulingProducers(second);
    ASSERT_EQ(consumers.taskCount, 1u);
    ASSERT_EQ(producers.taskCount, 1u);
    EXPECT_EQ(consumers[0u], second.index);
    EXPECT_EQ(producers[0u], first.index);

    u32 declaredTaskCount = 0u;
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        declaredTaskCount = static_cast<u32>(declarations.taskCount());
        graphGeneration = declarations.generation();
    }
    const Graphics::GpuTaskId outOfRangeTask{
        declaredTaskCount,
        graphGeneration,
    };
    const Graphics::GpuTaskId staleTask{ first.index, graphGeneration + 1u };
    Graphics::GpuTaskGraph foreignGraph(testArena.arena);
    const Graphics::GpuTaskId foreignTask = AddTask(
        foreignGraph,
        Name("tests/task_graph/scheduling_adjacency_foreign"),
        "Scheduling Adjacency Foreign"
    );
    ASSERT_TRUE(foreignTask.valid());
    EXPECT_TRUE(analysis.schedulingConsumers({}).empty());
    EXPECT_TRUE(analysis.schedulingConsumers(outOfRangeTask).empty());
    EXPECT_TRUE(analysis.schedulingConsumers(staleTask).empty());
    EXPECT_TRUE(analysis.schedulingConsumers(foreignTask).empty());
    EXPECT_TRUE(analysis.schedulingProducers({}).empty());
    EXPECT_TRUE(analysis.schedulingProducers(outOfRangeTask).empty());
    EXPECT_TRUE(analysis.schedulingProducers(staleTask).empty());
    EXPECT_TRUE(analysis.schedulingProducers(foreignTask).empty());

    analysis.reset();
    EXPECT_FALSE(analysis.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_FALSE(analysis.validFor(declarations));
    }
    EXPECT_TRUE(analysis.schedulingConsumers(first).empty());
    EXPECT_TRUE(analysis.schedulingProducers(second).empty());
}

TEST(GpuTaskGraph, PackedSchedulingReachabilityPreservesStrictClosureAcrossWordBoundaries){
    constexpr usize s_MaxTaskCount = 129u;
    const usize taskCounts[] = { 63u, 64u, 65u, 66u, 127u, 128u, 129u };

    for(const usize taskCount : taskCounts){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskId tasks[s_MaxTaskCount] = {};
        const Name taskBaseName("tests/task_graph/packed_reachability_task_");
        char taskIndexBuffer[32u] = {};
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const Graphics::GpuTaskId* const dependency = taskIndex >= 2u ? &tasks[taskIndex - 2u] : nullptr;
            tasks[taskIndex] = AddTask(
                graph,
                DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer)),
                "Packed Reachability Task",
                dependency,
                dependency ? 1u : 0u
            );
            ASSERT_TRUE(tasks[taskIndex].valid());
            EXPECT_EQ(tasks[taskIndex].index, taskIndex);
        }

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        Core::Alloc::ScratchArena reachabilityScratchArena(Name("tests/graphics/task_graph_packed_reachability_scratch"));
        Graphics::GpuTaskGraphCompilerDetail::GpuTaskSchedulingReachability reachability(reachabilityScratchArena);
        u64 graphGeneration = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

            ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(
                declarations,
                analysis,
                reachability
            ));
            graphGeneration = declarations.generation();
        }

        const usize lastEvenTask = (taskCount & 1u) != 0u ? taskCount - 1u : taskCount - 2u;
        const usize lastOddTask = (taskCount & 1u) != 0u ? taskCount - 2u : taskCount - 1u;
        EXPECT_TRUE(reachability.reaches(tasks[0u], tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.reaches(tasks[lastEvenTask], tasks[0u]));
        EXPECT_TRUE(reachability.reaches(tasks[1u], tasks[lastOddTask]));
        EXPECT_FALSE(reachability.reaches(tasks[lastOddTask], tasks[1u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[1u], tasks[lastOddTask]));
        EXPECT_TRUE(reachability.transitivelyIndependent(tasks[0u], tasks[lastOddTask]));
        EXPECT_TRUE(reachability.transitivelyIndependent(tasks[1u], tasks[lastEvenTask]));

        const usize boundaryProducer = taskCount - 3u;
        const usize boundaryConsumer = taskCount - 1u;
        EXPECT_TRUE(reachability.reaches(tasks[boundaryProducer], tasks[boundaryConsumer]));
        EXPECT_FALSE(reachability.reaches(tasks[boundaryConsumer], tasks[boundaryProducer]));
        EXPECT_TRUE(reachability.transitivelyIndependent(tasks[taskCount - 2u], tasks[boundaryConsumer]));
        if(taskCount >= 127u){
            const usize secondWordProducer = taskCount - 63u;
            EXPECT_TRUE(reachability.reaches(tasks[secondWordProducer], tasks[boundaryConsumer]));
            EXPECT_FALSE(reachability.reaches(tasks[boundaryConsumer], tasks[secondWordProducer]));
            EXPECT_TRUE(reachability.transitivelyIndependent(tasks[secondWordProducer - 1u], tasks[boundaryConsumer]));
        }

        const Graphics::GpuTaskId staleTask{ tasks[0u].index, graphGeneration + 1u };
        const Graphics::GpuTaskId outOfRangeTask{ static_cast<u32>(taskCount), graphGeneration };
        EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[0u]));
        EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[0u]));
        EXPECT_FALSE(reachability.reaches(staleTask, tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(staleTask, tasks[lastOddTask]));
        EXPECT_FALSE(reachability.reaches(outOfRangeTask, tasks[lastEvenTask]));
        EXPECT_FALSE(reachability.transitivelyIndependent(outOfRangeTask, tasks[lastOddTask]));

        if(taskCount == s_MaxTaskCount){
            Graphics::GpuTaskGraph emptyGraph(testArena.arena);
            Graphics::GpuTaskGraphAnalysis emptyAnalysis(testArena.arena);
            ASSERT_TRUE(Analyze(emptyGraph, emptyAnalysis));
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(emptyGraph);

            ASSERT_TRUE(Graphics::GpuTaskGraphCompilerDetail::BuildGpuTaskSchedulingReachability(
                declarations,
                emptyAnalysis,
                reachability
            ));
            EXPECT_FALSE(reachability.reaches(tasks[0u], tasks[lastEvenTask]));
            EXPECT_FALSE(reachability.transitivelyIndependent(tasks[0u], tasks[lastOddTask]));
        }
    }
}

TEST(GpuTaskGraph, TracksOnlyTheNearestWholeResourceWriters){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddHazardDomain(
        graph,
        Name("tests/task_graph/nearest_writer_resource"),
        "Nearest Writer Resource"
    );
    ASSERT_TRUE(resource.valid());
    const Graphics::GpuTaskResourceUse writerUse{
        .resource = resource,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/nearest_writer_first"),
        "Nearest Writer First",
        nullptr,
        0u,
        &writerUse,
        1u
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/nearest_writer_second"),
        "Nearest Writer Second",
        nullptr,
        0u,
        &writerUse,
        1u
    );
    const Graphics::GpuTaskId third = AddTask(
        graph,
        Name("tests/task_graph/nearest_writer_third"),
        "Nearest Writer Third",
        nullptr,
        0u,
        &writerUse,
        1u
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_NE(FindEdge(analysis, first, second), nullptr);
    EXPECT_NE(FindEdge(analysis, second, third), nullptr);
    EXPECT_EQ(FindEdge(analysis, first, third), nullptr);
    ASSERT_EQ(analysis.inferredEdges().size(), 2u);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        first,
        second,
        resource,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        second,
        third,
        resource,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
}

TEST(GpuTaskGraph, UsesTheFullExplicitOrderToOrientInferredHazards){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId firstResource = AddHazardDomain(
        graph,
        Name("tests/task_graph/explicit_first_resource"),
        "Explicit First Resource"
    );
    const Graphics::GpuGraphResourceId secondResource = AddHazardDomain(
        graph,
        Name("tests/task_graph/explicit_second_resource"),
        "Explicit Second Resource"
    );
    ASSERT_TRUE(firstResource.valid());
    ASSERT_TRUE(secondResource.valid());

    const Graphics::GpuTaskResourceUse firstUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = firstResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse secondUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = firstResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = secondResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse thirdUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = secondResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    // The explicit third -> first edge has to order both inferred resource hazards. Pair-local declaration order
    // would instead infer first -> second -> third and invent a cycle with this valid explicit edge.
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        graphGeneration = declarations.generation();
    }
    const Graphics::GpuTaskId futureThird{ 2u, graphGeneration };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/explicit_first"),
        "Explicit First",
        &futureThird,
        1u,
        firstUses,
        LengthOf(firstUses)
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/explicit_second"),
        "Explicit Second",
        nullptr,
        0u,
        secondUses,
        LengthOf(secondUses)
    );
    const Graphics::GpuTaskId third = AddTask(
        graph,
        Name("tests/task_graph/explicit_third"),
        "Explicit Third",
        nullptr,
        0u,
        thirdUses,
        LengthOf(thirdUses)
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    EXPECT_NE(FindEdge(analysis, third, first), nullptr);
    EXPECT_NE(FindEdge(analysis, second, first), nullptr);
    EXPECT_NE(FindEdge(analysis, second, third), nullptr);
    ASSERT_EQ(analysis.topologicalOrder().size(), 3u);
    EXPECT_EQ(analysis.topologicalOrder()[0], second);
    EXPECT_EQ(analysis.topologicalOrder()[1], third);
    EXPECT_EQ(analysis.topologicalOrder()[2], first);

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 3u);

    const Graphics::GpuTaskId topologicalTasks[] = { second, third, first };
    for(usize taskIndex = 0u; taskIndex < LengthOf(topologicalTasks); ++taskIndex){
        const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetIdAt(taskIndex);
        ASSERT_TRUE(packet.valid());
        ASSERT_EQ(compiledPlan.packet(packet).plan->taskCount, 1u);
        ASSERT_NE(compiledPlan.packet(packet).tasks, nullptr);
        EXPECT_EQ(compiledPlan.packet(packet).tasks[0u], topologicalTasks[taskIndex]);
    }

    const Graphics::GpuTaskId lookupOrder[] = { first, second, third, third, second, first };
    for(const Graphics::GpuTaskId task : lookupOrder){
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->task, task);
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        ASSERT_NE(compiledTask, nullptr);
        EXPECT_EQ(compiledTask->task, task);
        EXPECT_EQ(compiledPlan.packetForTask(task), compiledTask->packet);
        ASSERT_NE(compiledPlan.packet(compiledTask->packet).tasks, nullptr);
        EXPECT_EQ(compiledPlan.packet(compiledTask->packet).tasks[0u], task);
    }
}

TEST(GpuTaskGraph, RejectsExplicitCyclesAndExportsExternalMetadata){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Graphics::GpuTaskId futureSecond{ 1u, graphGeneration };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/cycle_first"),
        "Cycle First",
        &futureSecond,
        1u
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/cycle_second"),
        "Cycle Second",
        &first,
        1u
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    Graphics::GpuTaskGraphAnalysis cycleAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, cycleAnalysis));
    EXPECT_EQ(cycleAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    ASSERT_GE(cycleAnalysis.cyclePath().size(), 3u);
    EXPECT_EQ(cycleAnalysis.cyclePath().front(), cycleAnalysis.cyclePath().back());
    ASSERT_EQ(cycleAnalysis.cycleEdges().size(), cycleAnalysis.cyclePath().size() - 1u);
    EXPECT_EQ(cycleAnalysis.cycleEdges()[0].hazard, Graphics::GpuTaskHazardType::Explicit);
    EXPECT_EQ(cycleAnalysis.diagnostic().task, cycleAnalysis.cycleEdges()[0].consumer);
    EXPECT_EQ(cycleAnalysis.diagnostic().relatedTask, cycleAnalysis.cycleEdges()[0].producer);

    graph.reset();
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/external"))
            .setMarkerLabel("Prior Frame")
    );
    ASSERT_TRUE(completion.valid());
    const Graphics::GpuTaskId task = [&](){
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(Name("tests/task_graph/external_consumer"))
            .setMarkerLabel("External Consumer")
            .setExternalDependencies(&completion, 1u)
        ;
        return graph.addTask(desc);
    }();
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.externalDependencies().size(), 1u);

    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena telemetryScratchArena(s_TaskGraphScratchArena);
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_TRUE(declarations.appendFrameGraphTelemetry(builder, analysis, telemetryScratchArena));
    }
    ASSERT_EQ(nodes.size(), 2u);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].kind, Telemetry::FrameGraphEdgeKind::DependsOn);

    EXPECT_TRUE(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/late_external"))
            .setMarkerLabel("Late External")
    ).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_FALSE(declarations.appendFrameGraphTelemetry(builder, analysis, telemetryScratchArena));
    }
}

TEST(GpuTaskGraph, RejectsDeepExplicitCyclesWithoutCallStackGrowth){
    constexpr usize s_TaskCount = 8192u;

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u64 graphGeneration = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        graphGeneration = declarations.generation();
    }
    const Name taskBaseName("tests/task_graph/deep_cycle_task_");
    char taskIndexBuffer[32u] = {};
    const Graphics::GpuTaskId futureLast{
        static_cast<u32>(s_TaskCount - 1u),
        graphGeneration,
    };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        DeriveName(taskBaseName, FormatDecimal(0u, taskIndexBuffer)),
        "Deep Cycle Task",
        &futureLast,
        1u
    );
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskId previous = first;
    for(usize taskIndex = 1u; taskIndex < s_TaskCount; ++taskIndex){
        const Graphics::GpuTaskId current = AddTask(
            graph,
            DeriveName(taskBaseName, FormatDecimal(taskIndex, taskIndexBuffer)),
            "Deep Cycle Task",
            &previous,
            1u
        );
        ASSERT_TRUE(current.valid());
        previous = current;
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::Cycle);
    EXPECT_TRUE(analysis.topologicalOrder().empty());
    ASSERT_EQ(analysis.cyclePath().size(), s_TaskCount + 1u);
    ASSERT_EQ(analysis.cycleEdges().size(), s_TaskCount);
    for(usize taskIndex = 0u; taskIndex < s_TaskCount; ++taskIndex){
        const Graphics::GpuTaskId expectedTask{ static_cast<u32>(taskIndex), graphGeneration };
        EXPECT_EQ(analysis.cyclePath()[taskIndex], expectedTask);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].producer, analysis.cyclePath()[taskIndex]);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].consumer, analysis.cyclePath()[taskIndex + 1u]);
        EXPECT_EQ(analysis.cycleEdges()[taskIndex].hazard, Graphics::GpuTaskHazardType::Explicit);
        EXPECT_FALSE(analysis.cycleEdges()[taskIndex].resource.valid());
        EXPECT_FALSE(analysis.cycleEdges()[taskIndex].resourceVersion.valid());
    }
    EXPECT_EQ(analysis.cyclePath().back(), first);
    EXPECT_EQ(analysis.diagnostic().task, analysis.cycleEdges()[0u].consumer);
    EXPECT_EQ(analysis.diagnostic().relatedTask, analysis.cycleEdges()[0u].producer);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

