// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
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

[[nodiscard]] Graphics::GpuGraphResourceId AddTextureMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common
){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::Texture)
        .setInitialState(initialState)
    ;
    return graph.importResource(desc);
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

[[nodiscard]] bool Compile(
    const Graphics::GpuTaskGraph& graph,
    Graphics::GpuTaskGraphAnalysis& analysis,
    const Graphics::GpuTaskGraphQueueTopology& topology,
    Graphics::GpuTaskGraphQueueAssignments& assignments,
    Graphics::GpuCompiledGraph& compiledGraph
){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena);
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

struct PacketLifecycleTask{
    struct Payload{
        u32* acceptedCount = nullptr;
        u32* discardedCount = nullptr;
        Graphics::QueueSubmissionToken* acceptedToken = nullptr;
    };

    static void accepted(Payload& payload, const Graphics::QueueSubmissionToken& token){
        if(payload.acceptedCount)
            ++*payload.acceptedCount;
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
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

TEST(GpuTaskGraph, CompilesOneTaskPacketsWithDependenciesAndLifecycleBoundaries){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuExternalCompletionId completion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/packet_external"))
            .setMarkerLabel("External Completion")
    );
    ASSERT_TRUE(completion.valid());

    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;
    Graphics::QueueSubmissionToken acceptedToken;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/packet_first"))
        .setMarkerLabel("Packet First")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            true,
            true,
        })
    ;
    const Graphics::GpuTaskId first = graph.addTask<PacketLifecycleTask>(
        firstDesc,
        PacketLifecycleTask::Payload{ &acceptedCount, &discardedCount, &acceptedToken }
    );
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskDesc secondDesc;
    secondDesc
        .setIdentity(Name("tests/task_graph/packet_second"))
        .setMarkerLabel("Packet Second")
        .setDependencies(&first, 1u)
        .setExternalDependencies(&completion, 1u)
    ;
    const Graphics::GpuTaskId second = graph.addTask<PacketLifecycleTask>(
        secondDesc,
        PacketLifecycleTask::Payload{ &acceptedCount, &discardedCount, &acceptedToken }
    );
    ASSERT_TRUE(second.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_TRUE(compiledGraph.validFor(graph));
    ASSERT_EQ(compiledGraph.taskCount(), 2u);
    ASSERT_EQ(compiledGraph.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(first);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledGraph.packetForTask(second);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    EXPECT_NE(firstPacket, secondPacket);
    const auto& secondPacketPlan = compiledGraph.packet(secondPacket);
    ASSERT_EQ(secondPacketPlan.taskCount, 1u);
    ASSERT_EQ(secondPacketPlan.dependencyCount, 1u);
    ASSERT_EQ(secondPacketPlan.externalDependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(secondPacket)[0].producer, firstPacket);
    EXPECT_EQ(compiledGraph.packetExternalDependencies(secondPacket)[0], completion);

    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    transaction.reset(compiledGraph);
    ASSERT_TRUE(transaction.markPacketRecorded(firstPacket));
    const Graphics::QueueSubmissionToken firstToken{ Graphics::CommandQueue::Compute, 41u };
    transaction.acceptPacket(graph, compiledGraph, firstPacket, firstToken);
    EXPECT_EQ(acceptedCount, 1u);
    EXPECT_EQ(discardedCount, 0u);
    EXPECT_EQ(acceptedToken.queue, firstToken.queue);
    EXPECT_EQ(acceptedToken.value, firstToken.value);
    ASSERT_NE(transaction.latestAcceptedToken(compiledGraph.packet(firstPacket).queue), nullptr);
    EXPECT_EQ(transaction.latestAcceptedToken(compiledGraph.packet(firstPacket).queue)->value, firstToken.value);

    // A later packet that never reaches submission must discard only itself; the accepted producer remains visible
    // to recovery and must never receive a rollback callback.
    transaction.discardUnaccepted(graph, compiledGraph);
    EXPECT_EQ(acceptedCount, 1u);
    EXPECT_EQ(discardedCount, 1u);
    ASSERT_NE(transaction.packetRuntime(secondPacket), nullptr);
    EXPECT_EQ(
        transaction.packetRuntime(secondPacket)->state,
        Graphics::GpuPacketRuntimeState::Rejected
    );
}


TEST(GpuTaskGraph, MergesExplicitCompatibleSuccessorIntoOnePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskSchedulingHint prefixScheduling;
    prefixScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/merged_prefix"))
        .setMarkerLabel("Merged Prefix")
        .setScheduling(prefixScheduling)
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    Graphics::GpuTaskSchedulingHint suffixScheduling;
    suffixScheduling.allowPacketMerge = true;
    suffixScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc suffixDesc;
    suffixDesc
        .setIdentity(Name("tests/task_graph/merged_suffix"))
        .setMarkerLabel("Merged Suffix")
        .setScheduling(suffixScheduling)
        .setDependencies(&prefix, 1u)
    ;
    const Graphics::GpuTaskId suffix = graph.addTask(suffixDesc);
    ASSERT_TRUE(suffix.valid());

    Graphics::GpuTaskSchedulingHint finalSuffixScheduling;
    finalSuffixScheduling.allowPacketMerge = true;
    finalSuffixScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc finalSuffixDesc;
    finalSuffixDesc
        .setIdentity(Name("tests/task_graph/merged_final_suffix"))
        .setMarkerLabel("Merged Final Suffix")
        .setScheduling(finalSuffixScheduling)
        .setDependencies(&suffix, 1u)
    ;
    const Graphics::GpuTaskId finalSuffix = graph.addTask(finalSuffixDesc);
    ASSERT_TRUE(finalSuffix.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_TRUE(compiledGraph.validFor(graph));
    ASSERT_EQ(compiledGraph.packetCount(), 1u);

    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId suffixPacket = compiledGraph.packetForTask(suffix);
    const Graphics::GpuSubmissionPacketId finalSuffixPacket = compiledGraph.packetForTask(finalSuffix);
    ASSERT_TRUE(prefixPacket.valid());
    EXPECT_EQ(prefixPacket, suffixPacket);
    EXPECT_EQ(prefixPacket, finalSuffixPacket);
    const Graphics::GpuSubmissionPacket& packet = compiledGraph.packet(prefixPacket);
    ASSERT_EQ(packet.taskCount, 3u);
    ASSERT_NE(compiledGraph.packetTasks(prefixPacket), nullptr);
    EXPECT_EQ(compiledGraph.packetTasks(prefixPacket)[0u], prefix);
    EXPECT_EQ(compiledGraph.packetTasks(prefixPacket)[1u], suffix);
    EXPECT_EQ(compiledGraph.packetTasks(prefixPacket)[2u], finalSuffix);
    EXPECT_EQ(packet.dependencyCount, 0u);
}


TEST(GpuTaskGraph, PlansPacketBoundaryTransitionsAndUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/planned_transitions"),
        "Planned Transitions"
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceUse writeUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse uavReadUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskResourceUse shaderReadUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId writer = AddTask(
        graph,
        Name("tests/task_graph/planned_writer"),
        "Writer",
        nullptr,
        0u,
        writeUse,
        LengthOf(writeUse)
    );
    const Graphics::GpuTaskId uavReader = AddTask(
        graph,
        Name("tests/task_graph/planned_uav_reader"),
        "UAV Reader",
        nullptr,
        0u,
        uavReadUse,
        LengthOf(uavReadUse)
    );
    const Graphics::GpuTaskId shaderReader = AddTask(
        graph,
        Name("tests/task_graph/planned_shader_reader"),
        "Shader Reader",
        nullptr,
        0u,
        shaderReadUse,
        LengthOf(shaderReadUse)
    );
    ASSERT_TRUE(writer.valid());
    ASSERT_TRUE(uavReader.valid());
    ASSERT_TRUE(shaderReader.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuCompiledTask* const compiledWriter = compiledGraph.findTask(writer);
    const Graphics::GpuCompiledTask* const compiledUavReader = compiledGraph.findTask(uavReader);
    const Graphics::GpuCompiledTask* const compiledShaderReader = compiledGraph.findTask(shaderReader);
    ASSERT_NE(compiledWriter, nullptr);
    ASSERT_NE(compiledUavReader, nullptr);
    ASSERT_NE(compiledShaderReader, nullptr);
    ASSERT_EQ(compiledWriter->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledUavReader->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledShaderReader->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledWriter->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledUavReader->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledShaderReader->prologueBarrierCount, 1u);

    const Graphics::GpuCompiledBarrier* const writerBarrier = compiledGraph.taskPrologueBarriers(writer);
    const Graphics::GpuCompiledBarrier* const uavBarrier = compiledGraph.taskPrologueBarriers(uavReader);
    const Graphics::GpuCompiledBarrier* const shaderBarrier = compiledGraph.taskPrologueBarriers(shaderReader);
    const Graphics::GpuPacketStateSeed* const uavSeed = compiledGraph.taskPrologueStateSeeds(uavReader);
    const Graphics::GpuPacketStateSeed* const shaderSeed = compiledGraph.taskPrologueStateSeeds(shaderReader);
    ASSERT_NE(writerBarrier, nullptr);
    ASSERT_NE(uavBarrier, nullptr);
    ASSERT_NE(shaderBarrier, nullptr);
    ASSERT_NE(uavSeed, nullptr);
    ASSERT_NE(shaderSeed, nullptr);
    EXPECT_EQ(uavSeed[0].resource, texture);
    EXPECT_EQ(uavSeed[0].sourcePacket, compiledWriter->packet);
    EXPECT_EQ(shaderSeed[0].resource, texture);
    EXPECT_EQ(shaderSeed[0].sourcePacket, compiledUavReader->packet);
    EXPECT_EQ(writerBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(writerBarrier[0].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(writerBarrier[0].after, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(uavBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureUav);
    EXPECT_EQ(uavBarrier[0].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(uavBarrier[0].after, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(shaderBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(shaderBarrier[0].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(shaderBarrier[0].after, Graphics::ResourceStates::ShaderResource);
}


TEST(GpuTaskGraph, AllowsIndependentConcurrentReadStateSources){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const auto importTexture = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::ResourceQueueSharing::Mask queueSharing,
        const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common
    ){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(initialState)
            .setQueueSharing(queueSharing)
        ;
        return graph.importResource(desc);
    };
    const Graphics::GpuGraphResourceId concurrentTexture = importTexture(
        Name("tests/task_graph/concurrent_read_only"),
        "Concurrent Read Only",
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId defaultConcurrentTexture = importTexture(
        Name("tests/task_graph/default_concurrent_read_only"),
        "Default Concurrent Read Only",
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId exclusiveTexture = importTexture(
        Name("tests/task_graph/exclusive_read_only"),
        "Exclusive Read Only",
        Graphics::ResourceQueueSharing::Exclusive
    );
    ASSERT_TRUE(concurrentTexture.valid());
    ASSERT_TRUE(defaultConcurrentTexture.valid());
    ASSERT_TRUE(exclusiveTexture.valid());

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
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
    const auto addReadTask = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::GpuQueueRequest& queue,
        const bool hasIndependentStateSource
    ){
        const Graphics::GpuTaskResourceUse uses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::ShaderResource,
                .access = Graphics::GpuTaskResourceAccess::Read,
                .hasIndependentStateSource = hasIndependentStateSource,
            },
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queue)
            .setScheduling(scheduling)
            .setResourceUses(uses, LengthOf(uses))
        ;
        return graph.addTask(desc);
    };
    const Graphics::GpuTaskId concurrentGraphics = addReadTask(
        Name("tests/task_graph/concurrent_read_graphics"),
        "Concurrent Graphics Read",
        concurrentTexture,
        graphicsRequest,
        false
    );
    const Graphics::GpuTaskId concurrentCompute = addReadTask(
        Name("tests/task_graph/concurrent_read_compute"),
        "Concurrent Compute Read",
        concurrentTexture,
        computeRequest,
        true
    );
    const Graphics::GpuTaskId defaultConcurrentGraphics = addReadTask(
        Name("tests/task_graph/default_concurrent_read_graphics"),
        "Default Concurrent Graphics Read",
        defaultConcurrentTexture,
        graphicsRequest,
        false
    );
    const Graphics::GpuTaskId defaultConcurrentCompute = addReadTask(
        Name("tests/task_graph/default_concurrent_read_compute"),
        "Default Concurrent Compute Read",
        defaultConcurrentTexture,
        computeRequest,
        false
    );
    const Graphics::GpuTaskId exclusiveGraphics = addReadTask(
        Name("tests/task_graph/exclusive_read_graphics"),
        "Exclusive Graphics Read",
        exclusiveTexture,
        graphicsRequest,
        false
    );
    const Graphics::GpuTaskId exclusiveCompute = addReadTask(
        Name("tests/task_graph/exclusive_read_compute"),
        "Exclusive Compute Read",
        exclusiveTexture,
        computeRequest,
        true
    );
    ASSERT_TRUE(concurrentGraphics.valid());
    ASSERT_TRUE(concurrentCompute.valid());
    ASSERT_TRUE(defaultConcurrentGraphics.valid());
    ASSERT_TRUE(defaultConcurrentCompute.valid());
    ASSERT_TRUE(exclusiveGraphics.valid());
    ASSERT_TRUE(exclusiveCompute.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_EQ(FindEdge(analysis, concurrentGraphics, concurrentCompute), nullptr);
    EXPECT_EQ(FindEdge(analysis, defaultConcurrentGraphics, defaultConcurrentCompute), nullptr);
    EXPECT_EQ(FindEdge(analysis, exclusiveGraphics, exclusiveCompute), nullptr);

    ASSERT_EQ(compiledGraph.packetCount(), 6u);
    const Graphics::GpuSubmissionPacketId concurrentGraphicsPacket = compiledGraph.packetForTask(concurrentGraphics);
    const Graphics::GpuSubmissionPacketId concurrentComputePacket = compiledGraph.packetForTask(concurrentCompute);
    const Graphics::GpuSubmissionPacketId defaultConcurrentGraphicsPacket = compiledGraph.packetForTask(
        defaultConcurrentGraphics
    );
    const Graphics::GpuSubmissionPacketId defaultConcurrentComputePacket = compiledGraph.packetForTask(
        defaultConcurrentCompute
    );
    const Graphics::GpuSubmissionPacketId exclusiveGraphicsPacket = compiledGraph.packetForTask(exclusiveGraphics);
    const Graphics::GpuSubmissionPacketId exclusiveComputePacket = compiledGraph.packetForTask(exclusiveCompute);
    ASSERT_TRUE(concurrentGraphicsPacket.valid());
    ASSERT_TRUE(concurrentComputePacket.valid());
    ASSERT_TRUE(defaultConcurrentGraphicsPacket.valid());
    ASSERT_TRUE(defaultConcurrentComputePacket.valid());
    ASSERT_TRUE(exclusiveGraphicsPacket.valid());
    ASSERT_TRUE(exclusiveComputePacket.valid());
    EXPECT_EQ(compiledGraph.packetIdAt(0u), concurrentGraphicsPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), concurrentComputePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), defaultConcurrentGraphicsPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(3u), defaultConcurrentComputePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(4u), exclusiveGraphicsPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(5u), exclusiveComputePacket);

    const Graphics::GpuCompiledTask* const compiledConcurrentCompute = compiledGraph.findTask(concurrentCompute);
    const Graphics::GpuCompiledTask* const compiledDefaultConcurrentCompute = compiledGraph.findTask(
        defaultConcurrentCompute
    );
    const Graphics::GpuCompiledTask* const compiledExclusiveCompute = compiledGraph.findTask(exclusiveCompute);
    ASSERT_NE(compiledConcurrentCompute, nullptr);
    ASSERT_NE(compiledDefaultConcurrentCompute, nullptr);
    ASSERT_NE(compiledExclusiveCompute, nullptr);
    EXPECT_EQ(compiledConcurrentCompute->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledDefaultConcurrentCompute->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledExclusiveCompute->prologueStateSeedCount, 1u);
    EXPECT_EQ(compiledConcurrentCompute->prologueBarrierCount, 0u);
    EXPECT_EQ(compiledDefaultConcurrentCompute->prologueBarrierCount, 0u);
    EXPECT_EQ(compiledExclusiveCompute->prologueBarrierCount, 0u);
    const Graphics::GpuPacketStateSeed* const defaultConcurrentSeed = compiledGraph.taskPrologueStateSeeds(
        defaultConcurrentCompute
    );
    const Graphics::GpuPacketStateSeed* const exclusiveSeed = compiledGraph.taskPrologueStateSeeds(exclusiveCompute);
    EXPECT_EQ(compiledGraph.taskPrologueStateSeeds(concurrentCompute), nullptr);
    ASSERT_NE(defaultConcurrentSeed, nullptr);
    ASSERT_NE(exclusiveSeed, nullptr);
    EXPECT_EQ(defaultConcurrentSeed[0u].sourcePacket, defaultConcurrentGraphicsPacket);
    EXPECT_EQ(exclusiveSeed[0u].sourcePacket, exclusiveGraphicsPacket);
    EXPECT_EQ(compiledGraph.packet(concurrentComputePacket).dependencyCount, 0u);
    ASSERT_EQ(compiledGraph.packet(defaultConcurrentComputePacket).dependencyCount, 1u);
    EXPECT_EQ(
        compiledGraph.packetDependencies(defaultConcurrentComputePacket)[0u].producer,
        defaultConcurrentGraphicsPacket
    );
    ASSERT_EQ(compiledGraph.packet(exclusiveComputePacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(exclusiveComputePacket)[0u].producer, exclusiveGraphicsPacket);

    // The combined sharing mask becomes Vulkan-concurrent only when the queues have distinct families. A pair of
    // logical queues in one family still uses exclusive ownership in the backend, so it must retain the handoff.
    Graphics::GpuPhysicalQueueInfo sameFamilyComputeQueue = DedicatedComputeQueue();
    sameFamilyComputeQueue.familyIndex = GraphicsQueue().familyIndex;
    const Graphics::GpuPhysicalQueueInfo sameFamilyQueues[] = {
        GraphicsQueue(),
        sameFamilyComputeQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology sameFamilyTopology{
        .queues = sameFamilyQueues,
        .queueCount = LengthOf(sameFamilyQueues),
    };
    Graphics::GpuTaskGraphAnalysis sameFamilyAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments sameFamilyAssignments(testArena.arena);
    Graphics::GpuCompiledGraph sameFamilyCompiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(
        graph,
        sameFamilyAnalysis,
        sameFamilyTopology,
        sameFamilyAssignments,
        sameFamilyCompiledGraph
    ));
    const Graphics::GpuSubmissionPacketId sameFamilyGraphicsPacket = sameFamilyCompiledGraph.packetForTask(
        concurrentGraphics
    );
    const Graphics::GpuSubmissionPacketId sameFamilyComputePacket = sameFamilyCompiledGraph.packetForTask(
        concurrentCompute
    );
    const Graphics::GpuCompiledTask* const sameFamilyCompiledCompute = sameFamilyCompiledGraph.findTask(
        concurrentCompute
    );
    ASSERT_TRUE(sameFamilyGraphicsPacket.valid());
    ASSERT_TRUE(sameFamilyComputePacket.valid());
    ASSERT_NE(sameFamilyCompiledCompute, nullptr);
    ASSERT_EQ(sameFamilyCompiledCompute->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const sameFamilySeed = sameFamilyCompiledGraph.taskPrologueStateSeeds(
        concurrentCompute
    );
    ASSERT_NE(sameFamilySeed, nullptr);
    EXPECT_EQ(sameFamilySeed[0u].sourcePacket, sameFamilyGraphicsPacket);
    ASSERT_EQ(sameFamilyCompiledGraph.packet(sameFamilyComputePacket).dependencyCount, 1u);
    EXPECT_EQ(
        sameFamilyCompiledGraph.packetDependencies(sameFamilyComputePacket)[0u].producer,
        sameFamilyGraphicsPacket
    );
}


TEST(GpuTaskGraph, RoutesLaggedLightingAlongsideAvboit){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const auto importTexture = [&](const Name& identity, const AStringView label, const Graphics::ResourceStates::Mask initialState){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(initialState)
            .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
        ;
        return graph.importResource(desc);
    };
    const Graphics::GpuGraphResourceId sharedPrefixRead = importTexture(
        Name("tests/task_graph/lagged_shared_prefix_read"),
        "Shared Prefix Read",
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId historyIrradiance = importTexture(
        Name("tests/task_graph/lagged_history_irradiance"),
        "Lagged Irradiance",
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId currentIrradiance = importTexture(
        Name("tests/task_graph/lagged_current_irradiance"),
        "Current Irradiance",
        Graphics::ResourceStates::CopySource
    );
    const Graphics::GpuGraphResourceId opaqueColor = importTexture(
        Name("tests/task_graph/lagged_opaque_color"),
        "Opaque Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId avboitAccumulation = importTexture(
        Name("tests/task_graph/lagged_avboit_accumulation"),
        "AVBOIT Accumulation",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId compositeColor = importTexture(
        Name("tests/task_graph/lagged_composite_color"),
        "Composite Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId backbuffer = AddHazardDomain(
        graph,
        Name("tests/task_graph/lagged_backbuffer"),
        "Back Buffer"
    );
    ASSERT_TRUE(sharedPrefixRead.valid());
    ASSERT_TRUE(historyIrradiance.valid());
    ASSERT_TRUE(currentIrradiance.valid());
    ASSERT_TRUE(opaqueColor.valid());
    ASSERT_TRUE(avboitAccumulation.valid());
    ASSERT_TRUE(compositeColor.valid());
    ASSERT_TRUE(backbuffer.valid());

    const Graphics::GpuExternalCompletionId prefixCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/lagged_prefix_complete"))
            .setMarkerLabel("Graphics Prefix Complete")
    );
    const Graphics::GpuExternalCompletionId historyCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/lagged_history_complete"))
            .setMarkerLabel("Lagged History Complete")
    );
    const Graphics::GpuExternalCompletionId surfelGiCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/lagged_surfel_gi_complete"))
            .setMarkerLabel("Surfel GI Complete")
    );
    ASSERT_TRUE(prefixCompletion.valid());
    ASSERT_TRUE(historyCompletion.valid());
    ASSERT_TRUE(surfelGiCompletion.valid());

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    const Graphics::GpuQueueRequest transferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    const Graphics::GpuExternalCompletionId prefixAndHistory[] = {
        prefixCompletion,
        historyCompletion,
    };
    const Graphics::GpuTaskResourceUse hardwareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc hardwareDesc;
    hardwareDesc
        .setIdentity(Name("tests/task_graph/lagged_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setExternalDependencies(prefixAndHistory, LengthOf(prefixAndHistory))
        .setResourceUses(hardwareUses, LengthOf(hardwareUses))
    ;
    const Graphics::GpuTaskId hardware = graph.addTask(hardwareDesc);
    ASSERT_TRUE(hardware.valid());

    const Graphics::GpuTaskResourceUse avboitPreUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc avboitPreDesc;
    avboitPreDesc
        .setIdentity(Name("tests/task_graph/lagged_avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setExternalDependencies(&prefixCompletion, 1u)
        .setResourceUses(avboitPreUses, LengthOf(avboitPreUses))
    ;
    const Graphics::GpuTaskId avboitPre = graph.addTask(avboitPreDesc);
    ASSERT_TRUE(avboitPre.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        },
        Graphics::GpuTaskResourceUse{
            .resource = historyIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setExternalDependencies(prefixAndHistory, LengthOf(prefixAndHistory))
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuTaskResourceUse compositeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId compositeTaskDependencies[] = {
        lighting,
        avboitPre,
    };
    Graphics::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(compositeTaskDependencies, LengthOf(compositeTaskDependencies))
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    const Graphics::GpuTaskId composite = graph.addTask(compositeDesc);
    ASSERT_TRUE(composite.valid());

    const Graphics::GpuTaskResourceUse presentUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::Present,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId presentDependencies[] = { composite };
    Graphics::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(presentDependencies, LengthOf(presentDependencies))
        .setExternalDependencies(&surfelGiCompletion, 1u)
        .setResourceUses(presentUses, LengthOf(presentUses))
    ;
    const Graphics::GpuTaskId present = graph.addTask(presentDesc);
    ASSERT_TRUE(present.valid());

    const Graphics::GpuTaskResourceUse historyCopyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = historyIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId historyCopyDependencies[] = { present };
    Graphics::GpuTaskDesc historyCopyDesc;
    historyCopyDesc
        .setIdentity(Name("tests/task_graph/lagged_history_copy"))
        .setMarkerLabel("Lagged Lighting History Copy")
        .setQueue(transferRequest)
        .setScheduling(scheduling)
        .setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies))
        .setResourceUses(historyCopyUses, LengthOf(historyCopyUses))
    ;
    const Graphics::GpuTaskId historyCopy = graph.addTask(historyCopyDesc);
    ASSERT_TRUE(historyCopy.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuTaskQueueAssignment* const hardwareAssignment = assignments.find(hardware);
    const Graphics::GpuTaskQueueAssignment* const avboitPreAssignment = assignments.find(avboitPre);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
    const Graphics::GpuTaskQueueAssignment* const presentAssignment = assignments.find(present);
    const Graphics::GpuTaskQueueAssignment* const historyCopyAssignment = assignments.find(historyCopy);
    ASSERT_NE(hardwareAssignment, nullptr);
    ASSERT_NE(avboitPreAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    ASSERT_NE(presentAssignment, nullptr);
    ASSERT_NE(historyCopyAssignment, nullptr);
    EXPECT_EQ(hardwareAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(avboitPreAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(lightingAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
    EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(presentAssignment->queueClass, Graphics::CommandQueue::Graphics);

    const Graphics::GpuSubmissionPacketId hardwarePacket = compiledGraph.packetForTask(hardware);
    const Graphics::GpuSubmissionPacketId avboitPrePacket = compiledGraph.packetForTask(avboitPre);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(composite);
    const Graphics::GpuSubmissionPacketId presentPacket = compiledGraph.packetForTask(present);
    const Graphics::GpuSubmissionPacketId historyCopyPacket = compiledGraph.packetForTask(historyCopy);
    ASSERT_TRUE(hardwarePacket.valid());
    ASSERT_TRUE(avboitPrePacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    ASSERT_TRUE(presentPacket.valid());
    ASSERT_TRUE(historyCopyPacket.valid());
    ASSERT_EQ(compiledGraph.packetCount(), 6u);
    EXPECT_EQ(compiledGraph.packetIdAt(0u), hardwarePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), avboitPrePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), lightingPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(3u), compositePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(4u), presentPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(5u), historyCopyPacket);
    EXPECT_EQ(FindEdge(analysis, avboitPre, lighting), nullptr);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledLighting, nullptr);
    EXPECT_EQ(compiledLighting->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 0u);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).externalDependencyCount, 2u);
    const Graphics::GpuExternalCompletionId* const lightingExternalDependencies = compiledGraph.packetExternalDependencies(
        lightingPacket
    );
    ASSERT_NE(lightingExternalDependencies, nullptr);
    EXPECT_EQ(lightingExternalDependencies[0u], prefixCompletion);
    EXPECT_EQ(lightingExternalDependencies[1u], historyCompletion);

    ASSERT_EQ(compiledGraph.packet(avboitPrePacket).externalDependencyCount, 1u);
    const Graphics::GpuExternalCompletionId* const avboitPreExternalDependencies =
        compiledGraph.packetExternalDependencies(avboitPrePacket);
    ASSERT_NE(avboitPreExternalDependencies, nullptr);
    EXPECT_EQ(avboitPreExternalDependencies[0u], prefixCompletion);

    ASSERT_EQ(compiledGraph.packet(compositePacket).dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const compositePacketDependencies = compiledGraph.packetDependencies(compositePacket);
    ASSERT_NE(compositePacketDependencies, nullptr);
    bool compositeWaitsForLighting = false;
    bool compositeWaitsForAvboit = false;
    for(usize index = 0u; index < compiledGraph.packet(compositePacket).dependencyCount; ++index){
        compositeWaitsForLighting = compositeWaitsForLighting
            || compositePacketDependencies[index].producer == lightingPacket
        ;
        compositeWaitsForAvboit = compositeWaitsForAvboit
            || compositePacketDependencies[index].producer == avboitPrePacket
        ;
    }
    EXPECT_TRUE(compositeWaitsForLighting);
    EXPECT_TRUE(compositeWaitsForAvboit);
    EXPECT_EQ(compiledGraph.packet(compositePacket).externalDependencyCount, 0u);

    const Graphics::GpuCompiledTask* const compiledPresent = compiledGraph.findTask(present);
    ASSERT_NE(compiledPresent, nullptr);
    ASSERT_EQ(compiledPresent->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const presentSeed = compiledGraph.taskPrologueStateSeeds(present);
    ASSERT_NE(presentSeed, nullptr);
    EXPECT_EQ(presentSeed[0u].resource, compositeColor);
    EXPECT_EQ(presentSeed[0u].sourcePacket, compositePacket);
    ASSERT_EQ(compiledGraph.packet(presentPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(presentPacket)[0u].producer, compositePacket);
    ASSERT_EQ(compiledGraph.packet(presentPacket).externalDependencyCount, 1u);
    const Graphics::GpuExternalCompletionId* const presentExternalDependencies = compiledGraph.packetExternalDependencies(
        presentPacket
    );
    ASSERT_NE(presentExternalDependencies, nullptr);
    EXPECT_EQ(presentExternalDependencies[0u], surfelGiCompletion);

    const Graphics::GpuPacketDependency* const historyCopyPacketDependencies = compiledGraph.packetDependencies(
        historyCopyPacket
    );
    ASSERT_NE(historyCopyPacketDependencies, nullptr);
    bool historyCopyWaitsForPresent = false;
    for(usize index = 0u; index < compiledGraph.packet(historyCopyPacket).dependencyCount; ++index){
        historyCopyWaitsForPresent = historyCopyWaitsForPresent
            || historyCopyPacketDependencies[index].producer == presentPacket
        ;
    }
    EXPECT_TRUE(historyCopyWaitsForPresent);
    EXPECT_EQ(compiledGraph.packet(historyCopyPacket).externalDependencyCount, 0u);
}


TEST(GpuTaskGraph, RoutesLiveAvboitBeforeDeferredLighting){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const auto importTexture = [&](const Name& identity, const AStringView label, const Graphics::ResourceStates::Mask initialState){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(initialState)
            .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
        ;
        return graph.importResource(desc);
    };
    const Graphics::GpuGraphResourceId sharedPrefixRead = importTexture(
        Name("tests/task_graph/live_shared_prefix_read"),
        "Shared Prefix Read",
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId avboitWorking = importTexture(
        Name("tests/task_graph/live_avboit_working"),
        "AVBOIT Working",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId avboitAccumulation = importTexture(
        Name("tests/task_graph/live_avboit_accumulation"),
        "AVBOIT Accumulation",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId opaqueColor = importTexture(
        Name("tests/task_graph/live_opaque_color"),
        "Opaque Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId compositeColor = importTexture(
        Name("tests/task_graph/live_composite_color"),
        "Composite Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId backbuffer = AddHazardDomain(
        graph,
        Name("tests/task_graph/live_backbuffer"),
        "Back Buffer"
    );
    ASSERT_TRUE(sharedPrefixRead.valid());
    ASSERT_TRUE(avboitWorking.valid());
    ASSERT_TRUE(avboitAccumulation.valid());
    ASSERT_TRUE(opaqueColor.valid());
    ASSERT_TRUE(compositeColor.valid());
    ASSERT_TRUE(backbuffer.valid());

    const Graphics::GpuExternalCompletionId prefixCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/live_prefix_complete"))
            .setMarkerLabel("Graphics Prefix Complete")
    );
    const Graphics::GpuExternalCompletionId surfelGiCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/live_surfel_gi_complete"))
            .setMarkerLabel("Surfel GI Complete")
    );
    ASSERT_TRUE(prefixCompletion.valid());
    ASSERT_TRUE(surfelGiCompletion.valid());

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };

    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/live_avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setExternalDependencies(&prefixCompletion, 1u)
        .setResourceUses(preUses, LengthOf(preUses))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const Graphics::GpuTaskResourceUse depthWarpUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId preDependency[] = { pre };
    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/live_avboit_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(preDependency, LengthOf(preDependency))
        .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuTaskResourceUse extinctionUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId depthWarpDependency[] = { depthWarp };
    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/live_avboit_extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
        .setResourceUses(extinctionUses, LengthOf(extinctionUses))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskResourceUse integrationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId extinctionDependency[] = { extinction };
    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/live_avboit_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
        .setResourceUses(integrationUses, LengthOf(integrationUses))
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId integrationDependency[] = { integration };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/live_avboit_accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId lightingDependencies[] = { accumulation };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/live_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(lightingDependencies, LengthOf(lightingDependencies))
        .setExternalDependencies(&surfelGiCompletion, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuTaskResourceUse compositeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId compositeDependencies[] = {
        lighting,
        accumulation,
    };
    Graphics::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/task_graph/live_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(compositeDependencies, LengthOf(compositeDependencies))
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    const Graphics::GpuTaskId composite = graph.addTask(compositeDesc);
    ASSERT_TRUE(composite.valid());

    const Graphics::GpuTaskResourceUse presentUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::Present,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId presentDependencies[] = { composite };
    Graphics::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("tests/task_graph/live_deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(presentDependencies, LengthOf(presentDependencies))
        .setResourceUses(presentUses, LengthOf(presentUses))
    ;
    const Graphics::GpuTaskId present = graph.addTask(presentDesc);
    ASSERT_TRUE(present.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuTaskQueueAssignment* const preAssignment = assignments.find(pre);
    const Graphics::GpuTaskQueueAssignment* const depthWarpAssignment = assignments.find(depthWarp);
    const Graphics::GpuTaskQueueAssignment* const extinctionAssignment = assignments.find(extinction);
    const Graphics::GpuTaskQueueAssignment* const integrationAssignment = assignments.find(integration);
    const Graphics::GpuTaskQueueAssignment* const accumulationAssignment = assignments.find(accumulation);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
    const Graphics::GpuTaskQueueAssignment* const presentAssignment = assignments.find(present);
    ASSERT_NE(preAssignment, nullptr);
    ASSERT_NE(depthWarpAssignment, nullptr);
    ASSERT_NE(extinctionAssignment, nullptr);
    ASSERT_NE(integrationAssignment, nullptr);
    ASSERT_NE(accumulationAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    ASSERT_NE(presentAssignment, nullptr);
    EXPECT_EQ(preAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(depthWarpAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(extinctionAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(integrationAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(accumulationAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(presentAssignment->queueClass, Graphics::CommandQueue::Graphics);

    const Graphics::GpuSubmissionPacketId prePacket = compiledGraph.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledGraph.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledGraph.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledGraph.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(composite);
    const Graphics::GpuSubmissionPacketId presentPacket = compiledGraph.packetForTask(present);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    ASSERT_TRUE(presentPacket.valid());
    ASSERT_EQ(compiledGraph.packetCount(), 8u);
    EXPECT_EQ(compiledGraph.packetIdAt(0u), prePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), depthWarpPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), extinctionPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(3u), integrationPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(4u), accumulationPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(5u), lightingPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(6u), compositePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(7u), presentPacket);

    EXPECT_NE(FindEdge(analysis, accumulation, lighting), nullptr);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledLighting, nullptr);
    ASSERT_GT(compiledLighting->prologueStateSeedCount, 0u);
    const Graphics::GpuPacketStateSeed* const lightingSeeds = compiledGraph.taskPrologueStateSeeds(lighting);
    ASSERT_NE(lightingSeeds, nullptr);
    bool lightingImportsAccumulationState = false;
    for(usize index = 0u; index < compiledLighting->prologueStateSeedCount; ++index){
        lightingImportsAccumulationState = lightingImportsAccumulationState
            || lightingSeeds[index].sourcePacket == accumulationPacket
        ;
    }
    EXPECT_TRUE(lightingImportsAccumulationState);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).externalDependencyCount, 1u);
    const Graphics::GpuExternalCompletionId* const lightingExternalDependencies = compiledGraph.packetExternalDependencies(
        lightingPacket
    );
    ASSERT_NE(lightingExternalDependencies, nullptr);
    EXPECT_EQ(lightingExternalDependencies[0u], surfelGiCompletion);

    const Graphics::GpuPacketDependency* const compositePacketDependencies = compiledGraph.packetDependencies(compositePacket);
    ASSERT_NE(compositePacketDependencies, nullptr);
    bool compositeWaitsForLighting = false;
    bool compositeWaitsForAccumulation = false;
    for(usize index = 0u; index < compiledGraph.packet(compositePacket).dependencyCount; ++index){
        compositeWaitsForLighting = compositeWaitsForLighting
            || compositePacketDependencies[index].producer == lightingPacket
        ;
        compositeWaitsForAccumulation = compositeWaitsForAccumulation
            || compositePacketDependencies[index].producer == accumulationPacket
        ;
    }
    EXPECT_TRUE(compositeWaitsForLighting);
    EXPECT_TRUE(compositeWaitsForAccumulation);
    EXPECT_EQ(compiledGraph.packet(compositePacket).externalDependencyCount, 0u);
}


TEST(GpuTaskGraph, PlansTextureStatesPerDeclaredSubresourceRange){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/subresource_states"),
        "Subresource States"
    );
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceUse firstUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet{ 0u, 1u, 0u, 1u },
            },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse secondUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet{ 1u, 1u, 0u, 1u },
            },
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/subresource_first"),
        "First Subresource",
        nullptr,
        0u,
        firstUse,
        LengthOf(firstUse)
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/subresource_second"),
        "Second Subresource",
        nullptr,
        0u,
        secondUse,
        LengthOf(secondUse)
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuCompiledBarrier* const secondBarrier = compiledGraph.taskPrologueBarriers(second);
    ASSERT_NE(secondBarrier, nullptr);
    EXPECT_EQ(secondBarrier[0].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(secondBarrier[0].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(secondBarrier[0].after, Graphics::ResourceStates::ShaderResource);
}


TEST(GpuTaskGraph, CompilesTransferPreferenceToGraphicsFallbackWithoutARendererPath){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;
    Graphics::QueueSubmissionToken acceptedToken;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/transfer_fallback"))
        .setMarkerLabel("Transfer Fallback")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const Graphics::GpuTaskId copyTask = graph.addTask<PacketLifecycleTask>(
        desc,
        PacketLifecycleTask::Payload{ &acceptedCount, &discardedCount, &acceptedToken }
    );
    ASSERT_TRUE(copyTask.valid());

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::Fallback);

    const Graphics::GpuSubmissionPacketId packet = compiledGraph.packetForTask(copyTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledGraph.packet(packet).queue, assignment->queue);
    const Graphics::GpuPhysicalQueueInfo* const packetQueue = compiledGraph.queueInfo(compiledGraph.packet(packet).queue);
    ASSERT_NE(packetQueue, nullptr);
    EXPECT_EQ(packetQueue->queueClass, Graphics::CommandQueue::Graphics);
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

