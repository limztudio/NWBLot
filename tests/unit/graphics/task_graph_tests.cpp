// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/task_graph/compiler.h>
#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphTestsTag>;
namespace Graphics = Core;
namespace Telemetry = Core::Telemetry;

inline constexpr Name s_TaskGraphScratchArena("tests/graphics/task_graph_scratch");


[[nodiscard]] Graphics::GpuGraphResourceId AddHazardDomain(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label
){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::HazardDomain)
    ;
    return graph.importHazardDomain(desc);
}

[[nodiscard]] Graphics::GpuTaskId AddTask(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuTaskId* const dependencies = nullptr,
    const usize dependencyCount = 0u,
    const Graphics::GpuTaskResourceUse* const resourceUses = nullptr,
    const usize resourceUseCount = 0u
){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setDependencies(dependencies, dependencyCount)
        .setResourceUses(resourceUses, resourceUseCount)
    ;
    return graph.addTask(desc);
}

[[nodiscard]] Graphics::GpuTaskId AddTaskWithQueue(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuQueueRequest& queue,
    const Graphics::GpuTaskSchedulingHint& scheduling = {}
){
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setQueue(queue)
        .setScheduling(scheduling)
    ;
    return graph.addTask(desc);
}

[[nodiscard]] bool Analyze(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis
){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.analyze(graph, analysis, scratchArena);
}

[[nodiscard]] bool Assign(
    const Graphics::GpuTaskGraph& graph,
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskGraphQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments
){
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.assignQueues(graph, analysis, topology, assignments);
}

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

[[nodiscard]] Graphics::GpuPhysicalQueueInfo GraphicsQueue(
    const u16 index = 0u,
    const Graphics::GpuQueueCapability::Mask capabilities = QueueCapabilities(
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueueCapability::Transfer
    )
){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ index, 1u },
        .queueClass = Graphics::CommandQueue::Graphics,
        .capabilities = capabilities,
        .familyIndex = 0u,
        .queueIndex = 0u,
        .dedicated = false,
    };
}

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedComputeQueue(const u16 index = 1u){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ index, 1u },
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

[[nodiscard]] const Graphics::GpuTaskDependencyEdge* FindEdge(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskId producer,
    const Graphics::GpuTaskId consumer
){
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.edges()){
        if(edge.producer == producer && edge.consumer == consumer)
            return &edge;
    }
    return nullptr;
}

[[nodiscard]] bool HasInferredHazard(
    const Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskId producer,
    const Graphics::GpuTaskId consumer,
    const Graphics::GpuGraphResourceId resource,
    const Graphics::GpuTaskHazardType::Enum hazard
){
    for(const Graphics::GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(
            edge.producer == producer
            && edge.consumer == consumer
            && edge.resource == resource
            && edge.hazard == hazard
        )
            return true;
    }
    return false;
}

struct PayloadDestroyTask{
    struct Payload{
        u32* destructionCount = nullptr;

        explicit Payload(u32* const value)
            : destructionCount(value)
        {}
        Payload(Payload&& other)noexcept
            : destructionCount(other.destructionCount)
        {
            other.destructionCount = nullptr;
        }
        Payload(const Payload&) = delete;
        ~Payload(){
            if(destructionCount)
                ++*destructionCount;
        }
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, CopiesCallerMetadataAndDestroysTypedPayloadOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddHazardDomain(graph, Name("tests/task_graph/resource"), "Resource");
    ASSERT_TRUE(resource.valid());

    const Graphics::GpuTaskId predecessor = AddTask(graph, Name("tests/task_graph/predecessor"), "Predecessor");
    ASSERT_TRUE(predecessor.valid());

    Graphics::GpuTaskId dependencies[] = { predecessor };
    Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    char markerLabel[] = "Stack Marker";
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/payload"))
        .setMarkerLabel(AStringView(markerLabel))
        .setDependencies(dependencies, LengthOf(dependencies))
        .setResourceUses(uses, LengthOf(uses))
    ;

    u32 destructionCount = 0u;
    PayloadDestroyTask::Payload payload(&destructionCount);
    const Graphics::GpuTaskId task = graph.addTask<PayloadDestroyTask>(desc, Move(payload));
    ASSERT_TRUE(task.valid());

    dependencies[0] = {};
    uses[0].resource = {};
    markerLabel[0] = 'X';
    const Graphics::GpuTaskGraphTaskView stored = graph.taskAt(task.index);
    ASSERT_EQ(stored.dependencyCount, 1u);
    ASSERT_EQ(stored.resourceUseCount, 1u);
    EXPECT_EQ(stored.dependencies[0], predecessor);
    EXPECT_EQ(stored.resourceUses[0].resource, resource);
    EXPECT_EQ(stored.markerLabel, AStringView("Stack Marker"));
    EXPECT_TRUE(stored.hasPayload);

    graph.reset();
    EXPECT_EQ(destructionCount, 1u);
    EXPECT_FALSE(graph.validTask(task));
    EXPECT_FALSE(graph.validResource(resource));
}

TEST(GpuTaskGraph, RejectsStaleDependencyHandlesDuringAnalysis){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId staleTask = AddTask(graph, Name("tests/task_graph/old"), "Old");
    ASSERT_TRUE(staleTask.valid());

    graph.reset();
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/new"),
        "New",
        &staleTask,
        1u
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, analysis));
    EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidTaskDependency);
    EXPECT_EQ(analysis.diagnostic().task, task);
    EXPECT_EQ(analysis.diagnostic().relatedTask, staleTask);
}

TEST(GpuTaskGraph, RejectsEmptyLabelsAndStaleResourceAndExternalHandles){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc unlabeledResource;
    unlabeledResource
        .setIdentity(Name("tests/task_graph/unlabeled_resource"))
        .setType(Graphics::GpuGraphResourceType::HazardDomain)
    ;
    EXPECT_FALSE(graph.importHazardDomain(unlabeledResource).valid());
    EXPECT_FALSE(AddTask(graph, Name("tests/task_graph/unlabeled_task"), "").valid());
    EXPECT_FALSE(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/unlabeled_external"))
    ).valid());

    const Graphics::GpuGraphResourceId staleResource = AddHazardDomain(
        graph,
        Name("tests/task_graph/stale_resource"),
        "Stale Resource"
    );
    const Graphics::GpuExternalCompletionId staleExternal = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/stale_external"))
            .setMarkerLabel("Stale External")
    );
    ASSERT_TRUE(staleResource.valid());
    ASSERT_TRUE(staleExternal.valid());

    graph.reset();
    const Graphics::GpuTaskResourceUse staleResourceUse{
        .resource = staleResource,
        .range = {},
        .requiredState = Graphics::ResourceStates::ShaderResource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    const Graphics::GpuTaskId resourceTask = AddTask(
        graph,
        Name("tests/task_graph/stale_resource_consumer"),
        "Stale Resource Consumer",
        nullptr,
        0u,
        &staleResourceUse,
        1u
    );
    ASSERT_TRUE(resourceTask.valid());
    Graphics::GpuTaskGraphAnalysis resourceAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, resourceAnalysis));
    EXPECT_EQ(resourceAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
    EXPECT_EQ(resourceAnalysis.diagnostic().task, resourceTask);
    EXPECT_EQ(resourceAnalysis.diagnostic().resource, staleResource);

    graph.reset();
    Graphics::GpuTaskDesc externalDesc;
    externalDesc
        .setIdentity(Name("tests/task_graph/stale_external_consumer"))
        .setMarkerLabel("Stale External Consumer")
        .setExternalDependencies(&staleExternal, 1u)
    ;
    const Graphics::GpuTaskId externalTask = graph.addTask(externalDesc);
    ASSERT_TRUE(externalTask.valid());
    Graphics::GpuTaskGraphAnalysis externalAnalysis(testArena.arena);
    EXPECT_FALSE(Analyze(graph, externalAnalysis));
    EXPECT_EQ(externalAnalysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency);
    EXPECT_EQ(externalAnalysis.diagnostic().task, externalTask);
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

TEST(GpuTaskGraph, KeepsTextureSubresourcesIndependentAndBuffersConservative){
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
    EXPECT_NE(FindEdge(bufferAnalysis, first, second), nullptr);
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

TEST(GpuTaskGraph, AssignsOnlyCompatiblePhysicalQueuesAndFallsBackToGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;
    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    computeRequest.allowFallback = true;
    computeRequest.compilerMayOverridePreference = true;

    const Graphics::GpuTaskId graphicsTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_graphics"),
        "Queue Graphics",
        graphicsRequest
    );
    const Graphics::GpuTaskId computeTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_compute"),
        "Queue Compute",
        computeRequest
    );
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(computeTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        DedicatedComputeQueue(),
        GraphicsQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    ASSERT_TRUE(assignments.validFor(graph));

    const Graphics::GpuTaskQueueAssignment* const graphicsAssignment = assignments.find(graphicsTask);
    const Graphics::GpuTaskQueueAssignment* const computeAssignment = assignments.find(computeTask);
    ASSERT_NE(graphicsAssignment, nullptr);
    ASSERT_NE(computeAssignment, nullptr);
    EXPECT_EQ(graphicsAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(graphicsAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_EQ(computeAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_TRUE(computeAssignment->dedicated);
    EXPECT_EQ(computeAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology graphicsOnlyTopology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    Graphics::GpuTaskGraphQueueAssignments graphicsFallbackAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, graphicsOnlyTopology, graphicsFallbackAssignments));
    const Graphics::GpuTaskQueueAssignment* const fallbackAssignment = graphicsFallbackAssignments.find(computeTask);
    ASSERT_NE(fallbackAssignment, nullptr);
    EXPECT_EQ(fallbackAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(fallbackAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Fallback);
}

TEST(GpuTaskGraph, RetainsTinyAndNonOverlappingComputeTasksOnGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    computeRequest.allowFallback = true;
    computeRequest.compilerMayOverridePreference = true;

    Graphics::GpuTaskSchedulingHint tinyScheduling;
    tinyScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    Graphics::GpuTaskSchedulingHint noOverlapScheduling;
    noOverlapScheduling.overlapPreferred = false;
    Graphics::GpuQueueRequest strictComputeRequest = computeRequest;
    strictComputeRequest.compilerMayOverridePreference = false;

    const Graphics::GpuTaskId tinyTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_tiny"),
        "Queue Tiny",
        computeRequest,
        tinyScheduling
    );
    const Graphics::GpuTaskId noOverlapTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_no_overlap"),
        "Queue No Overlap",
        computeRequest,
        noOverlapScheduling
    );
    const Graphics::GpuTaskId strictTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_strict_compute"),
        "Queue Strict Compute",
        strictComputeRequest,
        tinyScheduling
    );
    ASSERT_TRUE(tinyTask.valid());
    ASSERT_TRUE(noOverlapTask.valid());
    ASSERT_TRUE(strictTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));

    const Graphics::GpuTaskQueueAssignment* const tinyAssignment = assignments.find(tinyTask);
    const Graphics::GpuTaskQueueAssignment* const noOverlapAssignment = assignments.find(noOverlapTask);
    const Graphics::GpuTaskQueueAssignment* const strictAssignment = assignments.find(strictTask);
    ASSERT_NE(tinyAssignment, nullptr);
    ASSERT_NE(noOverlapAssignment, nullptr);
    ASSERT_NE(strictAssignment, nullptr);
    EXPECT_EQ(tinyAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(noOverlapAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(strictAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(strictAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
}

TEST(GpuTaskGraph, RejectsInvalidAndIncompatibleQueueTopologiesDeterministically){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_diagnostic"),
        "Queue Diagnostic",
        computeRequest
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    EXPECT_FALSE(Assign(graph, analysis, Graphics::GpuTaskGraphQueueTopology{}, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = {
        GraphicsQueue(0u, Graphics::GpuQueueCapability::Graphics),
    };
    const Graphics::GpuTaskGraphQueueTopology graphicsOnlyTopology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    EXPECT_FALSE(Assign(graph, analysis, graphicsOnlyTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue
    );
    EXPECT_EQ(assignments.diagnostic().task, task);
    EXPECT_EQ(assignments.diagnostic().requiredCapabilities, Graphics::GpuQueueCapability::Compute);

    const Graphics::GpuPhysicalQueueInfo topologyA[] = { DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueInfo topologyB[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology firstTopology{
        .queues = topologyA,
        .queueCount = LengthOf(topologyA),
    };
    const Graphics::GpuTaskGraphQueueTopology secondTopology{
        .queues = topologyB,
        .queueCount = LengthOf(topologyB),
    };
    Graphics::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, firstTopology, firstAssignments));
    ASSERT_TRUE(Assign(graph, analysis, secondTopology, secondAssignments));
    const Graphics::GpuTaskQueueAssignment* const firstAssignment = firstAssignments.find(task);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = secondAssignments.find(task);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, secondAssignment->queue);
    EXPECT_EQ(firstAssignment->queueClass, secondAssignment->queueClass);
    EXPECT_EQ(firstAssignment->reason, secondAssignment->reason);
}

TEST(GpuTaskGraph, ExportsInferredEvidenceAndLegacyScheduleMismatches){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddHazardDomain(
        graph,
        Name("tests/task_graph/telemetry_resource"),
        "Telemetry Resource"
    );
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
    const Graphics::GpuTaskId writer = AddTask(
        graph,
        Name("tests/task_graph/telemetry_writer"),
        "Telemetry Writer",
        nullptr,
        0u,
        &writerUse,
        1u
    );
    const Graphics::GpuTaskId reader = AddTask(
        graph,
        Name("tests/task_graph/telemetry_reader"),
        "Telemetry Reader",
        &writer,
        1u,
        &readerUse,
        1u
    );
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(reader.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuTaskDependencyEdge mismatch{
        .producer = writer,
        .consumer = reader,
        .resource = resource,
        .hazard = Graphics::GpuTaskHazardType::ReadAfterWrite,
    };
    Telemetry::FrameGraphNodeDescs nodes(testArena.arena);
    Telemetry::FrameGraphEdgeDescs edges(testArena.arena);
    Telemetry::FrameGraphPendingNameEdges pendingEdges(testArena.arena);
    Telemetry::FrameGraphBuilder builder(nodes, edges, pendingEdges);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    const Graphics::GpuTaskGraphTelemetryOptions telemetryOptions{
        .legacyScheduleMismatches = &mismatch,
        .legacyScheduleMismatchCount = 1u,
        .queueAssignments = &assignments,
        .legacyQueueMismatches = &reader,
        .legacyQueueMismatchCount = 1u,
    };
    ASSERT_TRUE(graph.appendFrameGraphTelemetry(builder, analysis, scratchArena, telemetryOptions));

    const u8 expectedFlags =
        Graphics::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency
        | Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency
        | Graphics::GpuTaskGraphTelemetryEdgeFlag::MissingLegacyScheduleDependency
    ;
    bool foundDependency = false;
    for(const Telemetry::FrameGraphEdgeDesc& edge : edges){
        if(edge.kind != Telemetry::FrameGraphEdgeKind::DependsOn)
            continue;
        foundDependency = true;
        EXPECT_EQ(edge.flags, expectedFlags);
    }
    EXPECT_TRUE(foundDependency);

    ASSERT_EQ(nodes.size(), 3u);
    EXPECT_EQ(
        nodes[1u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
    );
    EXPECT_EQ(
        nodes[2u].flags,
        Graphics::GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue
            | Graphics::GpuTaskGraphTelemetryNodeFlag::LegacyQueueAssignmentMismatch
    );
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
    const Graphics::GpuTaskId futureThird{ 2u, graph.generation() };
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
}

TEST(GpuTaskGraph, RejectsExplicitCyclesAndExportsExternalMetadata){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId futureSecond{ 1u, graph.generation() };
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
    EXPECT_TRUE(graph.appendFrameGraphTelemetry(builder, analysis, telemetryScratchArena));
    ASSERT_EQ(nodes.size(), 2u);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].kind, Telemetry::FrameGraphEdgeKind::DependsOn);

    EXPECT_TRUE(graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/late_external"))
            .setMarkerLabel("Late External")
    ).valid());
    EXPECT_FALSE(graph.appendFrameGraphTelemetry(builder, analysis, telemetryScratchArena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

