// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <cstddef>

#include <gtest/gtest.h>

#include <core/graphics/capture/command_ir.h>
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
    const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common,
    const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
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
}

[[nodiscard]] Graphics::GpuGraphResourceId AddBufferMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common,
    const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(initialState)
        .setQueueSharing(queueSharing)
    ;
    return graph.importResource(desc);
}

[[nodiscard]] Graphics::GpuGraphResourceId AddAccelStructMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::ResourceStates::Mask initialState = Graphics::ResourceStates::Common,
    const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
){
    Graphics::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Graphics::GpuGraphResourceType::AccelStruct)
        .setInitialState(initialState)
        .setQueueSharing(queueSharing)
    ;
    return graph.importResource(desc);
}

[[nodiscard]] Graphics::GpuGraphPipelineId AddPipelineMetadata(
    Graphics::GpuTaskGraph& graph,
    const Name& identity,
    const AStringView label,
    const Graphics::GpuGraphPipelineType::Enum type
){
    Graphics::GpuGraphPipelineDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(type)
    ;
    return graph.importPipeline(desc);
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
    Graphics::GpuCompiledGraph& compiledGraph,
    const Graphics::GpuTaskGraphCompileOptions& options = {}
){
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;
    return compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena, options);
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

[[nodiscard]] Graphics::GpuPhysicalQueueInfo DedicatedTransferQueue(const u16 index = 2u){
    return Graphics::GpuPhysicalQueueInfo{
        .id = Graphics::GpuPhysicalQueueId{ index, 1u },
        .queueClass = Graphics::CommandQueue::Transfer,
        .capabilities = Graphics::GpuQueueCapability::Transfer,
        .familyIndex = 2u,
        .queueIndex = 0u,
        .dedicated = true,
    };
}

struct TransferOwnershipPair{
    Graphics::GpuGraphResourceId texture;
    Graphics::GpuTaskId producer;
    Graphics::GpuTaskId consumer;
};

[[nodiscard]] TransferOwnershipPair AddTransferOwnershipPair(
    Graphics::GpuTaskGraph& graph,
    const Graphics::ResourceQueueSharing::Mask queueSharing
){
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/transfer_ownership_texture"),
        "Transfer Ownership Texture",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    if(!texture.valid())
        return {};

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse consumerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/transfer_ownership_producer"))
        .setMarkerLabel("Transfer Ownership Producer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/transfer_ownership_consumer"))
        .setMarkerLabel("Transfer Ownership Consumer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;

    return TransferOwnershipPair{
        .texture = texture,
        .producer = graph.addTask(producerDesc),
        .consumer = graph.addTask(consumerDesc),
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

inline constexpr Graphics::GpuTaskId s_CommandIrTask{ 4u, 17u };
inline constexpr Graphics::GpuSubmissionPacketId s_CommandIrPacket{ 2u, 17u };
inline constexpr Graphics::GpuPhysicalQueueId s_CommandIrQueue{ 1u, 3u };
inline constexpr Graphics::GpuGraphResourceId s_CommandIrSource{ 5u, 17u };
inline constexpr Graphics::GpuGraphResourceId s_CommandIrDestination{ 6u, 17u };

inline constexpr usize s_CommandIrCopyBufferOffset = sizeof(Graphics::GpuCommandIrStreamHeader);
inline constexpr usize s_CommandIrCopyTextureOffset = s_CommandIrCopyBufferOffset
    + sizeof(Graphics::GpuCommandIrCopyBufferRecord);
inline constexpr usize s_CommandIrClearBufferOffset = s_CommandIrCopyTextureOffset
    + sizeof(Graphics::GpuCommandIrCopyTextureRecord);
inline constexpr usize s_CommandIrClearTextureOffset = s_CommandIrClearBufferOffset
    + sizeof(Graphics::GpuCommandIrClearBufferRecord);

[[nodiscard]] bool CaptureAllBuiltinCommandIrRecords(Graphics::GpuCommandIrCapture& capture){
    Graphics::TextureSlice sourceSlice;
    sourceSlice
        .setOrigin(1u, 2u, 3u)
        .setSize(4u, 5u, 6u)
        .setMipLevel(7u)
        .setArraySlice(8u)
    ;
    Graphics::TextureSlice destinationSlice;
    destinationSlice
        .setOrigin(9u, 10u, 11u)
        .setSize(12u, 13u, 14u)
        .setMipLevel(15u)
        .setArraySlice(16u)
    ;
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = s_CommandIrDestination;
    clearTexture.subresources = Graphics::TextureSubresourceSet(2u, 3u, 4u, 5u);
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.floatValue = Graphics::Color(0.25f, 0.5f, 0.75f, 1.f);
    clearTexture.uintValue = Graphics::UIntColor(2u, 3u, 5u, 7u);
    clearTexture.intValue = Graphics::IntColor(-2, -3, -5, -7);
    clearTexture.depthValue = 0.125f;
    clearTexture.stencilValue = 19u;
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;

    return capture.captureCopyBuffer(
        s_CommandIrTask,
        s_CommandIrPacket,
        s_CommandIrQueue,
        s_CommandIrSource,
        16u,
        s_CommandIrDestination,
        32u,
        64u
    )
        && capture.captureCopyTexture(
            s_CommandIrTask,
            s_CommandIrPacket,
            s_CommandIrQueue,
            s_CommandIrSource,
            sourceSlice,
            s_CommandIrDestination,
            destinationSlice
        )
        && capture.captureClearBuffer(
            s_CommandIrTask,
            s_CommandIrPacket,
            s_CommandIrQueue,
            s_CommandIrDestination,
            0xdecafbadU
        )
        && capture.captureClearTexture(
            s_CommandIrTask,
            s_CommandIrPacket,
            s_CommandIrQueue,
            s_CommandIrDestination,
            clearTexture
        )
    ;
}

static void CopyCommandIrBytes(
    Graphics::GraphicsBytes& outBytes,
    const BinaryByteView source
){
    outBytes.resize(source.size());
    if(!source.empty())
        NWB_MEMCPY(outBytes.data(), outBytes.size(), source.data(), source.size());
}

template<typename PodT>
static void WriteCommandIrPod(
    Graphics::GraphicsBytes& bytes,
    const usize byteOffset,
    const PodT& value
){
    NWB_ASSERT(byteOffset <= bytes.size());
    NWB_ASSERT(bytes.size() - byteOffset >= sizeof(PodT));
    NWB_MEMCPY(bytes.data() + byteOffset, bytes.size() - byteOffset, &value, sizeof(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, CopiesCallerMetadataAndDestroysTypedPayloadOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = AddHazardDomain(graph, Name("tests/task_graph/resource"), "Resource");
    ASSERT_TRUE(resource.valid());

    const Graphics::GpuTaskId predecessor = AddTask(graph, Name("tests/task_graph/predecessor"), "Predecessor");
    ASSERT_TRUE(predecessor.valid());

    Graphics::GpuTaskId dependencies[] = { predecessor };
    Graphics::CommandListResourceStateHandoff externalStateSource(testArena.arena);
    Graphics::GpuTaskExternalStateSource externalStateSources[] = {
        Graphics::GpuTaskExternalStateSource{ .states = &externalStateSource },
    };
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
        .setExternalStateSources(externalStateSources, LengthOf(externalStateSources))
        .setResourceUses(uses, LengthOf(uses))
    ;

    u32 destructionCount = 0u;
    PayloadDestroyTask::Payload payload(&destructionCount);
    const Graphics::GpuTaskId task = graph.addTask<PayloadDestroyTask>(desc, Move(payload));
    ASSERT_TRUE(task.valid());

    dependencies[0] = {};
    externalStateSources[0].states = nullptr;
    uses[0].resource = {};
    markerLabel[0] = 'X';
    const Graphics::GpuTaskGraphTaskView stored = graph.taskAt(task.index);
    ASSERT_EQ(stored.dependencyCount, 1u);
    ASSERT_EQ(stored.externalStateSourceCount, 1u);
    ASSERT_EQ(stored.resourceUseCount, 1u);
    EXPECT_EQ(stored.dependencies[0], predecessor);
    EXPECT_EQ(stored.externalStateSources[0].states, &externalStateSource);
    EXPECT_EQ(stored.resourceUses[0].resource, resource);
    EXPECT_EQ(stored.markerLabel, AStringView("Stack Marker"));
    EXPECT_TRUE(stored.hasPayload);

    graph.reset();
    EXPECT_EQ(destructionCount, 1u);
    EXPECT_FALSE(graph.validTask(task));
    EXPECT_FALSE(graph.validResource(resource));
}

TEST(GpuTaskGraph, OwnsUploadBlobsAndInvalidatesThemOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u8 sourceBytes[] = { 0x17u, 0x3au, 0x5cu, 0x8eu };

    EXPECT_FALSE(graph.copyUploadData(nullptr, sizeof(sourceBytes), alignof(u8)).valid());
    EXPECT_FALSE(graph.copyUploadData(sourceBytes, 0u, alignof(u8)).valid());
    EXPECT_FALSE(graph.copyUploadData(sourceBytes, sizeof(sourceBytes), 3u).valid());

    const Graphics::GpuUploadBlobId blob = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(blob.valid());
    EXPECT_TRUE(graph.validUploadBlob(blob));
    EXPECT_EQ(graph.uploadBlobCount(), 1u);

    sourceBytes[0u] = 0u;
    usize byteSize = 0u;
    const auto* const storedBytes = static_cast<const u8*>(graph.uploadBlobData(blob, byteSize));
    ASSERT_NE(storedBytes, nullptr);
    ASSERT_EQ(byteSize, sizeof(sourceBytes));
    EXPECT_EQ(storedBytes[0u], 0x17u);
    EXPECT_EQ(storedBytes[1u], 0x3au);
    EXPECT_EQ(storedBytes[2u], 0x5cu);
    EXPECT_EQ(storedBytes[3u], 0x8eu);

    graph.reset();
    EXPECT_FALSE(graph.validUploadBlob(blob));
    EXPECT_EQ(graph.uploadBlobCount(), 0u);
    byteSize = Limit<usize>::s_Max;
    EXPECT_EQ(graph.uploadBlobData(blob, byteSize), nullptr);
    EXPECT_EQ(byteSize, 0u);

    const Graphics::GpuUploadBlobId replacement = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.index, 0u);
    EXPECT_NE(replacement.generation, blob.generation);
}

TEST(GpuTaskGraph, OwnsPipelineMetadataAndInvalidatesPipelineIdsOnReset){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphPipelineDesc invalidDesc;
    invalidDesc
        .setIdentity(Name("tests/task_graph/invalid_pipeline"))
        .setMarkerLabel("Invalid Pipeline")
    ;
    EXPECT_FALSE(graph.importPipeline(invalidDesc).valid());
    EXPECT_FALSE(graph.importComputePipeline(
        Graphics::ComputePipelineHandle{},
        Graphics::GpuGraphPipelineDesc{}
            .setIdentity(Name("tests/task_graph/null_compute_pipeline"))
            .setMarkerLabel("Null Compute Pipeline")
            .setType(Graphics::GpuGraphPipelineType::Compute)
    ).valid());

    char markerLabel[] = "Deferred Lighting Pipeline";
    Graphics::GpuGraphPipelineDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/deferred_lighting_pipeline"))
        .setMarkerLabel(AStringView(markerLabel))
        .setType(Graphics::GpuGraphPipelineType::Compute)
    ;
    const Graphics::GpuGraphPipelineId pipeline = graph.importPipeline(desc);
    ASSERT_TRUE(pipeline.valid());
    EXPECT_TRUE(graph.validPipeline(pipeline));
    EXPECT_EQ(graph.pipelineCount(), 1u);
    EXPECT_EQ(graph.importPipeline(desc), pipeline);

    markerLabel[0] = 'X';
    const Graphics::GpuTaskGraphPipelineView stored = graph.pipelineAt(pipeline.index);
    EXPECT_EQ(stored.id, pipeline);
    EXPECT_EQ(stored.identity, desc.identity);
    EXPECT_EQ(stored.markerLabel, AStringView("Deferred Lighting Pipeline"));
    EXPECT_EQ(stored.type, Graphics::GpuGraphPipelineType::Compute);
    EXPECT_FALSE(stored.hasBackendPipeline);
    EXPECT_EQ(graph.graphicsPipelineFor(pipeline), nullptr);
    EXPECT_EQ(graph.computePipelineFor(pipeline), nullptr);
    EXPECT_EQ(graph.meshletPipelineFor(pipeline), nullptr);
    EXPECT_EQ(graph.rayTracingPipelineFor(pipeline), nullptr);

    Graphics::GpuGraphPipelineDesc mismatchedType = desc;
    mismatchedType.setType(Graphics::GpuGraphPipelineType::Graphics);
    EXPECT_FALSE(graph.importPipeline(mismatchedType).valid());

    graph.reset();
    EXPECT_EQ(graph.pipelineCount(), 0u);
    EXPECT_FALSE(graph.validPipeline(pipeline));
    EXPECT_EQ(graph.computePipelineFor(pipeline), nullptr);

    const Graphics::GpuGraphPipelineId replacement = AddPipelineMetadata(
        graph,
        Name("tests/task_graph/deferred_lighting_pipeline"),
        "Replacement Pipeline",
        Graphics::GpuGraphPipelineType::Compute
    );
    ASSERT_TRUE(replacement.valid());
    EXPECT_EQ(replacement.index, 0u);
    EXPECT_NE(replacement.generation, pipeline.generation);
    EXPECT_NE(replacement, pipeline);
}

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
        .destination = destination,
    };
    EXPECT_FALSE(graph.addCopyTextureTask(
        desc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &region,
            .regionCount = 1u,
        }
    ).valid());
    EXPECT_EQ(graph.taskCount(), 0u);
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
    EXPECT_EQ(graph.taskCount(), 0u);
}

TEST(GpuCommandIrCapture, RetainsBuiltInRecordsForOneGraphGeneration){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ 4u, 17u };
    const Graphics::GpuSubmissionPacketId packet{ 2u, 17u };
    const Graphics::GpuPhysicalQueueId queue{ 1u, 3u };
    const Graphics::GpuGraphResourceId source{ 5u, 17u };
    const Graphics::GpuGraphResourceId destination{ 6u, 17u };

    ASSERT_TRUE(capture.captureCopyBuffer(
        task,
        packet,
        queue,
        source,
        16u,
        destination,
        32u,
        64u
    ));
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = destination;
    clearTexture.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    clearTexture.floatValue = Graphics::Color(0.25f, 0.5f, 0.75f, 1.f);
    // This legacy record keeps descriptor fields verbatim; the byte stream canonicalizes them separately.
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;
    ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, destination, clearTexture));

    ASSERT_EQ(capture.recordCount(), 2u);
    EXPECT_EQ(capture.graphGeneration(), task.generation);
    const Graphics::GpuCommandIrBuiltinTaskRecord* const copyRecord = capture.recordAt(0u);
    ASSERT_NE(copyRecord, nullptr);
    EXPECT_EQ(copyRecord->opcode, Graphics::GpuCommandIrOpcode::CopyBuffer);
    EXPECT_EQ(copyRecord->task, task);
    EXPECT_EQ(copyRecord->packet, packet);
    EXPECT_EQ(copyRecord->queue, queue);
    EXPECT_EQ(copyRecord->source, source);
    EXPECT_EQ(copyRecord->destination, destination);
    EXPECT_EQ(copyRecord->sourceOffsetBytes, 16u);
    EXPECT_EQ(copyRecord->destinationOffsetBytes, 32u);
    EXPECT_EQ(copyRecord->dataSizeBytes, 64u);

    const Graphics::GpuCommandIrBuiltinTaskRecord* const clearRecord = capture.recordAt(1u);
    ASSERT_NE(clearRecord, nullptr);
    EXPECT_EQ(clearRecord->opcode, Graphics::GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(clearRecord->destination, destination);
    EXPECT_EQ(clearRecord->destinationSubresources, clearTexture.subresources);
    EXPECT_EQ(clearRecord->clearTextureValueType, clearTexture.valueType);
    EXPECT_EQ(clearRecord->floatClearValue, clearTexture.floatValue);
    EXPECT_TRUE(clearRecord->clearDepth);
    EXPECT_TRUE(clearRecord->clearStencil);

    {
        const BinaryByteView capturedBytes = capture.commandBytes();
        usize captureCursor = sizeof(Graphics::GpuCommandIrStreamHeader);
        Graphics::GpuCommandIrCopyBufferRecord capturedCopy;
        Graphics::GpuCommandIrClearTextureRecord capturedClear;
        ASSERT_TRUE(ReadPOD(capturedBytes, captureCursor, capturedCopy));
        ASSERT_TRUE(ReadPOD(capturedBytes, captureCursor, capturedClear));
        EXPECT_EQ(capturedClear.clearTextureValueType, clearTexture.valueType);
        EXPECT_EQ(capturedClear.clearFlags, Graphics::GpuCommandIrClearTextureFlag::None);
        EXPECT_EQ(captureCursor, capturedBytes.size());
    }

    const BinaryByteView bytesBeforeRejectedRecord = capture.commandBytes();
    Graphics::GraphicsBytes streamBeforeRejectedRecord(testArena.arena);
    streamBeforeRejectedRecord.resize(bytesBeforeRejectedRecord.size());
    NWB_MEMCPY(
        streamBeforeRejectedRecord.data(),
        streamBeforeRejectedRecord.size(),
        bytesBeforeRejectedRecord.data(),
        streamBeforeRejectedRecord.size()
    );
    const Graphics::GpuTaskId anotherGraphTask{ task.index, task.generation + 1u };
    const Graphics::GpuSubmissionPacketId anotherGraphPacket{ packet.index, packet.generation + 1u };
    const Graphics::GpuGraphResourceId anotherGraphResource{ destination.index, destination.generation + 1u };
    EXPECT_FALSE(capture.captureClearBuffer(
        anotherGraphTask,
        anotherGraphPacket,
        queue,
        anotherGraphResource,
        0xdecafbadU
    ));
    EXPECT_EQ(capture.recordCount(), 2u);
    const BinaryByteView bytesAfterRejectedRecord = capture.commandBytes();
    EXPECT_EQ(bytesAfterRejectedRecord.size(), streamBeforeRejectedRecord.size());
    EXPECT_EQ(
        NWB_MEMCMP(
            bytesAfterRejectedRecord.data(),
            streamBeforeRejectedRecord.data(),
            streamBeforeRejectedRecord.size()
        ),
        0
    );

    capture.reset();
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_EQ(capture.graphGeneration(), 0u);
    EXPECT_EQ(capture.recordAt(0u), nullptr);
    const BinaryByteView resetBytes = capture.commandBytes();
    usize resetCursor = 0u;
    Graphics::GpuCommandIrStreamHeader resetHeader;
    ASSERT_TRUE(ReadPOD(resetBytes, resetCursor, resetHeader));
    EXPECT_EQ(resetHeader.magic, Graphics::s_GpuCommandIrStreamMagic);
    EXPECT_EQ(resetHeader.graphGeneration, 0u);
    EXPECT_EQ(resetHeader.recordCount, 0u);
    EXPECT_EQ(resetHeader.payloadBytes, 0u);
    EXPECT_EQ(resetCursor, resetBytes.size());
}

TEST(GpuCommandIrCapture, EncodesBuiltInsAsLinearPodRecordsAndRollsBackAtRecordBoundaries){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    const Graphics::GpuTaskId task{ 4u, 17u };
    const Graphics::GpuSubmissionPacketId packet{ 2u, 17u };
    const Graphics::GpuPhysicalQueueId queue{ 1u, 3u };
    const Graphics::GpuGraphResourceId source{ 5u, 17u };
    const Graphics::GpuGraphResourceId destination{ 6u, 17u };

    const BinaryByteView emptyBytes = capture.commandBytes();
    ASSERT_EQ(emptyBytes.size(), sizeof(Graphics::GpuCommandIrStreamHeader));
    usize cursor = 0u;
    Graphics::GpuCommandIrStreamHeader streamHeader;
    ASSERT_TRUE(ReadPOD(emptyBytes, cursor, streamHeader));
    EXPECT_EQ(streamHeader.magic, Graphics::s_GpuCommandIrStreamMagic);
    EXPECT_EQ(streamHeader.version, Graphics::s_GpuCommandIrStreamVersion);
    EXPECT_EQ(streamHeader.reserved, 0u);
    EXPECT_EQ(streamHeader.graphGeneration, 0u);
    EXPECT_EQ(streamHeader.recordCount, 0u);
    EXPECT_EQ(streamHeader.payloadBytes, 0u);
    EXPECT_EQ(cursor, emptyBytes.size());

    Graphics::TextureSlice sourceSlice;
    sourceSlice
        .setOrigin(1u, 2u, 3u)
        .setSize(4u, 5u, 6u)
        .setMipLevel(7u)
        .setArraySlice(8u)
    ;
    Graphics::TextureSlice destinationSlice;
    destinationSlice
        .setOrigin(9u, 10u, 11u)
        .setSize(12u, 13u, 14u)
        .setMipLevel(15u)
        .setArraySlice(16u)
    ;
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = destination;
    clearTexture.subresources = Graphics::TextureSubresourceSet(2u, 3u, 4u, 5u);
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.floatValue = Graphics::Color(0.25f, 0.5f, 0.75f, 1.f);
    clearTexture.uintValue = Graphics::UIntColor(2u, 3u, 5u, 7u);
    clearTexture.intValue = Graphics::IntColor(-2, -3, -5, -7);
    clearTexture.depthValue = 0.125f;
    clearTexture.stencilValue = 19u;
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;

    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 16u, destination, 32u, 64u));
    ASSERT_TRUE(capture.captureCopyTexture(task, packet, queue, source, sourceSlice, destination, destinationSlice));
    ASSERT_TRUE(capture.captureClearBuffer(task, packet, queue, destination, 0xdecafbadU));
    ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, destination, clearTexture));

    const BinaryByteView bytes = capture.commandBytes();
    cursor = 0u;
    ASSERT_TRUE(ReadPOD(bytes, cursor, streamHeader));
    EXPECT_EQ(streamHeader.magic, Graphics::s_GpuCommandIrStreamMagic);
    EXPECT_EQ(streamHeader.version, Graphics::s_GpuCommandIrStreamVersion);
    EXPECT_EQ(streamHeader.reserved, 0u);
    EXPECT_EQ(streamHeader.graphGeneration, task.generation);
    EXPECT_EQ(streamHeader.recordCount, 4u);
    EXPECT_EQ(
        streamHeader.payloadBytes,
        sizeof(Graphics::GpuCommandIrCopyBufferRecord)
            + sizeof(Graphics::GpuCommandIrCopyTextureRecord)
            + sizeof(Graphics::GpuCommandIrClearBufferRecord)
            + sizeof(Graphics::GpuCommandIrClearTextureRecord)
    );
    const usize copyTextureEnd = cursor
        + sizeof(Graphics::GpuCommandIrCopyBufferRecord)
        + sizeof(Graphics::GpuCommandIrCopyTextureRecord)
    ;

    Graphics::GpuCommandIrCopyBufferRecord copyBuffer;
    ASSERT_TRUE(ReadPOD(bytes, cursor, copyBuffer));
    EXPECT_EQ(copyBuffer.header.opcode, Graphics::GpuCommandIrWireOpcode::CopyBuffer);
    EXPECT_EQ(copyBuffer.header.byteSize, sizeof(copyBuffer));
    EXPECT_EQ(copyBuffer.context.taskIndex, task.index);
    EXPECT_EQ(copyBuffer.context.packetIndex, packet.index);
    EXPECT_EQ(copyBuffer.context.queueIndex, queue.index);
    EXPECT_EQ(copyBuffer.context.queueDeviceGeneration, queue.deviceGeneration);
    EXPECT_EQ(copyBuffer.sourceResourceIndex, source.index);
    EXPECT_EQ(copyBuffer.destinationResourceIndex, destination.index);
    EXPECT_EQ(copyBuffer.sourceOffsetBytes, 16u);
    EXPECT_EQ(copyBuffer.destinationOffsetBytes, 32u);
    EXPECT_EQ(copyBuffer.dataSizeBytes, 64u);

    Graphics::GpuCommandIrCopyTextureRecord copyTexture;
    ASSERT_TRUE(ReadPOD(bytes, cursor, copyTexture));
    EXPECT_EQ(copyTexture.header.opcode, Graphics::GpuCommandIrWireOpcode::CopyTexture);
    EXPECT_EQ(copyTexture.header.byteSize, sizeof(copyTexture));
    EXPECT_EQ(copyTexture.context.taskIndex, task.index);
    EXPECT_EQ(copyTexture.context.packetIndex, packet.index);
    EXPECT_EQ(copyTexture.context.queueIndex, queue.index);
    EXPECT_EQ(copyTexture.context.queueDeviceGeneration, queue.deviceGeneration);
    EXPECT_EQ(copyTexture.sourceResourceIndex, source.index);
    EXPECT_EQ(copyTexture.destinationResourceIndex, destination.index);
    EXPECT_EQ(copyTexture.sourceSlice.x, sourceSlice.x);
    EXPECT_EQ(copyTexture.sourceSlice.y, sourceSlice.y);
    EXPECT_EQ(copyTexture.sourceSlice.z, sourceSlice.z);
    EXPECT_EQ(copyTexture.sourceSlice.width, sourceSlice.width);
    EXPECT_EQ(copyTexture.sourceSlice.height, sourceSlice.height);
    EXPECT_EQ(copyTexture.sourceSlice.depth, sourceSlice.depth);
    EXPECT_EQ(copyTexture.sourceSlice.mipLevel, sourceSlice.mipLevel);
    EXPECT_EQ(copyTexture.sourceSlice.arraySlice, sourceSlice.arraySlice);
    EXPECT_EQ(copyTexture.destinationSlice.x, destinationSlice.x);
    EXPECT_EQ(copyTexture.destinationSlice.y, destinationSlice.y);
    EXPECT_EQ(copyTexture.destinationSlice.z, destinationSlice.z);
    EXPECT_EQ(copyTexture.destinationSlice.width, destinationSlice.width);
    EXPECT_EQ(copyTexture.destinationSlice.height, destinationSlice.height);
    EXPECT_EQ(copyTexture.destinationSlice.depth, destinationSlice.depth);
    EXPECT_EQ(copyTexture.destinationSlice.mipLevel, destinationSlice.mipLevel);
    EXPECT_EQ(copyTexture.destinationSlice.arraySlice, destinationSlice.arraySlice);

    Graphics::GpuCommandIrClearBufferRecord clearBuffer;
    ASSERT_TRUE(ReadPOD(bytes, cursor, clearBuffer));
    EXPECT_EQ(clearBuffer.header.opcode, Graphics::GpuCommandIrWireOpcode::ClearBuffer);
    EXPECT_EQ(clearBuffer.header.byteSize, sizeof(clearBuffer));
    EXPECT_EQ(clearBuffer.context.taskIndex, task.index);
    EXPECT_EQ(clearBuffer.destinationResourceIndex, destination.index);
    EXPECT_EQ(clearBuffer.clearValue, 0xdecafbadU);

    Graphics::GpuCommandIrClearTextureRecord clearTextureRecord;
    ASSERT_TRUE(ReadPOD(bytes, cursor, clearTextureRecord));
    EXPECT_EQ(clearTextureRecord.header.opcode, Graphics::GpuCommandIrWireOpcode::ClearTexture);
    EXPECT_EQ(clearTextureRecord.header.byteSize, sizeof(clearTextureRecord));
    EXPECT_EQ(clearTextureRecord.context.taskIndex, task.index);
    EXPECT_EQ(clearTextureRecord.context.packetIndex, packet.index);
    EXPECT_EQ(clearTextureRecord.context.queueIndex, queue.index);
    EXPECT_EQ(clearTextureRecord.context.queueDeviceGeneration, queue.deviceGeneration);
    EXPECT_EQ(clearTextureRecord.destinationResourceIndex, destination.index);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.baseMipLevel, clearTexture.subresources.baseMipLevel);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.numMipLevels, clearTexture.subresources.numMipLevels);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.baseArraySlice, clearTexture.subresources.baseArraySlice);
    EXPECT_EQ(clearTextureRecord.destinationSubresources.numArraySlices, clearTexture.subresources.numArraySlices);
    EXPECT_EQ(clearTextureRecord.floatClearValue.r, clearTexture.floatValue.r);
    EXPECT_EQ(clearTextureRecord.floatClearValue.g, clearTexture.floatValue.g);
    EXPECT_EQ(clearTextureRecord.floatClearValue.b, clearTexture.floatValue.b);
    EXPECT_EQ(clearTextureRecord.floatClearValue.a, clearTexture.floatValue.a);
    EXPECT_EQ(clearTextureRecord.uintClearValue.r, clearTexture.uintValue.r);
    EXPECT_EQ(clearTextureRecord.uintClearValue.g, clearTexture.uintValue.g);
    EXPECT_EQ(clearTextureRecord.uintClearValue.b, clearTexture.uintValue.b);
    EXPECT_EQ(clearTextureRecord.uintClearValue.a, clearTexture.uintValue.a);
    EXPECT_EQ(clearTextureRecord.intClearValue.r, clearTexture.intValue.r);
    EXPECT_EQ(clearTextureRecord.intClearValue.g, clearTexture.intValue.g);
    EXPECT_EQ(clearTextureRecord.intClearValue.b, clearTexture.intValue.b);
    EXPECT_EQ(clearTextureRecord.intClearValue.a, clearTexture.intValue.a);
    EXPECT_EQ(clearTextureRecord.depthClearValue, clearTexture.depthValue);
    EXPECT_EQ(clearTextureRecord.stencilClearValue, clearTexture.stencilValue);
    EXPECT_EQ(clearTextureRecord.clearTextureValueType, clearTexture.valueType);
    EXPECT_EQ(
        clearTextureRecord.clearFlags,
        static_cast<Graphics::GpuCommandIrClearTextureFlag::Mask>(
            Graphics::GpuCommandIrClearTextureFlag::ClearDepth | Graphics::GpuCommandIrClearTextureFlag::ClearStencil
        )
    );
    EXPECT_EQ(clearTextureRecord.reserved, 0u);
    EXPECT_EQ(cursor, bytes.size());

    Graphics::GraphicsBytes expectedPrefix(testArena.arena);
    expectedPrefix.resize(copyTextureEnd);
    NWB_MEMCPY(expectedPrefix.data(), expectedPrefix.size(), bytes.data(), expectedPrefix.size());
    capture.rollback(2u);
    const BinaryByteView rolledBackBytes = capture.commandBytes();
    EXPECT_EQ(capture.recordCount(), 2u);
    EXPECT_EQ(capture.graphGeneration(), task.generation);
    EXPECT_EQ(rolledBackBytes.size(), expectedPrefix.size());
    // Rollback rewrites the stream header's count/payload fields, while the surviving two POD records remain an
    // exact byte prefix of the original capture.
    EXPECT_EQ(
        NWB_MEMCMP(
            rolledBackBytes.data() + sizeof(Graphics::GpuCommandIrStreamHeader),
            expectedPrefix.data() + sizeof(Graphics::GpuCommandIrStreamHeader),
            expectedPrefix.size() - sizeof(Graphics::GpuCommandIrStreamHeader)
        ),
        0
    );

    cursor = 0u;
    ASSERT_TRUE(ReadPOD(rolledBackBytes, cursor, streamHeader));
    EXPECT_EQ(streamHeader.recordCount, 2u);
    EXPECT_EQ(
        streamHeader.payloadBytes,
        sizeof(Graphics::GpuCommandIrCopyBufferRecord) + sizeof(Graphics::GpuCommandIrCopyTextureRecord)
    );
}

TEST(GpuCommandIrStreamReader, DecodesTheCompleteBuiltinPodStream){
    TestArena testArena;
    Graphics::GpuCommandIrCapture emptyCapture(testArena.arena);
    Graphics::GpuCommandIrStreamReader emptyReader(emptyCapture.commandBytes());
    Graphics::GpuCommandIrBuiltinTaskRecord emptyOutput;
    emptyOutput.task = Graphics::GpuTaskId{ 99u, 98u };
    EXPECT_EQ(emptyReader.next(emptyOutput), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(emptyReader.validation().valid());
    EXPECT_EQ(emptyReader.graphGeneration(), 0u);
    EXPECT_EQ(emptyReader.recordCount(), 0u);
    EXPECT_EQ(emptyOutput.task.index, 99u);
    EXPECT_EQ(emptyOutput.task.generation, 98u);

    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView bytes = capture.commandBytes();

    const Graphics::GpuCommandIrStreamValidationResult completeValidation = Graphics::ValidateGpuCommandIrStream(bytes);
    EXPECT_TRUE(completeValidation.complete);
    EXPECT_TRUE(completeValidation.valid());
    EXPECT_FALSE(completeValidation.failed());
    EXPECT_EQ(completeValidation.byteOffset, bytes.size());
    EXPECT_EQ(completeValidation.recordIndex, 4u);

    Graphics::GpuCommandIrStreamReader reader(bytes);
    EXPECT_FALSE(reader.validation().complete);
    EXPECT_FALSE(reader.validation().valid());
    EXPECT_FALSE(reader.validation().failed());
    EXPECT_EQ(reader.graphGeneration(), s_CommandIrTask.generation);
    EXPECT_EQ(reader.recordCount(), 4u);

    Graphics::GpuCommandIrBuiltinTaskRecord record;
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::CopyBuffer);
    EXPECT_EQ(record.task, s_CommandIrTask);
    EXPECT_EQ(record.packet, s_CommandIrPacket);
    EXPECT_EQ(record.queue, s_CommandIrQueue);
    EXPECT_EQ(record.source, s_CommandIrSource);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.sourceOffsetBytes, 16u);
    EXPECT_EQ(record.destinationOffsetBytes, 32u);
    EXPECT_EQ(record.dataSizeBytes, 64u);

    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::CopyTexture);
    EXPECT_EQ(record.task.generation, s_CommandIrTask.generation);
    EXPECT_EQ(record.source, s_CommandIrSource);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.sourceSlice.x, 1u);
    EXPECT_EQ(record.sourceSlice.width, 4u);
    EXPECT_EQ(record.sourceSlice.mipLevel, 7u);
    EXPECT_EQ(record.sourceSlice.arraySlice, 8u);
    EXPECT_EQ(record.destinationSlice.x, 9u);
    EXPECT_EQ(record.destinationSlice.width, 12u);
    EXPECT_EQ(record.destinationSlice.mipLevel, 15u);
    EXPECT_EQ(record.destinationSlice.arraySlice, 16u);

    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.uintClearValue, Graphics::UIntColor(0xdecafbadU));

    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(record.destination, s_CommandIrDestination);
    EXPECT_EQ(record.destinationSubresources, Graphics::TextureSubresourceSet(2u, 3u, 4u, 5u));
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::DepthStencil);
    EXPECT_EQ(record.floatClearValue, Graphics::Color(0.25f, 0.5f, 0.75f, 1.f));
    EXPECT_EQ(record.uintClearValue, Graphics::UIntColor(2u, 3u, 5u, 7u));
    EXPECT_EQ(record.intClearValue, Graphics::IntColor(-2, -3, -5, -7));
    EXPECT_EQ(record.depthClearValue, 0.125f);
    EXPECT_EQ(record.stencilClearValue, 19u);
    EXPECT_TRUE(record.clearDepth);
    EXPECT_TRUE(record.clearStencil);

    EXPECT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(reader.validation().complete);
    EXPECT_TRUE(reader.validation().valid());
    EXPECT_EQ(reader.validation().byteOffset, bytes.size());
    EXPECT_EQ(reader.validation().recordIndex, 4u);
}

TEST(GpuCommandIrStreamReader, DecodesCanonicalColorAndSingleAspectClearRecords){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = s_CommandIrDestination;
    clearTexture.subresources = Graphics::TextureSubresourceSet(1u, 2u, 3u, 4u);
    // The legacy capture record keeps these descriptor values, while the POD stream must canonicalize them away for
    // color clears because native color-clear lowering ignores depth/stencil aspect selection.
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    clearTexture.floatValue = Graphics::Color(0.1f, 0.2f, 0.3f, 0.4f);
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    clearTexture.uintValue = Graphics::UIntColor(11u, 12u, 13u, 14u);
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::Int;
    clearTexture.intValue = Graphics::IntColor(-11, -12, -13, -14);
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));
    clearTexture.valueType = Graphics::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.depthValue = 0.75f;
    clearTexture.stencilValue = 23u;
    clearTexture.clearStencil = false;
    ASSERT_TRUE(capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture));

    Graphics::GpuCommandIrStreamReader reader(capture.commandBytes());
    Graphics::GpuCommandIrBuiltinTaskRecord record;
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::Float);
    EXPECT_EQ(record.floatClearValue, clearTexture.floatValue);
    EXPECT_FALSE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::UInt);
    EXPECT_EQ(record.uintClearValue, Graphics::UIntColor(11u, 12u, 13u, 14u));
    EXPECT_FALSE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::Int);
    EXPECT_EQ(record.intClearValue, Graphics::IntColor(-11, -12, -13, -14));
    EXPECT_FALSE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.clearTextureValueType, Graphics::GpuClearTextureTaskValueType::DepthStencil);
    EXPECT_EQ(record.depthClearValue, 0.75f);
    EXPECT_EQ(record.stencilClearValue, 23u);
    EXPECT_TRUE(record.clearDepth);
    EXPECT_FALSE(record.clearStencil);
    EXPECT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::End);
    EXPECT_TRUE(reader.validation().valid());
}

TEST(GpuCommandIrStreamReader, RejectsMalformedHeadersBeforeReadingRecords){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView validBytes = capture.commandBytes();

    const Graphics::GpuCommandIrStreamValidationResult nullData = Graphics::ValidateGpuCommandIrStream(
        BinaryByteView{ nullptr, 1u }
    );
    EXPECT_EQ(nullData.error, Graphics::GpuCommandIrStreamValidationError::NullData);
    EXPECT_TRUE(nullData.complete);
    EXPECT_FALSE(nullData.valid());
    EXPECT_EQ(nullData.recordIndex, Limit<u64>::s_Max);

    const Graphics::GpuCommandIrStreamValidationResult truncatedHeader = Graphics::ValidateGpuCommandIrStream(
        BinaryByteView{ validBytes.data(), sizeof(Graphics::GpuCommandIrStreamHeader) - 1u }
    );
    EXPECT_EQ(truncatedHeader.error, Graphics::GpuCommandIrStreamValidationError::TruncatedStreamHeader);
    EXPECT_TRUE(truncatedHeader.complete);
    EXPECT_FALSE(truncatedHeader.valid());

    const auto expectHeaderError = [&testArena, validBytes](
        const auto& mutate,
        const Graphics::GpuCommandIrStreamValidationError::Enum expectedError
    ){
        Graphics::GraphicsBytes corruptedBytes(testArena.arena);
        CopyCommandIrBytes(corruptedBytes, validBytes);
        mutate(corruptedBytes);
        const Graphics::GpuCommandIrStreamValidationResult result = Graphics::ValidateGpuCommandIrStream(
            BinaryByteView{ corruptedBytes.data(), corruptedBytes.size() }
        );
        EXPECT_EQ(result.error, expectedError);
        EXPECT_TRUE(result.complete);
        EXPECT_TRUE(result.failed());
        EXPECT_FALSE(result.valid());
        EXPECT_EQ(result.byteOffset, 0u);
        EXPECT_EQ(result.recordIndex, Limit<u64>::s_Max);
    };

    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, magic), 0u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidMagic);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, version), static_cast<u16>(99u));
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedVersion);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, reserved), static_cast<u16>(1u));
    }, Graphics::GpuCommandIrStreamValidationError::InvalidHeaderReserved);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, payloadBytes),
            static_cast<u64>(bytes.size())
        );
    }, Graphics::GpuCommandIrStreamValidationError::PayloadSizeMismatch);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, graphGeneration), 0u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidGraphGeneration);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 0u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidGraphGeneration);
    expectHeaderError([](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 64u);
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecordCount);
}

TEST(GpuCommandIrStreamReader, RejectsMalformedRecordsWithoutPublishingPartialOutput){
    TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(CaptureAllBuiltinCommandIrRecords(capture));
    const BinaryByteView validBytes = capture.commandBytes();

    const auto expectRecordError = [&testArena, validBytes](
        const usize validPrefixCount,
        const usize expectedByteOffset,
        const auto& mutate,
        const Graphics::GpuCommandIrStreamValidationError::Enum expectedError
    ){
        Graphics::GraphicsBytes corruptedBytes(testArena.arena);
        CopyCommandIrBytes(corruptedBytes, validBytes);
        mutate(corruptedBytes);

        Graphics::GpuCommandIrStreamReader reader(BinaryByteView{ corruptedBytes.data(), corruptedBytes.size() });
        Graphics::GpuCommandIrBuiltinTaskRecord output;
        for(usize recordIndex = 0u; recordIndex < validPrefixCount; ++recordIndex)
            ASSERT_EQ(reader.next(output), Graphics::GpuCommandIrStreamReadStatus::Record);

        output.opcode = Graphics::GpuCommandIrOpcode::ClearTexture;
        output.task = Graphics::GpuTaskId{ 91u, 92u };
        output.packet = Graphics::GpuSubmissionPacketId{ 93u, 92u };
        output.queue = Graphics::GpuPhysicalQueueId{ 94u, 95u };
        output.source = Graphics::GpuGraphResourceId{ 96u, 92u };
        output.destination = Graphics::GpuGraphResourceId{ 97u, 92u };
        output.dataSizeBytes = 98u;
        output.clearDepth = true;
        output.clearStencil = true;
        EXPECT_EQ(reader.next(output), Graphics::GpuCommandIrStreamReadStatus::Error);
        EXPECT_EQ(output.opcode, Graphics::GpuCommandIrOpcode::ClearTexture);
        EXPECT_EQ(output.task.index, 91u);
        EXPECT_EQ(output.task.generation, 92u);
        EXPECT_EQ(output.packet.index, 93u);
        EXPECT_EQ(output.packet.generation, 92u);
        EXPECT_EQ(output.queue.index, 94u);
        EXPECT_EQ(output.queue.deviceGeneration, 95u);
        EXPECT_EQ(output.source.index, 96u);
        EXPECT_EQ(output.source.generation, 92u);
        EXPECT_EQ(output.destination.index, 97u);
        EXPECT_EQ(output.destination.generation, 92u);
        EXPECT_EQ(output.dataSizeBytes, 98u);
        EXPECT_TRUE(output.clearDepth);
        EXPECT_TRUE(output.clearStencil);
        EXPECT_EQ(reader.validation().error, expectedError);
        EXPECT_TRUE(reader.validation().complete);
        EXPECT_TRUE(reader.validation().failed());
        EXPECT_EQ(reader.validation().byteOffset, expectedByteOffset);
        EXPECT_EQ(reader.validation().recordIndex, validPrefixCount);
        EXPECT_EQ(reader.next(output), Graphics::GpuCommandIrStreamReadStatus::Error);
    };

    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, byteSize),
            static_cast<u16>(3u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecordSize);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, byteSize),
            Limit<u16>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecordSize);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, opcode),
            Graphics::GpuCommandIrWireOpcode::SetGraphicsState
        );
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedOpcode);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrHeader, opcode),
            Graphics::GpuCommandIrWireOpcode::kCount
        );
    }, Graphics::GpuCommandIrStreamValidationError::UnsupportedOpcode);
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyBufferOffset + offsetof(Graphics::GpuCommandIrCopyBufferRecord, dataSizeBytes),
            0u
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyBufferOffset + offsetof(Graphics::GpuCommandIrCopyBufferRecord, sourceResourceIndex),
            Limit<u32>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(0u, s_CommandIrCopyBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyBufferOffset
                + offsetof(Graphics::GpuCommandIrCopyBufferRecord, context)
                + offsetof(Graphics::GpuCommandIrRecordContext, queueDeviceGeneration),
            static_cast<u16>(0u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrCopyTextureOffset + offsetof(Graphics::GpuCommandIrCopyTextureRecord, destinationResourceIndex),
            Limit<u32>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(2u, s_CommandIrClearBufferOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearBufferOffset + offsetof(Graphics::GpuCommandIrClearBufferRecord, destinationResourceIndex),
            Limit<u32>::s_Max
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset
                + offsetof(Graphics::GpuCommandIrClearTextureRecord, destinationSubresources)
                + offsetof(Graphics::GpuCommandIrTextureSubresourceSet, numMipLevels),
            0u
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearTextureValueType),
            static_cast<u8>(Graphics::GpuClearTextureTaskValueType::kCount)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearFlags),
            static_cast<Graphics::GpuCommandIrClearTextureFlag::Mask>(4u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearFlags),
            Graphics::GpuCommandIrClearTextureFlag::None
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearTextureValueType),
            static_cast<u8>(Graphics::GpuClearTextureTaskValueType::Float)
        );
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, clearFlags),
            Graphics::GpuCommandIrClearTextureFlag::ClearDepth
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(
            bytes,
            s_CommandIrClearTextureOffset + offsetof(Graphics::GpuCommandIrClearTextureRecord, reserved),
            static_cast<u8>(1u)
        );
    }, Graphics::GpuCommandIrStreamValidationError::InvalidRecord);
    expectRecordError(3u, s_CommandIrClearTextureOffset, [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 3u);
    }, Graphics::GpuCommandIrStreamValidationError::TrailingPayload);
    expectRecordError(4u, validBytes.size(), [](Graphics::GraphicsBytes& bytes){
        WriteCommandIrPod(bytes, offsetof(Graphics::GpuCommandIrStreamHeader, recordCount), 5u);
    }, Graphics::GpuCommandIrStreamValidationError::TruncatedRecord);
    expectRecordError(1u, s_CommandIrCopyTextureOffset, [](Graphics::GraphicsBytes& bytes){
        bytes.resize(s_CommandIrCopyTextureOffset + sizeof(Graphics::GpuCommandIrHeader));
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, recordCount),
            2u
        );
        WriteCommandIrPod(
            bytes,
            offsetof(Graphics::GpuCommandIrStreamHeader, payloadBytes),
            static_cast<u64>(bytes.size() - sizeof(Graphics::GpuCommandIrStreamHeader))
        );
    }, Graphics::GpuCommandIrStreamValidationError::TruncatedRecord);
}

TEST(GpuCommandIrReplay, PreflightsTheWholeStreamAgainstTheCompiledPacketBeforeLowering){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId source = AddBufferMetadata(
        graph,
        Name("tests/command_ir_replay/source"),
        "Replay Source"
    );
    const Graphics::GpuGraphResourceId destination = AddBufferMetadata(
        graph,
        Name("tests/command_ir_replay/destination"),
        "Replay Destination"
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(destination.valid());

    const Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = source,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = destination,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/command_ir_replay/copy"))
        .setMarkerLabel("Replay Copy")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setResourceUses(uses, LengthOf(uses))
    ;
    const Graphics::GpuTaskId task = graph.addTask(desc);
    ASSERT_TRUE(task.valid());
    const Graphics::GpuTaskId secondDependencies[] = { task };
    Graphics::GpuTaskDesc secondDesc = desc;
    secondDesc
        .setIdentity(Name("tests/command_ir_replay/copy_second"))
        .setMarkerLabel("Replay Copy Second")
        .setDependencies(secondDependencies, LengthOf(secondDependencies))
    ;
    const Graphics::GpuTaskId secondTask = graph.addTask(secondDesc);
    ASSERT_TRUE(secondTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledGraph.packetForTask(secondTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(secondPacket, packet);
    const Graphics::GpuPhysicalQueueId queue = compiledGraph.packet(packet).queue;
    ASSERT_EQ(compiledGraph.packet(secondPacket).queue, queue);

    Graphics::GpuCommandIrCapture capture(testArena.arena);
    ASSERT_TRUE(capture.captureCopyBuffer(task, packet, queue, source, 0u, destination, 0u, 4u));
    ASSERT_TRUE(capture.captureCopyBuffer(secondTask, secondPacket, queue, source, 0u, destination, 0u, 4u));
    ASSERT_EQ(capture.recordCount(), 2u);
    const Graphics::GpuCommandIrReplayResult validContext = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        graph,
        compiledGraph,
        packet
    );
    // Metadata-only imports cannot be lowered, but preflight has already established the stream, graph,
    // packet, queue, task order, resource kinds, and declared CopySource/CopyDest uses before that boundary.
    EXPECT_EQ(validContext.error, Graphics::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(validContext.streamValidation.valid());
    EXPECT_EQ(validContext.recordIndex, 0u);

    // A normal capture concatenates packet bodies. Selecting the second packet must skip the first record rather
    // than rejecting the full frame artifact before its requested packet is reached.
    const Graphics::GpuCommandIrReplayResult secondPacketContext = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        graph,
        compiledGraph,
        secondPacket
    );
    EXPECT_EQ(secondPacketContext.error, Graphics::GpuCommandIrReplayError::MissingBackendResource);
    EXPECT_TRUE(secondPacketContext.streamValidation.valid());
    EXPECT_EQ(secondPacketContext.recordIndex, 1u);

    const Graphics::GpuCommandIrReplayResult invalidPacket = Graphics::PreflightGpuCommandIrPacket(
        capture.commandBytes(),
        graph,
        compiledGraph,
        Graphics::GpuSubmissionPacketId{ Limit<u32>::s_Max - 1u, packet.generation }
    );
    EXPECT_EQ(invalidPacket.error, Graphics::GpuCommandIrReplayError::InvalidPacket);
    EXPECT_TRUE(invalidPacket.streamValidation.valid());

    Graphics::GpuCommandIrCapture wrongQueueCapture(testArena.arena);
    ASSERT_TRUE(wrongQueueCapture.captureCopyBuffer(
        task,
        packet,
        Graphics::GpuPhysicalQueueId{ static_cast<u16>(queue.index + 1u), queue.deviceGeneration },
        source,
        0u,
        destination,
        0u,
        4u
    ));
    const Graphics::GpuCommandIrReplayResult wrongQueue = Graphics::PreflightGpuCommandIrPacket(
        wrongQueueCapture.commandBytes(),
        graph,
        compiledGraph,
        packet
    );
    EXPECT_EQ(wrongQueue.error, Graphics::GpuCommandIrReplayError::RecordQueueMismatch);
    EXPECT_TRUE(wrongQueue.streamValidation.valid());
    EXPECT_EQ(wrongQueue.recordIndex, 0u);

    const Graphics::GpuCommandIrReplayResult malformed = Graphics::PreflightGpuCommandIrPacket(
        BinaryByteView{},
        graph,
        compiledGraph,
        packet
    );
    EXPECT_EQ(malformed.error, Graphics::GpuCommandIrReplayError::InvalidStream);
    EXPECT_EQ(malformed.streamValidation.error, Graphics::GpuCommandIrStreamValidationError::TruncatedStreamHeader);
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

TEST(GpuTaskGraph, RoutesOptedInWorkAcrossSameClassPhysicalQueues){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/same_class_buffer"),
        "Same Class Buffer"
    );
    ASSERT_TRUE(buffer.valid());

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
    producerScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskResourceUse producerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopyDest,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/same_class_producer"))
        .setMarkerLabel("Same Class Producer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setResourceUses(&producerUse, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint consumerScheduling;
    consumerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    consumerScheduling.allowSameClassQueueRouting = true;
    const Graphics::GpuTaskId consumerDependencies[] = { producer };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = buffer,
        .range = {},
        .requiredState = Graphics::ResourceStates::CopySource,
        .access = Graphics::GpuTaskResourceAccess::Read,
    };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/same_class_consumer"))
        .setMarkerLabel("Same Class Consumer")
        .setQueue(graphicsRequest)
        .setScheduling(consumerScheduling)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
        .setResourceUses(&consumerUse, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    Graphics::GpuPhysicalQueueInfo secondaryGraphicsQueue = GraphicsQueue(1u);
    secondaryGraphicsQueue.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        secondaryGraphicsQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, queues[0u].id);
    EXPECT_EQ(consumerAssignment->queue, queues[1u].id);
    EXPECT_EQ(consumerAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::SameClassRouting);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledGraph.packetForTask(consumer);
    const Graphics::GpuCompiledTask* const compiledProducer = compiledGraph.findTask(producer);
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledGraph.findTask(consumer);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(consumerPacket.valid());
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_NE(compiledGraph.packet(producerPacket).queue, compiledGraph.packet(consumerPacket).queue);
    ASSERT_EQ(compiledGraph.packet(consumerPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(consumerPacket)[0u].producer, producerPacket);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    EXPECT_EQ(
        compiledGraph.taskPrologueBarriers(consumer)[0u].type,
        Graphics::GpuCompiledBarrierType::BufferTransition
    );
}


TEST(GpuTaskGraph, ExportsRequiredImportedResourceFinalStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc textureDesc;
    textureDesc
        .setIdentity(Name("tests/task_graph/external_final_texture"))
        .setMarkerLabel("External Final Texture")
        .setType(Graphics::GpuGraphResourceType::Texture)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    const Graphics::GpuGraphResourceId texture = graph.importResource(textureDesc);
    ASSERT_TRUE(texture.valid());

    Graphics::GpuGraphResourceDesc bufferDesc;
    bufferDesc
        .setIdentity(Name("tests/task_graph/external_final_buffer"))
        .setMarkerLabel("External Final Buffer")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    const Graphics::GpuGraphResourceId buffer = graph.importResource(bufferDesc);
    ASSERT_TRUE(buffer.valid());
    EXPECT_EQ(graph.resourceAt(texture.index).externalFinalState, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(graph.resourceAt(buffer.index).externalFinalState, Graphics::ResourceStates::ShaderResource);

    const Graphics::GpuTaskResourceUse uses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = buffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/external_final_writer"),
        "External Final Writer",
        nullptr,
        0u,
        uses,
        LengthOf(uses)
    );
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
    ASSERT_NE(compiledTask, nullptr);
    ASSERT_EQ(compiledTask->epilogueBarrierCount, 2u);
    const Graphics::GpuCompiledBarrier* const barriers = compiledGraph.taskEpilogueBarriers(task);
    ASSERT_NE(barriers, nullptr);
    bool exportedTexture = false;
    bool exportedBuffer = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
        exportedTexture = exportedTexture || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureStateExport
            && barrier.resource == texture
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && barrier.sourceQueue == compiledTask->queue
            && barrier.destinationQueue == compiledTask->queue
        );
        exportedBuffer = exportedBuffer || (
            barrier.type == Graphics::GpuCompiledBarrierType::BufferStateExport
            && barrier.resource == buffer
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
            && barrier.sourceQueue == compiledTask->queue
            && barrier.destinationQueue == compiledTask->queue
        );
    }
    EXPECT_TRUE(exportedTexture);
    EXPECT_TRUE(exportedBuffer);
}


TEST(GpuTaskGraph, RejectsUnpublishableExternalFinalStateContracts){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuGraphResourceDesc unsupportedDesc;
    unsupportedDesc
        .setIdentity(Name("tests/task_graph/external_final_accel_struct"))
        .setMarkerLabel("External Final Accel Struct")
        .setType(Graphics::GpuGraphResourceType::AccelStruct)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    EXPECT_FALSE(graph.importResource(unsupportedDesc).valid());

    Graphics::GpuGraphResourceDesc untouchedDesc;
    untouchedDesc
        .setIdentity(Name("tests/task_graph/external_final_untouched"))
        .setMarkerLabel("External Final Untouched")
        .setType(Graphics::GpuGraphResourceType::Buffer)
        .setInitialState(Graphics::ResourceStates::Common)
        .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
    ;
    EXPECT_TRUE(graph.importResource(untouchedDesc).valid());
    ASSERT_TRUE(AddTask(
        graph,
        Name("tests/task_graph/external_final_unrelated"),
        "External Final Unrelated"
    ).valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_FALSE(compiledGraph.valid());
}


TEST(GpuTaskGraph, ValidatesInitialExclusiveOwnerBeforeFirstUse){
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Graphics::GpuQueueRequest graphicsQueue{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeQueue{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const auto addFirstUse = [&](
        Graphics::GpuTaskGraph& graph,
        const Graphics::GpuGraphResourceId resource,
        const Name& identity,
        const AStringView label,
        const Graphics::GpuQueueRequest& queue
    ){
        const Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queue)
            .setResourceUses(&use, 1u)
        ;
        return graph.addTask(desc);
    };
    const auto addBuffer = [&](
        Graphics::GpuTaskGraph& graph,
        const Name& identity,
        const AStringView label,
        const Graphics::GpuPhysicalQueueId owner,
        const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Exclusive
    ){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Common)
            .setInitialOwnerQueue(owner)
            .setQueueSharing(queueSharing)
        ;
        return graph.importResource(desc);
    };

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_graphics"),
            "Initial Owner Graphics",
            queues[0u].id
        );
        ASSERT_TRUE(resource.valid());
        EXPECT_EQ(graph.resourceAt(resource.index).initialOwnerQueue, queues[0u].id);
        const Graphics::GpuTaskId task = addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_graphics_use"),
            "Initial Owner Graphics Use",
            graphicsQueue
        );
        ASSERT_TRUE(task.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
        const Graphics::GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
        ASSERT_NE(compiledTask, nullptr);
        EXPECT_EQ(compiledTask->queue, queues[0u].id);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_cross_queue"),
            "Initial Owner Cross Queue",
            queues[0u].id
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_compute_use"),
            "Initial Owner Compute Use",
            computeQueue
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
        EXPECT_FALSE(compiledGraph.valid());
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_stale"),
            "Initial Owner Stale",
            Graphics::GpuPhysicalQueueId{ queues[0u].id.index, static_cast<u16>(queues[0u].id.deviceGeneration + 1u) }
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_stale_use"),
            "Initial Owner Stale Use",
            graphicsQueue
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
        EXPECT_FALSE(compiledGraph.valid());
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = addBuffer(
            graph,
            Name("tests/task_graph/initial_owner_concurrent"),
            "Initial Owner Concurrent",
            queues[0u].id,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(addFirstUse(
            graph,
            resource,
            Name("tests/task_graph/initial_owner_concurrent_use"),
            "Initial Owner Concurrent Use",
            graphicsQueue
        ).valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
        EXPECT_FALSE(compiledGraph.valid());
    }
}

TEST(GpuTaskGraph, RecreatesPacketRecordingStateAfterRecompile){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);

    const Graphics::GpuTaskId firstTask = AddTask(
        graph,
        Name("tests/task_graph/recreate_recording_first"),
        "Recreate Recording First"
    );
    ASSERT_TRUE(firstTask.valid());
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(firstTask);
    ASSERT_TRUE(firstPacket.valid());

    Graphics::GpuRecordedGraph recordedGraph(testArena.arena);
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    ASSERT_TRUE(recordedGraph.validFor(compiledGraph));
    ASSERT_TRUE(transaction.validFor(compiledGraph));
    const u64 firstCompiledGeneration = compiledGraph.generation();

    graph.reset();
    const Graphics::GpuTaskId secondTask = AddTask(
        graph,
        Name("tests/task_graph/recreate_recording_second"),
        "Recreate Recording Second"
    );
    ASSERT_TRUE(secondTask.valid());
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_NE(compiledGraph.generation(), firstCompiledGeneration);
    EXPECT_FALSE(recordedGraph.validFor(compiledGraph));
    EXPECT_FALSE(transaction.validFor(compiledGraph));

    // reset() releases old packet-owned command-list handles and reconstructs the serial/per-packet recording
    // scratch for the new immutable graph generation before a future command arena can be leased again.
    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    EXPECT_TRUE(recordedGraph.validFor(compiledGraph));
    EXPECT_TRUE(transaction.validFor(compiledGraph));
    EXPECT_EQ(transaction.packetRuntime(firstPacket), nullptr);
    EXPECT_NE(transaction.packetRuntime(compiledGraph.packetForTask(secondTask)), nullptr);
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

    Graphics::GpuPhysicalQueueInfo invalidTransferQueue = DedicatedTransferQueue();
    invalidTransferQueue.capabilities = Graphics::GpuQueueCapability::Compute;
    const Graphics::GpuTaskGraphQueueTopology invalidTransferTopology{
        .queues = &invalidTransferQueue,
        .queueCount = 1u,
    };
    EXPECT_FALSE(Assign(graph, analysis, invalidTransferTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    Graphics::GpuPhysicalQueueInfo duplicateNativeQueue = GraphicsQueue(3u);
    // Different graph IDs must not alias the same Vulkan family/index transport.
    duplicateNativeQueue.queueIndex = GraphicsQueue().queueIndex;
    const Graphics::GpuPhysicalQueueInfo duplicateNativeQueues[] = {
        GraphicsQueue(),
        duplicateNativeQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology duplicateNativeTopology{
        .queues = duplicateNativeQueues,
        .queueCount = LengthOf(duplicateNativeQueues),
    };
    EXPECT_FALSE(Assign(graph, analysis, duplicateNativeTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

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

TEST(GpuTaskGraph, ExportsInferredEvidenceAndQueueAssignments){
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
        .queueAssignments = &assignments,
    };
    ASSERT_TRUE(graph.appendFrameGraphTelemetry(builder, analysis, scratchArena, telemetryOptions));

    const u8 expectedFlags =
        Graphics::GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency
        | Graphics::GpuTaskGraphTelemetryEdgeFlag::InferredDependency
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

    Graphics::GpuTaskDesc transferDesc;
    transferDesc
        .setIdentity(Name("tests/task_graph/packet_transfer"))
        .setMarkerLabel("Packet Transfer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            false,
            false,
        })
    ;
    const Graphics::GpuTaskId transfer = graph.addTask<PacketLifecycleTask>(
        transferDesc,
        PacketLifecycleTask::Payload{ &acceptedCount, &discardedCount, &acceptedToken }
    );
    ASSERT_TRUE(transfer.valid());

    const Graphics::GpuGraphResourceId recoveryDomain = AddHazardDomain(
        graph,
        Name("tests/task_graph/packet_recovery_domain"),
        "Frame Recovery Timing"
    );
    ASSERT_TRUE(recoveryDomain.valid());
    const Graphics::GpuTaskResourceUse recoveryUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = recoveryDomain,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskSchedulingHint recoveryScheduling;
    recoveryScheduling.forceSubmissionBoundary = true;
    recoveryScheduling.allowPacketMerge = false;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    Graphics::GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("tests/task_graph/packet_recovery"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(recoveryScheduling)
        .setResourceUses(recoveryUses, LengthOf(recoveryUses))
    ;
    const Graphics::GpuTaskId recovery = graph.addTask<PacketLifecycleTask>(
        recoveryDesc,
        PacketLifecycleTask::Payload{ &acceptedCount, &discardedCount, &acceptedToken }
    );
    ASSERT_TRUE(recovery.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_TRUE(compiledGraph.validFor(graph));
    ASSERT_EQ(compiledGraph.taskCount(), 4u);
    ASSERT_EQ(compiledGraph.packetCount(), 4u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(first);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledGraph.packetForTask(second);
    const Graphics::GpuSubmissionPacketId transferPacket = compiledGraph.packetForTask(transfer);
    const Graphics::GpuSubmissionPacketId recoveryPacket = compiledGraph.packetForTask(recovery);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(transferPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    EXPECT_NE(firstPacket, secondPacket);
    EXPECT_NE(recoveryPacket, secondPacket);
    const Graphics::GpuSubmissionPacketRange firstTwoPacketRange = compiledGraph.packetRange(
        firstPacket,
        secondPacket
    );
    ASSERT_TRUE(firstTwoPacketRange.valid());
    EXPECT_TRUE(compiledGraph.validPacketRange(firstTwoPacketRange));
    EXPECT_EQ(firstTwoPacketRange.first, firstPacket);
    EXPECT_EQ(firstTwoPacketRange.packetCount, 2u);
    const Graphics::GpuSubmissionPacketRange fullPacketRange = compiledGraph.allPacketRange();
    ASSERT_TRUE(fullPacketRange.valid());
    EXPECT_TRUE(compiledGraph.validPacketRange(fullPacketRange));
    EXPECT_EQ(fullPacketRange.first, firstPacket);
    EXPECT_EQ(fullPacketRange.packetCount, compiledGraph.packetCount());
    EXPECT_FALSE(compiledGraph.packetRange(secondPacket, firstPacket).valid());
    EXPECT_FALSE(compiledGraph.validPacketRange(Graphics::GpuSubmissionPacketRange{
        .first = firstPacket,
        .packetCount = compiledGraph.packetCount() + 1u,
    }));
    const auto& secondPacketPlan = compiledGraph.packet(secondPacket);
    ASSERT_EQ(secondPacketPlan.taskCount, 1u);
    ASSERT_EQ(secondPacketPlan.dependencyCount, 1u);
    ASSERT_EQ(secondPacketPlan.externalDependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(secondPacket)[0].producer, firstPacket);
    EXPECT_EQ(compiledGraph.packetExternalDependencies(secondPacket)[0], completion);
    const auto& recoveryPacketPlan = compiledGraph.packet(recoveryPacket);
    EXPECT_EQ(recoveryPacketPlan.dependencyCount, 0u);
    EXPECT_EQ(recoveryPacketPlan.externalDependencyCount, 0u);
    EXPECT_TRUE(recoveryPacketPlan.joinsAcceptedQueueFrontier);

    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    transaction.reset(compiledGraph);
    ASSERT_TRUE(transaction.markPacketRecorded(firstPacket));
    const Graphics::GpuPhysicalQueueId firstQueue = compiledGraph.packet(firstPacket).queue;
    const Graphics::GpuPhysicalQueueId recoveryQueue = compiledGraph.packet(recoveryPacket).queue;
    const Graphics::QueueSubmissionToken firstToken{
        .queue = Graphics::CommandQueue::Compute,
        .value = 41u,
        .physicalQueueIndex = firstQueue.index,
        .deviceGeneration = firstQueue.deviceGeneration,
    };
    const Graphics::GpuTaskGraphExternalCompletionToken externalCompletionToken{
        .completion = completion,
        .token = firstToken,
    };
    EXPECT_TRUE(externalCompletionToken.validFor(compiledGraph));
    // An external completion can originate on a valid current-device queue that this compiled graph does not use.
    // Concrete queue existence/identity is checked later by the submitting Device, not by this graph-local binding.
    const Graphics::GpuTaskGraphExternalCompletionToken inactiveQueueCompletionToken{
        .completion = completion,
        .token = Graphics::QueueSubmissionToken{
            .queue = Graphics::CommandQueue::Transfer,
            .value = 40u,
            .physicalQueueIndex = 2u,
            .deviceGeneration = compiledGraph.deviceGeneration(),
        },
    };
    EXPECT_TRUE(inactiveQueueCompletionToken.validFor(compiledGraph));
    Graphics::GpuTaskGraphExternalCompletionToken staleExternalCompletionToken = externalCompletionToken;
    staleExternalCompletionToken.token.deviceGeneration = firstQueue.deviceGeneration == Limit<u16>::s_Max
        ? 1u
        : static_cast<u16>(firstQueue.deviceGeneration + 1u)
    ;
    EXPECT_FALSE(staleExternalCompletionToken.validFor(compiledGraph));
    Graphics::QueueSubmissionToken mismatchedFirstToken = firstToken;
    mismatchedFirstToken.physicalQueueIndex = 0u;
    if(mismatchedFirstToken.physicalQueueIndex == firstQueue.index)
        mismatchedFirstToken.physicalQueueIndex = 1u;
    EXPECT_FALSE(transaction.acceptPacket(graph, compiledGraph, firstPacket, mismatchedFirstToken));
    ASSERT_TRUE(transaction.acceptPacket(graph, compiledGraph, firstPacket, firstToken));
    EXPECT_EQ(acceptedCount, 1u);
    EXPECT_EQ(discardedCount, 0u);
    EXPECT_EQ(acceptedToken.queue, firstToken.queue);
    EXPECT_EQ(acceptedToken.value, firstToken.value);
    ASSERT_NE(transaction.latestAcceptedToken(compiledGraph.packet(firstPacket).queue), nullptr);
    EXPECT_EQ(transaction.latestAcceptedToken(compiledGraph.packet(firstPacket).queue)->value, firstToken.value);
    EXPECT_EQ(transaction.latestAcceptedToken(recoveryQueue), nullptr);

    ASSERT_TRUE(transaction.markPacketRecorded(transferPacket));
    const Graphics::GpuPhysicalQueueId transferQueue = compiledGraph.packet(transferPacket).queue;
    const Graphics::QueueSubmissionToken transferToken{
        .queue = Graphics::CommandQueue::Transfer,
        .value = 42u,
        .physicalQueueIndex = transferQueue.index,
        .deviceGeneration = transferQueue.deviceGeneration,
    };
    ASSERT_TRUE(transaction.acceptPacket(graph, compiledGraph, transferPacket, transferToken));
    EXPECT_EQ(acceptedCount, 2u);
    ASSERT_NE(transaction.latestAcceptedToken(transferQueue), nullptr);
    EXPECT_EQ(transaction.latestAcceptedToken(transferQueue)->value, transferToken.value);

    // A later packet may reject while the independent recovery tail remains Declared. The accepted producer stays
    // visible, then recovery can still accept before the normal blanket cleanup rejects any remaining packet.
    transaction.rejectPacket(graph, compiledGraph, secondPacket);
    EXPECT_EQ(acceptedCount, 2u);
    EXPECT_EQ(discardedCount, 1u);
    ASSERT_NE(transaction.packetRuntime(secondPacket), nullptr);
    EXPECT_EQ(
        transaction.packetRuntime(secondPacket)->state,
        Graphics::GpuPacketRuntimeState::Rejected
    );
    ASSERT_NE(transaction.packetRuntime(recoveryPacket), nullptr);
    EXPECT_EQ(
        transaction.packetRuntime(recoveryPacket)->state,
        Graphics::GpuPacketRuntimeState::Declared
    );

    Core::Alloc::ScratchArena recoveryScratchArena(s_TaskGraphScratchArena);
    Vector<Graphics::QueueSubmissionToken, Core::Alloc::ScratchArena> recoveryWaitTokens(recoveryScratchArena);
    ASSERT_TRUE(transaction.appendAcceptedQueueFrontierWaitTokens(recoveryQueue, recoveryWaitTokens));
    ASSERT_EQ(recoveryWaitTokens.size(), 2u);
    EXPECT_EQ(recoveryWaitTokens[0u].value, firstToken.value);
    EXPECT_EQ(recoveryWaitTokens[0u].physicalQueueIndex, firstQueue.index);
    EXPECT_EQ(recoveryWaitTokens[1u].value, transferToken.value);
    EXPECT_EQ(recoveryWaitTokens[1u].physicalQueueIndex, transferQueue.index);

    ASSERT_TRUE(transaction.markPacketRecorded(recoveryPacket));
    const Graphics::QueueSubmissionToken recoveryToken{
        .queue = Graphics::CommandQueue::Graphics,
        .value = 43u,
        .physicalQueueIndex = recoveryQueue.index,
        .deviceGeneration = recoveryQueue.deviceGeneration,
    };
    ASSERT_TRUE(transaction.acceptPacket(graph, compiledGraph, recoveryPacket, recoveryToken));
    EXPECT_EQ(acceptedCount, 3u);
    EXPECT_EQ(discardedCount, 1u);
    EXPECT_EQ(acceptedToken.queue, recoveryToken.queue);
    EXPECT_EQ(acceptedToken.value, recoveryToken.value);
    ASSERT_NE(transaction.latestAcceptedToken(recoveryQueue), nullptr);
    EXPECT_EQ(transaction.latestAcceptedToken(recoveryQueue)->value, recoveryToken.value);

    transaction.discardUnaccepted(graph, compiledGraph);
    EXPECT_EQ(acceptedCount, 3u);
    EXPECT_EQ(discardedCount, 1u);
    ASSERT_NE(transaction.packetRuntime(recoveryPacket), nullptr);
    EXPECT_EQ(
        transaction.packetRuntime(recoveryPacket)->state,
        Graphics::GpuPacketRuntimeState::Accepted
    );
}


TEST(GpuTaskGraph, DerivesStableRecordingReadyFrontiersFromPacketDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuTaskId first = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_first"),
        "Recording Frontier First"
    );
    const Graphics::GpuTaskId second = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_second"),
        "Recording Frontier Second"
    );
    const Graphics::GpuTaskId thirdDependencies[] = { first };
    const Graphics::GpuTaskId third = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_third"),
        "Recording Frontier Third",
        thirdDependencies,
        LengthOf(thirdDependencies)
    );
    const Graphics::GpuTaskId fourthDependencies[] = { second, third };
    const Graphics::GpuTaskId fourth = AddTask(
        graph,
        Name("tests/task_graph/recording_frontier_fourth"),
        "Recording Frontier Fourth",
        fourthDependencies,
        LengthOf(fourthDependencies)
    );
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(third.valid());
    ASSERT_TRUE(fourth.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(first);
    const Graphics::GpuSubmissionPacketId secondPacket = compiledGraph.packetForTask(second);
    const Graphics::GpuSubmissionPacketId thirdPacket = compiledGraph.packetForTask(third);
    const Graphics::GpuSubmissionPacketId fourthPacket = compiledGraph.packetForTask(fourth);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_TRUE(thirdPacket.valid());
    ASSERT_TRUE(fourthPacket.valid());
    EXPECT_EQ(compiledGraph.packet(firstPacket).recordingFrontier, 0u);
    EXPECT_EQ(compiledGraph.packet(secondPacket).recordingFrontier, 0u);
    EXPECT_EQ(compiledGraph.packet(thirdPacket).recordingFrontier, 1u);
    EXPECT_EQ(compiledGraph.packet(fourthPacket).recordingFrontier, 2u);
    ASSERT_EQ(compiledGraph.packet(thirdPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(thirdPacket)[0u].producer, firstPacket);
    ASSERT_EQ(compiledGraph.packet(fourthPacket).dependencyCount, 2u);
    bool hasSecondProducer = false;
    bool hasThirdProducer = false;
    for(const Graphics::GpuPacketDependency& dependency : {
        compiledGraph.packetDependencies(fourthPacket)[0u],
        compiledGraph.packetDependencies(fourthPacket)[1u],
    }){
        hasSecondProducer = hasSecondProducer || dependency.producer == secondPacket;
        hasThirdProducer = hasThirdProducer || dependency.producer == thirdPacket;
    }
    EXPECT_TRUE(hasSecondProducer);
    EXPECT_TRUE(hasThirdProducer);
}


TEST(GpuTaskGraph, KeepsPresentationOverlayInDistinctTerminalGraphicsPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId backbuffer = AddHazardDomain(
        graph,
        Name("tests/task_graph/presentation_backbuffer"),
        "Presentation Back Buffer"
    );
    ASSERT_TRUE(backbuffer.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.overlapPreferred = false;
    scheduling.avoidQueueCrossing = true;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse backbufferWrite[] = {
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::Present,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };

    Graphics::GpuTaskDesc sceneOutputDesc;
    sceneOutputDesc
        .setIdentity(Name("tests/task_graph/scene_output"))
        .setMarkerLabel("Scene Output")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(backbufferWrite, LengthOf(backbufferWrite))
    ;
    const Graphics::GpuTaskId sceneOutput = graph.addTask(sceneOutputDesc);
    ASSERT_TRUE(sceneOutput.valid());

    Graphics::GpuTaskDesc overlayDesc;
    overlayDesc
        .setIdentity(Name("tests/task_graph/presentation_overlay"))
        .setMarkerLabel("Presentation Overlay")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&sceneOutput, 1u)
        .setResourceUses(backbufferWrite, LengthOf(backbufferWrite))
    ;
    const Graphics::GpuTaskId overlay = graph.addTask(overlayDesc);
    ASSERT_TRUE(overlay.valid());

    // A diagnostic/history tail need not depend on the backbuffer, but declaration order keeps it outside the
    // terminal presentation span. This lets the renderer signal presentation from the overlay packet while later
    // graph-owned maintenance work continues independently.
    Graphics::GpuTaskDesc lateTailDesc;
    lateTailDesc
        .setIdentity(Name("tests/task_graph/presentation_late_tail"))
        .setMarkerLabel("Presentation Late Tail")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
    ;
    const Graphics::GpuTaskId lateTail = graph.addTask(lateTailDesc);
    ASSERT_TRUE(lateTail.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuSubmissionPacketId sceneOutputPacket = compiledGraph.packetForTask(sceneOutput);
    const Graphics::GpuSubmissionPacketId overlayPacket = compiledGraph.packetForTask(overlay);
    const Graphics::GpuSubmissionPacketId lateTailPacket = compiledGraph.packetForTask(lateTail);
    ASSERT_TRUE(sceneOutputPacket.valid());
    ASSERT_TRUE(overlayPacket.valid());
    ASSERT_TRUE(lateTailPacket.valid());
    EXPECT_NE(sceneOutputPacket, overlayPacket);
    EXPECT_GT(lateTailPacket.index, overlayPacket.index);
    EXPECT_EQ(compiledGraph.packet(overlayPacket).queue, queues[0].id);
    const Graphics::GpuSubmissionPacketRange presentationRange = compiledGraph.packetRange(
        sceneOutputPacket,
        overlayPacket
    );
    ASSERT_TRUE(presentationRange.valid());
    EXPECT_EQ(presentationRange.packetCount, 2u);
    const auto& overlayPlan = compiledGraph.packet(overlayPacket);
    ASSERT_EQ(overlayPlan.taskCount, 1u);
    ASSERT_EQ(overlayPlan.dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(overlayPacket)[0].producer, sceneOutputPacket);
}

TEST(GpuTaskGraph, RoutesGraphOwnedSetupUploadsThroughTerminalPresentationSpan){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId backbuffer = AddHazardDomain(
        graph,
        Name("tests/task_graph/setup_upload_backbuffer"),
        "Setup Upload Back Buffer"
    );
    const Graphics::GpuGraphResourceId vertices = AddBufferMetadata(
        graph,
        Name("tests/task_graph/setup_upload_vertices"),
        "Setup Upload Vertices"
    );
    const Graphics::GpuGraphResourceId indices = AddBufferMetadata(
        graph,
        Name("tests/task_graph/setup_upload_indices"),
        "Setup Upload Indices"
    );
    const Graphics::GpuGraphResourceId fontTexture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/setup_upload_font"),
        "Setup Upload Font",
        Graphics::ResourceStates::ShaderResource
    );
    ASSERT_TRUE(backbuffer.valid());
    ASSERT_TRUE(vertices.valid());
    ASSERT_TRUE(indices.valid());
    ASSERT_TRUE(fontTexture.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest uploadRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Small;
    graphicsScheduling.avoidQueueCrossing = true;
    graphicsScheduling.forceSubmissionBoundary = true;
    graphicsScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint smallUploadScheduling;
    smallUploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    smallUploadScheduling.avoidQueueCrossing = true;
    smallUploadScheduling.forceSubmissionBoundary = true;
    smallUploadScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint largeUploadScheduling;
    largeUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    largeUploadScheduling.overlapPreferred = true;
    largeUploadScheduling.forceSubmissionBoundary = true;
    largeUploadScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse sceneUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::Present,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc sceneDesc;
    sceneDesc
        .setIdentity(Name("tests/task_graph/setup_upload_scene"))
        .setMarkerLabel("Setup Upload Scene")
        .setQueue(graphicsRequest)
        .setScheduling(graphicsScheduling)
        .setResourceUses(sceneUses, LengthOf(sceneUses))
    ;
    const Graphics::GpuTaskId scene = graph.addTask(sceneDesc);
    ASSERT_TRUE(scene.valid());

    const Graphics::GpuTaskId sceneDependencies[] = { scene };
    const Graphics::GpuTaskResourceUse vertexUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = vertices,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc vertexUploadDesc;
    vertexUploadDesc
        .setIdentity(Name("tests/task_graph/setup_upload_vertices_task"))
        .setMarkerLabel("Setup Upload Vertices")
        .setQueue(uploadRequest)
        .setScheduling(smallUploadScheduling)
        .setDependencies(sceneDependencies, LengthOf(sceneDependencies))
        .setResourceUses(vertexUploadUses, LengthOf(vertexUploadUses))
    ;
    const Graphics::GpuTaskId vertexUpload = graph.addTask(vertexUploadDesc);
    ASSERT_TRUE(vertexUpload.valid());

    const Graphics::GpuTaskResourceUse indexUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = indices,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc indexUploadDesc;
    indexUploadDesc
        .setIdentity(Name("tests/task_graph/setup_upload_indices_task"))
        .setMarkerLabel("Setup Upload Indices")
        .setQueue(uploadRequest)
        .setScheduling(smallUploadScheduling)
        .setDependencies(sceneDependencies, LengthOf(sceneDependencies))
        .setResourceUses(indexUploadUses, LengthOf(indexUploadUses))
    ;
    const Graphics::GpuTaskId indexUpload = graph.addTask(indexUploadDesc);
    ASSERT_TRUE(indexUpload.valid());

    const Graphics::GpuTaskResourceUse fontUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = fontTexture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc fontUploadDesc;
    fontUploadDesc
        .setIdentity(Name("tests/task_graph/setup_upload_font_task"))
        .setMarkerLabel("Setup Upload Font")
        .setQueue(uploadRequest)
        .setScheduling(largeUploadScheduling)
        .setDependencies(sceneDependencies, LengthOf(sceneDependencies))
        .setResourceUses(fontUploadUses, LengthOf(fontUploadUses))
    ;
    const Graphics::GpuTaskId fontUpload = graph.addTask(fontUploadDesc);
    ASSERT_TRUE(fontUpload.valid());

    const Graphics::GpuTaskId overlayDependencies[] = {
        scene,
        vertexUpload,
        indexUpload,
        fontUpload,
    };
    const Graphics::GpuTaskResourceUse overlayUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::Present,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = vertices,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = indices,
            .range = {},
            .requiredState = Graphics::ResourceStates::IndexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = fontTexture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc overlayDesc;
    overlayDesc
        .setIdentity(Name("tests/task_graph/setup_upload_overlay"))
        .setMarkerLabel("Setup Upload Overlay")
        .setQueue(graphicsRequest)
        .setScheduling(graphicsScheduling)
        .setDependencies(overlayDependencies, LengthOf(overlayDependencies))
        .setResourceUses(overlayUses, LengthOf(overlayUses))
    ;
    const Graphics::GpuTaskId overlay = graph.addTask(overlayDesc);
    ASSERT_TRUE(overlay.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));

    const Graphics::GpuSubmissionPacketId scenePacket = compiledGraph.packetForTask(scene);
    const Graphics::GpuSubmissionPacketId vertexPacket = compiledGraph.packetForTask(vertexUpload);
    const Graphics::GpuSubmissionPacketId indexPacket = compiledGraph.packetForTask(indexUpload);
    const Graphics::GpuSubmissionPacketId fontPacket = compiledGraph.packetForTask(fontUpload);
    const Graphics::GpuSubmissionPacketId overlayPacket = compiledGraph.packetForTask(overlay);
    ASSERT_TRUE(scenePacket.valid());
    ASSERT_TRUE(vertexPacket.valid());
    ASSERT_TRUE(indexPacket.valid());
    ASSERT_TRUE(fontPacket.valid());
    ASSERT_TRUE(overlayPacket.valid());
    EXPECT_EQ(compiledGraph.packet(scenePacket).queue, queues[0].id);
    // Tiny vertex/index deltas avoid a queue crossing, but an amortizable texture upload follows Transfer first.
    EXPECT_EQ(compiledGraph.packet(vertexPacket).queue, queues[0].id);
    EXPECT_EQ(compiledGraph.packet(indexPacket).queue, queues[0].id);
    EXPECT_EQ(compiledGraph.packet(fontPacket).queue, queues[1].id);
    EXPECT_EQ(compiledGraph.packet(overlayPacket).queue, queues[0].id);
    EXPECT_GT(vertexPacket.index, scenePacket.index);
    EXPECT_GT(indexPacket.index, scenePacket.index);
    EXPECT_GT(fontPacket.index, scenePacket.index);
    EXPECT_GT(overlayPacket.index, vertexPacket.index);
    EXPECT_GT(overlayPacket.index, indexPacket.index);
    EXPECT_GT(overlayPacket.index, fontPacket.index);
    const Graphics::GpuSubmissionPacketRange presentationRange = compiledGraph.packetRange(scenePacket, overlayPacket);
    ASSERT_TRUE(presentationRange.valid());
    EXPECT_EQ(presentationRange.packetCount, 5u);
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


TEST(GpuTaskGraph, KeepsAvboitUploadSequenceOutOfHardwareCausticsPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuTaskSchedulingHint hardwareCausticsScheduling;
    hardwareCausticsScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc hardwareCausticsDesc;
    hardwareCausticsDesc
        .setIdentity(Name("tests/task_graph/hardware_caustics_tail"))
        .setMarkerLabel("Hardware Caustics Tail")
        .setScheduling(hardwareCausticsScheduling)
    ;
    const Graphics::GpuTaskId hardwareCaustics = graph.addTask(hardwareCausticsDesc);
    ASSERT_TRUE(hardwareCaustics.valid());

    // The first prepared AVBOIT upload begins a separately accepted/timed semantic sequence. It must remain
    // mergeable itself so the rest of the upload -> Pre chain can share that new packet.
    Graphics::GpuTaskSchedulingHint firstAvboitUploadScheduling;
    firstAvboitUploadScheduling.allowPacketMerge = true;
    firstAvboitUploadScheduling.mergeWithPrevious = false;
    Graphics::GpuTaskDesc firstAvboitUploadDesc;
    firstAvboitUploadDesc
        .setIdentity(Name("tests/task_graph/avboit_first_upload"))
        .setMarkerLabel("AVBOIT First Upload")
        .setScheduling(firstAvboitUploadScheduling)
        .setDependencies(&hardwareCaustics, 1u)
    ;
    const Graphics::GpuTaskId firstAvboitUpload = graph.addTask(firstAvboitUploadDesc);
    ASSERT_TRUE(firstAvboitUpload.valid());

    Graphics::GpuTaskSchedulingHint avboitTailScheduling;
    avboitTailScheduling.allowPacketMerge = true;
    avboitTailScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc secondAvboitUploadDesc;
    secondAvboitUploadDesc
        .setIdentity(Name("tests/task_graph/avboit_second_upload"))
        .setMarkerLabel("AVBOIT Second Upload")
        .setScheduling(avboitTailScheduling)
        .setDependencies(&firstAvboitUpload, 1u)
    ;
    const Graphics::GpuTaskId secondAvboitUpload = graph.addTask(secondAvboitUploadDesc);
    ASSERT_TRUE(secondAvboitUpload.valid());

    Graphics::GpuTaskDesc avboitPreDesc;
    avboitPreDesc
        .setIdentity(Name("tests/task_graph/avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setScheduling(avboitTailScheduling)
        .setDependencies(&secondAvboitUpload, 1u)
    ;
    const Graphics::GpuTaskId avboitPre = graph.addTask(avboitPreDesc);
    ASSERT_TRUE(avboitPre.valid());

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
    ASSERT_EQ(compiledGraph.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId hardwareCausticsPacket = compiledGraph.packetForTask(hardwareCaustics);
    const Graphics::GpuSubmissionPacketId firstAvboitUploadPacket = compiledGraph.packetForTask(firstAvboitUpload);
    const Graphics::GpuSubmissionPacketId secondAvboitUploadPacket = compiledGraph.packetForTask(secondAvboitUpload);
    const Graphics::GpuSubmissionPacketId avboitPrePacket = compiledGraph.packetForTask(avboitPre);
    ASSERT_TRUE(hardwareCausticsPacket.valid());
    ASSERT_TRUE(firstAvboitUploadPacket.valid());
    EXPECT_NE(hardwareCausticsPacket, firstAvboitUploadPacket);
    EXPECT_EQ(firstAvboitUploadPacket, secondAvboitUploadPacket);
    EXPECT_EQ(firstAvboitUploadPacket, avboitPrePacket);

    ASSERT_EQ(compiledGraph.packet(hardwareCausticsPacket).taskCount, 1u);
    ASSERT_EQ(compiledGraph.packet(firstAvboitUploadPacket).taskCount, 3u);
    ASSERT_NE(compiledGraph.packetTasks(hardwareCausticsPacket), nullptr);
    ASSERT_NE(compiledGraph.packetTasks(firstAvboitUploadPacket), nullptr);
    EXPECT_EQ(compiledGraph.packetTasks(hardwareCausticsPacket)[0u], hardwareCaustics);
    EXPECT_EQ(compiledGraph.packetTasks(firstAvboitUploadPacket)[0u], firstAvboitUpload);
    EXPECT_EQ(compiledGraph.packetTasks(firstAvboitUploadPacket)[1u], secondAvboitUpload);
    EXPECT_EQ(compiledGraph.packetTasks(firstAvboitUploadPacket)[2u], avboitPre);
}


TEST(GpuTaskGraph, FrontierSafePacketizationSplitsBeforeCrossQueueConsumer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_first"))
        .setMarkerLabel("Frontier First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.allowPacketMerge = true;
    mergedScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc mergedDesc;
    mergedDesc
        .setIdentity(Name("tests/task_graph/frontier_unrelated_graphics"))
        .setMarkerLabel("Frontier Unrelated Graphics")
        .setQueue(graphicsRequest)
        .setScheduling(mergedScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId unrelatedGraphics = graph.addTask(mergedDesc);
    ASSERT_TRUE(unrelatedGraphics.valid());

    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_compute_consumer"))
        .setMarkerLabel("Frontier Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId computeConsumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(computeConsumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };

    Graphics::GpuTaskGraphAnalysis explicitMergeAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments explicitMergeAssignments(testArena.arena);
    Graphics::GpuCompiledGraph explicitMergeGraph(testArena.arena);
    ASSERT_TRUE(Compile(
        graph,
        explicitMergeAnalysis,
        topology,
        explicitMergeAssignments,
        explicitMergeGraph
    ));
    ASSERT_EQ(explicitMergeGraph.packetCount(), 2u);
    const Graphics::GpuSubmissionPacketId explicitFirstPacket = explicitMergeGraph.packetForTask(first);
    const Graphics::GpuSubmissionPacketId explicitUnrelatedPacket = explicitMergeGraph.packetForTask(unrelatedGraphics);
    const Graphics::GpuSubmissionPacketId explicitConsumerPacket = explicitMergeGraph.packetForTask(computeConsumer);
    ASSERT_TRUE(explicitFirstPacket.valid());
    EXPECT_EQ(explicitFirstPacket, explicitUnrelatedPacket);
    EXPECT_NE(explicitFirstPacket, explicitConsumerPacket);
    ASSERT_EQ(explicitMergeGraph.packet(explicitConsumerPacket).dependencyCount, 1u);
    EXPECT_EQ(
        explicitMergeGraph.packetDependencies(explicitConsumerPacket)[0u].producer,
        explicitFirstPacket
    );

    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis frontierAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments frontierAssignments(testArena.arena);
    Graphics::GpuCompiledGraph frontierGraph(testArena.arena);
    ASSERT_TRUE(Compile(
        graph,
        frontierAnalysis,
        topology,
        frontierAssignments,
        frontierGraph,
        frontierOptions
    ));
    ASSERT_EQ(frontierGraph.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketId frontierFirstPacket = frontierGraph.packetForTask(first);
    const Graphics::GpuSubmissionPacketId frontierUnrelatedPacket = frontierGraph.packetForTask(unrelatedGraphics);
    const Graphics::GpuSubmissionPacketId frontierConsumerPacket = frontierGraph.packetForTask(computeConsumer);
    ASSERT_TRUE(frontierFirstPacket.valid());
    EXPECT_NE(frontierFirstPacket, frontierUnrelatedPacket);
    EXPECT_NE(frontierUnrelatedPacket, frontierConsumerPacket);
    EXPECT_NE(
        frontierGraph.packet(frontierFirstPacket).queue,
        frontierGraph.packet(frontierConsumerPacket).queue
    );
    EXPECT_EQ(frontierGraph.packet(frontierFirstPacket).taskCount, 1u);
    EXPECT_EQ(frontierGraph.packet(frontierUnrelatedPacket).taskCount, 1u);
    ASSERT_EQ(frontierGraph.packet(frontierConsumerPacket).dependencyCount, 1u);
    EXPECT_EQ(
        frontierGraph.packetDependencies(frontierConsumerPacket)[0u].producer,
        frontierFirstPacket
    );
}


TEST(GpuTaskGraph, FrontierScoredPacketizationMergesCheapImmediateSuccessor){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_producer"))
        .setMarkerLabel("Frontier Scored Producer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    Graphics::GpuTaskDesc successorDesc;
    successorDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_successor"))
        .setMarkerLabel("Frontier Scored Successor")
        .setQueue(graphicsRequest)
        .setScheduling(successorScheduling)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId successor = graph.addTask(successorDesc);
    ASSERT_TRUE(successor.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    ASSERT_EQ(compiledGraph.packetCount(), 1u);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId successorPacket = compiledGraph.packetForTask(successor);
    ASSERT_TRUE(producerPacket.valid());
    EXPECT_EQ(producerPacket, successorPacket);
    EXPECT_EQ(compiledGraph.packet(producerPacket).taskCount, 2u);
}


TEST(GpuTaskGraph, FrontierScoredPacketizationPreservesCrossQueueConsumerFrontier){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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

    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_frontier_producer"))
        .setMarkerLabel("Frontier Scored Frontier Producer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint successorScheduling;
    successorScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    Graphics::GpuTaskDesc successorDesc;
    successorDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_frontier_successor"))
        .setMarkerLabel("Frontier Scored Frontier Successor")
        .setQueue(graphicsRequest)
        .setScheduling(successorScheduling)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId successor = graph.addTask(successorDesc);
    ASSERT_TRUE(successor.valid());

    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_scored_compute_consumer"))
        .setMarkerLabel("Frontier Scored Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierScored;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
    ASSERT_EQ(compiledGraph.packetCount(), 3u);

    const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId successorPacket = compiledGraph.packetForTask(successor);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledGraph.packetForTask(consumer);
    ASSERT_TRUE(producerPacket.valid());
    EXPECT_NE(producerPacket, successorPacket);
    EXPECT_NE(producerPacket, consumerPacket);
    ASSERT_EQ(compiledGraph.packet(consumerPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(consumerPacket)[0u].producer, producerPacket);
}


TEST(GpuTaskGraph, FrontierSafeConsumerFrontierOverrideRequiresExplicitImmediateDependency){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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
    const Graphics::GpuGraphResourceId orderedHandoff = AddHazardDomain(
        graph,
        Name("tests/task_graph/frontier_override_ordered_handoff"),
        "Frontier Override Ordered Handoff"
    );
    ASSERT_TRUE(orderedHandoff.valid());

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_override_first"))
        .setMarkerLabel("Frontier Override First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint immediateFinalizeScheduling;
    immediateFinalizeScheduling.allowPacketMerge = true;
    immediateFinalizeScheduling.mergeWithPrevious = true;
    immediateFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Graphics::GpuTaskResourceUse immediateFinalizeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = orderedHandoff,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc immediateFinalizeDesc;
    immediateFinalizeDesc
        .setIdentity(Name("tests/task_graph/frontier_override_immediate_finalize"))
        .setMarkerLabel("Frontier Override Immediate Finalize")
        .setQueue(graphicsRequest)
        .setScheduling(immediateFinalizeScheduling)
        .setDependencies(&first, 1u)
        .setResourceUses(immediateFinalizeUses, LengthOf(immediateFinalizeUses))
    ;
    const Graphics::GpuTaskId immediateFinalize = graph.addTask(immediateFinalizeDesc);
    ASSERT_TRUE(immediateFinalize.valid());

    // The inferred producer edge fixes this task after the finalizer, but its only explicit dependency remains
    // the first task. It must not inherit the finalizer's consumer-frontier override.
    Graphics::GpuTaskSchedulingHint unrelatedScheduling;
    unrelatedScheduling.allowPacketMerge = true;
    unrelatedScheduling.mergeWithPrevious = true;
    unrelatedScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse unrelatedUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = orderedHandoff,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc unrelatedDesc;
    unrelatedDesc
        .setIdentity(Name("tests/task_graph/frontier_override_unrelated_successor"))
        .setMarkerLabel("Frontier Override Unrelated Successor")
        .setQueue(graphicsRequest)
        .setScheduling(unrelatedScheduling)
        .setDependencies(&first, 1u)
        .setResourceUses(unrelatedUses, LengthOf(unrelatedUses))
    ;
    const Graphics::GpuTaskId unrelatedSuccessor = graph.addTask(unrelatedDesc);
    ASSERT_TRUE(unrelatedSuccessor.valid());

    // Keep the direct cross-queue edge from the first task, while sequencing the consumer after the attempted
    // unrelated merge so packet order cannot accidentally make this test pass.
    const Graphics::GpuTaskId consumerDependencies[] = { first, unrelatedSuccessor };
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_override_compute_consumer"))
        .setMarkerLabel("Frontier Override Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(consumerDependencies, LengthOf(consumerDependencies))
    ;
    const Graphics::GpuTaskId computeConsumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(computeConsumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_EQ(compiledGraph.packetCount(), 3u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(first);
    const Graphics::GpuSubmissionPacketId immediateFinalizePacket = compiledGraph.packetForTask(immediateFinalize);
    const Graphics::GpuSubmissionPacketId unrelatedPacket = compiledGraph.packetForTask(unrelatedSuccessor);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledGraph.packetForTask(computeConsumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, immediateFinalizePacket);
    EXPECT_NE(firstPacket, unrelatedPacket);
    EXPECT_NE(unrelatedPacket, consumerPacket);
    EXPECT_EQ(compiledGraph.packet(firstPacket).taskCount, 2u);
    EXPECT_EQ(compiledGraph.packet(unrelatedPacket).taskCount, 1u);
    ASSERT_EQ(compiledGraph.packet(consumerPacket).dependencyCount, 2u);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        immediateFinalize,
        unrelatedSuccessor,
        orderedHandoff,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
}


TEST(GpuTaskGraph, FrontierSafePacketizationKeepsMergeWhenLaterTaskOwnsCrossQueueConsumer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
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

    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc firstDesc;
    firstDesc
        .setIdentity(Name("tests/task_graph/frontier_merge_first"))
        .setMarkerLabel("Frontier Merge First")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId first = graph.addTask(firstDesc);
    ASSERT_TRUE(first.valid());

    Graphics::GpuTaskSchedulingHint mergedScheduling;
    mergedScheduling.allowPacketMerge = true;
    mergedScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc mergedDesc;
    mergedDesc
        .setIdentity(Name("tests/task_graph/frontier_merge_producer"))
        .setMarkerLabel("Frontier Merge Producer")
        .setQueue(graphicsRequest)
        .setScheduling(mergedScheduling)
        .setDependencies(&first, 1u)
    ;
    const Graphics::GpuTaskId producer = graph.addTask(mergedDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/frontier_merge_compute_consumer"))
        .setMarkerLabel("Frontier Merge Compute Consumer")
        .setQueue(computeRequest)
        .setDependencies(&producer, 1u)
    ;
    const Graphics::GpuTaskId computeConsumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(computeConsumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_EQ(compiledGraph.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(first);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId consumerPacket = compiledGraph.packetForTask(computeConsumer);
    ASSERT_TRUE(firstPacket.valid());
    EXPECT_EQ(firstPacket, producerPacket);
    EXPECT_NE(producerPacket, consumerPacket);
    EXPECT_EQ(compiledGraph.packet(firstPacket).taskCount, 2u);
    EXPECT_NE(compiledGraph.packet(firstPacket).queue, compiledGraph.packet(consumerPacket).queue);
    ASSERT_EQ(compiledGraph.packet(consumerPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(consumerPacket)[0u].producer, producerPacket);
}


TEST(GpuTaskGraph, MergesDeferredPreflightUploadsIntoShadowPreparePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/deferred_bindless_slots"),
        "Deferred Bindless Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/raytrace_material_context_slots"),
        "Ray-Trace Material Context Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId causticEmissionTargets = AddBufferMetadata(
        graph,
        Name("tests/task_graph/caustic_emission_targets"),
        "Caustic Emission Targets",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId surfelFrameConstants = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_frame_constants"),
        "Surfel Frame Constants",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId shadowInstanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_instance_materials"),
        "Shadow Instance Materials",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId shadowInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_instances"),
        "Shadow Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId shadowMaterialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_material_typed"),
        "Shadow Typed Materials",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneBvhNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/scene_bvh_nodes"),
        "Scene BVH Nodes",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneBvhInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/scene_bvh_instances"),
        "Scene BVH Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhParent = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_parent"),
        "Software BVH Parent",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhSortKeys = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_sort_keys"),
        "Software BVH Sort Keys",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhSortPayload = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_sort_payload"),
        "Software BVH Sort Payload",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId swBvhVisitCounter = AddBufferMetadata(
        graph,
        Name("tests/task_graph/sw_bvh_visit_counter"),
        "Software BVH Visit Counter",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneTlas = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/scene_tlas"),
        "Scene TLAS",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId sceneTlasBacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/scene_tlas_backing"),
        "Scene TLAS Backing",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasA = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a"),
        "Mesh BLAS A",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasABacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a_backing"),
        "Mesh BLAS A Backing",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasAPosition = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a_position"),
        "Mesh BLAS A Position",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasAIndex = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_a_index"),
        "Mesh BLAS A Index",
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasB = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_b"),
        "Mesh BLAS B",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId meshBlasBBacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/mesh_blas_b_backing"),
        "Mesh BLAS B Backing",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(causticEmissionTargets.valid());
    ASSERT_TRUE(surfelFrameConstants.valid());
    ASSERT_TRUE(shadowInstanceMaterials.valid());
    ASSERT_TRUE(shadowInstances.valid());
    ASSERT_TRUE(shadowMaterialTyped.valid());
    ASSERT_TRUE(sceneBvhNodes.valid());
    ASSERT_TRUE(sceneBvhInstances.valid());
    ASSERT_TRUE(swBvhParent.valid());
    ASSERT_TRUE(swBvhSortKeys.valid());
    ASSERT_TRUE(swBvhSortPayload.valid());
    ASSERT_TRUE(swBvhVisitCounter.valid());
    ASSERT_TRUE(sceneTlas.valid());
    ASSERT_TRUE(sceneTlasBacking.valid());
    ASSERT_TRUE(meshBlasA.valid());
    ASSERT_TRUE(meshBlasABacking.valid());
    ASSERT_TRUE(meshBlasAPosition.valid());
    ASSERT_TRUE(meshBlasAIndex.valid());
    ASSERT_TRUE(meshBlasB.valid());
    ASSERT_TRUE(meshBlasBBacking.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsUploadRequest{
        Graphics::GpuQueueCapability::Transfer,
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
    Graphics::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint shadowPrepareScheduling;
    shadowPrepareScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowPrepareScheduling.forceSubmissionBoundary = false;
    shadowPrepareScheduling.allowPacketMerge = true;
    shadowPrepareScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse uploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            // Keep-initial-state selector buffers publish Common after their built-in copy. The first native
            // consumer transitions it to ConstantBuffer and owns the following cross-queue state handoff.
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_slots_upload"))
        .setMarkerLabel("Deferred Bindless Slots Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(uploadScheduling)
        .setResourceUses(uploadUses, LengthOf(uploadUses))
    ;
    const Graphics::GpuTaskId upload = graph.addTask(uploadDesc);
    ASSERT_TRUE(upload.valid());

    const Graphics::GpuTaskResourceUse materialContextUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint materialContextUploadScheduling = uploadScheduling;
    materialContextUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc materialContextUploadDesc;
    materialContextUploadDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_slots_upload"))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(materialContextUploadScheduling)
        .setDependencies(&upload, 1u)
        .setResourceUses(materialContextUploadUses, LengthOf(materialContextUploadUses))
    ;
    const Graphics::GpuTaskId materialContextUpload = graph.addTask(materialContextUploadDesc);
    ASSERT_TRUE(materialContextUpload.valid());

    const Graphics::GpuTaskResourceUse causticEmissionTargetsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = causticEmissionTargets,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint causticEmissionTargetsUploadScheduling = uploadScheduling;
    causticEmissionTargetsUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    causticEmissionTargetsUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc causticEmissionTargetsUploadDesc;
    causticEmissionTargetsUploadDesc
        .setIdentity(Name("tests/task_graph/caustic_emission_targets_upload"))
        .setMarkerLabel("Caustic Emission Targets Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(causticEmissionTargetsUploadScheduling)
        .setDependencies(&materialContextUpload, 1u)
        .setResourceUses(causticEmissionTargetsUploadUses, LengthOf(causticEmissionTargetsUploadUses))
    ;
    const Graphics::GpuTaskId causticEmissionTargetsUpload = graph.addTask(causticEmissionTargetsUploadDesc);
    ASSERT_TRUE(causticEmissionTargetsUpload.valid());

    const Graphics::GpuTaskResourceUse surfelFrameConstantsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = surfelFrameConstants,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint surfelFrameConstantsUploadScheduling = uploadScheduling;
    surfelFrameConstantsUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc surfelFrameConstantsUploadDesc;
    surfelFrameConstantsUploadDesc
        .setIdentity(Name("tests/task_graph/surfel_frame_constants_upload"))
        .setMarkerLabel("Surfel Frame Constants Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(surfelFrameConstantsUploadScheduling)
        .setDependencies(&causticEmissionTargetsUpload, 1u)
        .setResourceUses(surfelFrameConstantsUploadUses, LengthOf(surfelFrameConstantsUploadUses))
    ;
    const Graphics::GpuTaskId surfelFrameConstantsUpload = graph.addTask(surfelFrameConstantsUploadDesc);
    ASSERT_TRUE(surfelFrameConstantsUpload.valid());

    const Graphics::GpuTaskResourceUse shadowInstanceMaterialsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint shadowMaterialContextUploadScheduling = uploadScheduling;
    shadowMaterialContextUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    shadowMaterialContextUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc shadowInstanceMaterialsUploadDesc;
    shadowInstanceMaterialsUploadDesc
        .setIdentity(Name("tests/task_graph/shadow_instance_materials_upload"))
        .setMarkerLabel("Shadow Instance Materials Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(shadowMaterialContextUploadScheduling)
        .setDependencies(&surfelFrameConstantsUpload, 1u)
        .setResourceUses(shadowInstanceMaterialsUploadUses, LengthOf(shadowInstanceMaterialsUploadUses))
    ;
    const Graphics::GpuTaskId shadowInstanceMaterialsUpload = graph.addTask(shadowInstanceMaterialsUploadDesc);
    ASSERT_TRUE(shadowInstanceMaterialsUpload.valid());

    const Graphics::GpuTaskResourceUse shadowInstancesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowInstancesUploadDesc;
    shadowInstancesUploadDesc
        .setIdentity(Name("tests/task_graph/shadow_instances_upload"))
        .setMarkerLabel("Shadow Instances Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(shadowMaterialContextUploadScheduling)
        .setDependencies(&shadowInstanceMaterialsUpload, 1u)
        .setResourceUses(shadowInstancesUploadUses, LengthOf(shadowInstancesUploadUses))
    ;
    const Graphics::GpuTaskId shadowInstancesUpload = graph.addTask(shadowInstancesUploadDesc);
    ASSERT_TRUE(shadowInstancesUpload.valid());

    const Graphics::GpuTaskResourceUse shadowMaterialTypedUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowMaterialTypedUploadDesc;
    shadowMaterialTypedUploadDesc
        .setIdentity(Name("tests/task_graph/shadow_material_typed_upload"))
        .setMarkerLabel("Shadow Typed Materials Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(shadowMaterialContextUploadScheduling)
        .setDependencies(&shadowInstancesUpload, 1u)
        .setResourceUses(shadowMaterialTypedUploadUses, LengthOf(shadowMaterialTypedUploadUses))
    ;
    const Graphics::GpuTaskId shadowMaterialTypedUpload = graph.addTask(shadowMaterialTypedUploadDesc);
    ASSERT_TRUE(shadowMaterialTypedUpload.valid());

    const Graphics::GpuTaskResourceUse sceneBvhNodesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint sceneBvhUploadScheduling = uploadScheduling;
    sceneBvhUploadScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    sceneBvhUploadScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc sceneBvhNodesUploadDesc;
    sceneBvhNodesUploadDesc
        .setIdentity(Name("tests/task_graph/scene_bvh_nodes_upload"))
        .setMarkerLabel("Scene BVH Nodes Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(sceneBvhUploadScheduling)
        .setDependencies(&shadowMaterialTypedUpload, 1u)
        .setResourceUses(sceneBvhNodesUploadUses, LengthOf(sceneBvhNodesUploadUses))
    ;
    const Graphics::GpuTaskId sceneBvhNodesUpload = graph.addTask(sceneBvhNodesUploadDesc);
    ASSERT_TRUE(sceneBvhNodesUpload.valid());

    const Graphics::GpuTaskResourceUse sceneBvhInstancesUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc sceneBvhInstancesUploadDesc;
    sceneBvhInstancesUploadDesc
        .setIdentity(Name("tests/task_graph/scene_bvh_instances_upload"))
        .setMarkerLabel("Scene BVH Instances Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(sceneBvhUploadScheduling)
        .setDependencies(&sceneBvhNodesUpload, 1u)
        .setResourceUses(sceneBvhInstancesUploadUses, LengthOf(sceneBvhInstancesUploadUses))
    ;
    const Graphics::GpuTaskId sceneBvhInstancesUpload = graph.addTask(sceneBvhInstancesUploadDesc);
    ASSERT_TRUE(sceneBvhInstancesUpload.valid());

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = causticEmissionTargets,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = surfelFrameConstants,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        // SW mesh build parent links and shared scratch remain native UAVs after Shadow Preparation. They are
        // state-only graph inputs so an accepted packet can seed a later build/refit without normalizing them to SRV.
        Graphics::GpuTaskResourceUse{
            .resource = swBvhParent,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = swBvhSortKeys,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = swBvhSortPayload,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = swBvhVisitCounter,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        // The frozen native TLAS recorder consumes the graph-owned build state. Its adjacent state-only finalizer
        // below publishes the descriptor-visible Read handoff before the Compute consumer can begin.
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        // The frozen BLAS build records with graph-owned typed/backing Write aliases. Its frozen shared geometry
        // inputs enter as build inputs here; the adjacent hybrid tail lowers them to SRV before SW-BVH work records.
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasAPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructBuildInput,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasAIndex,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructBuildInput,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasA,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasABacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        // A state-only retained BLAS continues to seed direct/native compatibility routes without adding a packet.
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasB,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasBBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareScheduling)
        .setDependencies(&sceneBvhInstancesUpload, 1u)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    // The hybrid HW-to-SW continuation records real compatibility work after the hardware build, but it keeps the
    // original accepting packet whole so the opaque fallback and its persistent state handoff stay atomic. Its
    // frozen BLAS inputs become graph-owned ShaderResource reads at this callback boundary.
    Graphics::GpuTaskSchedulingHint shadowPrepareHybridTailScheduling;
    shadowPrepareHybridTailScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowPrepareHybridTailScheduling.forceSubmissionBoundary = false;
    shadowPrepareHybridTailScheduling.allowPacketMerge = true;
    shadowPrepareHybridTailScheduling.mergeWithPrevious = true;
    shadowPrepareHybridTailScheduling.allowMergeAcrossConsumerFrontier = true;
    const Graphics::GpuTaskResourceUse shadowPrepareHybridTailUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasAPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasAIndex,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareHybridTailDesc;
    shadowPrepareHybridTailDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_prepare_hybrid_tail"))
        .setMarkerLabel("Shadow Preparation Hybrid Software Tail")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareHybridTailScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(shadowPrepareHybridTailUses, LengthOf(shadowPrepareHybridTailUses))
    ;
    const Graphics::GpuTaskId shadowPrepareHybridTail = graph.addTask(shadowPrepareHybridTailDesc);
    ASSERT_TRUE(shadowPrepareHybridTail.valid());

    const Graphics::GpuTaskResourceUse shadowPrepareTlasFinalizeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasA,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = meshBlasABacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskSchedulingHint shadowPrepareTlasFinalizeScheduling;
    shadowPrepareTlasFinalizeScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    shadowPrepareTlasFinalizeScheduling.forceSubmissionBoundary = false;
    shadowPrepareTlasFinalizeScheduling.allowPacketMerge = true;
    shadowPrepareTlasFinalizeScheduling.mergeWithPrevious = true;
    // Shadow Preparation still directly signals retained descriptor resources to Compute. The finalizer follows the
    // hybrid tail and explicitly retains its TLAS/BLAS Read handoffs in that accepting Graphics packet.
    shadowPrepareTlasFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Graphics::GpuTaskDesc shadowPrepareTlasFinalizeDesc;
    shadowPrepareTlasFinalizeDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_prepare_tlas_finalize"))
        .setMarkerLabel("Shadow Preparation TLAS Finalize")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareTlasFinalizeScheduling)
        .setDependencies(&shadowPrepareHybridTail, 1u)
        .setResourceUses(shadowPrepareTlasFinalizeUses, LengthOf(shadowPrepareTlasFinalizeUses))
    ;
    const Graphics::GpuTaskId shadowPrepareTlasFinalize = graph.addTask(shadowPrepareTlasFinalizeDesc);
    ASSERT_TRUE(shadowPrepareTlasFinalize.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = causticEmissionTargets,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = surfelFrameConstants,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/deferred_bindless_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&shadowPrepareTlasFinalize, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_EQ(compiledGraph.packetCount(), 2u);

    // Shadow Preparation retains direct cross-queue descriptor hazards even though Visibility explicitly depends on
    // the finalizer. The hybrid tail and finalizer's opt-ins must keep the complete callback chain in the first
    // accepting Graphics packet.
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        shadowPrepare,
        shadowVisibility,
        currentBindlessSlots,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        shadowPrepare,
        shadowPrepareHybridTail,
        meshBlasAPosition,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        shadowPrepare,
        shadowPrepareHybridTail,
        meshBlasAIndex,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuSubmissionPacketId uploadPacket = compiledGraph.packetForTask(upload);
    const Graphics::GpuSubmissionPacketId materialContextUploadPacket = compiledGraph.packetForTask(materialContextUpload);
    const Graphics::GpuSubmissionPacketId causticEmissionTargetsUploadPacket = compiledGraph.packetForTask(
        causticEmissionTargetsUpload
    );
    const Graphics::GpuSubmissionPacketId surfelFrameConstantsUploadPacket = compiledGraph.packetForTask(
        surfelFrameConstantsUpload
    );
    const Graphics::GpuSubmissionPacketId shadowInstanceMaterialsUploadPacket = compiledGraph.packetForTask(
        shadowInstanceMaterialsUpload
    );
    const Graphics::GpuSubmissionPacketId shadowInstancesUploadPacket = compiledGraph.packetForTask(
        shadowInstancesUpload
    );
    const Graphics::GpuSubmissionPacketId shadowMaterialTypedUploadPacket = compiledGraph.packetForTask(
        shadowMaterialTypedUpload
    );
    const Graphics::GpuSubmissionPacketId sceneBvhNodesUploadPacket = compiledGraph.packetForTask(sceneBvhNodesUpload);
    const Graphics::GpuSubmissionPacketId sceneBvhInstancesUploadPacket = compiledGraph.packetForTask(
        sceneBvhInstancesUpload
    );
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId shadowPrepareHybridTailPacket = compiledGraph.packetForTask(
        shadowPrepareHybridTail
    );
    const Graphics::GpuSubmissionPacketId shadowPrepareTlasFinalizePacket = compiledGraph.packetForTask(
        shadowPrepareTlasFinalize
    );
    const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledGraph.packetForTask(shadowVisibility);
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(materialContextUploadPacket.valid());
    ASSERT_TRUE(causticEmissionTargetsUploadPacket.valid());
    ASSERT_TRUE(surfelFrameConstantsUploadPacket.valid());
    ASSERT_TRUE(shadowInstanceMaterialsUploadPacket.valid());
    ASSERT_TRUE(shadowInstancesUploadPacket.valid());
    ASSERT_TRUE(shadowMaterialTypedUploadPacket.valid());
    ASSERT_TRUE(sceneBvhNodesUploadPacket.valid());
    ASSERT_TRUE(sceneBvhInstancesUploadPacket.valid());
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(shadowPrepareHybridTailPacket.valid());
    ASSERT_TRUE(shadowPrepareTlasFinalizePacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    EXPECT_EQ(uploadPacket, shadowPreparePacket);
    EXPECT_EQ(materialContextUploadPacket, shadowPreparePacket);
    EXPECT_EQ(causticEmissionTargetsUploadPacket, shadowPreparePacket);
    EXPECT_EQ(surfelFrameConstantsUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowInstanceMaterialsUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowInstancesUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowMaterialTypedUploadPacket, shadowPreparePacket);
    EXPECT_EQ(sceneBvhNodesUploadPacket, shadowPreparePacket);
    EXPECT_EQ(sceneBvhInstancesUploadPacket, shadowPreparePacket);
    EXPECT_EQ(shadowPrepareHybridTailPacket, shadowPreparePacket);
    EXPECT_EQ(shadowPrepareTlasFinalizePacket, shadowPreparePacket);
    EXPECT_NE(shadowPreparePacket, shadowVisibilityPacket);
    EXPECT_EQ(compiledGraph.packet(shadowPreparePacket).taskCount, 12u);
    const Graphics::GpuTaskId* const shadowPrepareTasks = compiledGraph.packetTasks(shadowPreparePacket);
    ASSERT_NE(shadowPrepareTasks, nullptr);
    EXPECT_EQ(shadowPrepareTasks[9u], shadowPrepare);
    EXPECT_EQ(shadowPrepareTasks[10u], shadowPrepareHybridTail);
    EXPECT_EQ(shadowPrepareTasks[11u], shadowPrepareTlasFinalize);
    const Graphics::GpuSubmissionPacketRange shadowPrepareRange = compiledGraph.packetRange(
        shadowPreparePacket,
        shadowPreparePacket
    );
    ASSERT_TRUE(shadowPrepareRange.valid());
    EXPECT_EQ(shadowPrepareRange.packetCount, 1u);
    const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledGraph.findTask(shadowPrepare);
    const Graphics::GpuCompiledTask* const compiledShadowPrepareHybridTail = compiledGraph.findTask(
        shadowPrepareHybridTail
    );
    const Graphics::GpuCompiledTask* const compiledShadowPrepareTlasFinalize = compiledGraph.findTask(
        shadowPrepareTlasFinalize
    );
    ASSERT_NE(compiledShadowPrepare, nullptr);
    ASSERT_NE(compiledShadowPrepareHybridTail, nullptr);
    ASSERT_NE(compiledShadowPrepareTlasFinalize, nullptr);
    const Graphics::GpuCompiledBarrier* const shadowPrepareBarriers = compiledGraph.taskPrologueBarriers(shadowPrepare);
    ASSERT_NE(shadowPrepareBarriers, nullptr);
    bool transitionsCurrentBindlessSlots = false;
    bool transitionsMaterialContextSlots = false;
    bool transitionsCausticEmissionTargets = false;
    bool transitionsSurfelFrameConstants = false;
    bool transitionsShadowInstanceMaterials = false;
    bool transitionsShadowInstances = false;
    bool transitionsShadowMaterialTyped = false;
    bool transitionsSceneBvhNodes = false;
    bool transitionsSceneBvhInstances = false;
    bool transitionsSwBvhParent = false;
    bool transitionsSwBvhSortKeys = false;
    bool transitionsSwBvhSortPayload = false;
    bool transitionsSwBvhVisitCounter = false;
    bool transitionsSceneTlas = false;
    bool transitionsSceneTlasBacking = false;
    bool transitionsMeshBlasA = false;
    bool transitionsMeshBlasABacking = false;
    bool transitionsMeshBlasBBacking = false;
    bool transitionsMeshBlasAPosition = false;
    bool transitionsMeshBlasAIndex = false;
    for(usize index = 0u; index < compiledShadowPrepare->prologueBarrierCount; ++index){
        const Graphics::GpuCompiledBarrier& barrier = shadowPrepareBarriers[index];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ConstantBuffer
        ){
            transitionsCurrentBindlessSlots = transitionsCurrentBindlessSlots || barrier.resource == currentBindlessSlots;
            transitionsMaterialContextSlots = transitionsMaterialContextSlots || barrier.resource == materialContextSlots;
            transitionsSurfelFrameConstants = transitionsSurfelFrameConstants || barrier.resource == surfelFrameConstants;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ShaderResource
        ){
            transitionsCausticEmissionTargets = transitionsCausticEmissionTargets || barrier.resource == causticEmissionTargets;
            transitionsShadowInstanceMaterials = transitionsShadowInstanceMaterials || barrier.resource == shadowInstanceMaterials;
            transitionsShadowInstances = transitionsShadowInstances || barrier.resource == shadowInstances;
            transitionsShadowMaterialTyped = transitionsShadowMaterialTyped || barrier.resource == shadowMaterialTyped;
            transitionsSceneBvhNodes = transitionsSceneBvhNodes || barrier.resource == sceneBvhNodes;
            transitionsSceneBvhInstances = transitionsSceneBvhInstances || barrier.resource == sceneBvhInstances;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        ){
            transitionsSwBvhParent = transitionsSwBvhParent || barrier.resource == swBvhParent;
            transitionsSwBvhSortKeys = transitionsSwBvhSortKeys || barrier.resource == swBvhSortKeys;
            transitionsSwBvhSortPayload = transitionsSwBvhSortPayload || barrier.resource == swBvhSortPayload;
            transitionsSwBvhVisitCounter = transitionsSwBvhVisitCounter || barrier.resource == swBvhVisitCounter;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::AccelStructTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructWrite
        )
            transitionsSceneTlas = transitionsSceneTlas || barrier.resource == sceneTlas;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructWrite
        ){
            transitionsSceneTlasBacking = transitionsSceneTlasBacking || barrier.resource == sceneTlasBacking;
            transitionsMeshBlasABacking = transitionsMeshBlasABacking || barrier.resource == meshBlasABacking;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::AccelStructTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructWrite
        )
            transitionsMeshBlasA = transitionsMeshBlasA || barrier.resource == meshBlasA;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.after == Graphics::ResourceStates::AccelStructBuildInput
        ){
            transitionsMeshBlasAPosition = transitionsMeshBlasAPosition
                || (
                    barrier.resource == meshBlasAPosition
                    && barrier.before == Graphics::ResourceStates::Common
                )
            ;
            transitionsMeshBlasAIndex = transitionsMeshBlasAIndex
                || (
                    barrier.resource == meshBlasAIndex
                    && barrier.before == Graphics::ResourceStates::ShaderResource
                )
            ;
        }
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::AccelStructRead
        ){
            transitionsMeshBlasBBacking = transitionsMeshBlasBBacking || barrier.resource == meshBlasBBacking;
        }
    }
    EXPECT_TRUE(transitionsCurrentBindlessSlots);
    EXPECT_TRUE(transitionsMaterialContextSlots);
    EXPECT_TRUE(transitionsCausticEmissionTargets);
    EXPECT_TRUE(transitionsSurfelFrameConstants);
    EXPECT_TRUE(transitionsShadowInstanceMaterials);
    EXPECT_TRUE(transitionsShadowInstances);
    EXPECT_TRUE(transitionsShadowMaterialTyped);
    EXPECT_TRUE(transitionsSceneBvhNodes);
    EXPECT_TRUE(transitionsSceneBvhInstances);
    EXPECT_TRUE(transitionsSwBvhParent);
    EXPECT_TRUE(transitionsSwBvhSortKeys);
    EXPECT_TRUE(transitionsSwBvhSortPayload);
    EXPECT_TRUE(transitionsSwBvhVisitCounter);
    EXPECT_TRUE(transitionsSceneTlas);
    EXPECT_TRUE(transitionsSceneTlasBacking);
    EXPECT_TRUE(transitionsMeshBlasA);
    EXPECT_TRUE(transitionsMeshBlasABacking);
    EXPECT_TRUE(transitionsMeshBlasBBacking);
    EXPECT_TRUE(transitionsMeshBlasAPosition);
    EXPECT_TRUE(transitionsMeshBlasAIndex);
    const Graphics::GpuCompiledBarrier* const shadowPrepareHybridTailBarriers =
        compiledGraph.taskPrologueBarriers(shadowPrepareHybridTail)
    ;
    ASSERT_NE(shadowPrepareHybridTailBarriers, nullptr);
    const auto hasShadowPrepareHybridTailInputBarrier = [&](const Graphics::GpuGraphResourceId resource){
        for(usize barrierIndex = 0u;
            barrierIndex < compiledShadowPrepareHybridTail->prologueBarrierCount;
            ++barrierIndex
        ){
            const Graphics::GpuCompiledBarrier& barrier = shadowPrepareHybridTailBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::AccelStructBuildInput
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowPrepareHybridTailInputBarrier(meshBlasAPosition));
    EXPECT_TRUE(hasShadowPrepareHybridTailInputBarrier(meshBlasAIndex));
    const Graphics::GpuCompiledBarrier* const shadowPrepareTlasFinalizeBarriers =
        compiledGraph.taskPrologueBarriers(shadowPrepareTlasFinalize)
    ;
    ASSERT_NE(shadowPrepareTlasFinalizeBarriers, nullptr);
    const auto hasShadowPrepareTlasFinalizeBarrier = [&](
        const Graphics::GpuCompiledBarrierType::Enum type,
        const Graphics::GpuGraphResourceId resource
    ){
        for(usize barrierIndex = 0u;
            barrierIndex < compiledShadowPrepareTlasFinalize->prologueBarrierCount;
            ++barrierIndex
        ){
            const Graphics::GpuCompiledBarrier& barrier = shadowPrepareTlasFinalizeBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::AccelStructWrite
                && barrier.after == Graphics::ResourceStates::AccelStructRead
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        sceneTlas
    ));
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneTlasBacking
    ));
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        meshBlasA
    ));
    EXPECT_TRUE(hasShadowPrepareTlasFinalizeBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        meshBlasABacking
    ));
    ASSERT_EQ(compiledGraph.packet(shadowVisibilityPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(shadowVisibilityPacket)[0u].producer, shadowPreparePacket);
}


// Shadow Visibility selects all of these through descriptor heaps. Its prepared graph callback deliberately does
// not reissue their entry transitions, so pin the descriptor-visible depth layout and the HW/SW traversal states
// here. The later lighting read also proves that the visibility UAV handoff remains graph-owned across packets.
TEST(GpuTaskGraph, PlansGraphOwnedShadowVisibilityEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_world_position"),
        "Shadow Visibility World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId normal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_normal"),
        "Shadow Visibility Normal",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_depth"),
        "Shadow Visibility Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_output"),
        "Shadow Visibility",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftHalfA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_soft_half_a"),
        "Shadow Soft Half A",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowCoarseTransmittance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_coarse_transmittance"),
        "Shadow Coarse Transmittance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_soft_geometry"),
        "Shadow Soft Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_bindless_slots"),
        "Deferred Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_scene_shading"),
        "Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_lights"),
        "Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_material_context_slots"),
        "Ray-Trace Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_software_mesh_nodes"),
        "Software Shadow Mesh Nodes",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneBvhNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_scene_bvh_nodes"),
        "Scene BVH Nodes",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_instance_materials"),
        "Shadow Instance Materials",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowMaterialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_material_typed"),
        "Shadow Typed Materials",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_instances"),
        "Shadow Instances",
        Graphics::ResourceStates::UnorderedAccess,
        queueSharing
    );
    const Graphics::GpuGraphResourceId edgeStatistics = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_edge_statistics"),
        "Shadow Edge Statistics",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneTlas = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_tlas"),
        "Scene TLAS",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneTlasBacking = AddBufferMetadata(
        graph,
        Name("tests/task_graph/shadow_visibility_tlas_backing"),
        "Scene TLAS Backing",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(normal.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(shadowVisibility.valid());
    ASSERT_TRUE(shadowSoftHalfA.valid());
    ASSERT_TRUE(shadowCoarseTransmittance.valid());
    ASSERT_TRUE(shadowSoftGeometry.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(sceneShading.valid());
    ASSERT_TRUE(lights.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(softwareMeshNodes.valid());
    ASSERT_TRUE(sceneBvhNodes.valid());
    ASSERT_TRUE(shadowInstanceMaterials.valid());
    ASSERT_TRUE(shadowMaterialTyped.valid());
    ASSERT_TRUE(shadowInstances.valid());
    ASSERT_TRUE(edgeStatistics.valid());
    ASSERT_TRUE(sceneTlas.valid());
    ASSERT_TRUE(sceneTlasBacking.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint prepareScheduling;
    prepareScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    prepareScheduling.forceSubmissionBoundary = true;
    prepareScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(prepareScheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = normal,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = depth,
            .range = {},
            .requiredState = Graphics::ResourceStates::DepthWrite,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneShading,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = lights,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint prefixScheduling = prepareScheduling;
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_prefix"))
        .setMarkerLabel("G-Buffer Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(prefixScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = normal,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        // Depth is sampled through the bindless descriptor heap. Keep this ShaderResource rather than DepthRead:
        // Vulkan maps the two states to distinct depth-read-only and shader-read-only image layouts.
        Graphics::GpuTaskResourceUse{
            .resource = depth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowSoftHalfA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowCoarseTransmittance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneShading,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = lights,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = softwareMeshNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneBvhNodes,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstanceMaterials,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowMaterialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = shadowInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = edgeStatistics,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlas,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = sceneTlasBacking,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint shadowScheduling;
    shadowScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowScheduling.forceSubmissionBoundary = true;
    shadowScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(shadowScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibilityTask = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibilityTask.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskSchedulingHint lightingScheduling = shadowScheduling;
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graph_owned_shadow_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(&shadowVisibilityTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        shadowVisibilityTask,
        lighting,
        shadowVisibility,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuCompiledTask* const compiledShadowVisibility = compiledGraph.findTask(shadowVisibilityTask);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledShadowVisibility, nullptr);
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId shadowPacket = compiledGraph.packetForTask(shadowVisibilityTask);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_NE(shadowPreparePacket, prefixPacket);
    EXPECT_NE(prefixPacket, shadowPacket);
    EXPECT_NE(shadowPacket, lightingPacket);
    const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledGraph.findTask(shadowPrepare);
    ASSERT_NE(compiledShadowPrepare, nullptr);
    const Graphics::GpuCompiledBarrier* const shadowPrepareBarriers = compiledGraph.taskPrologueBarriers(shadowPrepare);
    ASSERT_NE(shadowPrepareBarriers, nullptr);
    const auto hasShadowPrepareBarrier = [&](
        const Graphics::GpuCompiledBarrierType::Enum type,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        for(usize barrierIndex = 0u; barrierIndex < compiledShadowPrepare->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = shadowPrepareBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        currentBindlessSlots,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        materialContextSlots,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        sceneTlas,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::AccelStructRead
    ));
    EXPECT_TRUE(hasShadowPrepareBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneTlasBacking,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::AccelStructRead
    ));
    const Graphics::GpuPacketStateSeed* const shadowSeeds = compiledGraph.taskPrologueStateSeeds(shadowVisibilityTask);
    ASSERT_NE(shadowSeeds, nullptr);
    const auto hasShadowSeed = [&](const Graphics::GpuGraphResourceId resource, const Graphics::GpuSubmissionPacketId sourcePacket){
        for(usize seedIndex = 0u; seedIndex < compiledShadowVisibility->prologueStateSeedCount; ++seedIndex){
            if(shadowSeeds[seedIndex].resource == resource && shadowSeeds[seedIndex].sourcePacket == sourcePacket)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowSeed(worldPosition, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(normal, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(depth, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(shadowVisibility, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(sceneShading, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(lights, prefixPacket));
    EXPECT_TRUE(hasShadowSeed(currentBindlessSlots, shadowPreparePacket));
    EXPECT_TRUE(hasShadowSeed(materialContextSlots, shadowPreparePacket));
    EXPECT_TRUE(hasShadowSeed(sceneTlasBacking, shadowPreparePacket));
    const auto shadowPacketWaitsFor = [&](const Graphics::GpuSubmissionPacketId producer){
        const Graphics::GpuSubmissionPacket& packet = compiledGraph.packet(shadowPacket);
        const Graphics::GpuPacketDependency* const dependencies = compiledGraph.packetDependencies(shadowPacket);
        if(packet.dependencyCount != 0u && !dependencies)
            return false;
        for(usize dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            if(dependencies[dependencyIndex].producer == producer)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(shadowPacketWaitsFor(shadowPreparePacket));
    EXPECT_TRUE(shadowPacketWaitsFor(prefixPacket));
    const Graphics::GpuCompiledBarrier* const shadowBarriers = compiledGraph.taskPrologueBarriers(shadowVisibilityTask);
    ASSERT_NE(shadowBarriers, nullptr);
    const auto hasShadowBarrier = [&](
        const Graphics::GpuCompiledBarrierType::Enum type,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        for(usize barrierIndex = 0u; barrierIndex < compiledShadowVisibility->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = shadowBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        worldPosition,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        normal,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        depth,
        Graphics::ResourceStates::DepthWrite,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowVisibility,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowSoftHalfA,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowCoarseTransmittance,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        shadowSoftGeometry,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneShading,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        lights,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        softwareMeshNodes,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneBvhNodes,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        shadowInstanceMaterials,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        shadowMaterialTyped,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        shadowInstances,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        edgeStatistics,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_FALSE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::AccelStructTransition,
        sceneTlas,
        Graphics::ResourceStates::AccelStructRead,
        Graphics::ResourceStates::AccelStructRead
    ));
    EXPECT_FALSE(hasShadowBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneTlasBacking,
        Graphics::ResourceStates::AccelStructRead,
        Graphics::ResourceStates::AccelStructRead
    ));
    ASSERT_EQ(compiledLighting->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const lightingSeed = compiledGraph.taskPrologueStateSeeds(lighting);
    ASSERT_NE(lightingSeed, nullptr);
    EXPECT_EQ(lightingSeed[0u].resource, shadowVisibility);
    EXPECT_EQ(lightingSeed[0u].sourcePacket, shadowPacket);
    ASSERT_EQ(compiledLighting->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const lightingBarrier = compiledGraph.taskPrologueBarriers(lighting);
    ASSERT_NE(lightingBarrier, nullptr);
    EXPECT_EQ(lightingBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(lightingBarrier[0u].resource, shadowVisibility);
    EXPECT_EQ(lightingBarrier[0u].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(lightingBarrier[0u].after, Graphics::ResourceStates::ShaderResource);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(lightingPacket)[0u].producer, shadowPacket);
}


// The transparent software trace retains graph-owned static traversal and descriptor entry states. The following
// focused test covers its newly graph-owned opaque-result image/UAV handoff.
TEST(GpuTaskGraph, PlansGraphOwnedSoftTransparentTraceEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const auto addBuffer = [&](const Name identity, const AStringView label){
        return AddBufferMetadata(
            graph,
            identity,
            label,
            Graphics::ResourceStates::Common,
            queueSharing
        );
    };
    const Graphics::GpuGraphResourceId shaderResources[] = {
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_nodes"), "Software Mesh Nodes"),
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_positions"), "Software Mesh Positions"),
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_indices"), "Software Mesh Indices"),
        addBuffer(Name("tests/task_graph/soft_transparent_mesh_attributes"), "Software Mesh Attributes"),
        addBuffer(Name("tests/task_graph/soft_transparent_scene_bvh_nodes"), "Scene BVH Nodes"),
        addBuffer(Name("tests/task_graph/soft_transparent_scene_instances"), "Scene Instances"),
        addBuffer(Name("tests/task_graph/soft_transparent_instance_materials"), "Shadow Instance Materials"),
        addBuffer(Name("tests/task_graph/soft_transparent_material_typed"), "Shadow Typed Materials"),
        addBuffer(Name("tests/task_graph/soft_transparent_instances"), "Shadow Instances"),
        addBuffer(Name("tests/task_graph/soft_transparent_lights"), "Deferred Lights"),
    };
    const Graphics::GpuGraphResourceId constantResources[] = {
        addBuffer(Name("tests/task_graph/soft_transparent_material_context_slots"), "Ray-Trace Material Context Slots"),
        addBuffer(Name("tests/task_graph/soft_transparent_bindless_slots"), "Deferred Bindless Slots"),
        addBuffer(Name("tests/task_graph/soft_transparent_scene_shading"), "Scene Shading"),
    };
    for(const Graphics::GpuGraphResourceId resource : shaderResources)
        ASSERT_TRUE(resource.valid());
    for(const Graphics::GpuGraphResourceId resource : constantResources)
        ASSERT_TRUE(resource.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Graphics::GpuTaskResourceUse prefixUses[LengthOf(shaderResources) + LengthOf(constantResources)] = {};
    usize prefixUseCount = 0u;
    for(const Graphics::GpuGraphResourceId resource : shaderResources){
        prefixUses[prefixUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
    }
    for(const Graphics::GpuGraphResourceId resource : constantResources){
        prefixUses[prefixUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
    }
    ASSERT_EQ(prefixUseCount, LengthOf(prefixUses));
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_prefix"))
        .setMarkerLabel("Transparent Shadow Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    Graphics::GpuTaskResourceUse traceUses[LengthOf(shaderResources) + LengthOf(constantResources)] = {};
    usize traceUseCount = 0u;
    for(const Graphics::GpuGraphResourceId resource : shaderResources){
        traceUses[traceUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
    }
    for(const Graphics::GpuGraphResourceId resource : constantResources){
        traceUses[traceUseCount++] = {
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
    }
    ASSERT_EQ(traceUseCount, LengthOf(traceUses));
    Graphics::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/task_graph/graph_owned_soft_transparent_trace"))
        .setMarkerLabel("Transparent Shadow Trace")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(traceUses, LengthOf(traceUses))
    ;
    const Graphics::GpuTaskId trace = graph.addTask(traceDesc);
    ASSERT_TRUE(trace.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId tracePacket = compiledGraph.packetForTask(trace);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    EXPECT_NE(prefixPacket, tracePacket);

    const Graphics::GpuCompiledTask* const compiledTrace = compiledGraph.findTask(trace);
    ASSERT_NE(compiledTrace, nullptr);
    EXPECT_EQ(compiledTrace->prologueStateSeedCount, LengthOf(traceUses));
    const Graphics::GpuPacketStateSeed* const traceSeeds = compiledGraph.taskPrologueStateSeeds(trace);
    ASSERT_NE(traceSeeds, nullptr);
    const auto hasTraceSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledTrace->prologueStateSeedCount; ++seedIndex){
            if(traceSeeds[seedIndex].resource == resource && traceSeeds[seedIndex].sourcePacket == prefixPacket)
                return true;
        }
        return false;
    };
    for(const Graphics::GpuGraphResourceId resource : shaderResources)
        EXPECT_TRUE(hasTraceSeed(resource));
    for(const Graphics::GpuGraphResourceId resource : constantResources)
        EXPECT_TRUE(hasTraceSeed(resource));

    const Graphics::GpuCompiledBarrier* const traceBarriers = compiledGraph.taskPrologueBarriers(trace);
    ASSERT_NE(traceBarriers, nullptr);
    const auto hasTraceBarrier = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledTrace->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::CopyDest
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    for(const Graphics::GpuGraphResourceId resource : shaderResources)
        EXPECT_TRUE(hasTraceBarrier(resource, Graphics::ResourceStates::ShaderResource));
    for(const Graphics::GpuGraphResourceId resource : constantResources)
        EXPECT_TRUE(hasTraceBarrier(resource, Graphics::ResourceStates::ConstantBuffer));
    ASSERT_EQ(compiledGraph.packet(tracePacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(tracePacket)[0u].producer, prefixPacket);
}


// Prepared soft-transparent shadows split opaque production, first wavelet, resolve tail, transparent trace,
// transparent first wavelet, and terminal resolve tail without adding an effects submission. The compiler owns all
// sampled handoffs and both first-wavelet-to-tail transitions.
TEST(GpuTaskGraph, MergesGraphOwnedSoftTransparentTraceAndResolveAfterOpaqueVisibility){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_shadow_visibility"),
        "Shadow Visibility",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId opaqueSoftHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_opaque_half"),
        "Opaque Shadow Soft Half",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId opaqueWaveletHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_opaque_wavelet_half"),
        "Opaque Shadow First Wavelet Half",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentSoftHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_half"),
        "Transparent Shadow Soft Half",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentHistoryA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_history_a"),
        "Transparent Shadow History A",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentMomentsA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_moments_a"),
        "Transparent Shadow Moments A",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentHistoryB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_history_b"),
        "Transparent Shadow History B",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId transparentMomentsB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_moments_b"),
        "Transparent Shadow Moments B",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowSoftGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_geometry"),
        "Shadow Soft Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId previousGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_geometry_previous"),
        "Previous Shadow Soft Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_world_position"),
        "World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId normal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_normal"),
        "G-buffer Normal",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_depth"),
        "G-buffer Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/soft_transparent_fold_scene_shading"),
        "Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(shadowVisibility.valid());
    ASSERT_TRUE(opaqueSoftHalf.valid());
    ASSERT_TRUE(opaqueWaveletHalf.valid());
    ASSERT_TRUE(transparentSoftHalf.valid());
    ASSERT_TRUE(transparentHistoryA.valid());
    ASSERT_TRUE(transparentMomentsA.valid());
    ASSERT_TRUE(transparentHistoryB.valid());
    ASSERT_TRUE(transparentMomentsB.valid());
    ASSERT_TRUE(shadowSoftGeometry.valid());
    ASSERT_TRUE(previousGeometry.valid());
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(normal.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(sceneShading.valid());

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint opaqueScheduling;
    opaqueScheduling.cost = Graphics::GpuTaskCostHint::Large;
    opaqueScheduling.forceSubmissionBoundary = false;
    opaqueScheduling.allowPacketMerge = true;
    opaqueScheduling.mergeWithPrevious = false;
    const Graphics::GpuTaskResourceUse opaqueUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentHistoryA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentMomentsA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentHistoryB,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentMomentsB,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_fold_opaque"))
        .setMarkerLabel("Shadow Visibility Opaque")
        .setQueue(computeRequest)
        .setScheduling(opaqueScheduling)
        .setResourceUses(opaqueUses, LengthOf(opaqueUses))
    ;
    const Graphics::GpuTaskId opaque = graph.addTask(opaqueDesc);
    ASSERT_TRUE(opaque.valid());

    Graphics::GpuTaskSchedulingHint traceScheduling = opaqueScheduling;
    traceScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    traceScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse opaqueFirstWaveletUses[] = {
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = opaqueWaveletHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc opaqueFirstWaveletDesc;
    opaqueFirstWaveletDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_opaque_first_wavelet"))
        .setMarkerLabel("Shadow Opaque First Wavelet")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&opaque, 1u)
        .setResourceUses(opaqueFirstWaveletUses, LengthOf(opaqueFirstWaveletUses))
    ;
    const Graphics::GpuTaskId opaqueFirstWavelet = graph.addTask(opaqueFirstWaveletDesc);
    ASSERT_TRUE(opaqueFirstWavelet.valid());

    const Graphics::GpuTaskResourceUse opaqueResolveUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = opaqueWaveletHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc opaqueResolveDesc;
    opaqueResolveDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_opaque_resolve"))
        .setMarkerLabel("Shadow Opaque Soft Resolve")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&opaqueFirstWavelet, 1u)
        .setResourceUses(opaqueResolveUses, LengthOf(opaqueResolveUses))
    ;
    const Graphics::GpuTaskId opaqueResolve = graph.addTask(opaqueResolveDesc);
    ASSERT_TRUE(opaqueResolve.valid());

    const Graphics::GpuTaskResourceUse traceUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = transparentSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_trace"))
        .setMarkerLabel("Shadow Transparent Soft Trace")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&opaqueResolve, 1u)
        .setResourceUses(traceUses, LengthOf(traceUses))
    ;
    const Graphics::GpuTaskId trace = graph.addTask(traceDesc);
    ASSERT_TRUE(trace.valid());

    const Graphics::GpuTaskResourceUse transparentTemporalMergeUses[] = {
        {
            .resource = transparentSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = previousGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryA,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentMomentsA,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryB,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = transparentMomentsB,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc transparentTemporalMergeDesc;
    transparentTemporalMergeDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_temporal_merge"))
        .setMarkerLabel("Shadow Transparent Temporal Merge")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&trace, 1u)
        .setResourceUses(transparentTemporalMergeUses, LengthOf(transparentTemporalMergeUses))
    ;
    const Graphics::GpuTaskId transparentTemporalMerge = graph.addTask(transparentTemporalMergeDesc);
    ASSERT_TRUE(transparentTemporalMerge.valid());

    const Graphics::GpuTaskResourceUse transparentFirstWaveletUses[] = {
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentHistoryB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = transparentMomentsB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc transparentFirstWaveletDesc;
    transparentFirstWaveletDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_first_wavelet"))
        .setMarkerLabel("Shadow Transparent First Wavelet")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&transparentTemporalMerge, 1u)
        .setResourceUses(transparentFirstWaveletUses, LengthOf(transparentFirstWaveletUses))
    ;
    const Graphics::GpuTaskId transparentFirstWavelet = graph.addTask(transparentFirstWaveletDesc);
    ASSERT_TRUE(transparentFirstWavelet.valid());

    const Graphics::GpuTaskResourceUse foldUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = opaqueSoftHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = shadowSoftGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = normal,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = depth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = sceneShading,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc foldDesc;
    foldDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_fold"))
        .setMarkerLabel("Shadow Transparent Soft Fold")
        .setQueue(computeRequest)
        .setScheduling(traceScheduling)
        .setDependencies(&transparentFirstWavelet, 1u)
        .setResourceUses(foldUses, LengthOf(foldUses))
    ;
    const Graphics::GpuTaskId fold = graph.addTask(foldDesc);
    ASSERT_TRUE(fold.valid());

    Graphics::GpuTaskSchedulingHint lightingScheduling;
    lightingScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    lightingScheduling.forceSubmissionBoundary = true;
    lightingScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskResourceUse lightingUses[] = {
        {
            .resource = shadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/soft_transparent_fold_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(graphicsRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(&fold, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(opaque, opaqueFirstWavelet));
    EXPECT_TRUE(analysis.hasExplicitEdge(opaqueFirstWavelet, opaqueResolve));
    EXPECT_TRUE(analysis.hasExplicitEdge(opaqueResolve, trace));
    EXPECT_TRUE(analysis.hasExplicitEdge(trace, transparentTemporalMerge));
    EXPECT_TRUE(analysis.hasExplicitEdge(transparentTemporalMerge, transparentFirstWavelet));
    EXPECT_TRUE(analysis.hasExplicitEdge(transparentFirstWavelet, fold));
    EXPECT_TRUE(analysis.hasExplicitEdge(fold, lighting));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaque,
        opaqueFirstWavelet,
        shadowSoftGeometry,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaque,
        opaqueFirstWavelet,
        opaqueSoftHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaqueFirstWavelet,
        opaqueResolve,
        opaqueWaveletHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        trace,
        transparentTemporalMerge,
        transparentSoftHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentTemporalMerge,
        transparentFirstWavelet,
        transparentHistoryB,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentTemporalMerge,
        transparentFirstWavelet,
        transparentMomentsB,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentFirstWavelet,
        fold,
        opaqueSoftHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuSubmissionPacketId opaquePacket = compiledGraph.packetForTask(opaque);
    const Graphics::GpuSubmissionPacketId opaqueFirstWaveletPacket = compiledGraph.packetForTask(opaqueFirstWavelet);
    const Graphics::GpuSubmissionPacketId opaqueResolvePacket = compiledGraph.packetForTask(opaqueResolve);
    const Graphics::GpuSubmissionPacketId tracePacket = compiledGraph.packetForTask(trace);
    const Graphics::GpuSubmissionPacketId transparentTemporalMergePacket = compiledGraph.packetForTask(transparentTemporalMerge);
    const Graphics::GpuSubmissionPacketId transparentFirstWaveletPacket = compiledGraph.packetForTask(transparentFirstWavelet);
    const Graphics::GpuSubmissionPacketId foldPacket = compiledGraph.packetForTask(fold);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    ASSERT_TRUE(opaquePacket.valid());
    ASSERT_TRUE(opaqueFirstWaveletPacket.valid());
    ASSERT_TRUE(opaqueResolvePacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    ASSERT_TRUE(transparentTemporalMergePacket.valid());
    ASSERT_TRUE(transparentFirstWaveletPacket.valid());
    ASSERT_TRUE(foldPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(opaquePacket, foldPacket);
    EXPECT_EQ(opaqueFirstWaveletPacket, foldPacket);
    EXPECT_EQ(opaqueResolvePacket, foldPacket);
    EXPECT_EQ(tracePacket, foldPacket);
    EXPECT_EQ(transparentTemporalMergePacket, foldPacket);
    EXPECT_EQ(transparentFirstWaveletPacket, foldPacket);
    EXPECT_NE(foldPacket, lightingPacket);
    ASSERT_EQ(compiledGraph.packetCount(), 2u);
    const Graphics::GpuSubmissionPacket& shadowPacket = compiledGraph.packet(foldPacket);
    ASSERT_EQ(shadowPacket.taskCount, 7u);
    const Graphics::GpuTaskId* const shadowTasks = compiledGraph.packetTasks(foldPacket);
    ASSERT_NE(shadowTasks, nullptr);
    EXPECT_EQ(shadowTasks[0u], opaque);
    EXPECT_EQ(shadowTasks[1u], opaqueFirstWavelet);
    EXPECT_EQ(shadowTasks[2u], opaqueResolve);
    EXPECT_EQ(shadowTasks[3u], trace);
    EXPECT_EQ(shadowTasks[4u], transparentTemporalMerge);
    EXPECT_EQ(shadowTasks[5u], transparentFirstWavelet);
    EXPECT_EQ(shadowTasks[6u], fold);

    const Graphics::GpuCompiledTask* const compiledOpaqueFirstWavelet = compiledGraph.findTask(opaqueFirstWavelet);
    ASSERT_NE(compiledOpaqueFirstWavelet, nullptr);
    const Graphics::GpuCompiledBarrier* const opaqueFirstWaveletBarriers = compiledGraph.taskPrologueBarriers(opaqueFirstWavelet);
    ASSERT_NE(opaqueFirstWaveletBarriers, nullptr);
    const auto hasOpaqueFirstWaveletTransition = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 barrierIndex = 0u; barrierIndex < compiledOpaqueFirstWavelet->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = opaqueFirstWaveletBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasOpaqueFirstWaveletTransition(opaqueSoftHalf));
    EXPECT_TRUE(hasOpaqueFirstWaveletTransition(shadowSoftGeometry));

    const Graphics::GpuCompiledTask* const compiledOpaqueResolve = compiledGraph.findTask(opaqueResolve);
    ASSERT_NE(compiledOpaqueResolve, nullptr);
    const Graphics::GpuCompiledBarrier* const opaqueResolveBarriers = compiledGraph.taskPrologueBarriers(opaqueResolve);
    ASSERT_NE(opaqueResolveBarriers, nullptr);
    bool hasOpaqueWaveletResolveTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOpaqueResolve->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = opaqueResolveBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == opaqueWaveletHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        ){
            hasOpaqueWaveletResolveTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasOpaqueWaveletResolveTransition);

    const Graphics::GpuCompiledTask* const compiledTrace = compiledGraph.findTask(trace);
    ASSERT_NE(compiledTrace, nullptr);
    const Graphics::GpuCompiledBarrier* const traceBarriers = compiledGraph.taskPrologueBarriers(trace);
    ASSERT_NE(traceBarriers, nullptr);
    bool hasOpaqueResolveToTraceUav = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTrace->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.resource == shadowVisibility
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        ){
            hasOpaqueResolveToTraceUav = true;
            break;
        }
    }
    EXPECT_TRUE(hasOpaqueResolveToTraceUav);

    const Graphics::GpuCompiledTask* const compiledTransparentTemporalMerge = compiledGraph.findTask(transparentTemporalMerge);
    ASSERT_NE(compiledTransparentTemporalMerge, nullptr);
    const Graphics::GpuCompiledBarrier* const transparentTemporalMergeBarriers =
        compiledGraph.taskPrologueBarriers(transparentTemporalMerge)
    ;
    ASSERT_NE(transparentTemporalMergeBarriers, nullptr);
    const auto hasTransparentTemporalMergeShaderResourceTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentTemporalMerge->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = transparentTemporalMergeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentSoftHalf, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentHistoryA, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(transparentMomentsA, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(previousGeometry, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasTransparentTemporalMergeShaderResourceTransition(worldPosition, Graphics::ResourceStates::Common));

    const Graphics::GpuCompiledTask* const compiledTransparentFirstWavelet = compiledGraph.findTask(transparentFirstWavelet);
    ASSERT_NE(compiledTransparentFirstWavelet, nullptr);
    const Graphics::GpuCompiledBarrier* const transparentFirstWaveletBarriers =
        compiledGraph.taskPrologueBarriers(transparentFirstWavelet)
    ;
    ASSERT_NE(transparentFirstWaveletBarriers, nullptr);
    const auto hasTransparentFirstWaveletShaderResourceTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentFirstWavelet->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = transparentFirstWaveletBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransparentFirstWaveletShaderResourceTransition(transparentHistoryB, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTransparentFirstWaveletShaderResourceTransition(transparentMomentsB, Graphics::ResourceStates::UnorderedAccess));
    bool hasTransparentTraceToFirstWaveletUav = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentFirstWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = transparentFirstWaveletBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.resource == opaqueSoftHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        ){
            hasTransparentTraceToFirstWaveletUav = true;
            break;
        }
    }
    EXPECT_TRUE(hasTransparentTraceToFirstWaveletUav);

    const Graphics::GpuCompiledTask* const compiledFold = compiledGraph.findTask(fold);
    ASSERT_NE(compiledFold, nullptr);
    const Graphics::GpuCompiledBarrier* const foldBarriers = compiledGraph.taskPrologueBarriers(fold);
    ASSERT_NE(foldBarriers, nullptr);
    const auto hasFoldShaderResourceTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(u32 barrierIndex = 0u; barrierIndex < compiledFold->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = foldBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasFoldShaderResourceTransition(opaqueSoftHalf, Graphics::ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasFoldShaderResourceTransition(normal, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasFoldShaderResourceTransition(depth, Graphics::ResourceStates::Common));
    bool hasSceneShadingTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFold->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = foldBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == sceneShading
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::ConstantBuffer
        ){
            hasSceneShadingTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasSceneShadingTransition);

    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuCompiledBarrier* const lightingBarriers = compiledGraph.taskPrologueBarriers(lighting);
    ASSERT_NE(lightingBarriers, nullptr);
    bool hasLightingTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledLighting->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = lightingBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == shadowVisibility
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        ){
            hasLightingTransition = true;
            break;
        }
    }
    EXPECT_TRUE(hasLightingTransition);
}


// The opaque temporal merge freezes the opposite selector from the terminal transparent merge. Its history/moment,
// stable previous-geometry, and world-position inputs must enter the opaque callback as SRVs while the next pair
// remains writable by that callback.
TEST(GpuTaskGraph, GraphOwnsOpaqueSoftTemporalMergeHistoryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId historyA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_history_a"),
        "Opaque Soft History A",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId momentsA = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_moments_a"),
        "Opaque Soft Moments A",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId historyB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_history_b"),
        "Opaque Soft History B",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId momentsB = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_moments_b"),
        "Opaque Soft Moments B",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId previousGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_geometry_previous"),
        "Previous Opaque Soft Geometry",
        Graphics::ResourceStates::UnorderedAccess
    );
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/opaque_soft_world_position"),
        "Opaque Soft World Position",
        Graphics::ResourceStates::UnorderedAccess
    );
    ASSERT_TRUE(historyA.valid());
    ASSERT_TRUE(momentsA.valid());
    ASSERT_TRUE(historyB.valid());
    ASSERT_TRUE(momentsB.valid());
    ASSERT_TRUE(previousGeometry.valid());
    ASSERT_TRUE(worldPosition.valid());

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Graphics::GpuTaskResourceUse opaqueMergeUses[] = {
        {
            .resource = historyB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = momentsB,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = previousGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = worldPosition,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = historyA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        {
            .resource = momentsA,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc opaqueMergeDesc;
    opaqueMergeDesc
        .setIdentity(Name("tests/task_graph/opaque_soft_temporal_merge"))
        .setMarkerLabel("Opaque Soft Temporal Merge")
        .setQueue(computeRequest)
        .setResourceUses(opaqueMergeUses, LengthOf(opaqueMergeUses))
    ;
    const Graphics::GpuTaskId opaqueMerge = graph.addTask(opaqueMergeDesc);
    ASSERT_TRUE(opaqueMerge.valid());

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
    ASSERT_EQ(compiledGraph.packetCount(), 1u);

    const Graphics::GpuCompiledTask* const compiledMerge = compiledGraph.findTask(opaqueMerge);
    ASSERT_NE(compiledMerge, nullptr);
    const Graphics::GpuCompiledBarrier* const mergeBarriers = compiledGraph.taskPrologueBarriers(opaqueMerge);
    ASSERT_NE(mergeBarriers, nullptr);
    const auto hasMergeInputTransition = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 barrierIndex = 0u; barrierIndex < compiledMerge->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = mergeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasMergeInputTransition(historyB));
    EXPECT_TRUE(hasMergeInputTransition(momentsB));
    EXPECT_TRUE(hasMergeInputTransition(previousGeometry));
    EXPECT_TRUE(hasMergeInputTransition(worldPosition));
}


// Pure-software Shadow Preparation can enter after an accepted prior hardware/direct frame left a frozen mesh input
// in AccelStructBuildInput. Its prepared SW-BVH recorder is getter-only for position/index in that route, so the
// graph must lower both a fresh Common seed and an accepted build-input seed to ShaderResource before it records.
TEST(GpuTaskGraph, PlansGraphOwnedPreparedSoftwareBvhInputStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId position = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_sw_bvh_position"),
        "Prepared SW-BVH Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId index = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_sw_bvh_index"),
        "Prepared SW-BVH Index",
        Graphics::ResourceStates::AccelStructBuildInput,
        queueSharing
    );
    ASSERT_TRUE(position.valid());
    ASSERT_TRUE(index.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    scheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse prepareUses[] = {
        {
            .resource = position,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = index,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/prepared_sw_bvh_shadow_prepare"))
        .setMarkerLabel("Prepared Software BVH Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    const Graphics::GpuTaskId prepare = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepare.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_EQ(compiledGraph.packetCount(), 1u);

    const Graphics::GpuCompiledTask* const compiledPrepare = compiledGraph.findTask(prepare);
    ASSERT_NE(compiledPrepare, nullptr);
    const Graphics::GpuCompiledBarrier* const prepareBarriers = compiledGraph.taskPrologueBarriers(prepare);
    ASSERT_NE(prepareBarriers, nullptr);
    const auto hasInputTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(usize barrierIndex = 0u; barrierIndex < compiledPrepare->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = prepareBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasInputTransition(position, Graphics::ResourceStates::Common));
    EXPECT_TRUE(hasInputTransition(index, Graphics::ResourceStates::AccelStructBuildInput));
}


// Prepared BLAS work with no following software-BVH consumer in Shadow Preparation has its position/index inputs
// enter as graph-owned build inputs, and the normalizer later restores their trace SRV state for
// the next Prefix/visibility consumers. Hybrid routes deliberately retain their native intra-callback bridge.
TEST(GpuTaskGraph, PlansGraphOwnedPreparedTailFreeBlasInputStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId position = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_tail_free_blas_position"),
        "Prepared Tail-Free BLAS Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId index = AddBufferMetadata(
        graph,
        Name("tests/task_graph/prepared_tail_free_blas_index"),
        "Prepared Tail-Free BLAS Index",
        Graphics::ResourceStates::ShaderResource,
        queueSharing
    );
    ASSERT_TRUE(position.valid());
    ASSERT_TRUE(index.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint prepareScheduling;
    prepareScheduling.cost = Graphics::GpuTaskCostHint::Large;
    prepareScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse prepareUses[] = {
        {
            .resource = position,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructBuildInput,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = index,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructBuildInput,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/prepared_tail_free_blas_shadow_prepare"))
        .setMarkerLabel("Prepared Tail-Free BLAS Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(prepareScheduling)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    const Graphics::GpuTaskId prepare = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepare.valid());

    Graphics::GpuTaskSchedulingHint normalizeScheduling = prepareScheduling;
    normalizeScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    normalizeScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse normalizeUses[] = {
        {
            .resource = position,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = index,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/task_graph/prepared_tail_free_blas_normalize"))
        .setMarkerLabel("Prepared Tail-Free BLAS Normalize")
        .setQueue(graphicsRequest)
        .setScheduling(normalizeScheduling)
        .setDependencies(&prepare, 1u)
        .setResourceUses(normalizeUses, LengthOf(normalizeUses))
    ;
    const Graphics::GpuTaskId normalize = graph.addTask(normalizeDesc);
    ASSERT_TRUE(normalize.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_EQ(compiledGraph.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId packet = compiledGraph.packetForTask(prepare);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledGraph.packetForTask(normalize), packet);
    EXPECT_EQ(compiledGraph.packet(packet).taskCount, 2u);

    const Graphics::GpuCompiledTask* const compiledPrepare = compiledGraph.findTask(prepare);
    const Graphics::GpuCompiledTask* const compiledNormalize = compiledGraph.findTask(normalize);
    ASSERT_NE(compiledPrepare, nullptr);
    ASSERT_NE(compiledNormalize, nullptr);
    const auto hasTransition = [&](
        const Graphics::GpuTaskId task,
        const Graphics::GpuCompiledTask& compiledTask,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask before,
        const Graphics::ResourceStates::Mask after
    ){
        const Graphics::GpuCompiledBarrier* const barriers = compiledGraph.taskPrologueBarriers(task);
        if(compiledTask.prologueBarrierCount != 0u && !barriers)
            return false;
        for(usize barrierIndex = 0u; barrierIndex < compiledTask.prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(
        prepare,
        *compiledPrepare,
        position,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        prepare,
        *compiledPrepare,
        index,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::AccelStructBuildInput
    ));
    EXPECT_TRUE(hasTransition(
        normalize,
        *compiledNormalize,
        position,
        Graphics::ResourceStates::AccelStructBuildInput,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasTransition(
        normalize,
        *compiledNormalize,
        index,
        Graphics::ResourceStates::AccelStructBuildInput,
        Graphics::ResourceStates::ShaderResource
    ));
}


// Shadow Preparation, G-buffer, and the post-G-buffer normalizer share their frozen trace geometry through one
// graph. The normalizer must restore both raster VertexBuffer and SW-BVH UAV producers to ShaderResource before the
// shadow callback records; it no longer relies on a native route-specific state bridge.
TEST(GpuTaskGraph, PlansGraphOwnedPostGbufferTraceGeometryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId rasterGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/post_gbuffer_raster_geometry"),
        "Post-G-Buffer Raster Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareBvh = AddBufferMetadata(
        graph,
        Name("tests/task_graph/post_gbuffer_software_bvh"),
        "Post-G-Buffer Software BVH",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(rasterGeometry.valid());
    ASSERT_TRUE(softwareBvh.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.allowPacketMerge = true;

    const Graphics::GpuTaskResourceUse prepareUses[] = {
        {
            .resource = rasterGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = softwareBvh,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/post_gbuffer_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    const Graphics::GpuTaskId prepare = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepare.valid());

    Graphics::GpuTaskSchedulingHint gbufferScheduling = scheduling;
    gbufferScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse gbufferUses[] = {
        {
            .resource = rasterGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::VertexBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = softwareBvh,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("tests/task_graph/post_gbuffer_gbuffer"))
        .setMarkerLabel("G-Buffer")
        .setQueue(graphicsRequest)
        .setScheduling(gbufferScheduling)
        .setDependencies(&prepare, 1u)
        .setResourceUses(gbufferUses, LengthOf(gbufferUses))
    ;
    const Graphics::GpuTaskId gbuffer = graph.addTask(gbufferDesc);
    ASSERT_TRUE(gbuffer.valid());

    const Graphics::GpuTaskResourceUse normalizeUses[] = {
        {
            .resource = rasterGeometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        {
            .resource = softwareBvh,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/task_graph/post_gbuffer_normalize_trace_geometry"))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setQueue(graphicsRequest)
        .setScheduling(gbufferScheduling)
        .setDependencies(&gbuffer, 1u)
        .setResourceUses(normalizeUses, LengthOf(normalizeUses))
    ;
    const Graphics::GpuTaskId normalize = graph.addTask(normalizeDesc);
    ASSERT_TRUE(normalize.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledTask* const compiledNormalize = compiledGraph.findTask(normalize);
    ASSERT_NE(compiledNormalize, nullptr);
    const Graphics::GpuCompiledBarrier* const normalizeBarriers = compiledGraph.taskPrologueBarriers(normalize);
    ASSERT_NE(normalizeBarriers, nullptr);
    const auto hasNormalizeBarrier = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before){
        for(usize barrierIndex = 0u; barrierIndex < compiledNormalize->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = normalizeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasNormalizeBarrier(rasterGeometry, Graphics::ResourceStates::VertexBuffer));
    EXPECT_TRUE(hasNormalizeBarrier(softwareBvh, Graphics::ResourceStates::UnorderedAccess));
}


// Software caustics succeeds the graph-owned shadow callback. Its photon shader samples the same depth layout and
// descriptor-selected traversal inputs, so the task must inherit those states rather than reintroducing a native
// entry bridge. A fresh temporal accumulator is zeroed by a typed graph clear; warm decay now has its own preceding
// graph task, and the geometry-downsample starts from these graph-owned entry states while later resolve handoffs
// remain task-local.
TEST(GpuTaskGraph, PlansGraphOwnedSoftwareCausticsEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_world_position"),
        "Software Caustics World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_depth"),
        "Software Caustics Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowVisibility = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_shadow_visibility"),
        "Software Caustics Shadow Visibility",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticAccumulator = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_accumulator"),
        "Software Caustics Accumulator",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticHistory = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_history"),
        "Software Caustics History",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticResolveHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_resolve_half"),
        "Software Caustics Resolve Half",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticResolveGeometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_resolve_geometry"),
        "Software Caustics Resolve Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticIrradiance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/software_caustics_irradiance"),
        "Software Caustics Irradiance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_bindless_slots"),
        "Software Caustics Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_material_context_slots"),
        "Software Caustics Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId causticEmissionTargets = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_emission_targets"),
        "Software Caustics Emission Targets",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId meshView = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_view"),
        "Software Caustics Mesh View",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneBvhNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_scene_bvh_nodes"),
        "Software Caustics Scene BVH Nodes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_scene_instances"),
        "Software Caustics Scene Instances",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_instance_materials"),
        "Software Caustics Shadow Instance Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowMaterialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_material_typed"),
        "Software Caustics Shadow Typed Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId shadowInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_instances"),
        "Software Caustics Shadow Instances",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshNodes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_nodes"),
        "Software Caustics Mesh Nodes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshPositions = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_positions"),
        "Software Caustics Mesh Positions",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshIndices = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_indices"),
        "Software Caustics Mesh Indices",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId softwareMeshAttributes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_mesh_attributes"),
        "Software Caustics Mesh Attributes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_scene_shading"),
        "Software Caustics Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/software_caustics_lights"),
        "Software Caustics Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(shadowVisibility.valid());
    ASSERT_TRUE(causticAccumulator.valid());
    ASSERT_TRUE(causticHistory.valid());
    ASSERT_TRUE(causticResolveHalf.valid());
    ASSERT_TRUE(causticResolveGeometry.valid());
    ASSERT_TRUE(causticIrradiance.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(causticEmissionTargets.valid());
    ASSERT_TRUE(meshView.valid());
    ASSERT_TRUE(sceneBvhNodes.valid());
    ASSERT_TRUE(sceneInstances.valid());
    ASSERT_TRUE(shadowInstanceMaterials.valid());
    ASSERT_TRUE(shadowMaterialTyped.valid());
    ASSERT_TRUE(shadowInstances.valid());
    ASSERT_TRUE(softwareMeshNodes.valid());
    ASSERT_TRUE(softwareMeshPositions.valid());
    ASSERT_TRUE(softwareMeshIndices.valid());
    ASSERT_TRUE(softwareMeshAttributes.valid());
    ASSERT_TRUE(sceneShading.valid());
    ASSERT_TRUE(lights.valid());

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
    const Graphics::GpuQueueRequest computeTransferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = causticEmissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneBvhNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = shadowInstanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = shadowMaterialTyped, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = shadowInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = softwareMeshNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = softwareMeshPositions, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = softwareMeshIndices, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = softwareMeshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/software_caustics_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::DepthWrite, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/software_caustics_prefix"))
        .setMarkerLabel("G-Buffer Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneBvhNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowMaterialTyped, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshPositions, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshIndices, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowVisibility, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/software_caustics_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibilityTask = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibilityTask.valid());

    // The no-producer black result starts the Software Caustics packet; the producer merges with it below.
    Graphics::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse irradianceClearUses[] = {
        { .resource = causticIrradiance, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics_irradiance_clear"))
        .setMarkerLabel("Software Caustics Irradiance Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(irradianceClearScheduling)
        .setDependencies(&shadowVisibilityTask, 1u)
        .setResourceUses(irradianceClearUses, LengthOf(irradianceClearUses))
    ;
    const Graphics::GpuTaskId irradianceClearTask = graph.addTask(irradianceClearDesc);
    ASSERT_TRUE(irradianceClearTask.valid());

    // The bootstrap clear follows the no-producer irradiance clear, then merges into the same Software Caustics
    // packet as the callback. Its CopyDest write must hand off to the producer's UAV accumulator access.
    const Graphics::GpuTaskResourceUse accumulatorBootstrapClearUses[] = {
        { .resource = causticAccumulator, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling = irradianceClearScheduling;
    accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc accumulatorBootstrapClearDesc;
    accumulatorBootstrapClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics_accumulator_bootstrap_clear"))
        .setMarkerLabel("Software Caustics Accumulator Bootstrap Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(accumulatorBootstrapClearScheduling)
        .setDependencies(&irradianceClearTask, 1u)
        .setResourceUses(accumulatorBootstrapClearUses, LengthOf(accumulatorBootstrapClearUses))
    ;
    const Graphics::GpuTaskId accumulatorBootstrapClearTask = graph.addTask(accumulatorBootstrapClearDesc);
    ASSERT_TRUE(accumulatorBootstrapClearTask.valid());

    const Graphics::GpuTaskResourceUse softwareCausticsUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = causticEmissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneBvhNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowMaterialTyped, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = shadowInstances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshNodes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshPositions, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshIndices, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = softwareMeshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = causticAccumulator, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticHistory, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticResolveHalf, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticResolveGeometry, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = causticIrradiance, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint causticsScheduling = boundaryScheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc softwareCausticsDesc;
    softwareCausticsDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(computeRequest)
        .setScheduling(causticsScheduling)
        .setDependencies(&accumulatorBootstrapClearTask, 1u)
        .setResourceUses(softwareCausticsUses, LengthOf(softwareCausticsUses))
    ;
    const Graphics::GpuTaskId softwareCausticsTask = graph.addTask(softwareCausticsDesc);
    ASSERT_TRUE(softwareCausticsTask.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        { .resource = causticIrradiance, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graph_owned_software_caustics_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&softwareCausticsTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        softwareCausticsTask,
        lighting,
        causticIrradiance,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        irradianceClearTask,
        softwareCausticsTask,
        causticIrradiance,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        accumulatorBootstrapClearTask,
        softwareCausticsTask,
        causticAccumulator,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const clearAssignment = assignments.find(irradianceClearTask);
    ASSERT_NE(clearAssignment, nullptr);
    EXPECT_EQ(clearAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId shadowPacket = compiledGraph.packetForTask(shadowVisibilityTask);
    const Graphics::GpuSubmissionPacketId irradianceClearPacket = compiledGraph.packetForTask(irradianceClearTask);
    const Graphics::GpuSubmissionPacketId accumulatorBootstrapClearPacket =
        compiledGraph.packetForTask(accumulatorBootstrapClearTask);
    const Graphics::GpuSubmissionPacketId causticsPacket = compiledGraph.packetForTask(softwareCausticsTask);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowPacket.valid());
    ASSERT_TRUE(irradianceClearPacket.valid());
    ASSERT_TRUE(accumulatorBootstrapClearPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_NE(shadowPreparePacket, prefixPacket);
    EXPECT_NE(prefixPacket, shadowPacket);
    EXPECT_NE(shadowPacket, causticsPacket);
    EXPECT_EQ(irradianceClearPacket, causticsPacket);
    EXPECT_EQ(accumulatorBootstrapClearPacket, causticsPacket);
    EXPECT_NE(causticsPacket, lightingPacket);
    EXPECT_EQ(compiledGraph.packetCount(), 5u);

    const Graphics::GpuCompiledTask* const compiledShadow = compiledGraph.findTask(shadowVisibilityTask);
    const Graphics::GpuCompiledTask* const compiledCaustics = compiledGraph.findTask(softwareCausticsTask);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledShadow, nullptr);
    ASSERT_NE(compiledCaustics, nullptr);
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuCompiledBarrier* const shadowBarriers = compiledGraph.taskPrologueBarriers(shadowVisibilityTask);
    ASSERT_NE(shadowBarriers, nullptr);
    bool shadowTransitionsDepthToShaderResource = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledShadow->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = shadowBarriers[barrierIndex];
        shadowTransitionsDepthToShaderResource = shadowTransitionsDepthToShaderResource || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == depth
            && barrier.before == Graphics::ResourceStates::DepthWrite
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(shadowTransitionsDepthToShaderResource);

    const Graphics::GpuPacketStateSeed* const causticsSeeds = compiledGraph.taskPrologueStateSeeds(softwareCausticsTask);
    ASSERT_NE(causticsSeeds, nullptr);
    const auto hasCausticsSeed = [&](const Graphics::GpuGraphResourceId resource, const Graphics::GpuSubmissionPacketId sourcePacket){
        for(usize seedIndex = 0u; seedIndex < compiledCaustics->prologueStateSeedCount; ++seedIndex){
            if(causticsSeeds[seedIndex].resource == resource && causticsSeeds[seedIndex].sourcePacket == sourcePacket)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasCausticsSeed(causticEmissionTargets, shadowPreparePacket));
    EXPECT_TRUE(hasCausticsSeed(meshView, prefixPacket));
    EXPECT_TRUE(hasCausticsSeed(worldPosition, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(depth, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(currentBindlessSlots, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(materialContextSlots, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(sceneBvhNodes, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(sceneInstances, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(shadowInstanceMaterials, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(shadowMaterialTyped, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(shadowInstances, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshNodes, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshPositions, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshIndices, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(softwareMeshAttributes, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(sceneShading, shadowPacket));
    EXPECT_TRUE(hasCausticsSeed(lights, shadowPacket));

    const auto causticsPacketWaitsFor = [&](const Graphics::GpuSubmissionPacketId producer){
        const Graphics::GpuSubmissionPacket& packet = compiledGraph.packet(causticsPacket);
        const Graphics::GpuPacketDependency* const dependencies = compiledGraph.packetDependencies(causticsPacket);
        if(packet.dependencyCount != 0u && !dependencies)
            return false;
        for(usize dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
            if(dependencies[dependencyIndex].producer == producer)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(causticsPacketWaitsFor(shadowPreparePacket));
    EXPECT_TRUE(causticsPacketWaitsFor(prefixPacket));
    EXPECT_TRUE(causticsPacketWaitsFor(shadowPacket));

    const Graphics::GpuCompiledBarrier* const causticsBarriers = compiledGraph.taskPrologueBarriers(softwareCausticsTask);
    ASSERT_NE(causticsBarriers, nullptr);
    const auto hasCausticsBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledCaustics->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = causticsBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_FALSE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        depth,
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceStates::DepthRead
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticAccumulator,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticHistory,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticResolveHalf,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticResolveGeometry,
        Graphics::ResourceStates::Common,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        causticIrradiance,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    ASSERT_EQ(compiledLighting->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const lightingSeed = compiledGraph.taskPrologueStateSeeds(lighting);
    ASSERT_NE(lightingSeed, nullptr);
    EXPECT_EQ(lightingSeed[0u].resource, causticIrradiance);
    EXPECT_EQ(lightingSeed[0u].sourcePacket, causticsPacket);
    ASSERT_EQ(compiledLighting->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const lightingBarrier = compiledGraph.taskPrologueBarriers(lighting);
    ASSERT_NE(lightingBarrier, nullptr);
    EXPECT_EQ(lightingBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(lightingBarrier[0u].resource, causticIrradiance);
    EXPECT_EQ(lightingBarrier[0u].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(lightingBarrier[0u].after, Graphics::ResourceStates::ShaderResource);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(lightingPacket)[0u].producer, causticsPacket);
}


// Photon, geometry, prepare, five wavelets, and upsample occupy distinct graph callbacks. Their packet remains
// unsplit, while compiler barriers own the immutable inputs and every ping-pong UAV-to-SRV handoff.
TEST(GpuTaskGraph, PlansGraphOwnedCausticPhotonGeometryPrepareFiveWaveletAndUpsampleHandoffs){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_accumulator"),
        "Caustic Photon Resolve Accumulator",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(accumulator.valid());
    const Graphics::GpuGraphResourceId geometry = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_geometry"),
        "Caustic Photon Resolve Geometry",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(geometry.valid());
    const Graphics::GpuGraphResourceId history = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_history"),
        "Caustic Photon Resolve History",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(history.valid());
    const Graphics::GpuGraphResourceId resolveHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/caustic_photon_resolve_half"),
        "Caustic Photon Resolve Half",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(resolveHalf.valid());

    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    Graphics::GpuTaskSchedulingHint photonScheduling;
    photonScheduling.cost = Graphics::GpuTaskCostHint::Large;
    photonScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse photonUses[] = {
        {
            .resource = accumulator,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc photonDesc;
    photonDesc
        .setIdentity(Name("tests/task_graph/caustic_photon_stage"))
        .setMarkerLabel("Caustic Photons")
        .setQueue(computeRequest)
        .setScheduling(photonScheduling)
        .setResourceUses(photonUses, LengthOf(photonUses))
    ;
    const Graphics::GpuTaskId photonTask = graph.addTask(photonDesc);
    ASSERT_TRUE(photonTask.valid());

    const Graphics::GpuTaskResourceUse geometryUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskSchedulingHint geometryScheduling = photonScheduling;
    geometryScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc geometryDesc;
    geometryDesc
        .setIdentity(Name("tests/task_graph/caustic_geometry_stage"))
        .setMarkerLabel("Caustic Geometry")
        .setQueue(computeRequest)
        .setScheduling(geometryScheduling)
        .setDependencies(&photonTask, 1u)
        .setResourceUses(geometryUses, LengthOf(geometryUses))
    ;
    const Graphics::GpuTaskId geometryTask = graph.addTask(geometryDesc);
    ASSERT_TRUE(geometryTask.valid());

    Graphics::GpuTaskSchedulingHint prepareScheduling = geometryScheduling;
    prepareScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse prepareUses[] = {
        {
            .resource = accumulator,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_prepare_stage"))
        .setMarkerLabel("Caustics Resolve Prepare")
        .setQueue(computeRequest)
        .setScheduling(prepareScheduling)
        .setDependencies(&geometryTask, 1u)
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    const Graphics::GpuTaskId prepareTask = graph.addTask(prepareDesc);
    ASSERT_TRUE(prepareTask.valid());

    Graphics::GpuTaskSchedulingHint waveletScheduling = prepareScheduling;
    waveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse waveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc waveletDesc;
    waveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Wavelet")
        .setQueue(computeRequest)
        .setScheduling(waveletScheduling)
        .setDependencies(&prepareTask, 1u)
        .setResourceUses(waveletUses, LengthOf(waveletUses))
    ;
    const Graphics::GpuTaskId waveletTask = graph.addTask(waveletDesc);
    ASSERT_TRUE(waveletTask.valid());

    Graphics::GpuTaskSchedulingHint secondWaveletScheduling = waveletScheduling;
    secondWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse secondWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc secondWaveletDesc;
    secondWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_second_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Second Wavelet")
        .setQueue(computeRequest)
        .setScheduling(secondWaveletScheduling)
        .setDependencies(&waveletTask, 1u)
        .setResourceUses(secondWaveletUses, LengthOf(secondWaveletUses))
    ;
    const Graphics::GpuTaskId secondWaveletTask = graph.addTask(secondWaveletDesc);
    ASSERT_TRUE(secondWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint thirdWaveletScheduling = secondWaveletScheduling;
    thirdWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse thirdWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc thirdWaveletDesc;
    thirdWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_third_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Third Wavelet")
        .setQueue(computeRequest)
        .setScheduling(thirdWaveletScheduling)
        .setDependencies(&secondWaveletTask, 1u)
        .setResourceUses(thirdWaveletUses, LengthOf(thirdWaveletUses))
    ;
    const Graphics::GpuTaskId thirdWaveletTask = graph.addTask(thirdWaveletDesc);
    ASSERT_TRUE(thirdWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint fourthWaveletScheduling = thirdWaveletScheduling;
    fourthWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse fourthWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc fourthWaveletDesc;
    fourthWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_fourth_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Fourth Wavelet")
        .setQueue(computeRequest)
        .setScheduling(fourthWaveletScheduling)
        .setDependencies(&thirdWaveletTask, 1u)
        .setResourceUses(fourthWaveletUses, LengthOf(fourthWaveletUses))
    ;
    const Graphics::GpuTaskId fourthWaveletTask = graph.addTask(fourthWaveletDesc);
    ASSERT_TRUE(fourthWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint fifthWaveletScheduling = fourthWaveletScheduling;
    fifthWaveletScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse fifthWaveletUses[] = {
        {
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = history,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc fifthWaveletDesc;
    fifthWaveletDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_fifth_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Fifth Wavelet")
        .setQueue(computeRequest)
        .setScheduling(fifthWaveletScheduling)
        .setDependencies(&fourthWaveletTask, 1u)
        .setResourceUses(fifthWaveletUses, LengthOf(fifthWaveletUses))
    ;
    const Graphics::GpuTaskId fifthWaveletTask = graph.addTask(fifthWaveletDesc);
    ASSERT_TRUE(fifthWaveletTask.valid());

    Graphics::GpuTaskSchedulingHint tailScheduling = fifthWaveletScheduling;
    tailScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse tailUses[] = {
        {
            .resource = resolveHalf,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc tailDesc;
    tailDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_upsample_stage"))
        .setMarkerLabel("Caustics Resolve Upsample")
        .setQueue(computeRequest)
        .setScheduling(tailScheduling)
        .setDependencies(&fifthWaveletTask, 1u)
        .setResourceUses(tailUses, LengthOf(tailUses))
    ;
    const Graphics::GpuTaskId tailTask = graph.addTask(tailDesc);
    ASSERT_TRUE(tailTask.valid());

    Graphics::GpuTaskSchedulingHint timingCloseScheduling = tailScheduling;
    timingCloseScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc timingCloseDesc;
    timingCloseDesc
        .setIdentity(Name("tests/task_graph/caustic_resolve_timing_close"))
        .setMarkerLabel("Caustics Resolve Timing Close")
        .setQueue(computeRequest)
        .setScheduling(timingCloseScheduling)
        .setDependencies(&tailTask, 1u)
    ;
    const Graphics::GpuTaskId timingCloseTask = graph.addTask(timingCloseDesc);
    ASSERT_TRUE(timingCloseTask.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        photonTask,
        prepareTask,
        accumulator,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        geometryTask,
        prepareTask,
        geometry,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prepareTask,
        waveletTask,
        history,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        waveletTask,
        secondWaveletTask,
        resolveHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        secondWaveletTask,
        thirdWaveletTask,
        history,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        thirdWaveletTask,
        fourthWaveletTask,
        resolveHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        fourthWaveletTask,
        fifthWaveletTask,
        history,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        fifthWaveletTask,
        tailTask,
        resolveHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuSubmissionPacketId photonPacket = compiledGraph.packetForTask(photonTask);
    const Graphics::GpuSubmissionPacketId geometryPacket = compiledGraph.packetForTask(geometryTask);
    const Graphics::GpuSubmissionPacketId preparePacket = compiledGraph.packetForTask(prepareTask);
    const Graphics::GpuSubmissionPacketId waveletPacket = compiledGraph.packetForTask(waveletTask);
    const Graphics::GpuSubmissionPacketId secondWaveletPacket = compiledGraph.packetForTask(secondWaveletTask);
    const Graphics::GpuSubmissionPacketId thirdWaveletPacket = compiledGraph.packetForTask(thirdWaveletTask);
    const Graphics::GpuSubmissionPacketId fourthWaveletPacket = compiledGraph.packetForTask(fourthWaveletTask);
    const Graphics::GpuSubmissionPacketId fifthWaveletPacket = compiledGraph.packetForTask(fifthWaveletTask);
    const Graphics::GpuSubmissionPacketId tailPacket = compiledGraph.packetForTask(tailTask);
    const Graphics::GpuSubmissionPacketId timingClosePacket = compiledGraph.packetForTask(timingCloseTask);
    ASSERT_TRUE(photonPacket.valid());
    ASSERT_TRUE(geometryPacket.valid());
    ASSERT_TRUE(preparePacket.valid());
    ASSERT_TRUE(waveletPacket.valid());
    ASSERT_TRUE(secondWaveletPacket.valid());
    ASSERT_TRUE(thirdWaveletPacket.valid());
    ASSERT_TRUE(fourthWaveletPacket.valid());
    ASSERT_TRUE(fifthWaveletPacket.valid());
    ASSERT_TRUE(tailPacket.valid());
    ASSERT_TRUE(timingClosePacket.valid());
    EXPECT_EQ(compiledGraph.packetCount(), 1u);
    EXPECT_EQ(geometryPacket, photonPacket);
    EXPECT_EQ(preparePacket, photonPacket);
    EXPECT_EQ(waveletPacket, photonPacket);
    EXPECT_EQ(secondWaveletPacket, photonPacket);
    EXPECT_EQ(thirdWaveletPacket, photonPacket);
    EXPECT_EQ(fourthWaveletPacket, photonPacket);
    EXPECT_EQ(fifthWaveletPacket, photonPacket);
    EXPECT_EQ(tailPacket, photonPacket);
    EXPECT_EQ(timingClosePacket, photonPacket);

    const Graphics::GpuCompiledTask* const compiledPrepare = compiledGraph.findTask(prepareTask);
    const Graphics::GpuCompiledTask* const compiledWavelet = compiledGraph.findTask(waveletTask);
    const Graphics::GpuCompiledTask* const compiledSecondWavelet = compiledGraph.findTask(secondWaveletTask);
    const Graphics::GpuCompiledTask* const compiledThirdWavelet = compiledGraph.findTask(thirdWaveletTask);
    const Graphics::GpuCompiledTask* const compiledFourthWavelet = compiledGraph.findTask(fourthWaveletTask);
    const Graphics::GpuCompiledTask* const compiledFifthWavelet = compiledGraph.findTask(fifthWaveletTask);
    const Graphics::GpuCompiledTask* const compiledTail = compiledGraph.findTask(tailTask);
    ASSERT_NE(compiledPrepare, nullptr);
    ASSERT_NE(compiledWavelet, nullptr);
    ASSERT_NE(compiledSecondWavelet, nullptr);
    ASSERT_NE(compiledThirdWavelet, nullptr);
    ASSERT_NE(compiledFourthWavelet, nullptr);
    ASSERT_NE(compiledFifthWavelet, nullptr);
    ASSERT_NE(compiledTail, nullptr);
    const Graphics::GpuCompiledBarrier* const prepareBarriers = compiledGraph.taskPrologueBarriers(prepareTask);
    const Graphics::GpuCompiledBarrier* const waveletBarriers = compiledGraph.taskPrologueBarriers(waveletTask);
    const Graphics::GpuCompiledBarrier* const secondWaveletBarriers = compiledGraph.taskPrologueBarriers(secondWaveletTask);
    const Graphics::GpuCompiledBarrier* const thirdWaveletBarriers = compiledGraph.taskPrologueBarriers(thirdWaveletTask);
    const Graphics::GpuCompiledBarrier* const fourthWaveletBarriers = compiledGraph.taskPrologueBarriers(fourthWaveletTask);
    const Graphics::GpuCompiledBarrier* const fifthWaveletBarriers = compiledGraph.taskPrologueBarriers(fifthWaveletTask);
    const Graphics::GpuCompiledBarrier* const tailBarriers = compiledGraph.taskPrologueBarriers(tailTask);
    ASSERT_NE(prepareBarriers, nullptr);
    ASSERT_NE(waveletBarriers, nullptr);
    ASSERT_NE(secondWaveletBarriers, nullptr);
    ASSERT_NE(thirdWaveletBarriers, nullptr);
    ASSERT_NE(fourthWaveletBarriers, nullptr);
    ASSERT_NE(fifthWaveletBarriers, nullptr);
    ASSERT_NE(tailBarriers, nullptr);
    bool hasAccumulatorHandoff = false;
    bool hasGeometryHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledPrepare->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = prepareBarriers[barrierIndex];
        hasAccumulatorHandoff = hasAccumulatorHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == accumulator
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
        hasGeometryHandoff = hasGeometryHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == geometry
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasAccumulatorHandoff);
    EXPECT_TRUE(hasGeometryHandoff);

    bool hasPrepareWaveletHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = waveletBarriers[barrierIndex];
        hasPrepareWaveletHandoff = hasPrepareWaveletHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == history
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasPrepareWaveletHandoff);

    bool hasWaveletSecondHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledSecondWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = secondWaveletBarriers[barrierIndex];
        hasWaveletSecondHandoff = hasWaveletSecondHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasWaveletSecondHandoff);

    bool hasSecondThirdHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledThirdWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = thirdWaveletBarriers[barrierIndex];
        hasSecondThirdHandoff = hasSecondThirdHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == history
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasSecondThirdHandoff);

    bool hasThirdFourthHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledFourthWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = fourthWaveletBarriers[barrierIndex];
        hasThirdFourthHandoff = hasThirdFourthHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasThirdFourthHandoff);

    bool hasFourthFifthHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledFifthWavelet->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = fifthWaveletBarriers[barrierIndex];
        hasFourthFifthHandoff = hasFourthFifthHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == history
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasFourthFifthHandoff);

    bool hasFifthUpsampleHandoff = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledTail->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = tailBarriers[barrierIndex];
        hasFifthUpsampleHandoff = hasFifthUpsampleHandoff || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalf
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasFifthUpsampleHandoff);
}


// Surfel GI starts after the shared graphics prefix has produced G-buffer, descriptor, traversal, and persistent
// resource data. Age/free, the graph-owned cell-head reset, Hash Build, Spawn, trace-build-args, Trace, and Resolve
// share one Compute packet; Upsample must inherit the compiler-owned half-irradiance result, and Lighting must own
// the final irradiance UAV-to-SRV handoff without a native bridge.
TEST(GpuTaskGraph, PlansGraphOwnedSurfelGiResolveAndEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_world_position"),
        "Surfel GI World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId normal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_normal"),
        "Surfel GI Normal",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId irradianceHalf = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_irradiance_half"),
        "Surfel GI Irradiance Half",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId irradiance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_irradiance"),
        "Surfel GI Irradiance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_bindless_slots"),
        "Surfel GI Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_material_context_slots"),
        "Surfel GI Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId surfelConstants = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_constants"),
        "Surfel GI Constants",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_scene_shading"),
        "Surfel GI Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId traceGeometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_trace_geometry"),
        "Surfel GI Trace Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId poolSnapshot = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_pool_snapshot"),
        "Surfel GI Pool Snapshot",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId cellHeadSnapshot = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_cell_head_snapshot"),
        "Surfel GI Cell Head Snapshot",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_lights"),
        "Surfel GI Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId pool = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_pool"),
        "Surfel GI Pool",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId cellHeads = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_cell_heads"),
        "Surfel GI Cell Heads",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId counter = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_counter"),
        "Surfel GI Counter",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId traceArgs = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_trace_args"),
        "Surfel GI Trace Arguments",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId freeList = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_gi_free_list"),
        "Surfel GI Free List",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(normal.valid());
    ASSERT_TRUE(irradianceHalf.valid());
    ASSERT_TRUE(irradiance.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(surfelConstants.valid());
    ASSERT_TRUE(sceneShading.valid());
    ASSERT_TRUE(traceGeometry.valid());
    ASSERT_TRUE(poolSnapshot.valid());
    ASSERT_TRUE(cellHeadSnapshot.valid());
    ASSERT_TRUE(lights.valid());
    ASSERT_TRUE(pool.valid());
    ASSERT_TRUE(cellHeads.valid());
    ASSERT_TRUE(counter.valid());
    ASSERT_TRUE(traceArgs.valid());
    ASSERT_TRUE(freeList.valid());

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
    const Graphics::GpuQueueRequest computeTransferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = irradianceHalf, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = traceGeometry, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = poolSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeadSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = traceArgs, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/surfel_gi_prefix"))
        .setMarkerLabel("Surfel GI Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    // The typed production clear starts a new Compute packet after the prefix; Surfel GI opts into merging with it,
    // so the CopyDest-to-UAV handoff stays graph-owned without adding a packet to the semantic GI range.
    Graphics::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    clearScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse clearUses[] = {
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_output_clear"))
        .setMarkerLabel("Surfel GI Output Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(clearScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(clearUses, LengthOf(clearUses))
    ;
    const Graphics::GpuTaskId outputClear = graph.addTask(clearDesc);
    ASSERT_TRUE(outputClear.valid());

    Graphics::GpuTaskSchedulingHint surfelScheduling = boundaryScheduling;
    surfelScheduling.forceSubmissionBoundary = false;
    surfelScheduling.allowPacketMerge = true;
    surfelScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskResourceUse ageFreeUses[] = {
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc ageFreeDesc;
    ageFreeDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_age_free"))
        .setMarkerLabel("Surfel GI Age Free")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&outputClear, 1u)
        .setResourceUses(ageFreeUses, LengthOf(ageFreeUses))
    ;
    const Graphics::GpuTaskId ageFree = graph.addTask(ageFreeDesc);
    ASSERT_TRUE(ageFree.valid());

    const Graphics::GpuTaskResourceUse cellHeadClearUses[] = {
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc cellHeadClearDesc;
    cellHeadClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_cell_head_clear"))
        .setMarkerLabel("Surfel GI Cell Head Clear")
        .setQueue(computeTransferRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&ageFree, 1u)
        .setResourceUses(cellHeadClearUses, LengthOf(cellHeadClearUses))
    ;
    const Graphics::GpuTaskId cellHeadClear = graph.addTask(cellHeadClearDesc);
    ASSERT_TRUE(cellHeadClear.valid());

    const Graphics::GpuTaskResourceUse hashBuildUses[] = {
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc hashBuildDesc;
    hashBuildDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_hash_build"))
        .setMarkerLabel("Surfel GI Hash Build")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&cellHeadClear, 1u)
        .setResourceUses(hashBuildUses, LengthOf(hashBuildUses))
    ;
    const Graphics::GpuTaskId hashBuild = graph.addTask(hashBuildDesc);
    ASSERT_TRUE(hashBuild.valid());

    const Graphics::GpuTaskResourceUse spawnUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
    };
    Graphics::GpuTaskDesc spawnDesc;
    spawnDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_spawn"))
        .setMarkerLabel("Surfel GI Spawn")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&hashBuild, 1u)
        .setResourceUses(spawnUses, LengthOf(spawnUses))
    ;
    const Graphics::GpuTaskId spawn = graph.addTask(spawnDesc);
    ASSERT_TRUE(spawn.valid());

    const Graphics::GpuTaskResourceUse traceBuildArgsUses[] = {
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = traceArgs, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc traceBuildArgsDesc;
    traceBuildArgsDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_trace_build_args"))
        .setMarkerLabel("Surfel GI Trace Build Args")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&spawn, 1u)
        .setResourceUses(traceBuildArgsUses, LengthOf(traceBuildArgsUses))
    ;
    const Graphics::GpuTaskId traceBuildArgs = graph.addTask(traceBuildArgsDesc);
    ASSERT_TRUE(traceBuildArgs.valid());

    const Graphics::GpuTaskResourceUse traceUses[] = {
        { .resource = currentBindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = traceGeometry, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = poolSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = cellHeadSnapshot, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = traceArgs, .range = {}, .requiredState = Graphics::ResourceStates::IndirectArgument, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    Graphics::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_trace"))
        .setMarkerLabel("Surfel GI Trace")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&traceBuildArgs, 1u)
        .setResourceUses(traceUses, LengthOf(traceUses))
    ;
    const Graphics::GpuTaskId trace = graph.addTask(traceDesc);
    ASSERT_TRUE(trace.valid());

    const Graphics::GpuTaskResourceUse resolveUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = surfelConstants, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = irradianceHalf, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc resolveDesc;
    resolveDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_resolve"))
        .setMarkerLabel("Surfel GI Resolve")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&trace, 1u)
        .setResourceUses(resolveUses, LengthOf(resolveUses))
    ;
    const Graphics::GpuTaskId resolve = graph.addTask(resolveDesc);
    ASSERT_TRUE(resolve.valid());

    const Graphics::GpuTaskResourceUse surfelUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = normal, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = irradianceHalf, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc surfelDesc;
    surfelDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(computeRequest)
        .setScheduling(surfelScheduling)
        .setDependencies(&resolve, 1u)
        .setResourceUses(surfelUses, LengthOf(surfelUses))
    ;
    const Graphics::GpuTaskId surfelGi = graph.addTask(surfelDesc);
    ASSERT_TRUE(surfelGi.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_gi_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&surfelGi, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

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
    Graphics::GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
    EXPECT_TRUE(analysis.hasExplicitEdge(ageFree, cellHeadClear));
    EXPECT_TRUE(analysis.hasExplicitEdge(cellHeadClear, hashBuild));
    EXPECT_TRUE(analysis.hasExplicitEdge(hashBuild, spawn));
    EXPECT_TRUE(analysis.hasExplicitEdge(spawn, traceBuildArgs));
    EXPECT_TRUE(analysis.hasExplicitEdge(traceBuildArgs, trace));
    EXPECT_TRUE(analysis.hasExplicitEdge(trace, resolve));
    EXPECT_TRUE(analysis.hasExplicitEdge(resolve, surfelGi));
    EXPECT_TRUE(analysis.hasExplicitEdge(surfelGi, lighting));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prefix,
        ageFree,
        pool,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    ASSERT_TRUE(HasInferredHazard(
        analysis,
        outputClear,
        surfelGi,
        irradiance,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        ageFree,
        hashBuild,
        pool,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        cellHeadClear,
        hashBuild,
        cellHeads,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        hashBuild,
        spawn,
        cellHeads,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        spawn,
        traceBuildArgs,
        counter,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        traceBuildArgs,
        trace,
        traceArgs,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        trace,
        resolve,
        pool,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        spawn,
        resolve,
        cellHeads,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        resolve,
        surfelGi,
        irradianceHalf,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        surfelGi,
        lighting,
        irradiance,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const clearAssignment = assignments.find(outputClear);
    ASSERT_NE(clearAssignment, nullptr);
    EXPECT_EQ(clearAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuTaskQueueAssignment* const surfelAssignment = assignments.find(surfelGi);
    ASSERT_NE(surfelAssignment, nullptr);
    EXPECT_EQ(surfelAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    ASSERT_NE(lightingAssignment, nullptr);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId clearPacket = compiledGraph.packetForTask(outputClear);
    const Graphics::GpuSubmissionPacketId ageFreePacket = compiledGraph.packetForTask(ageFree);
    const Graphics::GpuSubmissionPacketId cellHeadClearPacket = compiledGraph.packetForTask(cellHeadClear);
    const Graphics::GpuSubmissionPacketId hashBuildPacket = compiledGraph.packetForTask(hashBuild);
    const Graphics::GpuSubmissionPacketId spawnPacket = compiledGraph.packetForTask(spawn);
    const Graphics::GpuSubmissionPacketId traceBuildArgsPacket = compiledGraph.packetForTask(traceBuildArgs);
    const Graphics::GpuSubmissionPacketId tracePacket = compiledGraph.packetForTask(trace);
    const Graphics::GpuSubmissionPacketId resolvePacket = compiledGraph.packetForTask(resolve);
    const Graphics::GpuSubmissionPacketId surfelPacket = compiledGraph.packetForTask(surfelGi);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(ageFreePacket.valid());
    ASSERT_TRUE(cellHeadClearPacket.valid());
    ASSERT_TRUE(hashBuildPacket.valid());
    ASSERT_TRUE(spawnPacket.valid());
    ASSERT_TRUE(traceBuildArgsPacket.valid());
    ASSERT_TRUE(tracePacket.valid());
    ASSERT_TRUE(resolvePacket.valid());
    ASSERT_TRUE(surfelPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_NE(prefixPacket, surfelPacket);
    EXPECT_EQ(clearPacket, surfelPacket);
    EXPECT_EQ(ageFreePacket, surfelPacket);
    EXPECT_EQ(cellHeadClearPacket, surfelPacket);
    EXPECT_EQ(hashBuildPacket, surfelPacket);
    EXPECT_EQ(spawnPacket, surfelPacket);
    EXPECT_EQ(traceBuildArgsPacket, surfelPacket);
    EXPECT_EQ(tracePacket, surfelPacket);
    EXPECT_EQ(resolvePacket, surfelPacket);
    EXPECT_NE(lightingPacket, surfelPacket);
    EXPECT_EQ(compiledGraph.packetCount(), 3u);
    ASSERT_NE(compiledGraph.packetTasks(surfelPacket), nullptr);
    ASSERT_EQ(compiledGraph.packet(surfelPacket).taskCount, 9u);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[0u], outputClear);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[1u], ageFree);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[2u], cellHeadClear);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[3u], hashBuild);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[4u], spawn);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[5u], traceBuildArgs);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[6u], trace);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[7u], resolve);
    EXPECT_EQ(compiledGraph.packetTasks(surfelPacket)[8u], surfelGi);
    const Graphics::GpuCompiledTask* const compiledAgeFree = compiledGraph.findTask(ageFree);
    ASSERT_NE(compiledAgeFree, nullptr);
    const Graphics::GpuPacketStateSeed* const ageFreeSeeds = compiledGraph.taskPrologueStateSeeds(ageFree);
    ASSERT_NE(ageFreeSeeds, nullptr);
    const auto hasAgeFreeSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledAgeFree->prologueStateSeedCount; ++seedIndex){
            if(
                ageFreeSeeds[seedIndex].resource == resource
                && ageFreeSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasAgeFreeSeed(surfelConstants));
    EXPECT_TRUE(hasAgeFreeSeed(pool));
    EXPECT_TRUE(hasAgeFreeSeed(counter));
    EXPECT_TRUE(hasAgeFreeSeed(freeList));
    const Graphics::GpuCompiledTask* const compiledSpawn = compiledGraph.findTask(spawn);
    ASSERT_NE(compiledSpawn, nullptr);
    const Graphics::GpuPacketStateSeed* const spawnSeeds = compiledGraph.taskPrologueStateSeeds(spawn);
    ASSERT_NE(spawnSeeds, nullptr);
    const auto hasSpawnSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledSpawn->prologueStateSeedCount; ++seedIndex){
            if(
                spawnSeeds[seedIndex].resource == resource
                && spawnSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasSpawnSeed(worldPosition));
    EXPECT_TRUE(hasSpawnSeed(normal));
    const Graphics::GpuCompiledTask* const compiledTraceBuildArgs = compiledGraph.findTask(traceBuildArgs);
    ASSERT_NE(compiledTraceBuildArgs, nullptr);
    const Graphics::GpuPacketStateSeed* const traceBuildArgsSeeds = compiledGraph.taskPrologueStateSeeds(traceBuildArgs);
    ASSERT_NE(traceBuildArgsSeeds, nullptr);
    const auto hasTraceBuildArgsSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledTraceBuildArgs->prologueStateSeedCount; ++seedIndex){
            if(
                traceBuildArgsSeeds[seedIndex].resource == resource
                && traceBuildArgsSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTraceBuildArgsSeed(traceArgs));
    const Graphics::GpuCompiledTask* const compiledTrace = compiledGraph.findTask(trace);
    ASSERT_NE(compiledTrace, nullptr);
    const Graphics::GpuPacketStateSeed* const traceSeeds = compiledGraph.taskPrologueStateSeeds(trace);
    ASSERT_NE(traceSeeds, nullptr);
    const auto hasTraceSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledTrace->prologueStateSeedCount; ++seedIndex){
            if(
                traceSeeds[seedIndex].resource == resource
                && traceSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTraceSeed(currentBindlessSlots));
    EXPECT_TRUE(hasTraceSeed(traceGeometry));
    EXPECT_TRUE(hasTraceSeed(poolSnapshot));
    const Graphics::GpuCompiledTask* const compiledResolve = compiledGraph.findTask(resolve);
    ASSERT_NE(compiledResolve, nullptr);
    const Graphics::GpuPacketStateSeed* const resolveSeeds = compiledGraph.taskPrologueStateSeeds(resolve);
    ASSERT_NE(resolveSeeds, nullptr);
    const auto hasResolveSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledResolve->prologueStateSeedCount; ++seedIndex){
            if(
                resolveSeeds[seedIndex].resource == resource
                && resolveSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasResolveSeed(irradianceHalf));

    const Graphics::GpuCompiledBarrier* const ageFreeBarriers = compiledGraph.taskPrologueBarriers(ageFree);
    ASSERT_NE(ageFreeBarriers, nullptr);
    const auto hasAgeFreeBarrier = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledAgeFree->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = ageFreeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasAgeFreeBarrier(
        surfelConstants,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasAgeFreeBarrier(
        pool,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasAgeFreeBarrier(
        counter,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasAgeFreeBarrier(
        freeList,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledTask* const compiledHashBuild = compiledGraph.findTask(hashBuild);
    ASSERT_NE(compiledHashBuild, nullptr);
    const Graphics::GpuCompiledBarrier* const hashBuildBarriers = compiledGraph.taskPrologueBarriers(hashBuild);
    ASSERT_NE(hashBuildBarriers, nullptr);
    const auto hasHashBuildBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledHashBuild->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = hashBuildBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasHashBuildBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        cellHeads,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledBarrier* const spawnBarriers = compiledGraph.taskPrologueBarriers(spawn);
    ASSERT_NE(spawnBarriers, nullptr);
    const auto hasSpawnBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledSpawn->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = spawnBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasSpawnBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        worldPosition,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasSpawnBarrier(
        Graphics::GpuCompiledBarrierType::BufferUav,
        cellHeads,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledBarrier* const traceBuildArgsBarriers = compiledGraph.taskPrologueBarriers(traceBuildArgs);
    ASSERT_NE(traceBuildArgsBarriers, nullptr);
    const auto hasTraceBuildArgsBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledTraceBuildArgs->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = traceBuildArgsBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTraceBuildArgsBarrier(
        Graphics::GpuCompiledBarrierType::BufferUav,
        counter,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTraceBuildArgsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        traceArgs,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));

    const Graphics::GpuCompiledBarrier* const traceBarriers = compiledGraph.taskPrologueBarriers(trace);
    ASSERT_NE(traceBarriers, nullptr);
    const auto hasTraceBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledTrace->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = traceBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTraceBarrier(
        Graphics::GpuCompiledBarrierType::BufferUav,
        pool,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasTraceBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        traceArgs,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::IndirectArgument
    ));

    const Graphics::GpuCompiledBarrier* const resolveBarriers = compiledGraph.taskPrologueBarriers(resolve);
    ASSERT_NE(resolveBarriers, nullptr);
    const auto hasResolveBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledResolve->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = resolveBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasResolveBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        pool,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasResolveBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        cellHeads,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasResolveBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradianceHalf,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    const Graphics::GpuCompiledTask* const compiledSurfel = compiledGraph.findTask(surfelGi);
    ASSERT_NE(compiledSurfel, nullptr);
    const Graphics::GpuCompiledBarrier* const surfelBarriers = compiledGraph.taskPrologueBarriers(surfelGi);
    ASSERT_NE(surfelBarriers, nullptr);
    const auto hasSurfelBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledSurfel->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = surfelBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasSurfelBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradianceHalf,
        Graphics::ResourceStates::UnorderedAccess,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasSurfelBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradiance,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    ASSERT_EQ(compiledGraph.packet(surfelPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(surfelPacket)[0u].producer, prefixPacket);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledLighting, nullptr);
    const Graphics::GpuCompiledBarrier* const lightingBarriers = compiledGraph.taskPrologueBarriers(lighting);
    ASSERT_NE(lightingBarriers, nullptr);
    bool lightingTransitionsIrradiance = false;
    for(usize barrierIndex = 0u; barrierIndex < compiledLighting->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = lightingBarriers[barrierIndex];
        lightingTransitionsIrradiance = lightingTransitionsIrradiance
            || (
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == irradiance
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::ShaderResource
            )
        ;
    }
    EXPECT_TRUE(lightingTransitionsIrradiance);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(lightingPacket)[0u].producer, surfelPacket);
}


// Surfel initialization is a graph task in its own right. The callback only clears four persistent buffers, so the
// graph must lower their inherited UAV state to CopyDest before recording begins and leave that state for snapshot.
TEST(GpuTaskGraph, PlansGraphOwnedSurfelInitializationEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId pool = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_pool"),
        "Surfel Pool",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId cellHeads = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_cell_heads"),
        "Surfel Cell Heads",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId counter = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_counter"),
        "Surfel Counter",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId freeList = AddBufferMetadata(
        graph,
        Name("tests/task_graph/surfel_initialize_free_list"),
        "Surfel Free List",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(pool.valid());
    ASSERT_TRUE(cellHeads.valid());
    ASSERT_TRUE(counter.valid());
    ASSERT_TRUE(freeList.valid());

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
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/surfel_initialize_prefix"))
        .setMarkerLabel("Surfel Initialize Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuTaskResourceUse initializeUses[] = {
        { .resource = pool, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = cellHeads, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = counter, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = freeList, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc initializeDesc;
    initializeDesc
        .setIdentity(Name("tests/task_graph/graph_owned_surfel_initialize"))
        .setMarkerLabel("Surfel GI Initialize")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(initializeUses, LengthOf(initializeUses))
    ;
    const Graphics::GpuTaskId initialize = graph.addTask(initializeDesc);
    ASSERT_TRUE(initialize.valid());

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
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prefix,
        initialize,
        pool,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const initializeAssignment = assignments.find(initialize);
    ASSERT_NE(initializeAssignment, nullptr);
    EXPECT_EQ(initializeAssignment->queueClass, Graphics::CommandQueue::Compute);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId initializePacket = compiledGraph.packetForTask(initialize);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(initializePacket.valid());
    EXPECT_NE(prefixPacket, initializePacket);
    const Graphics::GpuCompiledTask* const compiledInitialize = compiledGraph.findTask(initialize);
    ASSERT_NE(compiledInitialize, nullptr);
    const Graphics::GpuPacketStateSeed* const initializeSeeds = compiledGraph.taskPrologueStateSeeds(initialize);
    ASSERT_NE(initializeSeeds, nullptr);
    const auto hasInitializeSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(usize seedIndex = 0u; seedIndex < compiledInitialize->prologueStateSeedCount; ++seedIndex){
            if(
                initializeSeeds[seedIndex].resource == resource
                && initializeSeeds[seedIndex].sourcePacket == prefixPacket
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasInitializeSeed(pool));
    EXPECT_TRUE(hasInitializeSeed(cellHeads));
    EXPECT_TRUE(hasInitializeSeed(counter));
    EXPECT_TRUE(hasInitializeSeed(freeList));

    const Graphics::GpuCompiledBarrier* const initializeBarriers = compiledGraph.taskPrologueBarriers(initialize);
    ASSERT_NE(initializeBarriers, nullptr);
    const auto hasInitializeBarrier = [&](const Graphics::GpuGraphResourceId resource){
        for(usize barrierIndex = 0u; barrierIndex < compiledInitialize->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = initializeBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::CopyDest
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasInitializeBarrier(pool));
    EXPECT_TRUE(hasInitializeBarrier(cellHeads));
    EXPECT_TRUE(hasInitializeBarrier(counter));
    EXPECT_TRUE(hasInitializeBarrier(freeList));
    ASSERT_EQ(compiledGraph.packet(initializePacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(initializePacket)[0u].producer, prefixPacket);
}


// Hardware caustics inherits the opaque prefix's descriptor-selected producer inputs on its Graphics packet. The
// producer's fresh-accumulator clear and warm temporal decay are graph-owned. This pins the static entry batch and
// CopyDest-to-UAV handoff the graph must establish before the ray-tracing callback records.
TEST(GpuTaskGraph, PlansGraphOwnedHardwareCausticsEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ;
    const Graphics::GpuGraphResourceId worldPosition = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_world_position"),
        "Hardware Caustics World Position",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId depth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_depth"),
        "Hardware Caustics Depth",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId irradiance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_irradiance"),
        "Hardware Caustics Irradiance",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_accumulator"),
        "Hardware Caustics Accumulator",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId meshAttributes = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_mesh_attributes"),
        "Hardware Caustics Mesh Attributes",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId instanceMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_instance_materials"),
        "Hardware Caustics Instance Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId typedMaterials = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_typed_materials"),
        "Hardware Caustics Typed Materials",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId instances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_instances"),
        "Hardware Caustics Instances",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId emissionTargets = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_emission_targets"),
        "Hardware Caustics Emission Targets",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId lights = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_lights"),
        "Hardware Caustics Lights",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId meshView = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_mesh_view"),
        "Hardware Caustics Mesh View",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId bindlessSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_bindless_slots"),
        "Hardware Caustics Bindless Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_material_context_slots"),
        "Hardware Caustics Material Context Slots",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId sceneShading = AddBufferMetadata(
        graph,
        Name("tests/task_graph/hardware_caustics_scene_shading"),
        "Hardware Caustics Scene Shading",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(worldPosition.valid());
    ASSERT_TRUE(depth.valid());
    ASSERT_TRUE(irradiance.valid());
    ASSERT_TRUE(accumulator.valid());
    ASSERT_TRUE(meshAttributes.valid());
    ASSERT_TRUE(instanceMaterials.valid());
    ASSERT_TRUE(typedMaterials.valid());
    ASSERT_TRUE(instances.valid());
    ASSERT_TRUE(emissionTargets.valid());
    ASSERT_TRUE(lights.valid());
    ASSERT_TRUE(meshView.valid());
    ASSERT_TRUE(bindlessSlots.valid());
    ASSERT_TRUE(materialContextSlots.valid());
    ASSERT_TRUE(sceneShading.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsTransferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::RenderTarget, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::DepthWrite, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = meshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = instanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = typedMaterials, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = instances, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = emissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = bindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/hardware_caustics_prefix"))
        .setMarkerLabel("Hardware Caustics Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    // Keep the no-producer black result in the existing Hardware Caustics Graphics packet.
    Graphics::GpuTaskSchedulingHint irradianceClearScheduling;
    irradianceClearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    irradianceClearScheduling.allowPacketMerge = true;
    const Graphics::GpuTaskResourceUse irradianceClearUses[] = {
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskDesc irradianceClearDesc;
    irradianceClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_hardware_caustics_irradiance_clear"))
        .setMarkerLabel("Hardware Caustics Irradiance Clear")
        .setQueue(graphicsTransferRequest)
        .setScheduling(irradianceClearScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(irradianceClearUses, LengthOf(irradianceClearUses))
    ;
    const Graphics::GpuTaskId irradianceClearTask = graph.addTask(irradianceClearDesc);
    ASSERT_TRUE(irradianceClearTask.valid());

    const Graphics::GpuTaskResourceUse accumulatorBootstrapClearUses[] = {
        { .resource = accumulator, .range = {}, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint accumulatorBootstrapClearScheduling = irradianceClearScheduling;
    accumulatorBootstrapClearScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc accumulatorBootstrapClearDesc;
    accumulatorBootstrapClearDesc
        .setIdentity(Name("tests/task_graph/graph_owned_hardware_caustics_accumulator_bootstrap_clear"))
        .setMarkerLabel("Hardware Caustics Accumulator Bootstrap Clear")
        .setQueue(graphicsTransferRequest)
        .setScheduling(accumulatorBootstrapClearScheduling)
        .setDependencies(&irradianceClearTask, 1u)
        .setResourceUses(accumulatorBootstrapClearUses, LengthOf(accumulatorBootstrapClearUses))
    ;
    const Graphics::GpuTaskId accumulatorBootstrapClearTask = graph.addTask(accumulatorBootstrapClearDesc);
    ASSERT_TRUE(accumulatorBootstrapClearTask.valid());

    const Graphics::GpuTaskResourceUse causticsUses[] = {
        { .resource = worldPosition, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = depth, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = meshAttributes, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = instanceMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = typedMaterials, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = instances, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = emissionTargets, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = lights, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = meshView, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = bindlessSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = materialContextSlots, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = sceneShading, .range = {}, .requiredState = Graphics::ResourceStates::ConstantBuffer, .access = Graphics::GpuTaskResourceAccess::Read },
        { .resource = accumulator, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::ReadWrite },
        { .resource = irradiance, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
    };
    Graphics::GpuTaskSchedulingHint causticsScheduling = boundaryScheduling;
    causticsScheduling.forceSubmissionBoundary = false;
    causticsScheduling.allowPacketMerge = true;
    causticsScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc causticsDesc;
    causticsDesc
        .setIdentity(Name("tests/task_graph/graph_owned_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(graphicsRequest)
        .setScheduling(causticsScheduling)
        .setDependencies(&accumulatorBootstrapClearTask, 1u)
        .setResourceUses(causticsUses, LengthOf(causticsUses))
    ;
    const Graphics::GpuTaskId caustics = graph.addTask(causticsDesc);
    ASSERT_TRUE(caustics.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        prefix,
        caustics,
        meshAttributes,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        irradianceClearTask,
        caustics,
        irradiance,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    ASSERT_TRUE(HasInferredHazard(
        analysis,
        accumulatorBootstrapClearTask,
        caustics,
        accumulator,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    const Graphics::GpuTaskQueueAssignment* const causticsAssignment = assignments.find(caustics);
    ASSERT_NE(causticsAssignment, nullptr);
    EXPECT_EQ(causticsAssignment->queueClass, Graphics::CommandQueue::Graphics);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId irradianceClearPacket = compiledGraph.packetForTask(irradianceClearTask);
    const Graphics::GpuSubmissionPacketId accumulatorBootstrapClearPacket =
        compiledGraph.packetForTask(accumulatorBootstrapClearTask);
    const Graphics::GpuSubmissionPacketId causticsPacket = compiledGraph.packetForTask(caustics);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(irradianceClearPacket.valid());
    ASSERT_TRUE(accumulatorBootstrapClearPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    EXPECT_NE(prefixPacket, causticsPacket);
    EXPECT_EQ(irradianceClearPacket, causticsPacket);
    EXPECT_EQ(accumulatorBootstrapClearPacket, causticsPacket);
    EXPECT_EQ(compiledGraph.packetCount(), 2u);
    const Graphics::GpuCompiledTask* const compiledCaustics = compiledGraph.findTask(caustics);
    ASSERT_NE(compiledCaustics, nullptr);
    const Graphics::GpuCompiledBarrier* const causticsBarriers = compiledGraph.taskPrologueBarriers(caustics);
    ASSERT_NE(causticsBarriers, nullptr);
    const auto hasCausticsBarrier = [&](const Graphics::GpuCompiledBarrierType::Enum type, const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask before, const Graphics::ResourceStates::Mask after){
        for(usize barrierIndex = 0u; barrierIndex < compiledCaustics->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = causticsBarriers[barrierIndex];
            if(
                barrier.type == type
                && barrier.resource == resource
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        depth,
        Graphics::ResourceStates::DepthWrite,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        worldPosition,
        Graphics::ResourceStates::RenderTarget,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        meshAttributes,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ShaderResource
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        meshView,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::BufferTransition,
        sceneShading,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::ConstantBuffer
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        irradiance,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    EXPECT_TRUE(hasCausticsBarrier(
        Graphics::GpuCompiledBarrierType::TextureTransition,
        accumulator,
        Graphics::ResourceStates::CopyDest,
        Graphics::ResourceStates::UnorderedAccess
    ));
    ASSERT_EQ(compiledGraph.packet(causticsPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(causticsPacket)[0u].producer, prefixPacket);
}


// A non-temporal accumulator is cleared every frame before the selected photon producer.  The clear starts in
// CopyDest state and the producer must receive the compiler-owned CopyDest -> UAV handoff in the same packet.
// Exercise both software's locked Compute transfer route and hardware's Graphics transfer route.
TEST(GpuTaskGraph, PlansNonTemporalCausticAccumulatorClearBeforePhotonProducer){
    const auto verifyRoute = [](const bool hardwareCaustics){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
            graph,
            Name("tests/task_graph/non_temporal_caustics_accumulator"),
            "Non-Temporal Caustic Accumulator",
            Graphics::ResourceStates::ShaderResource,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
        );
        ASSERT_TRUE(accumulator.valid());

        const Graphics::GpuQueueRequest graphicsUploadRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        };
        const Graphics::GpuQueueRequest computeTransferRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Compute,
            true,
            false,
        };
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
        Graphics::GpuTaskSchedulingHint clearScheduling;
        clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
        clearScheduling.allowPacketMerge = true;
        const Graphics::GpuTaskResourceUse clearUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::CopyDest,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        Graphics::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(Name("tests/task_graph/non_temporal_caustics_accumulator_clear"))
            .setMarkerLabel("Caustic Accumulator Clear")
            .setQueue(hardwareCaustics ? graphicsUploadRequest : computeTransferRequest)
            .setScheduling(clearScheduling)
            .setResourceUses(clearUses, LengthOf(clearUses))
        ;
        const Graphics::GpuTaskId clear = graph.addTask(clearDesc);
        ASSERT_TRUE(clear.valid());

        Graphics::GpuTaskSchedulingHint producerScheduling;
        producerScheduling.cost = Graphics::GpuTaskCostHint::Large;
        producerScheduling.allowPacketMerge = true;
        producerScheduling.mergeWithPrevious = true;
        const Graphics::GpuTaskResourceUse producerUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = Graphics::GpuTaskResourceAccess::ReadWrite,
            },
        };
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(Name("tests/task_graph/non_temporal_caustics_photon_producer"))
            .setMarkerLabel(hardwareCaustics ? "Hardware Caustics" : "Software Caustics")
            .setQueue(hardwareCaustics ? graphicsRequest : computeRequest)
            .setScheduling(producerScheduling)
            .setDependencies(&clear, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
        ASSERT_TRUE(producer.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphCompileOptions compileOptions;
        compileOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            clear,
            producer,
            accumulator,
            Graphics::GpuTaskHazardType::WriteAfterWrite
        ));

        const Graphics::GpuTaskQueueAssignment* const clearAssignment = assignments.find(clear);
        const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
        ASSERT_NE(clearAssignment, nullptr);
        ASSERT_NE(producerAssignment, nullptr);
        const Graphics::CommandQueue::Enum expectedQueue = hardwareCaustics
            ? Graphics::CommandQueue::Graphics
            : Graphics::CommandQueue::Compute
        ;
        EXPECT_EQ(clearAssignment->queueClass, expectedQueue);
        EXPECT_EQ(producerAssignment->queueClass, expectedQueue);

        const Graphics::GpuSubmissionPacketId clearPacket = compiledGraph.packetForTask(clear);
        const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
        ASSERT_TRUE(clearPacket.valid());
        EXPECT_EQ(producerPacket, clearPacket);
        ASSERT_EQ(compiledGraph.packetCount(), 1u);
        const Graphics::GpuSubmissionPacket& packet = compiledGraph.packet(clearPacket);
        ASSERT_EQ(packet.taskCount, 2u);
        ASSERT_NE(compiledGraph.packetTasks(clearPacket), nullptr);
        EXPECT_EQ(compiledGraph.packetTasks(clearPacket)[0u], clear);
        EXPECT_EQ(compiledGraph.packetTasks(clearPacket)[1u], producer);

        const Graphics::GpuCompiledTask* const compiledClear = compiledGraph.findTask(clear);
        const Graphics::GpuCompiledTask* const compiledProducer = compiledGraph.findTask(producer);
        ASSERT_NE(compiledClear, nullptr);
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_EQ(compiledClear->prologueBarrierCount, 1u);
        ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const clearBarrier = compiledGraph.taskPrologueBarriers(clear);
        const Graphics::GpuCompiledBarrier* const producerBarrier = compiledGraph.taskPrologueBarriers(producer);
        ASSERT_NE(clearBarrier, nullptr);
        ASSERT_NE(producerBarrier, nullptr);
        EXPECT_EQ(clearBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(clearBarrier[0u].resource, accumulator);
        EXPECT_EQ(clearBarrier[0u].before, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(clearBarrier[0u].after, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(producerBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(producerBarrier[0u].resource, accumulator);
        EXPECT_EQ(producerBarrier[0u].before, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(producerBarrier[0u].after, Graphics::ResourceStates::UnorderedAccess);
    };

    verifyRoute(false);
    verifyRoute(true);
}


// Once temporal caustic history is initialized, its decay must be a real graph task before the selected photon
// producer. The initial ShaderResource -> UAV transition belongs to decay; the producer then needs a same-UAV
// ordering barrier. Exercise both software's Compute route and hardware's Graphics route without a native bridge.
TEST(GpuTaskGraph, PlansWarmCausticAccumulatorDecayBeforePhotonProducer){
    const auto verifyRoute = [](const bool hardwareCaustics){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId accumulator = AddTextureMetadata(
            graph,
            Name("tests/task_graph/warm_caustics_accumulator"),
            "Warm Caustic Accumulator",
            Graphics::ResourceStates::ShaderResource,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
        );
        ASSERT_TRUE(accumulator.valid());

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
        Graphics::GpuTaskSchedulingHint decayScheduling;
        decayScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
        decayScheduling.allowPacketMerge = true;
        const Graphics::GpuTaskResourceUse decayUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = Graphics::GpuTaskResourceAccess::ReadWrite,
            },
        };
        Graphics::GpuTaskDesc decayDesc;
        decayDesc
            .setIdentity(Name("tests/task_graph/warm_caustics_accumulator_decay"))
            .setMarkerLabel("Caustic Accumulator Decay")
            .setQueue(hardwareCaustics ? graphicsRequest : computeRequest)
            .setScheduling(decayScheduling)
            .setResourceUses(decayUses, LengthOf(decayUses))
        ;
        const Graphics::GpuTaskId decay = graph.addTask(decayDesc);
        ASSERT_TRUE(decay.valid());

        Graphics::GpuTaskSchedulingHint producerScheduling = decayScheduling;
        producerScheduling.mergeWithPrevious = true;
        const Graphics::GpuTaskResourceUse producerUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = accumulator,
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = Graphics::GpuTaskResourceAccess::ReadWrite,
            },
        };
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(Name("tests/task_graph/warm_caustics_photon_producer"))
            .setMarkerLabel(hardwareCaustics ? "Hardware Caustics" : "Software Caustics")
            .setQueue(hardwareCaustics ? graphicsRequest : computeRequest)
            .setScheduling(producerScheduling)
            .setDependencies(&decay, 1u)
            .setResourceUses(producerUses, LengthOf(producerUses))
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
        ASSERT_TRUE(producer.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            DedicatedComputeQueue(),
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphCompileOptions compileOptions;
        compileOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, compileOptions));
        EXPECT_TRUE(HasInferredHazard(
            analysis,
            decay,
            producer,
            accumulator,
            Graphics::GpuTaskHazardType::WriteAfterWrite
        ));

        const Graphics::GpuTaskQueueAssignment* const decayAssignment = assignments.find(decay);
        const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
        ASSERT_NE(decayAssignment, nullptr);
        ASSERT_NE(producerAssignment, nullptr);
        const Graphics::CommandQueue::Enum expectedQueue = hardwareCaustics
            ? Graphics::CommandQueue::Graphics
            : Graphics::CommandQueue::Compute
        ;
        EXPECT_EQ(decayAssignment->queueClass, expectedQueue);
        EXPECT_EQ(producerAssignment->queueClass, expectedQueue);

        const Graphics::GpuSubmissionPacketId decayPacket = compiledGraph.packetForTask(decay);
        const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
        ASSERT_TRUE(decayPacket.valid());
        EXPECT_EQ(producerPacket, decayPacket);
        ASSERT_EQ(compiledGraph.packetCount(), 1u);
        const Graphics::GpuSubmissionPacket& packet = compiledGraph.packet(decayPacket);
        ASSERT_EQ(packet.taskCount, 2u);
        ASSERT_NE(compiledGraph.packetTasks(decayPacket), nullptr);
        EXPECT_EQ(compiledGraph.packetTasks(decayPacket)[0u], decay);
        EXPECT_EQ(compiledGraph.packetTasks(decayPacket)[1u], producer);

        const Graphics::GpuCompiledTask* const compiledDecay = compiledGraph.findTask(decay);
        const Graphics::GpuCompiledTask* const compiledProducer = compiledGraph.findTask(producer);
        ASSERT_NE(compiledDecay, nullptr);
        ASSERT_NE(compiledProducer, nullptr);
        ASSERT_EQ(compiledDecay->prologueBarrierCount, 1u);
        ASSERT_EQ(compiledProducer->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const decayBarrier = compiledGraph.taskPrologueBarriers(decay);
        const Graphics::GpuCompiledBarrier* const producerBarrier = compiledGraph.taskPrologueBarriers(producer);
        ASSERT_NE(decayBarrier, nullptr);
        ASSERT_NE(producerBarrier, nullptr);
        EXPECT_EQ(decayBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureTransition);
        EXPECT_EQ(decayBarrier[0u].resource, accumulator);
        EXPECT_EQ(decayBarrier[0u].before, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(decayBarrier[0u].after, Graphics::ResourceStates::UnorderedAccess);
        EXPECT_EQ(producerBarrier[0u].type, Graphics::GpuCompiledBarrierType::TextureUav);
        EXPECT_EQ(producerBarrier[0u].resource, accumulator);
        EXPECT_EQ(producerBarrier[0u].before, Graphics::ResourceStates::UnorderedAccess);
        EXPECT_EQ(producerBarrier[0u].after, Graphics::ResourceStates::UnorderedAccess);
    };

    verifyRoute(false);
    verifyRoute(true);
}


TEST(GpuTaskGraph, MergesRayTraceMaterialContextUploadIntoShadowPreparePacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId materialContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/only_raytrace_material_context_slots"),
        "Ray-Trace Material Context Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(materialContextSlots.valid());

    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest graphicsUploadRequest{
        Graphics::GpuQueueCapability::Transfer,
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
    Graphics::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint shadowPrepareScheduling;
    shadowPrepareScheduling.cost = Graphics::GpuTaskCostHint::Large;
    shadowPrepareScheduling.forceSubmissionBoundary = false;
    shadowPrepareScheduling.allowPacketMerge = true;
    shadowPrepareScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse uploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_slots_upload"))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setQueue(graphicsUploadRequest)
        .setScheduling(uploadScheduling)
        .setResourceUses(uploadUses, LengthOf(uploadUses))
    ;
    const Graphics::GpuTaskId upload = graph.addTask(uploadDesc);
    ASSERT_TRUE(upload.valid());

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(graphicsRequest)
        .setScheduling(shadowPrepareScheduling)
        .setDependencies(&upload, 1u)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/raytrace_material_context_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_EQ(compiledGraph.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId uploadPacket = compiledGraph.packetForTask(upload);
    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledGraph.packetForTask(shadowVisibility);
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    EXPECT_EQ(uploadPacket, shadowPreparePacket);
    EXPECT_NE(shadowPreparePacket, shadowVisibilityPacket);
    EXPECT_EQ(compiledGraph.packet(shadowPreparePacket).taskCount, 2u);
    const Graphics::GpuSubmissionPacketRange shadowPrepareRange = compiledGraph.packetRange(
        shadowPreparePacket,
        shadowPreparePacket
    );
    ASSERT_TRUE(shadowPrepareRange.valid());
    EXPECT_EQ(shadowPrepareRange.packetCount, 1u);
    ASSERT_EQ(compiledGraph.packet(shadowVisibilityPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(shadowVisibilityPacket)[0u].producer, shadowPreparePacket);
}


TEST(GpuTaskGraph, MergesExtinctionUploadChainIntoAsyncAvboitPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId avboitWorking = AddTextureMetadata(
        graph,
        Name("tests/task_graph/extinction_upload_working"),
        "AVBOIT Extinction Working",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/extinction_upload_instances"),
        "AVBOIT Extinction Material Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/extinction_upload_typed"),
        "AVBOIT Extinction Material Typed",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(avboitWorking.valid());
    ASSERT_TRUE(materialInstances.valid());
    ASSERT_TRUE(materialTyped.valid());

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
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint mergeScheduling;
    mergeScheduling.allowPacketMerge = true;
    mergeScheduling.mergeWithPrevious = true;

    const Graphics::GpuTaskResourceUse workingReadWrite[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const Graphics::GpuTaskId preDependency[] = { pre };
    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(preDependency, LengthOf(preDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuTaskResourceUse instanceUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId depthWarpDependency[] = { depthWarp };
    Graphics::GpuTaskDesc instanceUploadDesc;
    instanceUploadDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_instances"))
        .setMarkerLabel("AVBOIT Extinction Material Instances Upload")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
        .setResourceUses(instanceUploadUses, LengthOf(instanceUploadUses))
    ;
    const Graphics::GpuTaskId instanceUpload = graph.addTask(instanceUploadDesc);
    ASSERT_TRUE(instanceUpload.valid());

    const Graphics::GpuTaskResourceUse typedUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId instanceUploadDependency[] = { instanceUpload };
    Graphics::GpuTaskDesc typedUploadDesc;
    typedUploadDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_typed"))
        .setMarkerLabel("AVBOIT Extinction Material Typed Upload")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(instanceUploadDependency, LengthOf(instanceUploadDependency))
        .setResourceUses(typedUploadUses, LengthOf(typedUploadUses))
    ;
    const Graphics::GpuTaskId typedUpload = graph.addTask(typedUploadDesc);
    ASSERT_TRUE(typedUpload.valid());

    const Graphics::GpuTaskResourceUse extinctionUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId typedUploadDependency[] = { typedUpload };
    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_native"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(typedUploadDependency, LengthOf(typedUploadDependency))
        .setResourceUses(extinctionUses, LengthOf(extinctionUses))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskId extinctionDependency[] = { extinction };
    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    const Graphics::GpuTaskId integrationDependency[] = { integration };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/extinction_upload_accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_EQ(compiledGraph.packetCount(), 5u);

    const Graphics::GpuSubmissionPacketId prePacket = compiledGraph.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledGraph.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId instanceUploadPacket = compiledGraph.packetForTask(instanceUpload);
    const Graphics::GpuSubmissionPacketId typedUploadPacket = compiledGraph.packetForTask(typedUpload);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledGraph.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledGraph.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(instanceUploadPacket.valid());
    ASSERT_TRUE(typedUploadPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    EXPECT_NE(prePacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_EQ(instanceUploadPacket, extinctionPacket);
    EXPECT_EQ(typedUploadPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_NE(integrationPacket, accumulationPacket);
    EXPECT_EQ(compiledGraph.packet(extinctionPacket).taskCount, 3u);

    const Graphics::GpuSubmissionPacketRange avboitRange = compiledGraph.packetRange(prePacket, accumulationPacket);
    ASSERT_TRUE(avboitRange.valid());
    EXPECT_EQ(avboitRange.packetCount, 5u);
    ASSERT_EQ(compiledGraph.packet(integrationPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(integrationPacket)[0u].producer, extinctionPacket);
}


TEST(GpuTaskGraph, MergesAccumulationUploadChainIntoAsyncAvboitPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId avboitWorking = AddTextureMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_working"),
        "AVBOIT Accumulation Working",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_instances"),
        "AVBOIT Accumulation Material Instances",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId materialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_typed"),
        "AVBOIT Accumulation Material Typed",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId csgReceiverRanges = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_receiver_ranges"),
        "AVBOIT Accumulation CSG Receiver Ranges",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId csgCutters = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_cutters"),
        "AVBOIT Accumulation CSG Cutters",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId csgClipContext = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_upload_clip_context"),
        "AVBOIT Accumulation CSG Clip Context",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    // The interval producer owns this input. Accumulation consumes but must not re-upload it.
    const Graphics::GpuGraphResourceId csgIntervalSampleState = AddBufferMetadata(
        graph,
        Name("tests/task_graph/accumulation_interval_sample_state"),
        "AVBOIT CSG Interval Sample State",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(avboitWorking.valid());
    ASSERT_TRUE(materialInstances.valid());
    ASSERT_TRUE(materialTyped.valid());
    ASSERT_TRUE(csgReceiverRanges.valid());
    ASSERT_TRUE(csgCutters.valid());
    ASSERT_TRUE(csgClipContext.valid());
    ASSERT_TRUE(csgIntervalSampleState.valid());

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
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint mergeScheduling;
    mergeScheduling.allowPacketMerge = true;
    mergeScheduling.mergeWithPrevious = true;

    const Graphics::GpuTaskResourceUse workingReadWrite[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const Graphics::GpuTaskId preDependency[] = { pre };
    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(preDependency, LengthOf(preDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuTaskId depthWarpDependency[] = { depthWarp };
    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskId extinctionDependency[] = { extinction };
    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    const auto addAccumulationUpload = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::GpuTaskId dependency
    ){
        const Graphics::GpuTaskResourceUse uploadUse[] = {
            Graphics::GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::CopyDest,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        Graphics::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsRequest)
            .setScheduling(mergeScheduling)
            .setDependencies(&dependency, 1u)
            .setResourceUses(uploadUse, LengthOf(uploadUse))
        ;
        return graph.addTask(uploadDesc);
    };

    const Graphics::GpuTaskId instanceUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_instances"),
        "AVBOIT Accumulation Material Instances Upload",
        materialInstances,
        integration
    );
    ASSERT_TRUE(instanceUpload.valid());
    const Graphics::GpuTaskId typedUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_typed"),
        "AVBOIT Accumulation Material Typed Upload",
        materialTyped,
        instanceUpload
    );
    ASSERT_TRUE(typedUpload.valid());
    const Graphics::GpuTaskId receiverRangesUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_receiver_ranges"),
        "AVBOIT Accumulation CSG Receiver Ranges Upload",
        csgReceiverRanges,
        typedUpload
    );
    ASSERT_TRUE(receiverRangesUpload.valid());
    const Graphics::GpuTaskId cuttersUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_cutters"),
        "AVBOIT Accumulation CSG Cutters Upload",
        csgCutters,
        receiverRangesUpload
    );
    ASSERT_TRUE(cuttersUpload.valid());
    const Graphics::GpuTaskId clipContextUpload = addAccumulationUpload(
        Name("tests/task_graph/accumulation_upload_clip_context"),
        "AVBOIT Accumulation CSG Clip Context Slots Upload",
        csgClipContext,
        cuttersUpload
    );
    ASSERT_TRUE(clipContextUpload.valid());

    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgReceiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgCutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgClipContext,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = csgIntervalSampleState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId clipContextUploadDependency[] = { clipContextUpload };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_native"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(clipContextUploadDependency, LengthOf(clipContextUploadDependency))
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    // The final consumer owns the outgoing Graphics -> Compute edge so FrontierSafe can still merge the uploads.
    const Graphics::GpuTaskId accumulationDependency[] = { accumulation };
    Graphics::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/task_graph/accumulation_upload_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(accumulationDependency, LengthOf(accumulationDependency))
        .setResourceUses(workingReadWrite, LengthOf(workingReadWrite))
    ;
    const Graphics::GpuTaskId composite = graph.addTask(compositeDesc);
    ASSERT_TRUE(composite.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_EQ(compiledGraph.packetCount(), 6u);

    const Graphics::GpuSubmissionPacketId prePacket = compiledGraph.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledGraph.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledGraph.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledGraph.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId instanceUploadPacket = compiledGraph.packetForTask(instanceUpload);
    const Graphics::GpuSubmissionPacketId typedUploadPacket = compiledGraph.packetForTask(typedUpload);
    const Graphics::GpuSubmissionPacketId receiverRangesUploadPacket = compiledGraph.packetForTask(receiverRangesUpload);
    const Graphics::GpuSubmissionPacketId cuttersUploadPacket = compiledGraph.packetForTask(cuttersUpload);
    const Graphics::GpuSubmissionPacketId clipContextUploadPacket = compiledGraph.packetForTask(clipContextUpload);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(composite);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(instanceUploadPacket.valid());
    ASSERT_TRUE(typedUploadPacket.valid());
    ASSERT_TRUE(receiverRangesUploadPacket.valid());
    ASSERT_TRUE(cuttersUploadPacket.valid());
    ASSERT_TRUE(clipContextUploadPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_NE(prePacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_NE(integrationPacket, accumulationPacket);
    EXPECT_EQ(instanceUploadPacket, accumulationPacket);
    EXPECT_EQ(typedUploadPacket, accumulationPacket);
    EXPECT_EQ(receiverRangesUploadPacket, accumulationPacket);
    EXPECT_EQ(cuttersUploadPacket, accumulationPacket);
    EXPECT_EQ(clipContextUploadPacket, accumulationPacket);
    EXPECT_EQ(compiledGraph.packet(accumulationPacket).taskCount, 6u);
    ASSERT_EQ(compiledGraph.packet(compositePacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(compositePacket)[0u].producer, accumulationPacket);

    const Graphics::GpuSubmissionPacketRange avboitRange = compiledGraph.packetRange(prePacket, accumulationPacket);
    ASSERT_TRUE(avboitRange.valid());
    EXPECT_EQ(avboitRange.packetCount, 5u);
}


TEST(GpuTaskGraph, MergesAccumulationTailIntoGraphicsAvboitPacket){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId materialInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/graphics_accumulation_instances"),
        "AVBOIT Graphics Accumulation Material Instances"
    );
    ASSERT_TRUE(materialInstances.valid());

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
    Graphics::GpuTaskSchedulingHint firstScheduling;
    firstScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint mergeScheduling;
    mergeScheduling.allowPacketMerge = true;
    mergeScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(firstScheduling)
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const auto addMergedGraphicsTask = [&](const Name& identity, const AStringView label, const Graphics::GpuTaskId dependency){
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsRequest)
            .setScheduling(mergeScheduling)
            .setDependencies(&dependency, 1u)
        ;
        return graph.addTask(desc);
    };

    const Graphics::GpuTaskId occupancy = addMergedGraphicsTask(
        Name("tests/task_graph/graphics_accumulation_occupancy"),
        "AVBOIT Occupancy",
        pre
    );
    ASSERT_TRUE(occupancy.valid());
    const Graphics::GpuTaskId extinction = addMergedGraphicsTask(
        Name("tests/task_graph/graphics_accumulation_extinction"),
        "AVBOIT Extinction",
        occupancy
    );
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskResourceUse uploadUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_instances_upload"))
        .setMarkerLabel("AVBOIT Accumulation Material Instances Upload")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(&extinction, 1u)
        .setResourceUses(uploadUse, LengthOf(uploadUse))
    ;
    const Graphics::GpuTaskId instanceUpload = graph.addTask(uploadDesc);
    ASSERT_TRUE(instanceUpload.valid());

    const Graphics::GpuTaskResourceUse accumulationUse[] = {
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_native"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(mergeScheduling)
        .setDependencies(&instanceUpload, 1u)
        .setResourceUses(accumulationUse, LengthOf(accumulationUse))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuTaskId accumulationDependency[] = { accumulation };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/graphics_accumulation_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(accumulationDependency, LengthOf(accumulationDependency))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_EQ(compiledGraph.packetCount(), 2u);

    const Graphics::GpuSubmissionPacketId prePacket = compiledGraph.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId occupancyPacket = compiledGraph.packetForTask(occupancy);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledGraph.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId uploadPacket = compiledGraph.packetForTask(instanceUpload);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(occupancyPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(uploadPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(occupancyPacket, prePacket);
    EXPECT_EQ(extinctionPacket, prePacket);
    EXPECT_EQ(uploadPacket, prePacket);
    EXPECT_EQ(accumulationPacket, prePacket);
    EXPECT_NE(lightingPacket, prePacket);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(lightingPacket)[0u].producer, accumulationPacket);
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


// AVBOIT's normal path clears coverage before occupancy, then the unsplit Graphics tail reads/writes it again.
// Keep the first state change and the later same-state UAV ordering as distinct graph compiler responsibilities:
// occupancy needs CopyDest -> UAV, while only the tail needs the UAV dependency.
TEST(GpuTaskGraph, PlansAvboitCoverageClearAndTailUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_coverage"),
        "AVBOIT Coverage"
    );
    ASSERT_TRUE(coverage.valid());

    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse tailUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId clear = AddTask(
        graph,
        Name("tests/task_graph/avboit_coverage_clear"),
        "AVBOIT Clear",
        nullptr,
        0u,
        clearUses,
        LengthOf(clearUses)
    );
    const Graphics::GpuTaskId occupancyDependencies[] = { clear };
    const Graphics::GpuTaskId occupancy = AddTask(
        graph,
        Name("tests/task_graph/avboit_coverage_occupancy"),
        "AVBOIT Occupancy",
        occupancyDependencies,
        LengthOf(occupancyDependencies),
        occupancyUses,
        LengthOf(occupancyUses)
    );
    const Graphics::GpuTaskId tailDependencies[] = { occupancy };
    const Graphics::GpuTaskId tail = AddTask(
        graph,
        Name("tests/task_graph/avboit_coverage_tail"),
        "AVBOIT Unsplit Tail",
        tailDependencies,
        LengthOf(tailDependencies),
        tailUses,
        LengthOf(tailUses)
    );
    ASSERT_TRUE(clear.valid());
    ASSERT_TRUE(occupancy.valid());
    ASSERT_TRUE(tail.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuCompiledTask* const compiledOccupancy = compiledGraph.findTask(occupancy);
    const Graphics::GpuCompiledTask* const compiledTail = compiledGraph.findTask(tail);
    ASSERT_NE(compiledOccupancy, nullptr);
    ASSERT_NE(compiledTail, nullptr);
    ASSERT_EQ(compiledOccupancy->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledTail->prologueBarrierCount, 1u);

    const Graphics::GpuCompiledBarrier* const occupancyBarrier = compiledGraph.taskPrologueBarriers(occupancy);
    const Graphics::GpuCompiledBarrier* const tailBarrier = compiledGraph.taskPrologueBarriers(tail);
    ASSERT_NE(occupancyBarrier, nullptr);
    ASSERT_NE(tailBarrier, nullptr);
    EXPECT_EQ(occupancyBarrier[0].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(occupancyBarrier[0].resource, coverage);
    EXPECT_EQ(occupancyBarrier[0].before, Graphics::ResourceStates::CopyDest);
    EXPECT_EQ(occupancyBarrier[0].after, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(tailBarrier[0].type, Graphics::GpuCompiledBarrierType::BufferUav);
    EXPECT_EQ(tailBarrier[0].resource, coverage);
    EXPECT_EQ(tailBarrier[0].before, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_EQ(tailBarrier[0].after, Graphics::ResourceStates::UnorderedAccess);
}


// AVBOIT accumulation ends a Graphics raster pass, while Deferred Composite may run on a dedicated Compute queue.
// Keep its color attachments and read-only depth handoff in a mergeable Graphics finalizer so following packets
// only consume ShaderResource state.
TEST(GpuTaskGraph, PlansAvboitAccumulationFinalizationOnGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_color"),
        "AVBOIT Accumulation Color"
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_extinction"),
        "AVBOIT Accumulation Extinction"
    );
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_deferred_depth"),
        "Deferred Depth"
    );
    ASSERT_TRUE(accumColor.valid());
    ASSERT_TRUE(accumExtinction.valid());
    ASSERT_TRUE(deferredDepth.valid());

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
    Graphics::GpuTaskSchedulingHint accumulationScheduling;
    accumulationScheduling.cost = Graphics::GpuTaskCostHint::Large;
    accumulationScheduling.forceSubmissionBoundary = false;
    accumulationScheduling.allowPacketMerge = true;
    Graphics::GpuTaskSchedulingHint finalizeScheduling;
    finalizeScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    finalizeScheduling.forceSubmissionBoundary = false;
    finalizeScheduling.allowPacketMerge = true;
    finalizeScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    compositeScheduling.forceSubmissionBoundary = true;
    compositeScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = accumColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumExtinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::DepthRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(accumulationScheduling)
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuTaskResourceUse finalizeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = accumColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumExtinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc finalizeDesc;
    finalizeDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_finalize"))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setQueue(graphicsRequest)
        .setScheduling(finalizeScheduling)
        .setDependencies(&accumulation, 1u)
        .setResourceUses(finalizeUses, LengthOf(finalizeUses))
    ;
    const Graphics::GpuTaskId finalizer = graph.addTask(finalizeDesc);
    ASSERT_TRUE(finalizer.valid());

    const Graphics::GpuTaskResourceUse compositeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = accumColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumExtinction,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };

    Graphics::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/task_graph/avboit_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(computeRequest)
        .setScheduling(compositeScheduling)
        .setDependencies(&finalizer, 1u)
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    const Graphics::GpuTaskId composite = graph.addTask(compositeDesc);
    ASSERT_TRUE(composite.valid());

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

    const Graphics::GpuTaskQueueAssignment* const finalizerAssignment = assignments.find(finalizer);
    const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
    ASSERT_NE(finalizerAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    EXPECT_EQ(finalizerAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Compute);

    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId finalizerPacket = compiledGraph.packetForTask(finalizer);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(composite);
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(finalizerPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_EQ(finalizerPacket, accumulationPacket);
    EXPECT_NE(compositePacket, finalizerPacket);

    const Graphics::GpuCompiledTask* const compiledFinalizer = compiledGraph.findTask(finalizer);
    const Graphics::GpuCompiledTask* const compiledComposite = compiledGraph.findTask(composite);
    ASSERT_NE(compiledFinalizer, nullptr);
    ASSERT_NE(compiledComposite, nullptr);
    ASSERT_EQ(compiledFinalizer->prologueBarrierCount, 3u);
    const Graphics::GpuCompiledBarrier* const finalizerBarriers = compiledGraph.taskPrologueBarriers(finalizer);
    ASSERT_NE(finalizerBarriers, nullptr);
    bool finalizesAccumColor = false;
    bool finalizesAccumExtinction = false;
    bool finalizesDeferredDepth = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFinalizer->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = finalizerBarriers[barrierIndex];
        const bool isAttachmentFinalization = barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::RenderTarget
            && barrier.after == Graphics::ResourceStates::ShaderResource
        ;
        finalizesAccumColor = finalizesAccumColor || (isAttachmentFinalization && barrier.resource == accumColor);
        finalizesAccumExtinction = finalizesAccumExtinction
            || (isAttachmentFinalization && barrier.resource == accumExtinction);
        finalizesDeferredDepth = finalizesDeferredDepth || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == deferredDepth
            && barrier.before == Graphics::ResourceStates::DepthRead
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(finalizesAccumColor);
    EXPECT_TRUE(finalizesAccumExtinction);
    EXPECT_TRUE(finalizesDeferredDepth);

    const Graphics::GpuCompiledBarrier* const compositeBarriers = compiledGraph.taskPrologueBarriers(composite);
    for(u32 barrierIndex = 0u; barrierIndex < compiledComposite->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = compositeBarriers[barrierIndex];
        EXPECT_FALSE(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && (
                barrier.before == Graphics::ResourceStates::RenderTarget
                || barrier.before == Graphics::ResourceStates::DepthRead
            )
        );
    }
}


// Frame-lagged Lighting reads current G-buffer depth independently of the temporal effect history. Transparent
// AVBOIT temporarily binds that depth read-only as DepthRead, so its Graphics finalizer must precede the otherwise
// independent dedicated-Compute Lighting packet before the latter can sample ShaderResource layout again.
TEST(GpuTaskGraph, OrdersLaggedLightingAfterAvboitDepthFinalizer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId deferredDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/lagged_avboit_deferred_depth"),
        "Deferred Depth",
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId accumulationColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/lagged_avboit_accumulation_color"),
        "AVBOIT Accumulation Color",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(deferredDepth.valid());
    ASSERT_TRUE(accumulationColor.valid());

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
    Graphics::GpuTaskSchedulingHint prefixScheduling;
    prefixScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    prefixScheduling.forceSubmissionBoundary = true;
    prefixScheduling.allowPacketMerge = false;
    Graphics::GpuTaskSchedulingHint accumulationScheduling;
    accumulationScheduling.cost = Graphics::GpuTaskCostHint::Large;
    accumulationScheduling.forceSubmissionBoundary = false;
    accumulationScheduling.allowPacketMerge = true;
    accumulationScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint finalizerScheduling;
    finalizerScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    finalizerScheduling.forceSubmissionBoundary = false;
    finalizerScheduling.allowPacketMerge = true;
    finalizerScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskSchedulingHint lightingScheduling;
    lightingScheduling.cost = Graphics::GpuTaskCostHint::Large;
    lightingScheduling.forceSubmissionBoundary = true;
    lightingScheduling.allowPacketMerge = false;

    const Graphics::GpuTaskResourceUse prefixUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/lagged_avboit_prefix"))
        .setMarkerLabel("Graphics Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(prefixScheduling)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::DepthRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumulationColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::RenderTarget,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/lagged_avboit_accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(accumulationScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuTaskResourceUse finalizerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accumulationColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc finalizerDesc;
    finalizerDesc
        .setIdentity(Name("tests/task_graph/lagged_avboit_finalize"))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setQueue(graphicsRequest)
        .setScheduling(finalizerScheduling)
        .setDependencies(&accumulation, 1u)
        .setResourceUses(finalizerUses, LengthOf(finalizerUses))
    ;
    const Graphics::GpuTaskId finalizer = graph.addTask(finalizerDesc);
    ASSERT_TRUE(finalizer.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = deferredDepth,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            // Match lagged Lighting's prefix-backed independent read source; the explicit finalizer dependency,
            // rather than read/read hazard analysis, orders the temporary depth layout transition.
            .hasIndependentStateSource = true,
        },
    };
    const Graphics::GpuTaskId lightingDependencies[] = { prefix, finalizer };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(lightingDependencies, LengthOf(lightingDependencies))
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));

    const Graphics::GpuTaskQueueAssignment* const finalizerAssignment = assignments.find(finalizer);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    ASSERT_NE(finalizerAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    EXPECT_EQ(finalizerAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(lightingAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
    EXPECT_NE(FindEdge(analysis, finalizer, lighting), nullptr);

    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId finalizerPacket = compiledGraph.packetForTask(finalizer);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(finalizerPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(finalizerPacket, accumulationPacket);
    EXPECT_NE(lightingPacket, finalizerPacket);

    const Graphics::GpuCompiledTask* const compiledFinalizer = compiledGraph.findTask(finalizer);
    ASSERT_NE(compiledFinalizer, nullptr);
    const Graphics::GpuCompiledBarrier* const finalizerBarriers = compiledGraph.taskPrologueBarriers(finalizer);
    ASSERT_NE(finalizerBarriers, nullptr);
    bool finalizesDeferredDepth = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFinalizer->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = finalizerBarriers[barrierIndex];
        finalizesDeferredDepth = finalizesDeferredDepth || (
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.resource == deferredDepth
            && barrier.before == Graphics::ResourceStates::DepthRead
            && barrier.after == Graphics::ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(finalizesDeferredDepth);

    const Graphics::GpuSubmissionPacket& lightingPacketInfo = compiledGraph.packet(lightingPacket);
    ASSERT_GT(lightingPacketInfo.dependencyCount, 0u);
    const Graphics::GpuPacketDependency* const lightingPacketDependencies = compiledGraph.packetDependencies(lightingPacket);
    ASSERT_NE(lightingPacketDependencies, nullptr);
    bool lightingWaitsForFinalizer = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < lightingPacketInfo.dependencyCount; ++dependencyIndex)
        lightingWaitsForFinalizer = lightingWaitsForFinalizer
            || lightingPacketDependencies[dependencyIndex].producer == finalizerPacket;
    EXPECT_TRUE(lightingWaitsForFinalizer);
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
    const Graphics::GpuCompiledTask* const compiledExclusiveGraphics = compiledGraph.findTask(exclusiveGraphics);
    const Graphics::GpuCompiledTask* const compiledExclusiveCompute = compiledGraph.findTask(exclusiveCompute);
    ASSERT_NE(compiledConcurrentCompute, nullptr);
    ASSERT_NE(compiledDefaultConcurrentCompute, nullptr);
    ASSERT_NE(compiledExclusiveGraphics, nullptr);
    ASSERT_NE(compiledExclusiveCompute, nullptr);
    EXPECT_EQ(compiledConcurrentCompute->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledDefaultConcurrentCompute->prologueStateSeedCount, 1u);
    ASSERT_EQ(compiledExclusiveCompute->prologueStateSeedCount, 1u);
    EXPECT_EQ(compiledConcurrentCompute->prologueBarrierCount, 0u);
    EXPECT_EQ(compiledDefaultConcurrentCompute->prologueBarrierCount, 0u);
    ASSERT_EQ(compiledExclusiveCompute->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledExclusiveGraphics->epilogueBarrierCount, 1u);
    const Graphics::GpuPacketStateSeed* const defaultConcurrentSeed = compiledGraph.taskPrologueStateSeeds(
        defaultConcurrentCompute
    );
    const Graphics::GpuPacketStateSeed* const exclusiveSeed = compiledGraph.taskPrologueStateSeeds(exclusiveCompute);
    const Graphics::GpuCompiledBarrier* const exclusiveAcquire = compiledGraph.taskPrologueBarriers(exclusiveCompute);
    const Graphics::GpuCompiledBarrier* const exclusiveRelease = compiledGraph.taskEpilogueBarriers(exclusiveGraphics);
    EXPECT_EQ(compiledGraph.taskPrologueStateSeeds(concurrentCompute), nullptr);
    ASSERT_NE(defaultConcurrentSeed, nullptr);
    ASSERT_NE(exclusiveSeed, nullptr);
    ASSERT_NE(exclusiveAcquire, nullptr);
    ASSERT_NE(exclusiveRelease, nullptr);
    EXPECT_EQ(defaultConcurrentSeed[0u].sourcePacket, defaultConcurrentGraphicsPacket);
    EXPECT_EQ(exclusiveSeed[0u].sourcePacket, exclusiveGraphicsPacket);
    EXPECT_EQ(exclusiveAcquire[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipAcquire);
    EXPECT_EQ(exclusiveAcquire[0u].sourceQueue, compiledExclusiveGraphics->queue);
    EXPECT_EQ(exclusiveAcquire[0u].destinationQueue, compiledExclusiveCompute->queue);
    EXPECT_EQ(exclusiveRelease[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(exclusiveRelease[0u].sourceQueue, compiledExclusiveGraphics->queue);
    EXPECT_EQ(exclusiveRelease[0u].destinationQueue, compiledExclusiveCompute->queue);
    EXPECT_EQ(compiledGraph.packet(concurrentComputePacket).dependencyCount, 0u);
    ASSERT_EQ(compiledGraph.packet(defaultConcurrentComputePacket).dependencyCount, 1u);
    EXPECT_EQ(
        compiledGraph.packetDependencies(defaultConcurrentComputePacket)[0u].producer,
        defaultConcurrentGraphicsPacket
    );
    ASSERT_EQ(compiledGraph.packet(exclusiveComputePacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(exclusiveComputePacket)[0u].producer, exclusiveGraphicsPacket);

    // The combined sharing mask becomes Vulkan-concurrent only when the queues have distinct families. Two real
    // VkQueues in one family still use exclusive ownership in the backend, so they must retain the handoff.
    Graphics::GpuPhysicalQueueInfo sameFamilyComputeQueue = DedicatedComputeQueue();
    sameFamilyComputeQueue.familyIndex = GraphicsQueue().familyIndex;
    sameFamilyComputeQueue.queueIndex = 1u;
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


TEST(GpuTaskGraph, PlansExclusiveOwnershipHandoffToDedicatedTransfer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(graph, Graphics::ResourceQueueSharing::Exclusive);
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuCompiledTask* const compiledProducer = compiledGraph.findTask(pair.producer);
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledGraph.findTask(pair.consumer);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    const Graphics::GpuPhysicalQueueInfo* const consumerQueue = compiledGraph.queueInfo(compiledConsumer->queue);
    ASSERT_NE(consumerQueue, nullptr);
    EXPECT_EQ(consumerQueue->queueClass, Graphics::CommandQueue::Transfer);
    ASSERT_EQ(compiledProducer->epilogueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);

    const Graphics::GpuCompiledBarrier* const release = compiledGraph.taskEpilogueBarriers(pair.producer);
    const Graphics::GpuCompiledBarrier* const acquire = compiledGraph.taskPrologueBarriers(pair.consumer);
    const Graphics::GpuPacketStateSeed* const stateSeed = compiledGraph.taskPrologueStateSeeds(pair.consumer);
    ASSERT_NE(release, nullptr);
    ASSERT_NE(acquire, nullptr);
    ASSERT_NE(stateSeed, nullptr);
    EXPECT_EQ(release[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipRelease);
    EXPECT_EQ(release[0u].resource, pair.texture);
    EXPECT_EQ(release[0u].before, Graphics::ResourceStates::CopySource);
    EXPECT_EQ(release[0u].after, Graphics::ResourceStates::CopySource);
    EXPECT_EQ(release[0u].sourceQueue, compiledProducer->queue);
    EXPECT_EQ(release[0u].destinationQueue, compiledConsumer->queue);
    EXPECT_EQ(acquire[0u].type, Graphics::GpuCompiledBarrierType::TextureOwnershipAcquire);
    EXPECT_EQ(acquire[0u].resource, pair.texture);
    EXPECT_EQ(acquire[0u].sourceQueue, compiledProducer->queue);
    EXPECT_EQ(acquire[0u].destinationQueue, compiledConsumer->queue);
    EXPECT_EQ(stateSeed[0u].resource, pair.texture);
    EXPECT_EQ(stateSeed[0u].sourcePacket, compiledProducer->packet);
    ASSERT_EQ(compiledGraph.packet(compiledConsumer->packet).dependencyCount, 1u);
    EXPECT_EQ(
        compiledGraph.packetDependencies(compiledConsumer->packet)[0u].producer,
        compiledProducer->packet
    );
}


TEST(GpuTaskGraph, UsesDeclaredTripleQueueSharingForDedicatedTransfer){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(
        graph,
        Graphics::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer
    );
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuCompiledTask* const compiledProducer = compiledGraph.findTask(pair.producer);
    const Graphics::GpuCompiledTask* const compiledConsumer = compiledGraph.findTask(pair.consumer);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledConsumer, nullptr);
    EXPECT_EQ(compiledProducer->epilogueBarrierCount, 0u);
    EXPECT_EQ(compiledConsumer->prologueBarrierCount, 0u);
    ASSERT_EQ(compiledConsumer->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const stateSeed = compiledGraph.taskPrologueStateSeeds(pair.consumer);
    ASSERT_NE(stateSeed, nullptr);
    EXPECT_EQ(stateSeed[0u].resource, pair.texture);
    EXPECT_EQ(stateSeed[0u].sourcePacket, compiledProducer->packet);
}


TEST(GpuTaskGraph, RejectsDedicatedTransferUseOutsideConcurrentSharingContract){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const TransferOwnershipPair pair = AddTransferOwnershipPair(
        graph,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(pair.texture.valid());
    ASSERT_TRUE(pair.producer.valid());
    ASSERT_TRUE(pair.consumer.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_FALSE(compiledGraph.valid());
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
    const auto importBuffer = [&](const Name& identity, const AStringView label, const Graphics::ResourceStates::Mask initialState){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Buffer)
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
    const Graphics::GpuGraphResourceId currentBindlessSlots = importBuffer(
        Name("tests/task_graph/lagged_current_bindless_slots"),
        "Current Bindless Slots",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId historyBindlessSlots = importBuffer(
        Name("tests/task_graph/lagged_history_bindless_slots"),
        "Lagged History Bindless Slots",
        Graphics::ResourceStates::Common
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
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(historyBindlessSlots.valid());
    ASSERT_TRUE(historyIrradiance.valid());
    ASSERT_TRUE(currentIrradiance.valid());
    ASSERT_TRUE(opaqueColor.valid());
    ASSERT_TRUE(avboitAccumulation.valid());
    ASSERT_TRUE(compositeColor.valid());
    ASSERT_TRUE(backbuffer.valid());

    const Graphics::GpuExternalCompletionId historyCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/lagged_history_complete"))
            .setMarkerLabel("Lagged History Complete")
    );
    ASSERT_TRUE(historyCompletion.valid());

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
    const Graphics::GpuQueueRequest computeUploadRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    const Graphics::GpuQueueRequest transferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/lagged_shadow_prepare"))
        .setMarkerLabel("Shadow Prepare")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());
    const Graphics::GpuTaskResourceUse prefixUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/lagged_graphics_prefix"))
        .setMarkerLabel("Graphics Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());
    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/lagged_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());
    const Graphics::GpuTaskResourceUse surfelGiUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId surfelGiDependencies[] = { shadowVisibility };
    Graphics::GpuTaskDesc surfelGiDesc;
    surfelGiDesc
        .setIdentity(Name("tests/task_graph/lagged_surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(surfelGiDependencies, LengthOf(surfelGiDependencies))
        .setResourceUses(surfelGiUses, LengthOf(surfelGiUses))
    ;
    const Graphics::GpuTaskId surfelGi = graph.addTask(surfelGiDesc);
    ASSERT_TRUE(surfelGi.valid());
    const Graphics::GpuTaskResourceUse hardwareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        },
    };
    Graphics::GpuTaskDesc hardwareDesc;
    hardwareDesc
        .setIdentity(Name("tests/task_graph/lagged_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setExternalDependencies(&historyCompletion, 1u)
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
            .hasIndependentStateSource = true,
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
        .setDependencies(&prefix, 1u)
        .setResourceUses(avboitPreUses, LengthOf(avboitPreUses))
    ;
    const Graphics::GpuTaskId avboitPre = graph.addTask(avboitPreDesc);
    ASSERT_TRUE(avboitPre.valid());

    const Graphics::GpuTaskResourceUse laggedHistorySlotsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = historyBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint laggedHistorySlotsUploadScheduling;
    laggedHistorySlotsUploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    laggedHistorySlotsUploadScheduling.forceSubmissionBoundary = false;
    laggedHistorySlotsUploadScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc laggedHistorySlotsUploadDesc;
    laggedHistorySlotsUploadDesc
        .setIdentity(Name("tests/task_graph/lagged_history_bindless_slots_upload"))
        .setMarkerLabel("Lagged Lighting Bindless Slots Upload")
        .setQueue(computeUploadRequest)
        .setScheduling(laggedHistorySlotsUploadScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(laggedHistorySlotsUploadUses, LengthOf(laggedHistorySlotsUploadUses))
    ;
    const Graphics::GpuTaskId laggedHistorySlotsUpload = graph.addTask(laggedHistorySlotsUploadDesc);
    ASSERT_TRUE(laggedHistorySlotsUpload.valid());

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
            .resource = historyBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint lightingScheduling;
    lightingScheduling.cost = Graphics::GpuTaskCostHint::Large;
    lightingScheduling.forceSubmissionBoundary = false;
    lightingScheduling.allowPacketMerge = true;
    lightingScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId lightingDependencies[] = {
        prefix,
        laggedHistorySlotsUpload,
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(lightingDependencies, LengthOf(lightingDependencies))
        .setExternalDependencies(&historyCompletion, 1u)
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
    const Graphics::GpuTaskId presentDependencies[] = {
        composite,
        surfelGi,
    };
    Graphics::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(presentDependencies, LengthOf(presentDependencies))
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
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));

    const Graphics::GpuTaskQueueAssignment* const shadowPrepareAssignment = assignments.find(shadowPrepare);
    const Graphics::GpuTaskQueueAssignment* const prefixAssignment = assignments.find(prefix);
    const Graphics::GpuTaskQueueAssignment* const shadowVisibilityAssignment = assignments.find(shadowVisibility);
    const Graphics::GpuTaskQueueAssignment* const surfelGiAssignment = assignments.find(surfelGi);
    const Graphics::GpuTaskQueueAssignment* const hardwareAssignment = assignments.find(hardware);
    const Graphics::GpuTaskQueueAssignment* const avboitPreAssignment = assignments.find(avboitPre);
    const Graphics::GpuTaskQueueAssignment* const laggedHistorySlotsUploadAssignment = assignments.find(laggedHistorySlotsUpload);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
    const Graphics::GpuTaskQueueAssignment* const presentAssignment = assignments.find(present);
    const Graphics::GpuTaskQueueAssignment* const historyCopyAssignment = assignments.find(historyCopy);
    ASSERT_NE(shadowPrepareAssignment, nullptr);
    ASSERT_NE(prefixAssignment, nullptr);
    ASSERT_NE(shadowVisibilityAssignment, nullptr);
    ASSERT_NE(surfelGiAssignment, nullptr);
    ASSERT_NE(hardwareAssignment, nullptr);
    ASSERT_NE(avboitPreAssignment, nullptr);
    ASSERT_NE(laggedHistorySlotsUploadAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    ASSERT_NE(presentAssignment, nullptr);
    ASSERT_NE(historyCopyAssignment, nullptr);
    EXPECT_EQ(shadowPrepareAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(prefixAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(shadowVisibilityAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(surfelGiAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(hardwareAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(avboitPreAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(laggedHistorySlotsUploadAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(lightingAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
    EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(presentAssignment->queueClass, Graphics::CommandQueue::Graphics);

    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledGraph.packetForTask(shadowVisibility);
    const Graphics::GpuSubmissionPacketId surfelGiPacket = compiledGraph.packetForTask(surfelGi);
    const Graphics::GpuSubmissionPacketId hardwarePacket = compiledGraph.packetForTask(hardware);
    const Graphics::GpuSubmissionPacketId avboitPrePacket = compiledGraph.packetForTask(avboitPre);
    const Graphics::GpuSubmissionPacketId laggedHistorySlotsUploadPacket = compiledGraph.packetForTask(laggedHistorySlotsUpload);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(composite);
    const Graphics::GpuSubmissionPacketId presentPacket = compiledGraph.packetForTask(present);
    const Graphics::GpuSubmissionPacketId historyCopyPacket = compiledGraph.packetForTask(historyCopy);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    ASSERT_TRUE(surfelGiPacket.valid());
    ASSERT_TRUE(hardwarePacket.valid());
    ASSERT_TRUE(avboitPrePacket.valid());
    ASSERT_TRUE(laggedHistorySlotsUploadPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    ASSERT_TRUE(presentPacket.valid());
    ASSERT_TRUE(historyCopyPacket.valid());
    ASSERT_EQ(compiledGraph.packetCount(), 10u);
    EXPECT_EQ(compiledGraph.packetIdAt(0u), shadowPreparePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), prefixPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), shadowVisibilityPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(3u), surfelGiPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(4u), hardwarePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(5u), avboitPrePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(6u), lightingPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(7u), compositePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(8u), presentPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(9u), historyCopyPacket);
    EXPECT_EQ(laggedHistorySlotsUploadPacket, lightingPacket);
    EXPECT_EQ(compiledGraph.packet(lightingPacket).taskCount, 2u);
    const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledGraph.findTask(shadowPrepare);
    const Graphics::GpuCompiledTask* const compiledPrefix = compiledGraph.findTask(prefix);
    ASSERT_NE(compiledShadowPrepare, nullptr);
    ASSERT_NE(compiledPrefix, nullptr);
    ASSERT_EQ(compiledShadowPrepare->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const shadowPrepareBarrier = compiledGraph.taskPrologueBarriers(shadowPrepare);
    ASSERT_NE(shadowPrepareBarrier, nullptr);
    EXPECT_EQ(shadowPrepareBarrier[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(shadowPrepareBarrier[0u].resource, currentBindlessSlots);
    EXPECT_EQ(shadowPrepareBarrier[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(shadowPrepareBarrier[0u].after, Graphics::ResourceStates::ConstantBuffer);
    ASSERT_EQ(compiledPrefix->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const prefixSeeds = compiledGraph.taskPrologueStateSeeds(prefix);
    ASSERT_NE(prefixSeeds, nullptr);
    EXPECT_EQ(prefixSeeds[0u].resource, currentBindlessSlots);
    EXPECT_EQ(prefixSeeds[0u].sourcePacket, shadowPreparePacket);
    ASSERT_EQ(compiledGraph.packet(prefixPacket).dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const prefixPacketDependencies = compiledGraph.packetDependencies(prefixPacket);
    ASSERT_NE(prefixPacketDependencies, nullptr);
    EXPECT_EQ(prefixPacketDependencies[0u].producer, shadowPreparePacket);
    EXPECT_EQ(FindEdge(analysis, avboitPre, lighting), nullptr);
    EXPECT_EQ(FindEdge(analysis, surfelGi, lighting), nullptr);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledLighting, nullptr);
    bool lightingTransitionsLaggedHistorySlots = false;
    const Graphics::GpuCompiledBarrier* const lightingBarriers = compiledGraph.taskPrologueBarriers(lighting);
    for(usize index = 0u; index < compiledLighting->prologueBarrierCount; ++index){
        lightingTransitionsLaggedHistorySlots = lightingTransitionsLaggedHistorySlots
            || (
                lightingBarriers[index].type == Graphics::GpuCompiledBarrierType::BufferTransition
                && lightingBarriers[index].resource == historyBindlessSlots
                && lightingBarriers[index].before == Graphics::ResourceStates::Common
                && lightingBarriers[index].after == Graphics::ResourceStates::ConstantBuffer
            )
        ;
    }
    EXPECT_TRUE(lightingTransitionsLaggedHistorySlots);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const lightingPacketDependencies = compiledGraph.packetDependencies(lightingPacket);
    ASSERT_NE(lightingPacketDependencies, nullptr);
    EXPECT_EQ(lightingPacketDependencies[0u].producer, prefixPacket);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).externalDependencyCount, 1u);
    const Graphics::GpuExternalCompletionId* const lightingExternalDependencies = compiledGraph.packetExternalDependencies(
        lightingPacket
    );
    ASSERT_NE(lightingExternalDependencies, nullptr);
    EXPECT_EQ(lightingExternalDependencies[0u], historyCompletion);

    const Graphics::GpuCompiledTask* const compiledShadowVisibility = compiledGraph.findTask(shadowVisibility);
    ASSERT_NE(compiledShadowVisibility, nullptr);
    ASSERT_GT(compiledShadowVisibility->prologueStateSeedCount, 0u);
    const Graphics::GpuPacketStateSeed* const shadowVisibilitySeeds = compiledGraph.taskPrologueStateSeeds(
        shadowVisibility
    );
    ASSERT_NE(shadowVisibilitySeeds, nullptr);
    bool shadowVisibilityImportsBindlessSlotsState = false;
    for(usize index = 0u; index < compiledShadowVisibility->prologueStateSeedCount; ++index){
        shadowVisibilityImportsBindlessSlotsState = shadowVisibilityImportsBindlessSlotsState
            || (
                shadowVisibilitySeeds[index].resource == currentBindlessSlots
                && shadowVisibilitySeeds[index].sourcePacket == prefixPacket
            )
        ;
    }
    EXPECT_TRUE(shadowVisibilityImportsBindlessSlotsState);
    EXPECT_EQ(compiledGraph.packet(shadowVisibilityPacket).externalDependencyCount, 0u);
    ASSERT_EQ(compiledGraph.packet(shadowVisibilityPacket).dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const shadowVisibilityPacketDependencies = compiledGraph.packetDependencies(
        shadowVisibilityPacket
    );
    ASSERT_NE(shadowVisibilityPacketDependencies, nullptr);
    bool shadowVisibilityWaitsForShadowPrepare = false;
    bool shadowVisibilityWaitsForPrefix = false;
    for(usize index = 0u; index < compiledGraph.packet(shadowVisibilityPacket).dependencyCount; ++index){
        shadowVisibilityWaitsForShadowPrepare = shadowVisibilityWaitsForShadowPrepare
            || shadowVisibilityPacketDependencies[index].producer == shadowPreparePacket
        ;
        shadowVisibilityWaitsForPrefix = shadowVisibilityWaitsForPrefix
            || shadowVisibilityPacketDependencies[index].producer == prefixPacket
        ;
    }
    EXPECT_TRUE(shadowVisibilityWaitsForShadowPrepare);
    EXPECT_TRUE(shadowVisibilityWaitsForPrefix);
    ASSERT_EQ(compiledGraph.packet(surfelGiPacket).externalDependencyCount, 0u);
    ASSERT_GE(compiledGraph.packet(surfelGiPacket).dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const surfelGiPacketDependencies = compiledGraph.packetDependencies(
        surfelGiPacket
    );
    ASSERT_NE(surfelGiPacketDependencies, nullptr);
    bool surfelGiWaitsForShadowVisibility = false;
    for(usize index = 0u; index < compiledGraph.packet(surfelGiPacket).dependencyCount; ++index){
        surfelGiWaitsForShadowVisibility = surfelGiWaitsForShadowVisibility
            || surfelGiPacketDependencies[index].producer == shadowVisibilityPacket
        ;
    }
    EXPECT_TRUE(surfelGiWaitsForShadowVisibility);

    EXPECT_EQ(compiledGraph.packet(avboitPrePacket).externalDependencyCount, 0u);

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
    ASSERT_EQ(compiledGraph.packet(presentPacket).dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const presentPacketDependencies = compiledGraph.packetDependencies(presentPacket);
    ASSERT_NE(presentPacketDependencies, nullptr);
    bool presentWaitsForComposite = false;
    bool presentWaitsForSurfelGi = false;
    for(usize index = 0u; index < compiledGraph.packet(presentPacket).dependencyCount; ++index){
        presentWaitsForComposite = presentWaitsForComposite
            || presentPacketDependencies[index].producer == compositePacket
        ;
        presentWaitsForSurfelGi = presentWaitsForSurfelGi
            || presentPacketDependencies[index].producer == surfelGiPacket
        ;
    }
    EXPECT_TRUE(presentWaitsForComposite);
    EXPECT_TRUE(presentWaitsForSurfelGi);
    EXPECT_EQ(compiledGraph.packet(presentPacket).externalDependencyCount, 0u);

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
    const Graphics::GpuCompiledTask* const compiledHistoryCopy = compiledGraph.findTask(historyCopy);
    ASSERT_NE(compiledHistoryCopy, nullptr);
    const Graphics::GpuCompiledBarrier* const historyCopyBarriers = compiledGraph.taskPrologueBarriers(historyCopy);
    ASSERT_NE(historyCopyBarriers, nullptr);
    bool historyCopyTransitionsCurrentIrradiance = false;
    bool historyCopyTransitionsHistoryIrradiance = false;
    for(usize index = 0u; index < compiledHistoryCopy->prologueBarrierCount; ++index){
        const Graphics::GpuCompiledBarrier& barrier = historyCopyBarriers[index];
        historyCopyTransitionsCurrentIrradiance = historyCopyTransitionsCurrentIrradiance
            || (
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == currentIrradiance
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::CopySource
            )
        ;
        historyCopyTransitionsHistoryIrradiance = historyCopyTransitionsHistoryIrradiance
            || (
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == historyIrradiance
                && barrier.before == Graphics::ResourceStates::ShaderResource
                && barrier.after == Graphics::ResourceStates::CopyDest
            )
        ;
    }
    EXPECT_TRUE(historyCopyTransitionsCurrentIrradiance);
    EXPECT_TRUE(historyCopyTransitionsHistoryIrradiance);
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
    const auto importBuffer = [&](const Name& identity, const AStringView label, const Graphics::ResourceStates::Mask initialState){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Buffer)
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
    const Graphics::GpuGraphResourceId currentBindlessSlots = importBuffer(
        Name("tests/task_graph/live_current_bindless_slots"),
        "Current Bindless Slots",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId currentIrradiance = importTexture(
        Name("tests/task_graph/live_current_irradiance"),
        "Current Surfel Irradiance",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId currentShadowVisibility = importTexture(
        Name("tests/task_graph/live_current_shadow_visibility"),
        "Current Shadow Visibility",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId currentCausticIrradiance = importTexture(
        Name("tests/task_graph/live_current_caustic_irradiance"),
        "Current Caustic Irradiance",
        Graphics::ResourceStates::Common
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
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(currentIrradiance.valid());
    ASSERT_TRUE(currentShadowVisibility.valid());
    ASSERT_TRUE(currentCausticIrradiance.valid());
    ASSERT_TRUE(avboitWorking.valid());
    ASSERT_TRUE(avboitAccumulation.valid());
    ASSERT_TRUE(opaqueColor.valid());
    ASSERT_TRUE(compositeColor.valid());
    ASSERT_TRUE(backbuffer.valid());

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

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/live_shadow_prepare"))
        .setMarkerLabel("Shadow Prepare")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());
    const Graphics::GpuTaskResourceUse prefixUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/live_graphics_prefix"))
        .setMarkerLabel("Graphics Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentShadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/live_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());

    const Graphics::GpuTaskResourceUse softwareCausticsUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentCausticIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId softwareCausticsDependencies[] = { shadowVisibility };
    Graphics::GpuTaskDesc softwareCausticsDesc;
    softwareCausticsDesc
        .setIdentity(Name("tests/task_graph/live_software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(softwareCausticsDependencies, LengthOf(softwareCausticsDependencies))
        .setResourceUses(softwareCausticsUses, LengthOf(softwareCausticsUses))
    ;
    const Graphics::GpuTaskId softwareCaustics = graph.addTask(softwareCausticsDesc);
    ASSERT_TRUE(softwareCaustics.valid());

    const Graphics::GpuTaskResourceUse surfelGiUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId surfelGiDependencies[] = { softwareCaustics };
    Graphics::GpuTaskDesc surfelGiDesc;
    surfelGiDesc
        .setIdentity(Name("tests/task_graph/live_surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(surfelGiDependencies, LengthOf(surfelGiDependencies))
        .setResourceUses(surfelGiUses, LengthOf(surfelGiUses))
    ;
    const Graphics::GpuTaskId surfelGi = graph.addTask(surfelGiDesc);
    ASSERT_TRUE(surfelGi.valid());

    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
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
        .setDependencies(&prefix, 1u)
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
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentShadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentCausticIrradiance,
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
    const Graphics::GpuTaskId lightingDependencies[] = {
        shadowVisibility,
        softwareCaustics,
        surfelGi,
        accumulation,
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/live_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(lightingDependencies, LengthOf(lightingDependencies))
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

    const Graphics::GpuTaskQueueAssignment* const shadowPrepareAssignment = assignments.find(shadowPrepare);
    const Graphics::GpuTaskQueueAssignment* const prefixAssignment = assignments.find(prefix);
    const Graphics::GpuTaskQueueAssignment* const shadowVisibilityAssignment = assignments.find(shadowVisibility);
    const Graphics::GpuTaskQueueAssignment* const softwareCausticsAssignment = assignments.find(softwareCaustics);
    const Graphics::GpuTaskQueueAssignment* const surfelGiAssignment = assignments.find(surfelGi);
    const Graphics::GpuTaskQueueAssignment* const preAssignment = assignments.find(pre);
    const Graphics::GpuTaskQueueAssignment* const depthWarpAssignment = assignments.find(depthWarp);
    const Graphics::GpuTaskQueueAssignment* const extinctionAssignment = assignments.find(extinction);
    const Graphics::GpuTaskQueueAssignment* const integrationAssignment = assignments.find(integration);
    const Graphics::GpuTaskQueueAssignment* const accumulationAssignment = assignments.find(accumulation);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
    const Graphics::GpuTaskQueueAssignment* const presentAssignment = assignments.find(present);
    ASSERT_NE(shadowPrepareAssignment, nullptr);
    ASSERT_NE(prefixAssignment, nullptr);
    ASSERT_NE(shadowVisibilityAssignment, nullptr);
    ASSERT_NE(softwareCausticsAssignment, nullptr);
    ASSERT_NE(surfelGiAssignment, nullptr);
    ASSERT_NE(preAssignment, nullptr);
    ASSERT_NE(depthWarpAssignment, nullptr);
    ASSERT_NE(extinctionAssignment, nullptr);
    ASSERT_NE(integrationAssignment, nullptr);
    ASSERT_NE(accumulationAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    ASSERT_NE(presentAssignment, nullptr);
    EXPECT_EQ(shadowPrepareAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(prefixAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(shadowVisibilityAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(softwareCausticsAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(surfelGiAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(preAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(depthWarpAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(extinctionAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(integrationAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(accumulationAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(presentAssignment->queueClass, Graphics::CommandQueue::Graphics);

    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledGraph.packetForTask(shadowVisibility);
    const Graphics::GpuSubmissionPacketId softwareCausticsPacket = compiledGraph.packetForTask(softwareCaustics);
    const Graphics::GpuSubmissionPacketId surfelGiPacket = compiledGraph.packetForTask(surfelGi);
    const Graphics::GpuSubmissionPacketId prePacket = compiledGraph.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledGraph.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledGraph.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledGraph.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(composite);
    const Graphics::GpuSubmissionPacketId presentPacket = compiledGraph.packetForTask(present);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    ASSERT_TRUE(softwareCausticsPacket.valid());
    ASSERT_TRUE(surfelGiPacket.valid());
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    ASSERT_TRUE(presentPacket.valid());
    ASSERT_EQ(compiledGraph.packetCount(), 13u);
    EXPECT_EQ(compiledGraph.packetIdAt(0u), shadowPreparePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), prefixPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), shadowVisibilityPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(3u), softwareCausticsPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(4u), surfelGiPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(5u), prePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(6u), depthWarpPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(7u), extinctionPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(8u), integrationPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(9u), accumulationPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(10u), lightingPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(11u), compositePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(12u), presentPacket);
    const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledGraph.findTask(shadowPrepare);
    const Graphics::GpuCompiledTask* const compiledPrefix = compiledGraph.findTask(prefix);
    ASSERT_NE(compiledShadowPrepare, nullptr);
    ASSERT_NE(compiledPrefix, nullptr);
    ASSERT_EQ(compiledShadowPrepare->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const shadowPrepareBarrier = compiledGraph.taskPrologueBarriers(shadowPrepare);
    ASSERT_NE(shadowPrepareBarrier, nullptr);
    EXPECT_EQ(shadowPrepareBarrier[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(shadowPrepareBarrier[0u].resource, currentBindlessSlots);
    EXPECT_EQ(shadowPrepareBarrier[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(shadowPrepareBarrier[0u].after, Graphics::ResourceStates::ConstantBuffer);
    ASSERT_EQ(compiledPrefix->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const prefixSeeds = compiledGraph.taskPrologueStateSeeds(prefix);
    ASSERT_NE(prefixSeeds, nullptr);
    EXPECT_EQ(prefixSeeds[0u].resource, currentBindlessSlots);
    EXPECT_EQ(prefixSeeds[0u].sourcePacket, shadowPreparePacket);
    ASSERT_EQ(compiledGraph.packet(prefixPacket).dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const prefixPacketDependencies = compiledGraph.packetDependencies(prefixPacket);
    ASSERT_NE(prefixPacketDependencies, nullptr);
    EXPECT_EQ(prefixPacketDependencies[0u].producer, shadowPreparePacket);

    EXPECT_NE(FindEdge(analysis, shadowVisibility, lighting), nullptr);
    EXPECT_NE(FindEdge(analysis, softwareCaustics, lighting), nullptr);
    EXPECT_NE(FindEdge(analysis, surfelGi, lighting), nullptr);
    EXPECT_NE(FindEdge(analysis, accumulation, lighting), nullptr);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting);
    ASSERT_NE(compiledLighting, nullptr);
    ASSERT_GT(compiledLighting->prologueStateSeedCount, 0u);
    const Graphics::GpuPacketStateSeed* const lightingSeeds = compiledGraph.taskPrologueStateSeeds(lighting);
    ASSERT_NE(lightingSeeds, nullptr);
    bool lightingImportsShadowVisibilityState = false;
    bool lightingImportsSoftwareCausticsState = false;
    bool lightingImportsSurfelGiState = false;
    bool lightingImportsAccumulationState = false;
    for(usize index = 0u; index < compiledLighting->prologueStateSeedCount; ++index){
        lightingImportsShadowVisibilityState = lightingImportsShadowVisibilityState
            || lightingSeeds[index].sourcePacket == shadowVisibilityPacket
        ;
        lightingImportsSoftwareCausticsState = lightingImportsSoftwareCausticsState
            || lightingSeeds[index].sourcePacket == softwareCausticsPacket
        ;
        lightingImportsSurfelGiState = lightingImportsSurfelGiState
            || lightingSeeds[index].sourcePacket == surfelGiPacket
        ;
        lightingImportsAccumulationState = lightingImportsAccumulationState
            || lightingSeeds[index].sourcePacket == accumulationPacket
        ;
    }
    EXPECT_TRUE(lightingImportsShadowVisibilityState);
    EXPECT_TRUE(lightingImportsSoftwareCausticsState);
    EXPECT_TRUE(lightingImportsSurfelGiState);
    EXPECT_TRUE(lightingImportsAccumulationState);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 5u);
    const Graphics::GpuPacketDependency* const lightingPacketDependencies = compiledGraph.packetDependencies(lightingPacket);
    ASSERT_NE(lightingPacketDependencies, nullptr);
    bool lightingWaitsForPrefix = false;
    bool lightingWaitsForShadowVisibility = false;
    bool lightingWaitsForSoftwareCaustics = false;
    bool lightingWaitsForSurfelGi = false;
    bool lightingWaitsForAccumulation = false;
    for(usize index = 0u; index < compiledGraph.packet(lightingPacket).dependencyCount; ++index){
        lightingWaitsForPrefix = lightingWaitsForPrefix || lightingPacketDependencies[index].producer == prefixPacket;
        lightingWaitsForShadowVisibility = lightingWaitsForShadowVisibility
            || lightingPacketDependencies[index].producer == shadowVisibilityPacket
        ;
        lightingWaitsForSoftwareCaustics = lightingWaitsForSoftwareCaustics
            || lightingPacketDependencies[index].producer == softwareCausticsPacket
        ;
        lightingWaitsForSurfelGi = lightingWaitsForSurfelGi
            || lightingPacketDependencies[index].producer == surfelGiPacket
        ;
        lightingWaitsForAccumulation = lightingWaitsForAccumulation
            || lightingPacketDependencies[index].producer == accumulationPacket
        ;
    }
    EXPECT_TRUE(lightingWaitsForPrefix);
    EXPECT_TRUE(lightingWaitsForShadowVisibility);
    EXPECT_TRUE(lightingWaitsForSoftwareCaustics);
    EXPECT_TRUE(lightingWaitsForSurfelGi);
    EXPECT_TRUE(lightingWaitsForAccumulation);
    EXPECT_EQ(compiledGraph.packet(lightingPacket).externalDependencyCount, 0u);

    const Graphics::GpuCompiledTask* const compiledShadowVisibility = compiledGraph.findTask(shadowVisibility);
    ASSERT_NE(compiledShadowVisibility, nullptr);
    ASSERT_GT(compiledShadowVisibility->prologueStateSeedCount, 0u);
    const Graphics::GpuPacketStateSeed* const shadowVisibilitySeeds = compiledGraph.taskPrologueStateSeeds(
        shadowVisibility
    );
    ASSERT_NE(shadowVisibilitySeeds, nullptr);
    bool shadowVisibilityImportsBindlessSlotsState = false;
    for(usize index = 0u; index < compiledShadowVisibility->prologueStateSeedCount; ++index){
        shadowVisibilityImportsBindlessSlotsState = shadowVisibilityImportsBindlessSlotsState
            || (
                shadowVisibilitySeeds[index].resource == currentBindlessSlots
                && shadowVisibilitySeeds[index].sourcePacket == prefixPacket
            )
        ;
    }
    EXPECT_TRUE(shadowVisibilityImportsBindlessSlotsState);
    EXPECT_EQ(compiledGraph.packet(shadowVisibilityPacket).externalDependencyCount, 0u);
    ASSERT_EQ(compiledGraph.packet(shadowVisibilityPacket).dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const shadowVisibilityPacketDependencies = compiledGraph.packetDependencies(
        shadowVisibilityPacket
    );
    ASSERT_NE(shadowVisibilityPacketDependencies, nullptr);
    bool shadowVisibilityWaitsForShadowPrepare = false;
    bool shadowVisibilityWaitsForPrefix = false;
    for(usize index = 0u; index < compiledGraph.packet(shadowVisibilityPacket).dependencyCount; ++index){
        shadowVisibilityWaitsForShadowPrepare = shadowVisibilityWaitsForShadowPrepare
            || shadowVisibilityPacketDependencies[index].producer == shadowPreparePacket
        ;
        shadowVisibilityWaitsForPrefix = shadowVisibilityWaitsForPrefix
            || shadowVisibilityPacketDependencies[index].producer == prefixPacket
        ;
    }
    EXPECT_TRUE(shadowVisibilityWaitsForShadowPrepare);
    EXPECT_TRUE(shadowVisibilityWaitsForPrefix);
    ASSERT_EQ(compiledGraph.packet(softwareCausticsPacket).externalDependencyCount, 0u);
    ASSERT_GE(compiledGraph.packet(softwareCausticsPacket).dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const softwareCausticsPacketDependencies = compiledGraph.packetDependencies(
        softwareCausticsPacket
    );
    ASSERT_NE(softwareCausticsPacketDependencies, nullptr);
    bool softwareCausticsWaitsForShadowVisibility = false;
    for(usize index = 0u; index < compiledGraph.packet(softwareCausticsPacket).dependencyCount; ++index){
        softwareCausticsWaitsForShadowVisibility = softwareCausticsWaitsForShadowVisibility
            || softwareCausticsPacketDependencies[index].producer == shadowVisibilityPacket
        ;
    }
    EXPECT_TRUE(softwareCausticsWaitsForShadowVisibility);
    ASSERT_EQ(compiledGraph.packet(surfelGiPacket).externalDependencyCount, 0u);
    ASSERT_GE(compiledGraph.packet(surfelGiPacket).dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const surfelGiPacketDependencies = compiledGraph.packetDependencies(
        surfelGiPacket
    );
    ASSERT_NE(surfelGiPacketDependencies, nullptr);
    bool surfelGiWaitsForSoftwareCaustics = false;
    for(usize index = 0u; index < compiledGraph.packet(surfelGiPacket).dependencyCount; ++index){
        surfelGiWaitsForSoftwareCaustics = surfelGiWaitsForSoftwareCaustics
            || surfelGiPacketDependencies[index].producer == softwareCausticsPacket
        ;
    }
    EXPECT_TRUE(surfelGiWaitsForSoftwareCaustics);

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


// Opaque and prepared-transparent CSG each begin with the same StorageImage working set. The graph owns its
// initial peel/span/removed-output state setup, while the rect clear remains limited to the two values that
// accumulate across a frame. Keep the later same-state UAV handoff visible: it orders opaque writes before a
// transparent producer consumes the cap/depth/event/span/removed aliases.
TEST(GpuTaskGraph, PlansCsgIntervalWorkingSetStorageStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_cap_back_normal"),
        "CSG Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_interval_depth"),
        "CSG Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_interval_id"),
        "CSG Interval ID"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_receiver_event_data"),
        "CSG Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_receiver_event_count"),
        "CSG Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_receiver_span_data"),
        "CSG Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_receiver_span_count"),
        "CSG Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_removed_interval_depth"),
        "CSG Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_removed_interval_cap_normal"),
        "CSG Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_removed_interval_data"),
        "CSG Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_removed_interval_count"),
        "CSG Removed Interval Count"
    );
    ASSERT_TRUE(capBackNormal.valid());
    ASSERT_TRUE(intervalDepth.valid());
    ASSERT_TRUE(intervalId.valid());
    ASSERT_TRUE(receiverEventData.valid());
    ASSERT_TRUE(receiverEventCount.valid());
    ASSERT_TRUE(receiverSpanData.valid());
    ASSERT_TRUE(receiverSpanCount.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventDataRange(0u, 1u, 0u, 8u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanDataRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse intervalProducerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventDataRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanDataRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId opaqueClear = AddTask(
        graph,
        Name("tests/task_graph/csg_opaque_interval_clear"),
        "Opaque CSG Interval Clear",
        nullptr,
        0u,
        clearUses,
        LengthOf(clearUses)
    );
    const Graphics::GpuTaskId opaqueProducer = AddTask(
        graph,
        Name("tests/task_graph/csg_opaque_interval_producer"),
        "Opaque CSG Interval Producer",
        &opaqueClear,
        1u,
        intervalProducerUses,
        LengthOf(intervalProducerUses)
    );
    const Graphics::GpuTaskId transparentClear = AddTask(
        graph,
        Name("tests/task_graph/csg_transparent_interval_clear"),
        "Transparent CSG Interval Clear",
        &opaqueProducer,
        1u,
        clearUses,
        LengthOf(clearUses)
    );
    const Graphics::GpuTaskId transparentProducer = AddTask(
        graph,
        Name("tests/task_graph/csg_transparent_interval_producer"),
        "Transparent CSG Interval Producer",
        &transparentClear,
        1u,
        intervalProducerUses,
        LengthOf(intervalProducerUses)
    );
    ASSERT_TRUE(opaqueClear.valid());
    ASSERT_TRUE(opaqueProducer.valid());
    ASSERT_TRUE(transparentClear.valid());
    ASSERT_TRUE(transparentProducer.valid());
    EXPECT_EQ(graph.taskAt(opaqueClear.index).resourceUseCount, 2u);
    EXPECT_EQ(graph.taskAt(opaqueProducer.index).resourceUseCount, 11u);
    EXPECT_EQ(graph.taskAt(transparentClear.index).resourceUseCount, 2u);
    EXPECT_EQ(graph.taskAt(transparentProducer.index).resourceUseCount, 11u);

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    EXPECT_NE(FindEdge(analysis, opaqueClear, opaqueProducer), nullptr);
    EXPECT_NE(FindEdge(analysis, transparentClear, transparentProducer), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaqueClear,
        opaqueProducer,
        intervalId,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaqueClear,
        opaqueProducer,
        receiverEventCount,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    const Graphics::GpuCompiledBarrier* const opaqueProducerBarriers = compiledGraph.taskPrologueBarriers(opaqueProducer);
    const Graphics::GpuCompiledBarrier* const transparentProducerBarriers = compiledGraph.taskPrologueBarriers(
        transparentProducer
    );
    const Graphics::GpuCompiledTask* const compiledOpaqueProducer = compiledGraph.findTask(opaqueProducer);
    const Graphics::GpuCompiledTask* const compiledTransparentProducer = compiledGraph.findTask(transparentProducer);
    ASSERT_NE(compiledOpaqueProducer, nullptr);
    ASSERT_NE(compiledTransparentProducer, nullptr);
    ASSERT_NE(opaqueProducerBarriers, nullptr);
    ASSERT_NE(transparentProducerBarriers, nullptr);
    ASSERT_EQ(compiledOpaqueProducer->prologueBarrierCount, 11u);
    ASSERT_EQ(compiledTransparentProducer->prologueBarrierCount, 11u);
    bool capBackNormalTransition = false;
    bool intervalDepthTransition = false;
    bool intervalIdTransition = false;
    bool receiverEventDataTransition = false;
    bool receiverEventCountTransition = false;
    bool receiverSpanDataTransition = false;
    bool receiverSpanCountTransition = false;
    bool removedIntervalDepthTransition = false;
    bool removedIntervalCapNormalTransition = false;
    bool removedIntervalDataTransition = false;
    bool removedIntervalCountTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOpaqueProducer->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = opaqueProducerBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == capBackNormal
            && barrier.range.textureSubresources == peelRange
        )
            capBackNormalTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == intervalDepth
            && barrier.range.textureSubresources == peelRange
        )
            intervalDepthTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == intervalId
            && barrier.range.textureSubresources == peelRange
        )
            intervalIdTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverEventData
            && barrier.range.textureSubresources == receiverEventDataRange
        )
            receiverEventDataTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverEventCount
            && barrier.range.textureSubresources == receiverEventCountRange
        )
            receiverEventCountTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverSpanData
            && barrier.range.textureSubresources == receiverSpanDataRange
        )
            receiverSpanDataTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverSpanCount
            && barrier.range.textureSubresources == receiverSpanCountRange
        )
            receiverSpanCountTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalDepth
            && barrier.range.textureSubresources == removedIntervalRange
        )
            removedIntervalDepthTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalCapNormal
            && barrier.range.textureSubresources == removedIntervalRange
        )
            removedIntervalCapNormalTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalData
            && barrier.range.textureSubresources == removedIntervalRange
        )
            removedIntervalDataTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::Common
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalCount
            && barrier.range.textureSubresources == removedIntervalCountRange
        )
            removedIntervalCountTransition = true;
    }
    EXPECT_TRUE(capBackNormalTransition);
    EXPECT_TRUE(intervalDepthTransition);
    EXPECT_TRUE(intervalIdTransition);
    EXPECT_TRUE(receiverEventDataTransition);
    EXPECT_TRUE(receiverEventCountTransition);
    EXPECT_TRUE(receiverSpanDataTransition);
    EXPECT_TRUE(receiverSpanCountTransition);
    EXPECT_TRUE(removedIntervalDepthTransition);
    EXPECT_TRUE(removedIntervalCapNormalTransition);
    EXPECT_TRUE(removedIntervalDataTransition);
    EXPECT_TRUE(removedIntervalCountTransition);

    bool capBackNormalUav = false;
    bool intervalDepthUav = false;
    bool receiverEventDataUav = false;
    bool receiverSpanDataUav = false;
    bool receiverSpanCountUav = false;
    bool removedIntervalDepthUav = false;
    bool removedIntervalCapNormalUav = false;
    bool removedIntervalDataUav = false;
    bool removedIntervalCountUav = false;
    bool transparentIntervalIdTransition = false;
    bool transparentReceiverEventCountTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTransparentProducer->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = transparentProducerBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == capBackNormal
            && barrier.range.textureSubresources == peelRange
        )
            capBackNormalUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == intervalDepth
            && barrier.range.textureSubresources == peelRange
        )
            intervalDepthUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverEventData
            && barrier.range.textureSubresources == receiverEventDataRange
        )
            receiverEventDataUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverSpanData
            && barrier.range.textureSubresources == receiverSpanDataRange
        )
            receiverSpanDataUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverSpanCount
            && barrier.range.textureSubresources == receiverSpanCountRange
        )
            receiverSpanCountUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalDepth
            && barrier.range.textureSubresources == removedIntervalRange
        )
            removedIntervalDepthUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalCapNormal
            && barrier.range.textureSubresources == removedIntervalRange
        )
            removedIntervalCapNormalUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalData
            && barrier.range.textureSubresources == removedIntervalRange
        )
            removedIntervalDataUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
            && barrier.before == Graphics::ResourceStates::UnorderedAccess
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == removedIntervalCount
            && barrier.range.textureSubresources == removedIntervalCountRange
        )
            removedIntervalCountUav = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == intervalId
            && barrier.range.textureSubresources == peelRange
        )
            transparentIntervalIdTransition = true;
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
            && barrier.resource == receiverEventCount
            && barrier.range.textureSubresources == receiverEventCountRange
        )
            transparentReceiverEventCountTransition = true;
    }
    EXPECT_TRUE(capBackNormalUav);
    EXPECT_TRUE(intervalDepthUav);
    EXPECT_TRUE(receiverEventDataUav);
    EXPECT_TRUE(receiverSpanDataUav);
    EXPECT_TRUE(receiverSpanCountUav);
    EXPECT_TRUE(removedIntervalDepthUav);
    EXPECT_TRUE(removedIntervalCapNormalUav);
    EXPECT_TRUE(removedIntervalDataUav);
    EXPECT_TRUE(removedIntervalCountUav);
    EXPECT_TRUE(transparentIntervalIdTransition);
    EXPECT_TRUE(transparentReceiverEventCountTransition);
}


// Every graph-owned CSG record thunk starts from the same four heap-selected clip buffers. Their immutable uploads
// publish Common, so the compiler must establish the two SRV and two CBV states before a peel/material/cap callback
// records; direct and unprepared paths retain the native helper instead.
TEST(GpuTaskGraph, PlansCsgClipBufferEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId receiverRanges = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_clip_receiver_ranges"),
        "CSG Receiver Ranges",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId cutters = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_clip_cutters"),
        "CSG Cutters",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId clipContextSlots = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_clip_context_slots"),
        "CSG Clip Context Slots",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    const Graphics::GpuGraphResourceId intervalSampleState = AddBufferMetadata(
        graph,
        Name("tests/task_graph/csg_clip_interval_sample_state"),
        "CSG Interval Sample State",
        Graphics::ResourceStates::Common,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    ASSERT_TRUE(receiverRanges.valid());
    ASSERT_TRUE(cutters.valid());
    ASSERT_TRUE(clipContextSlots.valid());
    ASSERT_TRUE(intervalSampleState.valid());

    const Graphics::GpuTaskResourceUse csgClipUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverRanges,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = cutters,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = clipContextSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalSampleState,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint csgClipScheduling;
    csgClipScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    csgClipScheduling.forceSubmissionBoundary = true;
    csgClipScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc csgClipDesc;
    csgClipDesc
        .setIdentity(Name("tests/task_graph/csg_clip_entry"))
        .setMarkerLabel("CSG Clip Entry")
        .setQueue(graphicsRequest)
        .setScheduling(csgClipScheduling)
        .setResourceUses(csgClipUses, LengthOf(csgClipUses))
    ;
    const Graphics::GpuTaskId csgClipTask = graph.addTask(csgClipDesc);
    ASSERT_TRUE(csgClipTask.valid());
    EXPECT_EQ(graph.taskAt(csgClipTask.index).resourceUseCount, 4u);

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    ASSERT_EQ(compiledGraph.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId csgClipPacket = compiledGraph.packetForTask(csgClipTask);
    ASSERT_TRUE(csgClipPacket.valid());
    EXPECT_EQ(compiledGraph.packet(csgClipPacket).dependencyCount, 0u);
    const Graphics::GpuCompiledTask* const compiledCsgClip = compiledGraph.findTask(csgClipTask);
    ASSERT_NE(compiledCsgClip, nullptr);
    EXPECT_EQ(compiledCsgClip->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledCsgClip->prologueBarrierCount, 4u);
    const Graphics::GpuCompiledBarrier* const csgClipBarriers = compiledGraph.taskPrologueBarriers(csgClipTask);
    ASSERT_NE(csgClipBarriers, nullptr);
    const auto hasTransition = [&](
        const Graphics::GpuGraphResourceId resource,
        const Graphics::ResourceStates::Mask expectedState
    ){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCsgClip->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = csgClipBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == expectedState
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(receiverRanges, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(cutters, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(clipContextSlots, Graphics::ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasTransition(intervalSampleState, Graphics::ResourceStates::ConstantBuffer));
}


// Prepared opaque and AVBOIT material consumers share the mesh-view CBV with material instance/typed SRVs. Their
// uploads publish Common, so the graph must establish this three-buffer entry batch before native draw or CSG code
// records; only dynamically selected mesh geometry remains local to each draw.
TEST(GpuTaskGraph, PlansGraphOwnedMaterialFrameEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId meshView = AddBufferMetadata(
        graph,
        Name("tests/task_graph/material_frame_mesh_view"),
        "Material Frame Mesh View",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialInstances = AddBufferMetadata(
        graph,
        Name("tests/task_graph/material_frame_instances"),
        "Material Frame Instances",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    const Graphics::GpuGraphResourceId materialTyped = AddBufferMetadata(
        graph,
        Name("tests/task_graph/material_frame_typed"),
        "Material Frame Typed",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(meshView.valid());
    ASSERT_TRUE(materialInstances.valid());
    ASSERT_TRUE(materialTyped.valid());

    const Graphics::GpuTaskResourceUse materialUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = meshView,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialInstances,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = materialTyped,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc materialDesc;
    materialDesc
        .setIdentity(Name("tests/task_graph/material_frame_entry"))
        .setMarkerLabel("Material Frame Entry")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(materialUses, LengthOf(materialUses))
    ;
    const Graphics::GpuTaskId materialTask = graph.addTask(materialDesc);
    ASSERT_TRUE(materialTask.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledTask* const compiledMaterial = compiledGraph.findTask(materialTask);
    ASSERT_NE(compiledMaterial, nullptr);
    ASSERT_EQ(compiledMaterial->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledMaterial->prologueBarrierCount, 3u);
    const Graphics::GpuCompiledBarrier* const materialBarriers = compiledGraph.taskPrologueBarriers(materialTask);
    ASSERT_NE(materialBarriers, nullptr);
    const auto hasTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < compiledMaterial->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = materialBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
                && barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTransition(meshView, Graphics::ResourceStates::ConstantBuffer));
    EXPECT_TRUE(hasTransition(materialInstances, Graphics::ResourceStates::ShaderResource));
    EXPECT_TRUE(hasTransition(materialTyped, Graphics::ResourceStates::ShaderResource));
}


// Prepared draw streams retain the exact mesh source buffers selected during gathering. Unlike the shared material
// batch, these geometry SRVs are dynamic per stream, so verify the graph still lowers their Common predecessor
// before the getter-only draw thunk would run.
TEST(GpuTaskGraph, PlansGraphOwnedMaterialGeometryEntryStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    constexpr Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::Graphics;
    const Graphics::GpuGraphResourceId geometry = AddBufferMetadata(
        graph,
        Name("tests/task_graph/material_geometry_source"),
        "Prepared Material Geometry",
        Graphics::ResourceStates::Common,
        queueSharing
    );
    ASSERT_TRUE(geometry.valid());

    const Graphics::GpuTaskResourceUse materialUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = geometry,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc materialDesc;
    materialDesc
        .setIdentity(Name("tests/task_graph/material_geometry_entry"))
        .setMarkerLabel("Material Geometry Entry")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(materialUses, LengthOf(materialUses))
    ;
    const Graphics::GpuTaskId materialTask = graph.addTask(materialDesc);
    ASSERT_TRUE(materialTask.valid());

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledTask* const compiledMaterial = compiledGraph.findTask(materialTask);
    ASSERT_NE(compiledMaterial, nullptr);
    ASSERT_EQ(compiledMaterial->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledMaterial->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const materialBarrier = compiledGraph.taskPrologueBarriers(materialTask);
    ASSERT_NE(materialBarrier, nullptr);
    EXPECT_EQ(materialBarrier[0].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(materialBarrier[0].resource, geometry);
    EXPECT_EQ(materialBarrier[0].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(materialBarrier[0].after, Graphics::ResourceStates::ShaderResource);
}


// The opaque CSG graph exposes G-buffer, receiver-span build, interval combine, and material/cap sampling as four
// Graphics callbacks. Keep them in one packet when FrontierSafe permits it, but require the compiler to lower each
// same-UAV handoff instead of relying on renderer-owned state calls inside either thunk.
TEST(GpuTaskGraph, PlansMergedCsgGbufferSpanBuildCombineAndOpaqueSampleUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_cap_back_normal"),
        "CSG Combine Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_interval_depth"),
        "CSG Combine Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_interval_id"),
        "CSG Combine Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_receiver_event_data"),
        "CSG Span Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_receiver_event_count"),
        "CSG Span Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_receiver_span_data"),
        "CSG Combine Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_receiver_span_count"),
        "CSG Combine Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_depth"),
        "CSG Sample Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_cap_normal"),
        "CSG Sample Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_data"),
        "CSG Sample Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_removed_interval_count"),
        "CSG Sample Removed Interval Count"
    );
    ASSERT_TRUE(capBackNormal.valid());
    ASSERT_TRUE(intervalDepth.valid());
    ASSERT_TRUE(intervalId.valid());
    ASSERT_TRUE(receiverEventData.valid());
    ASSERT_TRUE(receiverEventCount.valid());
    ASSERT_TRUE(receiverSpanData.valid());
    ASSERT_TRUE(receiverSpanCount.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse spanBuildUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse sampleUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/csg_gbuffer"))
        .setMarkerLabel("Opaque CSG G-buffer")
        .setQueue(graphicsRequest)
        .setScheduling(producerScheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskSchedulingHint spanBuildScheduling;
    spanBuildScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    spanBuildScheduling.forceSubmissionBoundary = false;
    spanBuildScheduling.allowPacketMerge = true;
    spanBuildScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc spanBuildDesc;
    spanBuildDesc
        .setIdentity(Name("tests/task_graph/csg_receiver_span_build"))
        .setMarkerLabel("Opaque CSG Receiver Span Build")
        .setQueue(graphicsRequest)
        .setScheduling(spanBuildScheduling)
        .setDependencies(&producer, 1u)
        .setResourceUses(spanBuildUses, LengthOf(spanBuildUses))
    ;
    const Graphics::GpuTaskId spanBuild = graph.addTask(spanBuildDesc);
    ASSERT_TRUE(spanBuild.valid());

    Graphics::GpuTaskSchedulingHint combineScheduling;
    combineScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    combineScheduling.forceSubmissionBoundary = false;
    combineScheduling.allowPacketMerge = true;
    combineScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/task_graph/csg_interval_combine"))
        .setMarkerLabel("CSG Interval Combine")
        .setQueue(graphicsRequest)
        .setScheduling(combineScheduling)
        .setDependencies(&spanBuild, 1u)
        .setResourceUses(combineUses, LengthOf(combineUses))
    ;
    const Graphics::GpuTaskId combine = graph.addTask(combineDesc);
    ASSERT_TRUE(combine.valid());

    Graphics::GpuTaskSchedulingHint sampleScheduling;
    sampleScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    sampleScheduling.forceSubmissionBoundary = false;
    sampleScheduling.allowPacketMerge = true;
    sampleScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc sampleDesc;
    sampleDesc
        .setIdentity(Name("tests/task_graph/csg_interval_opaque_sample"))
        .setMarkerLabel("Opaque CSG Interval Sample")
        .setQueue(graphicsRequest)
        .setScheduling(sampleScheduling)
        .setDependencies(&combine, 1u)
        .setResourceUses(sampleUses, LengthOf(sampleUses))
    ;
    const Graphics::GpuTaskId sample = graph.addTask(sampleDesc);
    ASSERT_TRUE(sample.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_NE(FindEdge(analysis, producer, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, producer, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, sample), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalId,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledGraph.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledGraph.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledGraph.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId samplePacket = compiledGraph.packetForTask(sample);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(spanBuildPacket.valid());
    ASSERT_TRUE(combinePacket.valid());
    ASSERT_EQ(spanBuildPacket, producerPacket);
    ASSERT_EQ(combinePacket, producerPacket);
    ASSERT_EQ(samplePacket, combinePacket);
    const Graphics::GpuSubmissionPacket& packet = compiledGraph.packet(combinePacket);
    ASSERT_EQ(packet.taskCount, 4u);
    ASSERT_NE(compiledGraph.packetTasks(combinePacket), nullptr);
    EXPECT_EQ(compiledGraph.packetTasks(combinePacket)[0u], producer);
    EXPECT_EQ(compiledGraph.packetTasks(combinePacket)[1u], spanBuild);
    EXPECT_EQ(compiledGraph.packetTasks(combinePacket)[2u], combine);
    EXPECT_EQ(compiledGraph.packetTasks(combinePacket)[3u], sample);
    EXPECT_EQ(packet.dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const compiledProducer = compiledGraph.findTask(producer);
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledGraph.findTask(spanBuild);
    const Graphics::GpuCompiledTask* const compiledCombine = compiledGraph.findTask(combine);
    const Graphics::GpuCompiledTask* const compiledSample = compiledGraph.findTask(sample);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledSample, nullptr);
    ASSERT_EQ(compiledProducer->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledSpanBuild->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledCombine->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledSample->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, 5u);
    ASSERT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledCombine->prologueBarrierCount, 9u);
    ASSERT_EQ(compiledSample->prologueBarrierCount, 4u);
    const Graphics::GpuCompiledBarrier* const producerBarriers = compiledGraph.taskPrologueBarriers(producer);
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledGraph.taskPrologueBarriers(spanBuild);
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledGraph.taskPrologueBarriers(combine);
    const Graphics::GpuCompiledBarrier* const sampleBarriers = compiledGraph.taskPrologueBarriers(sample);
    ASSERT_NE(producerBarriers, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(sampleBarriers, nullptr);
    const auto hasProducerTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledProducer->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = producerBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasSpanBuildTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSpanBuild->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = spanBuildBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasSpanBuildUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSpanBuild->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = spanBuildBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasCombineTransition = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCombine->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = combineBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasCombineUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledCombine->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = combineBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasSampleUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledSample->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = sampleBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasProducerTransition(capBackNormal, peelRange));
    EXPECT_TRUE(hasProducerTransition(intervalDepth, peelRange));
    EXPECT_TRUE(hasProducerTransition(intervalId, peelRange));
    EXPECT_TRUE(hasProducerTransition(receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasProducerTransition(receiverEventCount, receiverEventCountRange));
    EXPECT_TRUE(hasSpanBuildUav(receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasSpanBuildUav(receiverEventCount, receiverEventCountRange));
    EXPECT_TRUE(hasSpanBuildTransition(receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasSpanBuildTransition(receiverSpanCount, receiverSpanCountRange));
    EXPECT_TRUE(hasCombineUav(capBackNormal, peelRange));
    EXPECT_TRUE(hasCombineUav(intervalDepth, peelRange));
    EXPECT_TRUE(hasCombineUav(intervalId, peelRange));
    EXPECT_TRUE(hasCombineUav(receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasCombineUav(receiverSpanCount, receiverSpanCountRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasCombineTransition(removedIntervalCount, removedIntervalCountRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasSampleUav(removedIntervalCount, removedIntervalCountRange));
}


// The normal opaque route keeps G-buffer, span build, combine, and sample in one Graphics packet. If FrontierSafe
// must split them, the Span and Combine callbacks cannot restore their StorageImage inputs natively: their packet
// prologues need source-state seeds and same-UAV fences for every cross-packet input.
TEST(GpuTaskGraph, PlansCsgGbufferSpanBuildCombineAndSampleUavDependenciesAcrossForcedPacketSplit){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_cap_back_normal"),
        "CSG Combine Split Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_interval_depth"),
        "CSG Combine Split Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_interval_id"),
        "CSG Combine Split Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_split_receiver_event_data"),
        "CSG Span Split Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_span_split_receiver_event_count"),
        "CSG Span Split Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_receiver_span_data"),
        "CSG Combine Split Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_combine_split_receiver_span_count"),
        "CSG Combine Split Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_depth"),
        "CSG Sample Split Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_cap_normal"),
        "CSG Sample Split Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_data"),
        "CSG Sample Split Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/csg_sample_split_removed_interval_count"),
        "CSG Sample Split Removed Interval Count"
    );
    ASSERT_TRUE(capBackNormal.valid());
    ASSERT_TRUE(intervalDepth.valid());
    ASSERT_TRUE(intervalId.valid());
    ASSERT_TRUE(receiverEventData.valid());
    ASSERT_TRUE(receiverEventCount.valid());
    ASSERT_TRUE(receiverSpanData.valid());
    ASSERT_TRUE(receiverSpanCount.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse producerUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse spanBuildUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse sampleUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/csg_split_gbuffer"))
        .setMarkerLabel("Opaque CSG G-buffer")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    ASSERT_TRUE(producer.valid());

    Graphics::GpuTaskDesc spanBuildDesc;
    spanBuildDesc
        .setIdentity(Name("tests/task_graph/csg_receiver_span_split_build"))
        .setMarkerLabel("Opaque CSG Receiver Span Build")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&producer, 1u)
        .setResourceUses(spanBuildUses, LengthOf(spanBuildUses))
    ;
    const Graphics::GpuTaskId spanBuild = graph.addTask(spanBuildDesc);
    ASSERT_TRUE(spanBuild.valid());

    Graphics::GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/task_graph/csg_interval_split_combine"))
        .setMarkerLabel("CSG Interval Combine")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&spanBuild, 1u)
        .setResourceUses(combineUses, LengthOf(combineUses))
    ;
    const Graphics::GpuTaskId combine = graph.addTask(combineDesc);
    ASSERT_TRUE(combine.valid());

    Graphics::GpuTaskDesc sampleDesc;
    sampleDesc
        .setIdentity(Name("tests/task_graph/csg_interval_split_sample"))
        .setMarkerLabel("Opaque CSG Interval Sample")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&combine, 1u)
        .setResourceUses(sampleUses, LengthOf(sampleUses))
    ;
    const Graphics::GpuTaskId sample = graph.addTask(sampleDesc);
    ASSERT_TRUE(sample.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_NE(FindEdge(analysis, producer, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, producer, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, sample), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        producer,
        combine,
        intervalId,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        sample,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledGraph.packetCount(), 4u);
    const Graphics::GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producer);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledGraph.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledGraph.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId samplePacket = compiledGraph.packetForTask(sample);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(spanBuildPacket.valid());
    ASSERT_TRUE(combinePacket.valid());
    ASSERT_TRUE(samplePacket.valid());
    EXPECT_NE(producerPacket, spanBuildPacket);
    EXPECT_NE(producerPacket, combinePacket);
    EXPECT_NE(spanBuildPacket, combinePacket);
    EXPECT_NE(combinePacket, samplePacket);
    EXPECT_EQ(compiledGraph.packet(producerPacket).taskCount, 1u);
    EXPECT_EQ(compiledGraph.packet(spanBuildPacket).taskCount, 1u);
    EXPECT_EQ(compiledGraph.packet(combinePacket).taskCount, 1u);
    EXPECT_EQ(compiledGraph.packet(samplePacket).taskCount, 1u);
    ASSERT_EQ(compiledGraph.packet(spanBuildPacket).dependencyCount, 1u);
    ASSERT_NE(compiledGraph.packetDependencies(spanBuildPacket), nullptr);
    EXPECT_EQ(compiledGraph.packetDependencies(spanBuildPacket)[0u].producer, producerPacket);
    ASSERT_EQ(compiledGraph.packet(combinePacket).dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const combineDependencies = compiledGraph.packetDependencies(combinePacket);
    ASSERT_NE(combineDependencies, nullptr);
    bool combineWaitsForProducer = false;
    bool combineWaitsForSpanBuild = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < compiledGraph.packet(combinePacket).dependencyCount; ++dependencyIndex){
        combineWaitsForProducer = combineWaitsForProducer || combineDependencies[dependencyIndex].producer == producerPacket;
        combineWaitsForSpanBuild = combineWaitsForSpanBuild || combineDependencies[dependencyIndex].producer == spanBuildPacket;
    }
    EXPECT_TRUE(combineWaitsForProducer);
    EXPECT_TRUE(combineWaitsForSpanBuild);
    ASSERT_EQ(compiledGraph.packet(samplePacket).dependencyCount, 1u);
    ASSERT_NE(compiledGraph.packetDependencies(samplePacket), nullptr);
    EXPECT_EQ(compiledGraph.packetDependencies(samplePacket)[0u].producer, combinePacket);

    const Graphics::GpuCompiledTask* const compiledProducer = compiledGraph.findTask(producer);
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledGraph.findTask(spanBuild);
    const Graphics::GpuCompiledTask* const compiledCombine = compiledGraph.findTask(combine);
    const Graphics::GpuCompiledTask* const compiledSample = compiledGraph.findTask(sample);
    ASSERT_NE(compiledProducer, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledSample, nullptr);
    ASSERT_EQ(compiledProducer->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledProducer->prologueBarrierCount, 5u);
    ASSERT_EQ(compiledSpanBuild->prologueStateSeedCount, 2u);
    ASSERT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledCombine->prologueStateSeedCount, 5u);
    ASSERT_EQ(compiledCombine->prologueBarrierCount, 9u);
    ASSERT_EQ(compiledSample->prologueStateSeedCount, 4u);
    ASSERT_EQ(compiledSample->prologueBarrierCount, 4u);
    const Graphics::GpuPacketStateSeed* const spanBuildSeeds = compiledGraph.taskPrologueStateSeeds(spanBuild);
    const Graphics::GpuPacketStateSeed* const combineSeeds = compiledGraph.taskPrologueStateSeeds(combine);
    const Graphics::GpuPacketStateSeed* const sampleSeeds = compiledGraph.taskPrologueStateSeeds(sample);
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledGraph.taskPrologueBarriers(spanBuild);
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledGraph.taskPrologueBarriers(combine);
    const Graphics::GpuCompiledBarrier* const sampleBarriers = compiledGraph.taskPrologueBarriers(sample);
    ASSERT_NE(spanBuildSeeds, nullptr);
    ASSERT_NE(combineSeeds, nullptr);
    ASSERT_NE(sampleSeeds, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(sampleBarriers, nullptr);
    const auto hasStateSeed = [](
        const Graphics::GpuPacketStateSeed* const seeds,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range,
        const Graphics::GpuSubmissionPacketId sourcePacket
    ){
        for(u32 seedIndex = 0u; seedIndex < count; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = seeds[seedIndex];
            if(
                seed.resource == resource
                && seed.range.textureSubresources == range
                && seed.sourcePacket == sourcePacket
            )
                return true;
        }
        return false;
    };
    const auto hasUav = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateSeed(
        spanBuildSeeds,
        compiledSpanBuild->prologueStateSeedCount,
        receiverEventData,
        receiverEventRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        spanBuildSeeds,
        compiledSpanBuild->prologueStateSeedCount,
        receiverEventCount,
        receiverEventCountRange,
        producerPacket
    ));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventData,
        receiverEventRange
    ));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        capBackNormal,
        peelRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        intervalDepth,
        peelRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        intervalId,
        peelRange,
        producerPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        receiverSpanData,
        receiverSpanRange,
        spanBuildPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        receiverSpanCount,
        receiverSpanCountRange,
        spanBuildPacket
    ));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, receiverSpanCount, receiverSpanCountRange));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalDepth,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalCapNormal,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalData,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        sampleSeeds,
        compiledSample->prologueStateSeedCount,
        removedIntervalCount,
        removedIntervalCountRange,
        combinePacket
    ));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasUav(sampleBarriers, compiledSample->prologueBarrierCount, removedIntervalCount, removedIntervalCountRange));
}


// Prepared transparent CSG now lowers receiver-surface events through a dedicated Span callback before Combine.
// Keep the exact Pre -> Span -> Combine -> Clear -> Occupancy same-UAV chain in one Graphics packet when
// FrontierSafe permits it, while retaining the independent Clear -> Occupancy coverage handoff.
TEST(GpuTaskGraph, PlansAvboitCsgIntervalProducerSpanBuildCombineToOccupancyUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_cap_back_normal"),
        "AVBOIT CSG Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_interval_depth"),
        "AVBOIT CSG Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_interval_id"),
        "AVBOIT CSG Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_event_data"),
        "AVBOIT CSG Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_event_count"),
        "AVBOIT CSG Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_span_data"),
        "AVBOIT CSG Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_receiver_span_count"),
        "AVBOIT CSG Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_depth"),
        "AVBOIT CSG Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_cap_normal"),
        "AVBOIT CSG Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_data"),
        "AVBOIT CSG Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_removed_interval_count"),
        "AVBOIT CSG Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_coverage"),
        "AVBOIT Coverage"
    );
    ASSERT_TRUE(capBackNormal.valid());
    ASSERT_TRUE(intervalDepth.valid());
    ASSERT_TRUE(intervalId.valid());
    ASSERT_TRUE(receiverEventData.valid());
    ASSERT_TRUE(receiverEventCount.valid());
    ASSERT_TRUE(receiverSpanData.valid());
    ASSERT_TRUE(receiverSpanCount.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(coverage.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse spanBuildUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverEventCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = capBackNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = receiverSpanCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = Graphics::GpuTaskCostHint::Large;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/avboit_transparent_csg_pre"))
        .setMarkerLabel("Transparent CSG Pre")
        .setQueue(graphicsRequest)
        .setScheduling(preScheduling)
        .setResourceUses(preUses, LengthOf(preUses))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    Graphics::GpuTaskSchedulingHint spanBuildScheduling = preScheduling;
    spanBuildScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    spanBuildScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc spanBuildDesc;
    spanBuildDesc
        .setIdentity(Name("tests/task_graph/avboit_transparent_csg_receiver_span"))
        .setMarkerLabel("Transparent CSG Receiver Span")
        .setQueue(graphicsRequest)
        .setScheduling(spanBuildScheduling)
        .setDependencies(&pre, 1u)
        .setResourceUses(spanBuildUses, LengthOf(spanBuildUses))
    ;
    const Graphics::GpuTaskId spanBuild = graph.addTask(spanBuildDesc);
    ASSERT_TRUE(spanBuild.valid());

    Graphics::GpuTaskSchedulingHint combineScheduling = preScheduling;
    combineScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    combineScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/task_graph/avboit_transparent_csg_combine"))
        .setMarkerLabel("Transparent CSG Interval Combine")
        .setQueue(graphicsRequest)
        .setScheduling(combineScheduling)
        .setDependencies(&spanBuild, 1u)
        .setResourceUses(combineUses, LengthOf(combineUses))
    ;
    const Graphics::GpuTaskId combine = graph.addTask(combineDesc);
    ASSERT_TRUE(combine.valid());

    Graphics::GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/task_graph/avboit_clear"))
        .setMarkerLabel("AVBOIT Clear")
        .setQueue(graphicsRequest)
        .setScheduling(clearScheduling)
        .setDependencies(&combine, 1u)
        .setResourceUses(clearUses, LengthOf(clearUses))
    ;
    const Graphics::GpuTaskId clear = graph.addTask(clearDesc);
    ASSERT_TRUE(clear.valid());

    Graphics::GpuTaskSchedulingHint occupancyScheduling;
    occupancyScheduling.cost = Graphics::GpuTaskCostHint::Large;
    occupancyScheduling.forceSubmissionBoundary = false;
    occupancyScheduling.allowPacketMerge = true;
    occupancyScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc occupancyDesc;
    occupancyDesc
        .setIdentity(Name("tests/task_graph/avboit_occupancy"))
        .setMarkerLabel("AVBOIT Occupancy")
        .setQueue(graphicsRequest)
        .setScheduling(occupancyScheduling)
        .setDependencies(&clear, 1u)
        .setResourceUses(occupancyUses, LengthOf(occupancyUses))
    ;
    const Graphics::GpuTaskId occupancy = graph.addTask(occupancyDesc);
    ASSERT_TRUE(occupancy.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_NE(FindEdge(analysis, pre, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, pre, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, clear), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, occupancy), nullptr);
    ASSERT_NE(FindEdge(analysis, clear, occupancy), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        intervalId,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        occupancy,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledGraph.packetCount(), 1u);
    const Graphics::GpuSubmissionPacketId prePacket = compiledGraph.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledGraph.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledGraph.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId clearPacket = compiledGraph.packetForTask(clear);
    const Graphics::GpuSubmissionPacketId occupancyPacket = compiledGraph.packetForTask(occupancy);
    ASSERT_TRUE(prePacket.valid());
    EXPECT_EQ(spanBuildPacket, prePacket);
    EXPECT_EQ(combinePacket, prePacket);
    EXPECT_EQ(clearPacket, prePacket);
    EXPECT_EQ(occupancyPacket, prePacket);
    const Graphics::GpuSubmissionPacket& packet = compiledGraph.packet(prePacket);
    ASSERT_EQ(packet.taskCount, 5u);
    ASSERT_NE(compiledGraph.packetTasks(prePacket), nullptr);
    EXPECT_EQ(compiledGraph.packetTasks(prePacket)[0u], pre);
    EXPECT_EQ(compiledGraph.packetTasks(prePacket)[1u], spanBuild);
    EXPECT_EQ(compiledGraph.packetTasks(prePacket)[2u], combine);
    EXPECT_EQ(compiledGraph.packetTasks(prePacket)[3u], clear);
    EXPECT_EQ(compiledGraph.packetTasks(prePacket)[4u], occupancy);
    EXPECT_EQ(packet.dependencyCount, 0u);

    const Graphics::GpuCompiledTask* const compiledPre = compiledGraph.findTask(pre);
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledGraph.findTask(spanBuild);
    const Graphics::GpuCompiledTask* const compiledCombine = compiledGraph.findTask(combine);
    const Graphics::GpuCompiledTask* const compiledClear = compiledGraph.findTask(clear);
    const Graphics::GpuCompiledTask* const compiledOccupancy = compiledGraph.findTask(occupancy);
    ASSERT_NE(compiledPre, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledClear, nullptr);
    ASSERT_NE(compiledOccupancy, nullptr);
    EXPECT_EQ(compiledPre->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledSpanBuild->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledCombine->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledClear->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledOccupancy->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledPre->prologueBarrierCount, 5u);
    EXPECT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    EXPECT_EQ(compiledCombine->prologueBarrierCount, 9u);
    EXPECT_EQ(compiledClear->prologueBarrierCount, 1u);
    EXPECT_EQ(compiledOccupancy->prologueBarrierCount, 5u);
    const Graphics::GpuCompiledBarrier* const preBarriers = compiledGraph.taskPrologueBarriers(pre);
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledGraph.taskPrologueBarriers(spanBuild);
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledGraph.taskPrologueBarriers(combine);
    const Graphics::GpuCompiledBarrier* const clearBarriers = compiledGraph.taskPrologueBarriers(clear);
    const Graphics::GpuCompiledBarrier* const occupancyBarriers = compiledGraph.taskPrologueBarriers(occupancy);
    ASSERT_NE(preBarriers, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(clearBarriers, nullptr);
    ASSERT_NE(occupancyBarriers, nullptr);
    const auto hasTextureTransition = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    const auto hasUav = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasTextureTransition(
        preBarriers,
        compiledPre->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    EXPECT_TRUE(hasUav(spanBuildBarriers, compiledSpanBuild->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanData,
        receiverSpanRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasUav(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalDepth,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalData,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    EXPECT_EQ(clearBarriers[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(clearBarriers[0u].resource, coverage);
    EXPECT_EQ(clearBarriers[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(clearBarriers[0u].after, Graphics::ResourceStates::CopyDest);
    bool coverageTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOccupancy->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == coverage
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        )
            coverageTransition = true;
    }
    EXPECT_TRUE(coverageTransition);
}


// The renderer keeps the prepared transparent chain co-located for its shared AVBOIT timing ticket, but the
// compiler must still retain exact state seeds if an independently-declared Pre -> Span -> Combine chain crosses
// packet frontiers. In particular, Occupancy waits on both the unrelated Clear and Combine's removed-interval outputs.
TEST(GpuTaskGraph, SeedsSplitAvboitCsgReceiverSpanBuildCombineAndOccupancyUavDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId capBackNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_cap_back_normal"),
        "AVBOIT CSG Split Cap Back Normal"
    );
    const Graphics::GpuGraphResourceId intervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_interval_depth"),
        "AVBOIT CSG Split Interval Depth"
    );
    const Graphics::GpuGraphResourceId intervalId = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_interval_id"),
        "AVBOIT CSG Split Interval Id"
    );
    const Graphics::GpuGraphResourceId receiverEventData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_event_data"),
        "AVBOIT CSG Split Receiver Event Data"
    );
    const Graphics::GpuGraphResourceId receiverEventCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_event_count"),
        "AVBOIT CSG Split Receiver Event Count"
    );
    const Graphics::GpuGraphResourceId receiverSpanData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_span_data"),
        "AVBOIT CSG Split Receiver Span Data"
    );
    const Graphics::GpuGraphResourceId receiverSpanCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_receiver_span_count"),
        "AVBOIT CSG Split Receiver Span Count"
    );
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_depth"),
        "AVBOIT CSG Split Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_cap_normal"),
        "AVBOIT CSG Split Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_data"),
        "AVBOIT CSG Split Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_removed_interval_count"),
        "AVBOIT CSG Split Removed Interval Count"
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_csg_split_coverage"),
        "AVBOIT CSG Split Coverage"
    );
    ASSERT_TRUE(capBackNormal.valid());
    ASSERT_TRUE(intervalDepth.valid());
    ASSERT_TRUE(intervalId.valid());
    ASSERT_TRUE(receiverEventData.valid());
    ASSERT_TRUE(receiverEventCount.valid());
    ASSERT_TRUE(receiverSpanData.valid());
    ASSERT_TRUE(receiverSpanCount.valid());
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());
    ASSERT_TRUE(coverage.valid());

    const Graphics::TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const Graphics::TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const Graphics::TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const auto textureUse = [](
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range,
        const Graphics::GpuTaskResourceAccess::Enum access
    ){
        return Graphics::GpuTaskResourceUse{
            .resource = resource,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = range },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = access,
        };
    };
    const Graphics::GpuTaskResourceUse preUses[] = {
        textureUse(capBackNormal, peelRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(intervalDepth, peelRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(intervalId, peelRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(receiverEventData, receiverEventRange, Graphics::GpuTaskResourceAccess::ReadWrite),
        textureUse(receiverEventCount, receiverEventCountRange, Graphics::GpuTaskResourceAccess::ReadWrite),
    };
    const Graphics::GpuTaskResourceUse spanBuildUses[] = {
        textureUse(receiverEventData, receiverEventRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverEventCount, receiverEventCountRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverSpanData, receiverSpanRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(receiverSpanCount, receiverSpanCountRange, Graphics::GpuTaskResourceAccess::Write),
    };
    const Graphics::GpuTaskResourceUse combineUses[] = {
        textureUse(capBackNormal, peelRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(intervalDepth, peelRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(intervalId, peelRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverSpanData, receiverSpanRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(receiverSpanCount, receiverSpanCountRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalDepth, removedIntervalRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(removedIntervalCapNormal, removedIntervalRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(removedIntervalData, removedIntervalRange, Graphics::GpuTaskResourceAccess::Write),
        textureUse(removedIntervalCount, removedIntervalCountRange, Graphics::GpuTaskResourceAccess::Write),
    };
    const Graphics::GpuTaskResourceUse clearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse occupancyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = coverage,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        textureUse(removedIntervalDepth, removedIntervalRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalCapNormal, removedIntervalRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalData, removedIntervalRange, Graphics::GpuTaskResourceAccess::Read),
        textureUse(removedIntervalCount, removedIntervalCountRange, Graphics::GpuTaskResourceAccess::Read),
    };
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Medium;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;
    const auto addTask = [&](const Name identity,
                             const char* const label,
                             const Graphics::GpuTaskId* const dependencies,
                             const u32 dependencyCount,
                             const Graphics::GpuTaskResourceUse* const uses,
                             const u32 useCount){
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsRequest)
            .setScheduling(boundaryScheduling)
            .setDependencies(dependencies, dependencyCount)
            .setResourceUses(uses, useCount)
        ;
        return graph.addTask(desc);
    };
    const Graphics::GpuTaskId pre = addTask(
        Name("tests/task_graph/avboit_csg_split_pre"),
        "Transparent CSG Pre",
        nullptr,
        0u,
        preUses,
        LengthOf(preUses)
    );
    ASSERT_TRUE(pre.valid());
    const Graphics::GpuTaskId spanBuild = addTask(
        Name("tests/task_graph/avboit_csg_split_receiver_span"),
        "Transparent CSG Receiver Span",
        &pre,
        1u,
        spanBuildUses,
        LengthOf(spanBuildUses)
    );
    ASSERT_TRUE(spanBuild.valid());
    const Graphics::GpuTaskId combine = addTask(
        Name("tests/task_graph/avboit_csg_split_combine"),
        "Transparent CSG Interval Combine",
        &spanBuild,
        1u,
        combineUses,
        LengthOf(combineUses)
    );
    ASSERT_TRUE(combine.valid());
    const Graphics::GpuTaskId clear = addTask(
        Name("tests/task_graph/avboit_csg_split_clear"),
        "AVBOIT Clear",
        &combine,
        1u,
        clearUses,
        LengthOf(clearUses)
    );
    ASSERT_TRUE(clear.valid());
    const Graphics::GpuTaskId occupancy = addTask(
        Name("tests/task_graph/avboit_csg_split_occupancy"),
        "AVBOIT CSG Occupancy",
        &clear,
        1u,
        occupancyUses,
        LengthOf(occupancyUses)
    );
    ASSERT_TRUE(occupancy.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    ASSERT_NE(FindEdge(analysis, pre, spanBuild), nullptr);
    ASSERT_NE(FindEdge(analysis, pre, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, spanBuild, combine), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, clear), nullptr);
    ASSERT_NE(FindEdge(analysis, combine, occupancy), nullptr);
    ASSERT_NE(FindEdge(analysis, clear, occupancy), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        spanBuild,
        receiverEventCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        capBackNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        intervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        pre,
        combine,
        intervalId,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        spanBuild,
        combine,
        receiverSpanCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        combine,
        occupancy,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        clear,
        occupancy,
        coverage,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    ASSERT_EQ(compiledGraph.packetCount(), 5u);
    const Graphics::GpuSubmissionPacketId prePacket = compiledGraph.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId spanBuildPacket = compiledGraph.packetForTask(spanBuild);
    const Graphics::GpuSubmissionPacketId combinePacket = compiledGraph.packetForTask(combine);
    const Graphics::GpuSubmissionPacketId clearPacket = compiledGraph.packetForTask(clear);
    const Graphics::GpuSubmissionPacketId occupancyPacket = compiledGraph.packetForTask(occupancy);
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(spanBuildPacket.valid());
    ASSERT_TRUE(combinePacket.valid());
    ASSERT_TRUE(clearPacket.valid());
    ASSERT_TRUE(occupancyPacket.valid());
    EXPECT_NE(prePacket, spanBuildPacket);
    EXPECT_NE(prePacket, combinePacket);
    EXPECT_NE(spanBuildPacket, combinePacket);
    EXPECT_NE(combinePacket, clearPacket);
    EXPECT_NE(clearPacket, occupancyPacket);
    EXPECT_EQ(compiledGraph.packet(prePacket).taskCount, 1u);
    EXPECT_EQ(compiledGraph.packet(spanBuildPacket).taskCount, 1u);
    EXPECT_EQ(compiledGraph.packet(combinePacket).taskCount, 1u);
    EXPECT_EQ(compiledGraph.packet(clearPacket).taskCount, 1u);
    EXPECT_EQ(compiledGraph.packet(occupancyPacket).taskCount, 1u);
    const auto packetWaitsFor = [&](const Graphics::GpuSubmissionPacketId packet,
                                    const Graphics::GpuSubmissionPacketId producer){
        const Graphics::GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        const Graphics::GpuPacketDependency* const dependencies = compiledGraph.packetDependencies(packet);
        for(u32 dependencyIndex = 0u; dependencyIndex < packetPlan.dependencyCount; ++dependencyIndex){
            if(dependencies[dependencyIndex].producer == producer)
                return true;
        }
        return false;
    };
    EXPECT_TRUE(packetWaitsFor(spanBuildPacket, prePacket));
    EXPECT_TRUE(packetWaitsFor(combinePacket, prePacket));
    EXPECT_TRUE(packetWaitsFor(combinePacket, spanBuildPacket));
    EXPECT_TRUE(packetWaitsFor(clearPacket, combinePacket));
    EXPECT_TRUE(packetWaitsFor(occupancyPacket, combinePacket));
    EXPECT_TRUE(packetWaitsFor(occupancyPacket, clearPacket));

    const Graphics::GpuCompiledTask* const compiledPre = compiledGraph.findTask(pre);
    const Graphics::GpuCompiledTask* const compiledSpanBuild = compiledGraph.findTask(spanBuild);
    const Graphics::GpuCompiledTask* const compiledCombine = compiledGraph.findTask(combine);
    const Graphics::GpuCompiledTask* const compiledClear = compiledGraph.findTask(clear);
    const Graphics::GpuCompiledTask* const compiledOccupancy = compiledGraph.findTask(occupancy);
    ASSERT_NE(compiledPre, nullptr);
    ASSERT_NE(compiledSpanBuild, nullptr);
    ASSERT_NE(compiledCombine, nullptr);
    ASSERT_NE(compiledClear, nullptr);
    ASSERT_NE(compiledOccupancy, nullptr);
    EXPECT_EQ(compiledPre->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledPre->prologueBarrierCount, 5u);
    EXPECT_EQ(compiledSpanBuild->prologueStateSeedCount, 2u);
    EXPECT_EQ(compiledSpanBuild->prologueBarrierCount, 4u);
    EXPECT_EQ(compiledCombine->prologueStateSeedCount, 5u);
    EXPECT_EQ(compiledCombine->prologueBarrierCount, 9u);
    EXPECT_EQ(compiledClear->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledClear->prologueBarrierCount, 1u);
    EXPECT_EQ(compiledOccupancy->prologueStateSeedCount, 5u);
    EXPECT_EQ(compiledOccupancy->prologueBarrierCount, 5u);
    const Graphics::GpuPacketStateSeed* const spanBuildSeeds = compiledGraph.taskPrologueStateSeeds(spanBuild);
    const Graphics::GpuPacketStateSeed* const combineSeeds = compiledGraph.taskPrologueStateSeeds(combine);
    const Graphics::GpuPacketStateSeed* const occupancySeeds = compiledGraph.taskPrologueStateSeeds(occupancy);
    const Graphics::GpuCompiledBarrier* const preBarriers = compiledGraph.taskPrologueBarriers(pre);
    const Graphics::GpuCompiledBarrier* const spanBuildBarriers = compiledGraph.taskPrologueBarriers(spanBuild);
    const Graphics::GpuCompiledBarrier* const combineBarriers = compiledGraph.taskPrologueBarriers(combine);
    const Graphics::GpuCompiledBarrier* const clearBarriers = compiledGraph.taskPrologueBarriers(clear);
    const Graphics::GpuCompiledBarrier* const occupancyBarriers = compiledGraph.taskPrologueBarriers(occupancy);
    ASSERT_NE(spanBuildSeeds, nullptr);
    ASSERT_NE(combineSeeds, nullptr);
    ASSERT_NE(occupancySeeds, nullptr);
    ASSERT_NE(preBarriers, nullptr);
    ASSERT_NE(spanBuildBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(clearBarriers, nullptr);
    ASSERT_NE(occupancyBarriers, nullptr);
    const auto hasTextureTransition = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::Common
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasTextureTransition(
        preBarriers,
        compiledPre->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    const auto hasStateSeed = [](
        const Graphics::GpuPacketStateSeed* const seeds,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range,
        const Graphics::GpuSubmissionPacketId sourcePacket
    ){
        for(u32 seedIndex = 0u; seedIndex < count; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = seeds[seedIndex];
            if(
                seed.resource == resource
                && seed.range.textureSubresources == range
                && seed.sourcePacket == sourcePacket
            )
                return true;
        }
        return false;
    };
    const auto hasUav = [](
        const Graphics::GpuCompiledBarrier* const barriers,
        const u32 count,
        const Graphics::GpuGraphResourceId resource,
        const Graphics::TextureSubresourceSet& range
    ){
        for(u32 barrierIndex = 0u; barrierIndex < count; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateSeed(
        spanBuildSeeds,
        compiledSpanBuild->prologueStateSeedCount,
        receiverEventData,
        receiverEventRange,
        prePacket
    ));
    EXPECT_TRUE(hasUav(spanBuildBarriers, compiledSpanBuild->prologueBarrierCount, receiverEventData, receiverEventRange));
    EXPECT_TRUE(hasUav(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverEventCount,
        receiverEventCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanData,
        receiverSpanRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        spanBuildBarriers,
        compiledSpanBuild->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasStateSeed(
        spanBuildSeeds,
        compiledSpanBuild->prologueStateSeedCount,
        receiverEventCount,
        receiverEventCountRange,
        prePacket
    ));
    EXPECT_TRUE(hasStateSeed(combineSeeds, compiledCombine->prologueStateSeedCount, capBackNormal, peelRange, prePacket));
    EXPECT_TRUE(hasStateSeed(combineSeeds, compiledCombine->prologueStateSeedCount, intervalDepth, peelRange, prePacket));
    EXPECT_TRUE(hasStateSeed(combineSeeds, compiledCombine->prologueStateSeedCount, intervalId, peelRange, prePacket));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        receiverSpanData,
        receiverSpanRange,
        spanBuildPacket
    ));
    EXPECT_TRUE(hasStateSeed(
        combineSeeds,
        compiledCombine->prologueStateSeedCount,
        receiverSpanCount,
        receiverSpanCountRange,
        spanBuildPacket
    ));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, capBackNormal, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalDepth, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, intervalId, peelRange));
    EXPECT_TRUE(hasUav(combineBarriers, compiledCombine->prologueBarrierCount, receiverSpanData, receiverSpanRange));
    EXPECT_TRUE(hasUav(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        receiverSpanCount,
        receiverSpanCountRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalDepth,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalData,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasTextureTransition(
        combineBarriers,
        compiledCombine->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalDepth,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalCapNormal,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalData,
        removedIntervalRange,
        combinePacket
    ));
    EXPECT_TRUE(hasStateSeed(
        occupancySeeds,
        compiledOccupancy->prologueStateSeedCount,
        removedIntervalCount,
        removedIntervalCountRange,
        combinePacket
    ));
    bool coverageSeededByClear = false;
    for(u32 seedIndex = 0u; seedIndex < compiledOccupancy->prologueStateSeedCount; ++seedIndex){
        const Graphics::GpuPacketStateSeed& seed = occupancySeeds[seedIndex];
        coverageSeededByClear = coverageSeededByClear || (seed.resource == coverage && seed.sourcePacket == clearPacket);
    }
    EXPECT_TRUE(coverageSeededByClear);
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCapNormal,
        removedIntervalRange
    ));
    EXPECT_TRUE(hasUav(occupancyBarriers, compiledOccupancy->prologueBarrierCount, removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasUav(
        occupancyBarriers,
        compiledOccupancy->prologueBarrierCount,
        removedIntervalCount,
        removedIntervalCountRange
    ));
    ASSERT_EQ(clearBarriers[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(clearBarriers[0u].resource, coverage);
    EXPECT_EQ(clearBarriers[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(clearBarriers[0u].after, Graphics::ResourceStates::CopyDest);
    bool coverageTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOccupancy->prologueBarrierCount; ++barrierIndex){
        const Graphics::GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
        if(
            barrier.type == Graphics::GpuCompiledBarrierType::BufferTransition
            && barrier.resource == coverage
            && barrier.before == Graphics::ResourceStates::CopyDest
            && barrier.after == Graphics::ResourceStates::UnorderedAccess
        )
            coverageTransition = true;
    }
    EXPECT_TRUE(coverageTransition);
}


// Extinction can run after the dedicated-Compute depth-warp packet. Its prepared CSG material draw still samples
// the interval producer's four StorageImage aliases, so retain their UAV state seed and ordering edge across that
// intervening packet instead of depending on an extinction-local native state bridge.
TEST(GpuTaskGraph, PlansAvboitCsgIntervalProducerToExtinctionSampleAcrossAsyncGap){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_depth"),
        "AVBOIT Extinction Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_cap_normal"),
        "AVBOIT Extinction Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_data"),
        "AVBOIT Extinction Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_extinction_removed_interval_count"),
        "AVBOIT Extinction Removed Interval Count"
    );
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse intervalUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse extinctionUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
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
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Graphics::GpuTaskDesc intervalDesc;
    intervalDesc
        .setIdentity(Name("tests/task_graph/avboit_extinction_intervals"))
        .setMarkerLabel("Transparent CSG Intervals")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(intervalUses, LengthOf(intervalUses))
    ;
    const Graphics::GpuTaskId intervals = graph.addTask(intervalDesc);
    ASSERT_TRUE(intervals.valid());

    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/avboit_extinction_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&intervals, 1u)
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/avboit_extinction_csg_sample"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&depthWarp, 1u)
        .setResourceUses(extinctionUses, LengthOf(extinctionUses))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        extinction,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledGraph.packetCount(), 3u);
    const Graphics::GpuSubmissionPacketId intervalsPacket = compiledGraph.packetForTask(intervals);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledGraph.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledGraph.packetForTask(extinction);
    ASSERT_TRUE(intervalsPacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    EXPECT_NE(intervalsPacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(intervalsPacket, extinctionPacket);
    const Graphics::GpuSubmissionPacket& extinctionPacketPlan = compiledGraph.packet(extinctionPacket);
    ASSERT_EQ(extinctionPacketPlan.dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const extinctionDependencies = compiledGraph.packetDependencies(extinctionPacket);
    ASSERT_NE(extinctionDependencies, nullptr);
    bool waitsForIntervals = false;
    bool waitsForDepthWarp = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < extinctionPacketPlan.dependencyCount; ++dependencyIndex){
        waitsForIntervals = waitsForIntervals || extinctionDependencies[dependencyIndex].producer == intervalsPacket;
        waitsForDepthWarp = waitsForDepthWarp || extinctionDependencies[dependencyIndex].producer == depthWarpPacket;
    }
    EXPECT_TRUE(waitsForIntervals);
    EXPECT_TRUE(waitsForDepthWarp);

    const Graphics::GpuCompiledTask* const compiledIntervals = compiledGraph.findTask(intervals);
    const Graphics::GpuCompiledTask* const compiledExtinction = compiledGraph.findTask(extinction);
    ASSERT_NE(compiledIntervals, nullptr);
    ASSERT_NE(compiledExtinction, nullptr);
    ASSERT_EQ(compiledIntervals->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledIntervals->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledExtinction->prologueStateSeedCount, 4u);
    ASSERT_EQ(compiledExtinction->prologueBarrierCount, 4u);
    const Graphics::GpuPacketStateSeed* const extinctionSeeds = compiledGraph.taskPrologueStateSeeds(extinction);
    const Graphics::GpuCompiledBarrier* const extinctionBarriers = compiledGraph.taskPrologueBarriers(extinction);
    ASSERT_NE(extinctionSeeds, nullptr);
    ASSERT_NE(extinctionBarriers, nullptr);
    const auto hasExtinctionStateSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 seedIndex = 0u; seedIndex < compiledExtinction->prologueStateSeedCount; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = extinctionSeeds[seedIndex];
            if(seed.resource == resource && seed.sourcePacket == intervalsPacket)
                return true;
        }
        return false;
    };
    const auto hasExtinctionUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledExtinction->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = extinctionBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalDepth));
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalCapNormal));
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalData));
    EXPECT_TRUE(hasExtinctionStateSeed(removedIntervalCount));
    EXPECT_TRUE(hasExtinctionUav(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasExtinctionUav(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasExtinctionUav(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasExtinctionUav(removedIntervalCount, removedIntervalCountRange));
}


// Accumulation runs after both Graphics extinction and Compute integration on the split route. It can still sample
// the interval producer's StorageImage aliases without a renderer-local state call only if the compiler retains the
// producer state seed and direct packet wait across the intervening AVBOIT work.
TEST(GpuTaskGraph, PlansAvboitCsgIntervalProducerToAccumulationSampleAcrossIntegrationGap){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId removedIntervalDepth = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_depth"),
        "AVBOIT Accumulation Removed Interval Depth"
    );
    const Graphics::GpuGraphResourceId removedIntervalCapNormal = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_cap_normal"),
        "AVBOIT Accumulation Removed Interval Cap Normal"
    );
    const Graphics::GpuGraphResourceId removedIntervalData = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_data"),
        "AVBOIT Accumulation Removed Interval Data"
    );
    const Graphics::GpuGraphResourceId removedIntervalCount = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_accumulation_removed_interval_count"),
        "AVBOIT Accumulation Removed Interval Count"
    );
    ASSERT_TRUE(removedIntervalDepth.valid());
    ASSERT_TRUE(removedIntervalCapNormal.valid());
    ASSERT_TRUE(removedIntervalData.valid());
    ASSERT_TRUE(removedIntervalCount.valid());

    const Graphics::TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const Graphics::TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const Graphics::GpuTaskResourceUse intervalUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalDepth,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCapNormal,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalData,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = removedIntervalCount,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
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
    Graphics::GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = Graphics::GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    Graphics::GpuTaskDesc intervalDesc;
    intervalDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_intervals"))
        .setMarkerLabel("Transparent CSG Intervals")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setResourceUses(intervalUses, LengthOf(intervalUses))
    ;
    const Graphics::GpuTaskId intervals = graph.addTask(intervalDesc);
    ASSERT_TRUE(intervals.valid());

    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&intervals, 1u)
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&depthWarp, 1u)
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&extinction, 1u)
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/avboit_accumulation_csg_sample"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(boundaryScheduling)
        .setDependencies(&integration, 1u)
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, frontierOptions));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalDepth,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalCapNormal,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalData,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        intervals,
        accumulation,
        removedIntervalCount,
        Graphics::GpuTaskHazardType::ReadAfterWrite
    ));

    ASSERT_EQ(compiledGraph.packetCount(), 5u);
    const Graphics::GpuSubmissionPacketId intervalsPacket = compiledGraph.packetForTask(intervals);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledGraph.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledGraph.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledGraph.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledGraph.packetForTask(accumulation);
    ASSERT_TRUE(intervalsPacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    EXPECT_NE(intervalsPacket, depthWarpPacket);
    EXPECT_NE(depthWarpPacket, extinctionPacket);
    EXPECT_NE(extinctionPacket, integrationPacket);
    EXPECT_NE(integrationPacket, accumulationPacket);
    const Graphics::GpuSubmissionPacket& accumulationPacketPlan = compiledGraph.packet(accumulationPacket);
    ASSERT_EQ(accumulationPacketPlan.dependencyCount, 2u);
    const Graphics::GpuPacketDependency* const accumulationDependencies = compiledGraph.packetDependencies(accumulationPacket);
    ASSERT_NE(accumulationDependencies, nullptr);
    bool waitsForIntervals = false;
    bool waitsForIntegration = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < accumulationPacketPlan.dependencyCount; ++dependencyIndex){
        waitsForIntervals = waitsForIntervals || accumulationDependencies[dependencyIndex].producer == intervalsPacket;
        waitsForIntegration = waitsForIntegration || accumulationDependencies[dependencyIndex].producer == integrationPacket;
    }
    EXPECT_TRUE(waitsForIntervals);
    EXPECT_TRUE(waitsForIntegration);

    const Graphics::GpuCompiledTask* const compiledIntervals = compiledGraph.findTask(intervals);
    const Graphics::GpuCompiledTask* const compiledAccumulation = compiledGraph.findTask(accumulation);
    ASSERT_NE(compiledIntervals, nullptr);
    ASSERT_NE(compiledAccumulation, nullptr);
    ASSERT_EQ(compiledIntervals->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledIntervals->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledAccumulation->prologueStateSeedCount, 4u);
    ASSERT_EQ(compiledAccumulation->prologueBarrierCount, 4u);
    const Graphics::GpuPacketStateSeed* const accumulationSeeds = compiledGraph.taskPrologueStateSeeds(accumulation);
    const Graphics::GpuCompiledBarrier* const accumulationBarriers = compiledGraph.taskPrologueBarriers(accumulation);
    ASSERT_NE(accumulationSeeds, nullptr);
    ASSERT_NE(accumulationBarriers, nullptr);
    const auto hasAccumulationStateSeed = [&](const Graphics::GpuGraphResourceId resource){
        for(u32 seedIndex = 0u; seedIndex < compiledAccumulation->prologueStateSeedCount; ++seedIndex){
            const Graphics::GpuPacketStateSeed& seed = accumulationSeeds[seedIndex];
            if(seed.resource == resource && seed.sourcePacket == intervalsPacket)
                return true;
        }
        return false;
    };
    const auto hasAccumulationUav = [&](const Graphics::GpuGraphResourceId resource, const Graphics::TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < compiledAccumulation->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = accumulationBarriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::UnorderedAccess
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalDepth));
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalCapNormal));
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalData));
    EXPECT_TRUE(hasAccumulationStateSeed(removedIntervalCount));
    EXPECT_TRUE(hasAccumulationUav(removedIntervalDepth, removedIntervalRange));
    EXPECT_TRUE(hasAccumulationUav(removedIntervalCapNormal, removedIntervalRange));
    EXPECT_TRUE(hasAccumulationUav(removedIntervalData, removedIntervalRange));
    EXPECT_TRUE(hasAccumulationUav(removedIntervalCount, removedIntervalCountRange));
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

TEST(GpuTaskGraph, CompilesEligibleTransferPreferenceToDedicatedTransferQueue){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/dedicated_transfer"))
        .setMarkerLabel("Dedicated Transfer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
    ;
    const Graphics::GpuTaskId copyTask = graph.addTask<PacketLifecycleTask>(
        desc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(copyTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Transfer);
    EXPECT_TRUE(assignment->dedicated);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedTransfer);

    const Graphics::GpuSubmissionPacketId packet = compiledGraph.packetForTask(copyTask);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuPhysicalQueueInfo* const packetQueue = compiledGraph.queueInfo(compiledGraph.packet(packet).queue);
    ASSERT_NE(packetQueue, nullptr);
    EXPECT_EQ(packetQueue->queueClass, Graphics::CommandQueue::Transfer);
    EXPECT_EQ(packetQueue->capabilities, Graphics::GpuQueueCapability::Transfer);
}


TEST(GpuTaskGraph, RetainsTinyTransferTasksOnGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/tiny_transfer"))
        .setMarkerLabel("Tiny Transfer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
    ;
    const Graphics::GpuTaskId copyTask = graph.addTask<PacketLifecycleTask>(
        desc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(copyTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::Fallback);
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
