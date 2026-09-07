// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// These compact graph tasks model skinning's descriptor-visible compute endpoint. They deliberately perform no
// synthetic native submission: their packet-local barriers publish the required compute states, and the task's
// accepted callback observes the same packet token as its uploads/copies.
struct SkinningGraphSelectorConsumerTask{
    struct Payload{
        GpuGraphResourceId selector;
        GpuGraphResourceId staticInput;
        bool* observedConstantBuffer = nullptr;
        bool* observedStaticInputShaderResource = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        Buffer* const selector = context.declarations.bufferForResource(payload.selector);
        Buffer* const staticInput = context.declarations.bufferForResource(payload.staticInput);
        if(!selector || !staticInput)
            return false;

        const bool observedConstantBuffer = commandList.getBufferState(selector) == ResourceStates::ConstantBuffer;
        const bool observedStaticInputShaderResource = commandList.getBufferState(staticInput) == ResourceStates::ShaderResource;
        if(payload.observedConstantBuffer)
            *payload.observedConstantBuffer = observedConstantBuffer;
        if(payload.observedStaticInputShaderResource)
            *payload.observedStaticInputShaderResource = observedStaticInputShaderResource;
        return observedConstantBuffer && observedStaticInputShaderResource;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};


struct SkinningGraphRestStreamConsumerTask{
    struct Payload{
        GpuGraphResourceId position;
        GpuGraphResourceId normal;
        GpuGraphResourceId tangent;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        Buffer* const position = context.declarations.bufferForResource(payload.position);
        Buffer* const normal = context.declarations.bufferForResource(payload.normal);
        Buffer* const tangent = context.declarations.bufferForResource(payload.tangent);
        if(!position || !normal || !tangent)
            return false;

        return
            commandList.getBufferState(position) == ResourceStates::ShaderResource
            && commandList.getBufferState(normal) == ResourceStates::ShaderResource
            && commandList.getBufferState(tangent) == ResourceStates::ShaderResource
        ;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};


// Skinning publishes its immutable bindless selector and consumes it with a static ShaderResource input from a
// graph-owned compute endpoint in one primary-Graphics packet. The callback is getter-only: packet barriers must
// establish both Common -> ConstantBuffer and Common -> ShaderResource before native skinning-style recording.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSkinningSelectorMergesWithGraphCompute){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_SelectorWords[] = {
        0x14a6f3c9u,
        0x09b5d217u,
        0x6c1e8f42u,
        0xcafebabeu,
        0x7f4a1c32u,
        0x10293847u,
        0x55aa33ccu,
        0x8badf00du,
        0x0ddba11au,
        0x4e6f7788u,
        0x9e3779b9u,
        0xfeedfaceu,
        0x243f6a88u,
        0x85a308d3u,
        0x13198a2eu,
        0x03707344u,
    };
    auto selectorBuffer = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/skinning_bindless_selector"))
            .setByteSize(sizeof(s_SelectorWords))
            .setIsConstantBuffer(true)
            .enableAutomaticStateTracking(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(selectorBuffer.get(), nullptr);
    auto staticInputBuffer = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/skinning_static_input"))
            .setByteSize(sizeof(s_SelectorWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(staticInputBuffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const BufferDesc& selectorDesc = selectorBuffer->getDescription();
    const GpuGraphResourceId selectorResource = graph.importBuffer(
        selectorBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(selectorDesc.debugName)
            .setMarkerLabel("Skinning Bindless Selector")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(selectorDesc.initialState)
            .setQueueSharing(selectorDesc.queueSharing)
    );
    const BufferDesc& staticInputDesc = staticInputBuffer->getDescription();
    const GpuGraphResourceId staticInputResource = graph.importBuffer(
        staticInputBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(staticInputDesc.debugName)
            .setMarkerLabel("Skinning Static Input")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(staticInputDesc.initialState)
            .setQueueSharing(staticInputDesc.queueSharing)
    );
    const GpuUploadBlobId selectorBlob = graph.copyUploadData(
        s_SelectorWords,
        sizeof(s_SelectorWords),
        alignof(u32)
    );
    ASSERT_TRUE(selectorResource.valid());
    ASSERT_TRUE(staticInputResource.valid());
    ASSERT_TRUE(selectorBlob.valid());

    const GpuQueueRequest graphicsUploadQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint selectorScheduling;
    selectorScheduling.cost = GpuTaskCostHint::Tiny;
    selectorScheduling.overlapPreferred = false;
    selectorScheduling.avoidQueueCrossing = true;
    selectorScheduling.forceSubmissionBoundary = false;
    selectorScheduling.allowPacketMerge = true;
    selectorScheduling.frontierScoredMergeDomain = Name("tests/descriptor_buffer/skinning_bindless_selector");
    QueueSubmissionToken selectorAcceptedToken;
    const GpuTaskId selectorTask = graph.addUploadBufferTask(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_bindless_selector_upload"))
            .setMarkerLabel("Skinning Bindless Slots Upload")
            .setQueue(graphicsUploadQueue)
            .setScheduling(selectorScheduling),
        GpuUploadBufferTaskDesc{
            .source = selectorBlob,
            .destination = selectorResource,
            // The graph-owned compute task promotes the selector to its descriptor-visible ConstantBuffer state,
            // while automatic tracking restores Common as the cross-frame handoff state.
            .finalState = ResourceStates::Common,
            .acceptedToken = &selectorAcceptedToken,
        }
    );
    ASSERT_TRUE(selectorTask.valid());

    const GpuTaskResourceUse selectorConsumerUses[] = {
        GpuTaskResourceUse{
            .resource = selectorResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = staticInputResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool selectorConsumerObservedConstantBuffer = false;
    bool selectorConsumerObservedStaticInputShaderResource = false;
    QueueSubmissionToken selectorConsumerAcceptedToken;
    const GpuTaskId selectorConsumerTask = graph.addTask<SkinningGraphSelectorConsumerTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_bindless_selector_consume"))
            .setMarkerLabel("Skinning Graph Compute Consume")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Compute,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(selectorScheduling)
            .setDependencies(&selectorTask, 1u)
            .setResourceUses(selectorConsumerUses, LengthOf(selectorConsumerUses)),
        SkinningGraphSelectorConsumerTask::Payload{
            .selector = selectorResource,
            .staticInput = staticInputResource,
            .observedConstantBuffer = &selectorConsumerObservedConstantBuffer,
            .observedStaticInputShaderResource = &selectorConsumerObservedStaticInputShaderResource,
            .acceptedToken = &selectorConsumerAcceptedToken,
        }
    );
    ASSERT_TRUE(selectorConsumerTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/skinning_selector_scratch"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierScored;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    const GpuTaskQueueAssignment* const selectorAssignment = assignments.find(selectorTask);
    ASSERT_NE(selectorAssignment, nullptr);
    EXPECT_EQ(selectorAssignment->queue, primaryGraphicsQueue);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId selectorPacket = views.compiled.packetForTask(selectorTask);
    const GpuSubmissionPacketId selectorConsumerPacket = views.compiled.packetForTask(selectorConsumerTask);
    ASSERT_TRUE(selectorPacket.valid());
    ASSERT_TRUE(selectorConsumerPacket.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(selectorConsumerPacket, selectorPacket);
    EXPECT_EQ(views.compiled.packet(selectorPacket).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(selectorPacket).plan->taskCount, 2u);
    EXPECT_EQ(
        views.compiled.packetizationDecisionForTask(selectorConsumerTask),
        GpuTaskPacketizationDecision::MergedFrontierScored
    );

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_TRUE(selectorConsumerObservedConstantBuffer);
    EXPECT_TRUE(selectorConsumerObservedStaticInputShaderResource);
    CommandListResourceStateHandoff graphFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        selectorTask,
        graphFinalStateStorage
    ));
    const CommandListResourceStateHandoff* const graphFinalState = &graphFinalStateStorage;
    auto graphStateProbe = device.createCommandList();
    ASSERT_NE(graphStateProbe.get(), nullptr);
    graphStateProbe->open(graphFinalState);
    EXPECT_EQ(graphStateProbe->getBufferState(selectorBuffer.get()), ResourceStates::Common);
    EXPECT_EQ(graphStateProbe->getBufferState(staticInputBuffer.get()), ResourceStates::ShaderResource);
    graphStateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken graphToken = transaction.packetToken(selectorPacket);
    ASSERT_TRUE(graphToken.valid());
    ASSERT_TRUE(selectorAcceptedToken.valid());
    EXPECT_EQ(selectorAcceptedToken.queue, graphToken.queue);
    EXPECT_EQ(selectorAcceptedToken.value, graphToken.value);
    ASSERT_TRUE(selectorConsumerAcceptedToken.valid());
    EXPECT_EQ(selectorConsumerAcceptedToken.queue, graphToken.queue);
    EXPECT_EQ(selectorConsumerAcceptedToken.value, graphToken.value);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const uploadedWords = static_cast<const u32*>(device.mapBuffer(selectorBuffer.get(), CpuAccessMode::Read));
    ASSERT_NE(uploadedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SelectorWords); ++wordIndex)
        EXPECT_EQ(uploadedWords[wordIndex], s_SelectorWords[wordIndex]);
    device.unmapBuffer(selectorBuffer.get());
}


// Skinning's no-active-pose path copies rest position, normal, and tangent streams into their skinned counterparts,
// then a graph-owned compute endpoint promotes every output before renderer consumption.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSkinningRestCopyMergesWithGraphCompute){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_PaletteWords[] = {
        0x6a5d39c1u,
        0x1147beefu,
        0x91c0ffeeu,
        0x3a72d409u,
    };
    static constexpr u32 s_RestPositionWords[] = {
        0x13c0ffeeu,
        0x4a7b12d3u,
        0x9e3779b9u,
        0xfeedfaceu,
    };
    static constexpr u32 s_RestNormalWords[] = {
        0x0ddba11au,
        0x51a7e001u,
        0x8badf00du,
        0x7f4a1c32u,
    };
    static constexpr u32 s_RestTangentWords[] = {
        0x42f0a7c3u,
        0x0decaf42u,
        0x7a5e19d4u,
        0xc001d00du,
    };

    const auto createRestBuffer = [&device](const Name& debugName){
        return device.createBuffer(
            BufferDesc()
                .setDebugName(debugName)
                .setByteSize(sizeof(s_RestPositionWords))
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
                .setCpuAccess(CpuAccessMode::Write)
        );
    };
    const auto createSkinnedBuffer = [&device](const Name& debugName){
        return device.createBuffer(
            BufferDesc()
                .setDebugName(debugName)
                .setByteSize(sizeof(s_RestPositionWords))
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
                .setCpuAccess(CpuAccessMode::Read)
        );
    };
    auto restPosition = createRestBuffer(Name("tests/descriptor_buffer/skinning_rest_position"));
    auto restNormal = createRestBuffer(Name("tests/descriptor_buffer/skinning_rest_normal"));
    auto restTangent = createRestBuffer(Name("tests/descriptor_buffer/skinning_rest_tangent"));
    auto skinnedPosition = createSkinnedBuffer(Name("tests/descriptor_buffer/skinning_skinned_position"));
    auto skinnedNormal = createSkinnedBuffer(Name("tests/descriptor_buffer/skinning_skinned_normal"));
    auto skinnedTangent = createSkinnedBuffer(Name("tests/descriptor_buffer/skinning_skinned_tangent"));
    auto palette = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/skinning_palette"))
            .setByteSize(sizeof(s_PaletteWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(restPosition.get(), nullptr);
    ASSERT_NE(restNormal.get(), nullptr);
    ASSERT_NE(restTangent.get(), nullptr);
    ASSERT_NE(skinnedPosition.get(), nullptr);
    ASSERT_NE(skinnedNormal.get(), nullptr);
    ASSERT_NE(skinnedTangent.get(), nullptr);
    ASSERT_NE(palette.get(), nullptr);

    const auto writeWords = [&device](Buffer* const buffer, const u32* const words){
        u32* const mappedWords = static_cast<u32*>(device.mapBuffer(buffer, CpuAccessMode::Write));
        ASSERT_NE(mappedWords, nullptr);
        for(usize wordIndex = 0u; wordIndex < LengthOf(s_RestPositionWords); ++wordIndex)
            mappedWords[wordIndex] = words[wordIndex];
        device.unmapBuffer(buffer);
    };
    writeWords(restPosition.get(), s_RestPositionWords);
    writeWords(restNormal.get(), s_RestNormalWords);
    writeWords(restTangent.get(), s_RestTangentWords);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](
        const BufferHandle& buffer,
        const Name& identity,
        const AStringView markerLabel
    ){
        const BufferDesc& description = buffer->getDescription();
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(description.initialState)
                .setQueueSharing(description.queueSharing)
        );
    };
    const GpuGraphResourceId paletteResource = importBuffer(
        palette,
        Name("tests/descriptor_buffer/skinning_palette"),
        "Skinning Palette"
    );
    const GpuGraphResourceId restPositionResource = importBuffer(
        restPosition,
        Name("tests/descriptor_buffer/skinning_rest_position"),
        "Skinning Rest Position"
    );
    const GpuGraphResourceId restNormalResource = importBuffer(
        restNormal,
        Name("tests/descriptor_buffer/skinning_rest_normal"),
        "Skinning Rest Normal"
    );
    const GpuGraphResourceId restTangentResource = importBuffer(
        restTangent,
        Name("tests/descriptor_buffer/skinning_rest_tangent"),
        "Skinning Rest Tangent"
    );
    const GpuGraphResourceId skinnedPositionResource = importBuffer(
        skinnedPosition,
        Name("tests/descriptor_buffer/skinning_skinned_position"),
        "Skinning Skinned Position"
    );
    const GpuGraphResourceId skinnedNormalResource = importBuffer(
        skinnedNormal,
        Name("tests/descriptor_buffer/skinning_skinned_normal"),
        "Skinning Skinned Normal"
    );
    const GpuGraphResourceId skinnedTangentResource = importBuffer(
        skinnedTangent,
        Name("tests/descriptor_buffer/skinning_skinned_tangent"),
        "Skinning Skinned Tangent"
    );
    ASSERT_TRUE(paletteResource.valid());
    ASSERT_TRUE(restPositionResource.valid());
    ASSERT_TRUE(restNormalResource.valid());
    ASSERT_TRUE(restTangentResource.valid());
    ASSERT_TRUE(skinnedPositionResource.valid());
    ASSERT_TRUE(skinnedNormalResource.valid());
    ASSERT_TRUE(skinnedTangentResource.valid());
    const GpuUploadBlobId paletteBlob = graph.copyUploadData(
        s_PaletteWords,
        sizeof(s_PaletteWords),
        alignof(u32)
    );
    ASSERT_TRUE(paletteBlob.valid());

    const GpuQueueRequest graphicsUploadQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint paletteScheduling;
    paletteScheduling.cost = GpuTaskCostHint::Tiny;
    paletteScheduling.overlapPreferred = false;
    paletteScheduling.avoidQueueCrossing = true;
    paletteScheduling.forceSubmissionBoundary = false;
    paletteScheduling.allowPacketMerge = true;
    paletteScheduling.frontierScoredMergeDomain = Name("tests/descriptor_buffer/skinning_rest_copy");
    GpuTaskDesc paletteDesc;
    paletteDesc
        .setIdentity(Name("tests/descriptor_buffer/skinning_palette_upload"))
        .setMarkerLabel("Skinning Palette Upload")
        .setQueue(graphicsUploadQueue)
        .setScheduling(paletteScheduling)
    ;
    const GpuTaskId paletteTask = graph.addUploadBufferTask(
        paletteDesc,
        GpuUploadBufferTaskDesc{
            .source = paletteBlob,
            .destination = paletteResource,
            .finalState = ResourceStates::ShaderResource,
        }
    );
    ASSERT_TRUE(paletteTask.valid());

    const GpuTaskDesc restCopyDesc = GpuTaskDesc{}
        .setIdentity(Name("tests/descriptor_buffer/skinning_rest_to_skinned_copy"))
        .setMarkerLabel("Skinning Rest-to-Skinned Copy")
        .setQueue(graphicsUploadQueue)
        .setScheduling(paletteScheduling)
        .setDependencies(&paletteTask, 1u)
    ;
    const GpuCopyBufferTaskRegion copyRegions[] = {
        GpuCopyBufferTaskRegion{
            .source = restPositionResource,
            .destination = skinnedPositionResource,
            .dataSizeBytes = sizeof(s_RestPositionWords),
        },
        GpuCopyBufferTaskRegion{
            .source = restNormalResource,
            .destination = skinnedNormalResource,
            .dataSizeBytes = sizeof(s_RestNormalWords),
        },
        GpuCopyBufferTaskRegion{
            .source = restTangentResource,
            .destination = skinnedTangentResource,
            .dataSizeBytes = sizeof(s_RestTangentWords),
        },
    };
    QueueSubmissionToken copyAcceptedToken;
    const GpuTaskId restCopyTask = graph.addCopyBufferTask(
        restCopyDesc,
        GpuCopyBufferTaskDesc{
            .regions = copyRegions,
            .regionCount = LengthOf(copyRegions),
            .acceptedToken = &copyAcceptedToken,
        }
    );
    ASSERT_TRUE(restCopyTask.valid());
    {
        const GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_EQ(declarations.taskAt(restCopyTask.index).resourceUseCount, 6u);
    }

    const GpuTaskResourceUse restStreamConsumerUses[] = {
        GpuTaskResourceUse{
            .resource = skinnedPositionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = skinnedNormalResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = skinnedTangentResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    QueueSubmissionToken restStreamConsumerAcceptedToken;
    const GpuTaskId restStreamConsumerTask = graph.addTask<SkinningGraphRestStreamConsumerTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_rest_stream_consume"))
            .setMarkerLabel("Skinning Graph Rest Stream Consume")
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Compute,
                GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setScheduling(paletteScheduling)
            .setDependencies(&restCopyTask, 1u)
            .setResourceUses(restStreamConsumerUses, LengthOf(restStreamConsumerUses)),
        SkinningGraphRestStreamConsumerTask::Payload{
            .position = skinnedPositionResource,
            .normal = skinnedNormalResource,
            .tangent = skinnedTangentResource,
            .acceptedToken = &restStreamConsumerAcceptedToken,
        }
    );
    ASSERT_TRUE(restStreamConsumerTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/skinning_rest_copy_scratch"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierScored;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    const GpuTaskQueueAssignment* const paletteAssignment = assignments.find(paletteTask);
    const GpuTaskQueueAssignment* const restCopyAssignment = assignments.find(restCopyTask);
    const GpuTaskQueueAssignment* const restStreamConsumerAssignment = assignments.find(restStreamConsumerTask);
    ASSERT_NE(paletteAssignment, nullptr);
    ASSERT_NE(restCopyAssignment, nullptr);
    ASSERT_NE(restStreamConsumerAssignment, nullptr);
    EXPECT_EQ(paletteAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(restCopyAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(restStreamConsumerAssignment->queue, primaryGraphicsQueue);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId palettePacket = views.compiled.packetForTask(paletteTask);
    const GpuSubmissionPacketId restCopyPacket = views.compiled.packetForTask(restCopyTask);
    const GpuSubmissionPacketId restStreamConsumerPacket = views.compiled.packetForTask(restStreamConsumerTask);
    ASSERT_TRUE(palettePacket.valid());
    ASSERT_TRUE(restCopyPacket.valid());
    ASSERT_TRUE(restStreamConsumerPacket.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(palettePacket, restCopyPacket);
    EXPECT_EQ(restStreamConsumerPacket, restCopyPacket);
    EXPECT_EQ(views.compiled.packet(restCopyPacket).plan->queue, primaryGraphicsQueue);
    EXPECT_EQ(views.compiled.packet(restCopyPacket).plan->taskCount, 3u);
    EXPECT_EQ(
        views.compiled.packetizationDecisionForTask(restCopyTask),
        GpuTaskPacketizationDecision::MergedFrontierScored
    );
    EXPECT_EQ(
        views.compiled.packetizationDecisionForTask(restStreamConsumerTask),
        GpuTaskPacketizationDecision::MergedFrontierScored
    );

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    CommandListResourceStateHandoff graphFinalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(
        compiledGraph,
        views.compiled,
        restCopyTask,
        graphFinalStateStorage
    ));
    const CommandListResourceStateHandoff* const graphFinalState = &graphFinalStateStorage;
    auto graphStateProbe = device.createCommandList();
    ASSERT_NE(graphStateProbe.get(), nullptr);
    graphStateProbe->open(graphFinalState);
    EXPECT_EQ(graphStateProbe->getBufferState(palette.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(graphStateProbe->getBufferState(restPosition.get()), ResourceStates::CopySource);
    EXPECT_EQ(graphStateProbe->getBufferState(restNormal.get()), ResourceStates::CopySource);
    EXPECT_EQ(graphStateProbe->getBufferState(restTangent.get()), ResourceStates::CopySource);
    EXPECT_EQ(graphStateProbe->getBufferState(skinnedPosition.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(graphStateProbe->getBufferState(skinnedNormal.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(graphStateProbe->getBufferState(skinnedTangent.get()), ResourceStates::ShaderResource);
    graphStateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken graphToken = transaction.packetToken(restCopyPacket);
    ASSERT_TRUE(graphToken.valid());
    ASSERT_TRUE(copyAcceptedToken.valid());
    EXPECT_EQ(copyAcceptedToken.queue, graphToken.queue);
    EXPECT_EQ(copyAcceptedToken.value, graphToken.value);
    ASSERT_TRUE(restStreamConsumerAcceptedToken.valid());
    EXPECT_EQ(restStreamConsumerAcceptedToken.queue, graphToken.queue);
    EXPECT_EQ(restStreamConsumerAcceptedToken.value, graphToken.value);
    ASSERT_TRUE(device.waitForIdle());

    const auto expectCopiedWords = [&device](Buffer* const buffer, const u32* const expectedWords){
        const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(buffer, CpuAccessMode::Read));
        ASSERT_NE(copiedWords, nullptr);
        for(usize wordIndex = 0u; wordIndex < LengthOf(s_RestPositionWords); ++wordIndex)
            EXPECT_EQ(copiedWords[wordIndex], expectedWords[wordIndex]);
        device.unmapBuffer(buffer);
    };
    expectCopiedWords(skinnedPosition.get(), s_RestPositionWords);
    expectCopiedWords(skinnedNormal.get(), s_RestNormalWords);
    expectCopiedWords(skinnedTangent.get(), s_RestTangentWords);
}


// Bounds and normal-repack outputs are the tail of the graph-owned skinning dispatch. Their callback emits no
// state transition: the producer's UAV entry and the finalizer's ShaderResource handoff must both be packet-owned.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedSkinningOutputStatesFinalizeInGraph){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto generatedOutput = device.createBuffer(
        BufferDesc()
            .setDebugName(Name("tests/descriptor_buffer/skinning_generated_output"))
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
    );
    ASSERT_NE(generatedOutput.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const BufferDesc& generatedOutputDesc = generatedOutput->getDescription();
    const GpuGraphResourceId generatedOutputResource = graph.importBuffer(
        generatedOutput,
        GpuGraphResourceDesc{}
            .setIdentity(generatedOutputDesc.debugName)
            .setMarkerLabel("Skinning Generated Output")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(generatedOutputDesc.initialState)
            .setQueueSharing(generatedOutputDesc.queueSharing)
    );
    ASSERT_TRUE(generatedOutputResource.valid());

    const GpuQueueRequest graphicsComputeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = GpuTaskCostHint::Small;
    producerScheduling.overlapPreferred = false;
    producerScheduling.avoidQueueCrossing = true;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    producerScheduling.mergeWithPrevious = false;
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = generatedOutputResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool producerObservedUnorderedAccess = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_generated_output_producer"))
            .setMarkerLabel("Skinning Generated Output")
            .setQueue(graphicsComputeQueue)
            .setScheduling(producerScheduling)
            .setResourceUses(producerUses, LengthOf(producerUses)),
        NativePacketPrefixTask::Payload{
            .buffer = generatedOutput.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &producerObservedUnorderedAccess,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskSchedulingHint finalizerScheduling = producerScheduling;
    finalizerScheduling.mergeWithPrevious = true;
    const GpuTaskResourceUse finalizerUses[] = {
        GpuTaskResourceUse{
            .resource = generatedOutputResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool finalizerObservedShaderResource = false;
    QueueSubmissionToken finalizerAcceptedToken;
    const GpuTaskId finalizerTask = graph.addTask<NativePacketPrefixTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/skinning_generated_output_finalize"))
            .setMarkerLabel("Skinning Finalize States")
            .setQueue(graphicsComputeQueue)
            .setScheduling(finalizerScheduling)
            .setDependencies(&producerTask, 1u)
            .setResourceUses(finalizerUses, LengthOf(finalizerUses)),
        NativePacketPrefixTask::Payload{
            .buffer = generatedOutput.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &finalizerObservedShaderResource,
            .acceptedToken = &finalizerAcceptedToken,
        }
    );
    ASSERT_TRUE(finalizerTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/skinning_output_finalize_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskQueueAssignment* const producerAssignment = assignments.find(producerTask);
    const GpuTaskQueueAssignment* const finalizerAssignment = assignments.find(finalizerTask);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(finalizerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queue, primaryGraphicsQueue);
    EXPECT_EQ(finalizerAssignment->queue, primaryGraphicsQueue);
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    const GpuSubmissionPacketId producerPacket = views.compiled.packetForTask(producerTask);
    const GpuSubmissionPacketId finalizerPacket = views.compiled.packetForTask(finalizerTask);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(finalizerPacket.valid());
    EXPECT_EQ(views.compiled.packetCount(), 1u);
    EXPECT_EQ(finalizerPacket, producerPacket);
    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    const GpuCompiledTaskView compiledFinalizer = views.compiled.findTask(finalizerTask);
    ASSERT_TRUE(compiledProducer.valid());
    ASSERT_TRUE(compiledFinalizer.valid());
    ASSERT_EQ(compiledProducer.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledFinalizer.plan->prologueBarrierCount, 1u);
    const GpuCompiledBarrier* const producerBarriers = views.compiled.findTask(producerTask).prologueBarriers;
    const GpuCompiledBarrier* const finalizerBarriers = views.compiled.findTask(finalizerTask).prologueBarriers;
    ASSERT_NE(producerBarriers, nullptr);
    ASSERT_NE(finalizerBarriers, nullptr);
    EXPECT_EQ(producerBarriers[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(producerBarriers[0u].resource, generatedOutputResource);
    EXPECT_EQ(producerBarriers[0u].before, ResourceStates::Common);
    EXPECT_EQ(producerBarriers[0u].after, ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalizerBarriers[0u].type, GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(finalizerBarriers[0u].resource, generatedOutputResource);
    EXPECT_EQ(finalizerBarriers[0u].before, ResourceStates::UnorderedAccess);
    EXPECT_EQ(finalizerBarriers[0u].after, ResourceStates::ShaderResource);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph
    ));
    EXPECT_TRUE(producerObservedUnorderedAccess);
    EXPECT_TRUE(finalizerObservedShaderResource);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, producerTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(generatedOutput.get()), ResourceStates::ShaderResource);
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(producerPacket);
    ASSERT_TRUE(packetToken.valid());
    ASSERT_TRUE(finalizerAcceptedToken.valid());
    EXPECT_EQ(finalizerAcceptedToken.queue, packetToken.queue);
    EXPECT_EQ(finalizerAcceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

