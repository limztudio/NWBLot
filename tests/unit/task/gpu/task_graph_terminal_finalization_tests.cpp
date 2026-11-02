// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_terminal_finalization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, OrdersIndependentTerminalFinalizationDependenciesNearestFirst){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuGraphResourceDesc resourceDesc;
    resourceDesc
        .setIdentity(Name("tests/task_graph/ordered_terminal_finalization_resource"))
        .setMarkerLabel("Ordered Terminal Finalization Resource")
        .setType(Core::GpuGraphResourceType::Texture)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setExternalFinalState(Core::ResourceStates::CopySource)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    const auto resourceResult = graph.importResource(resourceDesc);
    ASSERT_TRUE(resourceResult);
    const Core::GpuGraphResourceId resource = *resourceResult;
    ASSERT_TRUE(resource.valid());

    const Core::GpuTaskResourceRange range{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };
    const Core::GpuTaskResourceUse readerUse{ .resource = resource, .range = range, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, .hasIndependentStateSource = true, };
    Core::GpuTaskSchedulingHint readerScheduling;
    readerScheduling.forceSubmissionBoundary = true;
    readerScheduling.allowPacketMerge = false;
    const Array<Core::GpuTaskCommandRequirements, 4u> readerCommands = {
        Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics },
        Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Compute },
        Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics },
        Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Compute },
    };

    Array<Core::GpuTaskId, 4u> readers = {};
    const Name readerBaseName("tests/task_graph/ordered_terminal_finalization_reader_");
    for(usize readerIndex = 0u; readerIndex < LengthOf(readers); ++readerIndex){
        Core::GpuTaskDesc readerDesc;
        const auto readerNameResult = IndexedName(readerBaseName, readerIndex);
        ASSERT_TRUE(readerNameResult);
        readerDesc
            .setIdentity(*readerNameResult)
            .setMarkerLabel("Ordered Terminal Finalization Reader")
            .setScheduling(readerScheduling)
            .setDependencies(Span(readerIndex == 1u ? &readers[0u] : nullptr, readerIndex == 1u ? 1u : 0u))
            .setResourceUses(Span(&readerUse, 1u))
        ;
        const auto readerTaskAdmission = graph.addTask(readerDesc, readerCommands[readerIndex]);
        ASSERT_TRUE(readerTaskAdmission);
        readers[readerIndex] = *readerTaskAdmission;
        ASSERT_TRUE(readers[readerIndex].valid());
    }

    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedComputeQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    EXPECT_NE(FindEdge(analysis, readers[0u], readers[1u]), nullptr);
    EXPECT_EQ(FindEdge(analysis, readers[0u], readers[3u]), nullptr);
    EXPECT_EQ(FindEdge(analysis, readers[2u], readers[3u]), nullptr);
    ASSERT_EQ(compiledPlan.packetCount(), LengthOf(readers));

    Array<Core::GpuSubmissionPacketId, 4u> packets = {};
    for(usize readerIndex = 0u; readerIndex < LengthOf(readers); ++readerIndex){
        packets[readerIndex] = compiledPlan.packetForTask(readers[readerIndex]);
        ASSERT_TRUE(packets[readerIndex].valid());
        EXPECT_EQ(packets[readerIndex].index, readerIndex);
        EXPECT_EQ(compiledPlan.packet(packets[readerIndex]).plan->queue, queues[readerIndex % 2u].id);
    }

    ASSERT_EQ(compiledPlan.packet(packets[0u]).plan->dependencyCount, 0u);
    ASSERT_EQ(compiledPlan.packet(packets[1u]).plan->dependencyCount, 1u);
    ASSERT_EQ(compiledPlan.packet(packets[2u]).plan->dependencyCount, 0u);
    ASSERT_EQ(compiledPlan.packet(packets[3u]).plan->dependencyCount, 2u);
    const Core::GpuPacketDependency* const secondDependencies = compiledPlan.packet(packets[1u]).dependencies;
    const Core::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(packets[3u]).dependencies;
    ASSERT_NE(secondDependencies, nullptr);
    ASSERT_NE(terminalDependencies, nullptr);
    EXPECT_EQ(secondDependencies[0u].producer, packets[0u]);
    EXPECT_EQ(secondDependencies[0u].consumer, packets[1u]);
    EXPECT_EQ(terminalDependencies[0u].producer, packets[2u]);
    EXPECT_EQ(terminalDependencies[0u].consumer, packets[3u]);
    EXPECT_EQ(terminalDependencies[1u].producer, packets[1u]);
    EXPECT_EQ(terminalDependencies[1u].consumer, packets[3u]);

    for(usize readerIndex = 1u; readerIndex < LengthOf(readers); ++readerIndex){
        const Core::GpuCompiledTask* const compiledReader = compiledPlan.findTask(readers[readerIndex]).plan;
        ASSERT_NE(compiledReader, nullptr);
        EXPECT_EQ(compiledReader->prologueStateSeedCount, 0u);
    }
    const Core::GpuCompiledTask* const terminalReader = compiledPlan.findTask(readers[3u]).plan;
    ASSERT_NE(terminalReader, nullptr);
    ASSERT_EQ(terminalReader->epilogueBarrierCount, 1u);
    const Core::GpuCompiledBarrier* const terminalBarriers = compiledPlan.findTask(readers[3u]).epilogueBarriers;
    ASSERT_NE(terminalBarriers, nullptr);
    EXPECT_EQ(terminalBarriers[0u].type, Core::GpuCompiledBarrierType::TextureStateExport);
    EXPECT_EQ(terminalBarriers[0u].before, Core::ResourceStates::ShaderResource);
    EXPECT_EQ(terminalBarriers[0u].after, Core::ResourceStates::CopySource);
}

TEST(GpuTaskGraph, TerminalFinalizationReachabilityCrossesPackedWordBoundaries){
    const Array<usize, 4u> prefixPacketCounts = { 63u, 64u, 127u, 128u };
    const Core::GpuTaskResourceRange range{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u), };

    for(const usize prefixPacketCount : prefixPacketCounts){
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const Core::GpuTaskCommandRequirements prefixCommands{ Core::GpuQueueCapability::Graphics };
        Core::GpuTaskSchedulingHint prefixScheduling;
        prefixScheduling.forceSubmissionBoundary = true;
        prefixScheduling.allowPacketMerge = false;
        const Name prefixBaseName("tests/task_graph/terminal_packed_reachability_prefix_");
        for(usize taskIndex = 0u; taskIndex < prefixPacketCount; ++taskIndex){
            const auto prefixNameResult = IndexedName(prefixBaseName, taskIndex);
            ASSERT_TRUE(prefixNameResult);
            const auto prefixTaskResult = AddTaskWithCommands(*prefixNameResult, "Terminal Packed Reachability Prefix", prefixCommands, prefixScheduling, {}, {}, graph);
            ASSERT_TRUE(prefixTaskResult);
            const Core::GpuTaskId prefixTask = *prefixTaskResult;
            ASSERT_TRUE(prefixTask.valid());
        }

        const auto pairResult = AddExternalFinalReadPair(
            graph,
            Core::GpuGraphResourceType::Texture,
            range,
            range,
            Core::ResourceStates::ShaderResource,
            Core::ResourceStates::CopySource,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            false,
            true,
            {}
        );
        ASSERT_TRUE(pairResult);
        const ExternalFinalReadPair pair = *pairResult;
        ASSERT_TRUE(pair.resource.valid());
        ASSERT_TRUE(pair.earlierReader.valid());
        ASSERT_TRUE(pair.finalizingReader.valid());

        const Core::GpuTaskResourceUse terminalUse{ .resource = pair.resource, .range = range, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, .hasIndependentStateSource = true, };
        Core::GpuTaskSchedulingHint terminalScheduling;
        terminalScheduling.forceSubmissionBoundary = true;
        terminalScheduling.allowPacketMerge = false;
        Core::GpuTaskDesc terminalDesc;
        terminalDesc
            .setIdentity(Name("tests/task_graph/terminal_packed_reachability_reader"))
            .setMarkerLabel("Terminal Packed Reachability Reader")
            .setScheduling(terminalScheduling)
            .setDependencies(Span(&pair.finalizingReader, 1u))
            .setResourceUses(Span(&terminalUse, 1u))
        ;
        const auto terminalReaderResult = graph.addTask(terminalDesc, Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics });
        ASSERT_TRUE(terminalReaderResult);
        const Core::GpuTaskId terminalReader = *terminalReaderResult;
        ASSERT_TRUE(terminalReader.valid());

        TwoQueueCompile twoQueueCompile(testArena);
        ASSERT_TRUE(twoQueueCompile.compile(graph));
        const Core::GpuCompiledGraph::ReadView compiledPlan(twoQueueCompile.compiledGraph);

        EXPECT_EQ(compiledPlan.packetCount(), prefixPacketCount + 3u);

        const Core::GpuSubmissionPacketId earlierPacket = compiledPlan.packetForTask(pair.earlierReader);
        const Core::GpuSubmissionPacketId middlePacket = compiledPlan.packetForTask(pair.finalizingReader);
        const Core::GpuSubmissionPacketId terminalPacket = compiledPlan.packetForTask(terminalReader);
        ASSERT_TRUE(earlierPacket.valid());
        ASSERT_TRUE(middlePacket.valid());
        ASSERT_TRUE(terminalPacket.valid());
        EXPECT_EQ(earlierPacket.index, prefixPacketCount);
        EXPECT_EQ(middlePacket.index, prefixPacketCount + 1u);
        EXPECT_EQ(terminalPacket.index, prefixPacketCount + 2u);
        ASSERT_EQ(compiledPlan.packet(middlePacket).plan->dependencyCount, 1u);
        ASSERT_EQ(compiledPlan.packet(terminalPacket).plan->dependencyCount, 1u);
        const Core::GpuPacketDependency* const middleDependencies = compiledPlan.packet(middlePacket).dependencies;
        const Core::GpuPacketDependency* const terminalDependencies = compiledPlan.packet(terminalPacket).dependencies;
        ASSERT_NE(middleDependencies, nullptr);
        ASSERT_NE(terminalDependencies, nullptr);
        EXPECT_EQ(middleDependencies[0u].producer, earlierPacket);
        EXPECT_EQ(terminalDependencies[0u].producer, middlePacket);
    }
}

TEST(GpuTaskGraph, ExportsTextureTerminalFragmentsAfterPartialWholeResourceOverwrite){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 3u> queues = { GraphicsQueue(), DedicatedComputeQueue(), DedicatedTransferQueue(), };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues), };
    const auto textureResult = graph.importResource(
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/external_final_fragmented_texture"))
            .setMarkerLabel("External Final Fragmented Texture")
            .setType(Core::GpuGraphResourceType::Texture)
            .setInitialState(Core::ResourceStates::Common)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(queues[2u].id)
    );
    ASSERT_TRUE(textureResult);
    const Core::GpuGraphResourceId texture = *textureResult;
    ASSERT_TRUE(texture.valid());

    const Core::GpuTaskResourceRange wholeRange{ .textureSubresources = Core::s_AllSubresources, };
    const Core::GpuTaskResourceRange overwrittenRange{ .textureSubresources = Core::TextureSubresourceSet(0u, 1u, 0u, Core::TextureSubresourceSet::s_AllArraySlices), };
    const Core::GpuTaskResourceRange terminalTailRange{ .textureSubresources = Core::TextureSubresourceSet(1u, Core::TextureSubresourceSet::s_AllMipLevels, 0u, Core::TextureSubresourceSet::s_AllArraySlices), };
    const Core::GpuTaskResourceUse wholeWriterUse{ .resource = texture, .range = wholeRange, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceUse partialWriterUse{ .resource = texture, .range = overwrittenRange, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskCommandRequirements graphicsCommands = GraphicsCommands();
    const Core::GpuTaskCommandRequirements computeCommands = ComputeCommands();
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc wholeWriterDesc;
    wholeWriterDesc.setIdentity(Name("tests/task_graph/external_final_fragmented_whole_writer")).setMarkerLabel("External Final Fragmented Whole Writer").setScheduling(scheduling).setResourceUses(Span(&wholeWriterUse, 1u));
    Core::GpuTaskDesc partialWriterDesc;
    partialWriterDesc.setIdentity(Name("tests/task_graph/external_final_fragmented_partial_writer")).setMarkerLabel("External Final Fragmented Partial Writer").setScheduling(scheduling).setResourceUses(Span(&partialWriterUse, 1u));
    const auto wholeWriterResult = graph.addTask(wholeWriterDesc, graphicsCommands);
    ASSERT_TRUE(wholeWriterResult);
    const Core::GpuTaskId wholeWriter = *wholeWriterResult;
    const auto partialWriterResult = graph.addTask(partialWriterDesc, computeCommands);
    ASSERT_TRUE(partialWriterResult);
    const Core::GpuTaskId partialWriter = *partialWriterResult;
    ASSERT_TRUE(wholeWriter.valid());
    ASSERT_TRUE(partialWriter.valid());

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(CompileWithSeparatedCommandQueues(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Core::GpuCompiledTask* const compiledWholeWriter = compiledPlan.findTask(wholeWriter).plan;
    const Core::GpuCompiledTask* const compiledPartialWriter = compiledPlan.findTask(partialWriter).plan;
    ASSERT_NE(compiledWholeWriter, nullptr);
    ASSERT_NE(compiledPartialWriter, nullptr);
    EXPECT_EQ(compiledWholeWriter->queue, queues[0u].id);
    EXPECT_EQ(compiledPartialWriter->queue, queues[1u].id);

    const Core::GpuCompiledExternalResourceExportView exportView = compiledPlan.externalResourceExport(texture);
    const Core::GpuCompiledExternalResourceExport* const exportInfo = exportView.plan;
    ASSERT_NE(exportInfo, nullptr);
    EXPECT_EQ(exportInfo->resource, texture);
    EXPECT_EQ(exportInfo->destinationQueue, queues[2u].id);
    EXPECT_EQ(exportInfo->finalState, Core::ResourceStates::ShaderResource);
    EXPECT_FALSE(exportInfo->producerTask.valid());
    EXPECT_FALSE(exportInfo->sourceQueue.valid());
    ASSERT_EQ(exportInfo->sourceCount, 2u);
    const Core::GpuCompiledExternalResourceExportSource* const sources = exportView.sources;
    ASSERT_NE(sources, nullptr);
    b8 hasWholeWriterTail = false;
    b8 hasPartialWriterMipZero = false;
    for(u32 sourceIndex = 0u; sourceIndex < exportInfo->sourceCount; ++sourceIndex){
        const Core::GpuCompiledExternalResourceExportSource& source = sources[sourceIndex];
        hasWholeWriterTail = hasWholeWriterTail || (source.producerTask == wholeWriter && source.sourceQueue == queues[0u].id && source.range.textureSubresources == terminalTailRange.textureSubresources);
        hasPartialWriterMipZero = hasPartialWriterMipZero || (source.producerTask == partialWriter && source.sourceQueue == queues[1u].id && source.range.textureSubresources == overwrittenRange.textureSubresources);
    }
    EXPECT_TRUE(hasWholeWriterTail);
    EXPECT_TRUE(hasPartialWriterMipZero);

    const auto hasStateExport = [&](const Core::GpuTaskId task, const Core::TextureSubresourceSet& range, const Core::ResourceStates::Mask before, const Core::GpuPhysicalQueueId& queue)noexcept{
        const Core::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
        if(!compiledTask || !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
            const Core::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Core::GpuCompiledBarrierType::TextureStateExport
                && barrier.resource == texture
                && barrier.range.textureSubresources == range
                && barrier.before == before
                && barrier.after == Core::ResourceStates::ShaderResource
                && barrier.sourceQueue == queue
                && barrier.destinationQueue == queue
            )
                return true;
        }
        return false;
    };
    const auto hasExternalRelease = [&](const Core::GpuTaskId task, const Core::TextureSubresourceSet& range, const Core::GpuPhysicalQueueId& sourceQueue)noexcept{
        const Core::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Core::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).epilogueBarriers;
        if(!compiledTask || !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex){
            const Core::GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == Core::GpuCompiledBarrierType::TextureOwnershipRelease
                && barrier.resource == texture
                && barrier.range.textureSubresources == range
                && barrier.before == Core::ResourceStates::ShaderResource
                && barrier.after == Core::ResourceStates::ShaderResource
                && barrier.sourceQueue == sourceQueue
                && barrier.destinationQueue == queues[2u].id
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasStateExport(wholeWriter, terminalTailRange.textureSubresources, Core::ResourceStates::CopyDest, queues[0u].id));
    EXPECT_TRUE(hasStateExport(partialWriter, overwrittenRange.textureSubresources, Core::ResourceStates::UnorderedAccess, queues[1u].id));
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

