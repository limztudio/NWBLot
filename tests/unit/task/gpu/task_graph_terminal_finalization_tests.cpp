// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_terminal_finalization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, OrdersIndependentTerminalFinalizationDependenciesNearestFirst){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuGraphResourceDesc resourceDesc;
    resourceDesc
        .setIdentity(Name("tests/task_graph/ordered_terminal_finalization_resource"))
        .setMarkerLabel("Ordered Terminal Finalization Resource")
        .setType(Graphics::GpuGraphResourceType::Texture)
        .setInitialState(Graphics::ResourceStates::ShaderResource)
        .setExternalFinalState(Graphics::ResourceStates::CopySource)
        .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    const auto resourceResult = graph.importResource(resourceDesc);
    ASSERT_TRUE(resourceResult);
    const Graphics::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());

    const Graphics::GpuTaskResourceRange range{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const Graphics::GpuTaskResourceUse readerUse{ .resource = resource, .range = range, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, .hasIndependentStateSource = true, };
    Graphics::GpuTaskSchedulingHint readerScheduling;
    readerScheduling.forceSubmissionBoundary = true;
    readerScheduling.allowPacketMerge = false;
    const Graphics::GpuTaskCommandRequirements readerCommands[] = {
        { Graphics::GpuQueueCapability::Graphics },
        { Graphics::GpuQueueCapability::Compute },
        { Graphics::GpuQueueCapability::Graphics },
        { Graphics::GpuQueueCapability::Compute },
    };

    Graphics::GpuTaskId readers[4u] = {};
    const Name readerBaseName("tests/task_graph/ordered_terminal_finalization_reader_");
    char readerIndexBuffer[32u] = {};
    for(usize readerIndex = 0u; readerIndex < LengthOf(readers); ++readerIndex){
        Graphics::GpuTaskDesc readerDesc;
        const auto derivedNameAt653Col25 = DeriveName(readerBaseName, FormatDecimal(readerIndex, readerIndexBuffer));
        ASSERT_TRUE(derivedNameAt653Col25);
        readerDesc
            .setIdentity(*derivedNameAt653Col25)
            .setMarkerLabel("Ordered Terminal Finalization Reader")
            .setScheduling(readerScheduling)
            .setDependencies(readerIndex == 1u ? &readers[0u] : nullptr, readerIndex == 1u ? 1u : 0u)
            .setResourceUses(&readerUse, 1u)
        ;
        const auto gpuAdmissionAt631 = graph.addTask(readerDesc, readerCommands[readerIndex]);
        ASSERT_TRUE(gpuAdmissionAt631);
        readers[readerIndex] = *gpuAdmissionAt631;
        ASSERT_TRUE(readers[readerIndex].valid());
    }

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(operationScratchArena, graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_NE(FindEdge(analysis, readers[0u], readers[1u]), nullptr);
    EXPECT_EQ(FindEdge(analysis, readers[0u], readers[3u]), nullptr);
    EXPECT_EQ(FindEdge(analysis, readers[s_ThirdElementIndex], readers[3u]), nullptr);
    ASSERT_EQ(compiledPlan.packetCount(), LengthOf(readers));

    Graphics::GpuSubmissionPacketId packets[4u] = {};
    for(usize readerIndex = 0u; readerIndex < LengthOf(readers); ++readerIndex){
        packets[readerIndex] = compiledPlan.packetForTask(readers[readerIndex]);
        ASSERT_TRUE(packets[readerIndex].valid());
        EXPECT_EQ(packets[readerIndex].index, readerIndex);
        EXPECT_EQ(compiledPlan.packet(packets[readerIndex]).plan->queue, queues[readerIndex % s_ExpectedDualCount].id);
    }

    ASSERT_EQ(compiledPlan.packet(packets[0u]).plan->dependencyCount, 0u);
    ASSERT_EQ(compiledPlan.packet(packets[1u]).plan->dependencyCount, 1u);
    ASSERT_EQ(compiledPlan.packet(packets[s_ThirdElementIndex]).plan->dependencyCount, 0u);
    ASSERT_EQ(compiledPlan.packet(packets[3u]).plan->dependencyCount, s_ExpectedDualCount);
    const Graphics::GpuPacketDependency* const secondDependencies = compiledPlan.packet(packets[1u]).dependencies;
    const Graphics::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(packets[3u]).dependencies;
    ASSERT_NE(secondDependencies, nullptr);
    ASSERT_NE(terminalDependencies, nullptr);
    EXPECT_EQ(secondDependencies[0u].producer, packets[0u]);
    EXPECT_EQ(secondDependencies[0u].consumer, packets[1u]);
    EXPECT_EQ(terminalDependencies[0u].producer, packets[s_ThirdElementIndex]);
    EXPECT_EQ(terminalDependencies[0u].consumer, packets[3u]);
    EXPECT_EQ(terminalDependencies[1u].producer, packets[1u]);
    EXPECT_EQ(terminalDependencies[1u].consumer, packets[3u]);

    for(usize readerIndex = 1u; readerIndex < LengthOf(readers); ++readerIndex){
        const Graphics::GpuCompiledTask* const compiledReader = compiledPlan.findTask(readers[readerIndex]).plan;
        ASSERT_NE(compiledReader, nullptr);
        EXPECT_EQ(compiledReader->prologueStateSeedCount, 0u);
    }
    const Graphics::GpuCompiledTask* const terminalReader = compiledPlan.findTask(readers[3u]).plan;
    ASSERT_NE(terminalReader, nullptr);
    ASSERT_EQ(terminalReader->epilogueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const terminalBarriers = compiledPlan.findTask(readers[3u]).epilogueBarriers;
    ASSERT_NE(terminalBarriers, nullptr);
    EXPECT_EQ(terminalBarriers[0u].type, Graphics::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(terminalBarriers[0u].before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(terminalBarriers[0u].after, Graphics::ResourceStates::CopySource);
}

TEST(GpuTaskGraph, TerminalFinalizationReachabilityCrossesPackedWordBoundaries){
    const usize prefixPacketCounts[] = { 63u, 64u, 127u, 128u };
    const Graphics::GpuTaskResourceRange range{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u), };

    for(const usize prefixPacketCount : prefixPacketCounts){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuTaskCommandRequirements prefixCommands{ Graphics::GpuQueueCapability::Graphics };
        Graphics::GpuTaskSchedulingHint prefixScheduling;
        prefixScheduling.forceSubmissionBoundary = true;
        prefixScheduling.allowPacketMerge = false;
        const Name prefixBaseName("tests/task_graph/terminal_packed_reachability_prefix_");
        char taskIndexBuffer[32u] = {};
        for(usize taskIndex = 0u; taskIndex < prefixPacketCount; ++taskIndex){
            const auto derivedNameAt731Col69 = DeriveName(prefixBaseName, FormatDecimal(taskIndex, taskIndexBuffer));
            ASSERT_TRUE(derivedNameAt731Col69);
            const auto prefixTaskResult = AddTaskWithCommands(graph, *derivedNameAt731Col69, "Terminal Packed Reachability Prefix", prefixCommands, prefixScheduling);
            ASSERT_TRUE(prefixTaskResult);
            const Graphics::GpuTaskId prefixTask = *prefixTaskResult;
            ASSERT_TRUE(prefixTask.valid());
        }

        const auto pairResult = AddExternalFinalReadPair(
            graph,
            Graphics::GpuGraphResourceType::Texture,
            range,
            range,
            Graphics::ResourceStates::ShaderResource,
            Graphics::ResourceStates::CopySource,
            Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
            false,
            true,
            {}
        );
        ASSERT_TRUE(pairResult);
        const ExternalFinalReadPair pair = *pairResult;
        ASSERT_TRUE(pair.resource.valid());
        ASSERT_TRUE(pair.earlierReader.valid());
        ASSERT_TRUE(pair.finalizingReader.valid());

        const Graphics::GpuTaskResourceUse terminalUse{ .resource = pair.resource, .range = range, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read, .hasIndependentStateSource = true, };
        Graphics::GpuTaskSchedulingHint terminalScheduling;
        terminalScheduling.forceSubmissionBoundary = true;
        terminalScheduling.allowPacketMerge = false;
        Graphics::GpuTaskDesc terminalDesc;
        terminalDesc
            .setIdentity(Name("tests/task_graph/terminal_packed_reachability_reader"))
            .setMarkerLabel("Terminal Packed Reachability Reader")
            .setScheduling(terminalScheduling)
            .setDependencies(&pair.finalizingReader, 1u)
            .setResourceUses(&terminalUse, 1u)
        ;
        const auto terminalReaderResult = graph.addTask(terminalDesc, Graphics::GpuTaskCommandRequirements{ Graphics::GpuQueueCapability::Graphics });
        ASSERT_TRUE(terminalReaderResult);
        const Graphics::GpuTaskId terminalReader = *terminalReaderResult;
        ASSERT_TRUE(terminalReader.valid());

        TwoQueueCompile twoQueueCompile(testArena);
        ASSERT_TRUE(twoQueueCompile.compile(graph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);

        EXPECT_EQ(compiledPlan.packetCount(), prefixPacketCount + 3u);

        const Graphics::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
        const Graphics::GpuSubmissionPacketId middlePacket = compiledPlan.packetForTask(pair.finalizingReader);
        const Graphics::GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalReader);
        ASSERT_TRUE(earlierPacket.valid());
        ASSERT_TRUE(middlePacket.valid());
        ASSERT_TRUE(terminalPacket.valid());
        EXPECT_EQ(earlierPacket.index, prefixPacketCount);
        EXPECT_EQ(middlePacket.index, prefixPacketCount + 1u);
        EXPECT_EQ(terminalPacket.index, prefixPacketCount + s_ExpectedDualCount);
        ASSERT_EQ(compiledPlan.packet(middlePacket).plan->dependencyCount, 1u);
        ASSERT_EQ(compiledPlan.packet(terminalPacket).plan->dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const middleDependencies = compiledPlan.packet(middlePacket).dependencies;
        const Graphics::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(terminalPacket).dependencies;
        ASSERT_NE(middleDependencies, nullptr);
        ASSERT_NE(terminalDependencies, nullptr);
        EXPECT_EQ(middleDependencies[0u].producer, earlierPacket);
        EXPECT_EQ(terminalDependencies[0u].producer, middlePacket);
    }
}

TEST(GpuTaskGraph, ExportsTextureTerminalFragmentsAfterPartialWholeResourceOverwrite){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues), };
    const auto textureResult = graph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_fragmented_texture"))
            .setMarkerLabel("External Final Fragmented Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Common)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[s_ThirdElementIndex].id)
    );
    ASSERT_TRUE(textureResult);
    const Graphics::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Graphics::GpuTaskResourceRange wholeRange{ .textureSubresources = Graphics::s_AllSubresources, };
    const Graphics::GpuTaskResourceRange overwrittenRange{ .textureSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, Graphics::TextureSubresourceSet::s_AllArraySlices), };
    const Graphics::GpuTaskResourceRange terminalTailRange{ .textureSubresources = Graphics::TextureSubresourceSet(1u, Graphics::TextureSubresourceSet::s_AllMipLevels, 0u, Graphics::TextureSubresourceSet::s_AllArraySlices), };
    const Graphics::GpuTaskResourceUse wholeWriterUse{ .resource = texture, .range = wholeRange, .requiredState = Graphics::ResourceStates::CopyDest, .access = Graphics::GpuTaskResourceAccess::Write, };
    const Graphics::GpuTaskResourceUse partialWriterUse{ .resource = texture, .range = overwrittenRange, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write, };
    const Graphics::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Graphics::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc wholeWriterDesc;
    wholeWriterDesc.setIdentity(Name("tests/task_graph/external_final_fragmented_whole_writer")).setMarkerLabel("External Final Fragmented Whole Writer").setScheduling(scheduling).setResourceUses(&wholeWriterUse, 1u);
    Graphics::GpuTaskDesc partialWriterDesc;
    partialWriterDesc.setIdentity(Name("tests/task_graph/external_final_fragmented_partial_writer")).setMarkerLabel("External Final Fragmented Partial Writer").setScheduling(scheduling).setResourceUses(&partialWriterUse, 1u);
    const auto wholeWriterResult = graph.addTask(wholeWriterDesc, graphicsCommands);
    ASSERT_TRUE(wholeWriterResult);
    const Graphics::GpuTaskId wholeWriter = *wholeWriterResult;
    const auto partialWriterResult = graph.addTask(partialWriterDesc, computeCommands);
    ASSERT_TRUE(partialWriterResult);
    const Graphics::GpuTaskId partialWriter = *partialWriterResult;
    ASSERT_TRUE(wholeWriter.valid());
    ASSERT_TRUE(partialWriter.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(operationScratchArena, graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledWholeWriter = compiledPlan.findTask(wholeWriter).plan;
    const Graphics::GpuCompiledTask* const compiledPartialWriter = compiledPlan.findTask(partialWriter).plan;
    ASSERT_NE(compiledWholeWriter, nullptr);
    ASSERT_NE(compiledPartialWriter, nullptr);
    EXPECT_EQ(compiledWholeWriter->queue, queues[0u].id);
    EXPECT_EQ(compiledPartialWriter->queue, queues[1u].id);

    const Graphics::GpuCompiledExternalResourceExportView exportView = compiledPlan.externalResourceExport(texture);
    const Graphics::GpuCompiledExternalResourceExport* const exportInfo = exportView.plan;
    ASSERT_NE(exportInfo, nullptr);
    EXPECT_EQ(exportInfo->resource, texture);
    EXPECT_EQ(exportInfo->destinationQueue, queues[s_ThirdElementIndex].id);
    EXPECT_EQ(exportInfo->finalState, Graphics::ResourceStates::ShaderResource);
    EXPECT_FALSE(exportInfo->producerTask.valid());
    EXPECT_FALSE(exportInfo->sourceQueue.valid());
    ASSERT_EQ(exportInfo->sourceCount, s_ExpectedDualCount);
    const Graphics::GpuCompiledExternalResourceExportSource* const sources = exportView.sources;
    ASSERT_NE(sources, nullptr);
    bool hasWholeWriterTail = false;
    bool hasPartialWriterMipZero = false;
    for(u32 sourceIndex = 0u; sourceIndex < exportInfo->sourceCount; ++sourceIndex){
        const Graphics::GpuCompiledExternalResourceExportSource& source = sources[sourceIndex];
        hasWholeWriterTail = hasWholeWriterTail || (source.producerTask == wholeWriter && source.sourceQueue == queues[0u].id && source.range.textureSubresources == terminalTailRange.textureSubresources);
        hasPartialWriterMipZero = hasPartialWriterMipZero || (source.producerTask == partialWriter && source.sourceQueue == queues[1u].id && source.range.textureSubresources == overwrittenRange.textureSubresources);
    }
    EXPECT_TRUE(hasWholeWriterTail);
    EXPECT_TRUE(hasPartialWriterMipZero);

    const auto hasStateExport = [&](const Graphics::GpuTaskId task, const Graphics::TextureSubresourceSet& range, const Graphics::ResourceStates::Mask before, const Graphics::GpuPhysicalQueueId& queue){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
        if(!compiledTask || !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureStateExport
                && barrier.resource == texture
                && barrier.range.textureSubresources == range
                && barrier.before == before
                && barrier.after == Graphics::ResourceStates::ShaderResource
                && barrier.sourceQueue == queue
                && barrier.destinationQueue == queue
            )
                return true;
        }
        return false;
    };
    const auto hasExternalRelease = [&](const Graphics::GpuTaskId task, const Graphics::TextureSubresourceSet& range, const Graphics::GpuPhysicalQueueId& sourceQueue){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
        if(!compiledTask || !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Graphics::GpuCompiledBarrierType::TextureOwnershipRelease
                && barrier.resource == texture
                && barrier.range.textureSubresources == range
                && barrier.before == Graphics::ResourceStates::ShaderResource
                && barrier.after == Graphics::ResourceStates::ShaderResource
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == queues[s_ThirdElementIndex].id
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateExport(wholeWriter, terminalTailRange.textureSubresources, Graphics::ResourceStates::CopyDest, queues[0u].id));
    EXPECT_TRUE(hasStateExport(partialWriter, overwrittenRange.textureSubresources, Graphics::ResourceStates::UnorderedAccess, queues[1u].id));
    EXPECT_TRUE(hasExternalRelease(wholeWriter, terminalTailRange.textureSubresources, queues[0u].id));
    EXPECT_TRUE(hasExternalRelease(partialWriter, overwrittenRange.textureSubresources, queues[1u].id));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

