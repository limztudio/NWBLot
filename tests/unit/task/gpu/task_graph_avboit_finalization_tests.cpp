// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_finalization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuCompiledTask* const compiledOccupancy = compiledPlan.findTask(occupancy).plan;
    const Graphics::GpuCompiledTask* const compiledTail = compiledPlan.findTask(tail).plan;
    ASSERT_NE(compiledOccupancy, nullptr);
    ASSERT_NE(compiledTail, nullptr);
    ASSERT_EQ(compiledOccupancy->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledTail->prologueBarrierCount, 1u);

    const Graphics::GpuCompiledBarrier* const occupancyBarrier = compiledPlan.findTask(occupancy).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const tailBarrier = compiledPlan.findTask(tail).prologueBarriers;
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

TEST(GpuTaskGraph, KeepsAvboitTypedClearChainWithOccupancy){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId lowRaster = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_low_raster"),
        "AVBOIT Low Raster"
    );
    const Graphics::GpuGraphResourceId accumColor = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_accum_color"),
        "AVBOIT Accumulation Color"
    );
    const Graphics::GpuGraphResourceId accumExtinction = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_accum_extinction"),
        "AVBOIT Accumulation Extinction"
    );
    const Graphics::GpuGraphResourceId coverage = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_coverage"),
        "AVBOIT Coverage"
    );
    const Graphics::GpuGraphResourceId depthWarp = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_depth_warp"),
        "AVBOIT Depth Warp"
    );
    const Graphics::GpuGraphResourceId control = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_control"),
        "AVBOIT Control"
    );
    const Graphics::GpuGraphResourceId extinction = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_extinction"),
        "AVBOIT Extinction"
    );
    const Graphics::GpuGraphResourceId extinctionOverflow = AddBufferMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    const Graphics::GpuGraphResourceId transmittance = AddTextureMetadata(
        graph,
        Name("tests/task_graph/avboit_clear_transmittance"),
        "AVBOIT Transmittance"
    );
    const Graphics::GpuGraphResourceId clearResources[] = {
        lowRaster,
        accumColor,
        accumExtinction,
        coverage,
        depthWarp,
        control,
        extinction,
        extinctionOverflow,
        transmittance,
    };
    for(const Graphics::GpuGraphResourceId& resource : clearResources)
        ASSERT_TRUE(resource.valid());

    const Graphics::GpuQueueRequest graphicsTransferQueue{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    Graphics::GpuTaskId previousClear;
    const auto appendClear = [&graph, &previousClear, &graphicsTransferQueue](
        const Name identity,
        const AStringView label,
        const Graphics::GpuGraphResourceId resource
    ){
        const Graphics::GpuTaskResourceUse clearUses[] = {
            Graphics::GpuTaskResourceUse{
                .resource = resource,
                .range = {},
                .requiredState = Graphics::ResourceStates::CopyDest,
                .access = Graphics::GpuTaskResourceAccess::Write,
            },
        };
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.cost = Graphics::GpuTaskCostHint::Tiny;
        scheduling.forceSubmissionBoundary = false;
        scheduling.allowPacketMerge = true;
        scheduling.mergeWithPrevious = previousClear.valid();
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(graphicsTransferQueue)
            .setScheduling(scheduling)
            .setDependencies(previousClear.valid() ? &previousClear : nullptr, previousClear.valid() ? 1u : 0u)
            .setResourceUses(clearUses, LengthOf(clearUses))
        ;
        previousClear = graph.addTask(desc);
        return previousClear;
    };
    const Graphics::GpuTaskId clearTasks[] = {
        appendClear(Name("tests/task_graph/avboit_typed_clear_low_raster"), "AVBOIT Clear Low Raster", lowRaster),
        appendClear(Name("tests/task_graph/avboit_typed_clear_accum_color"), "AVBOIT Clear Accumulation Color", accumColor),
        appendClear(Name("tests/task_graph/avboit_typed_clear_accum_extinction"), "AVBOIT Clear Accumulation Extinction", accumExtinction),
        appendClear(Name("tests/task_graph/avboit_typed_clear_coverage"), "AVBOIT Clear Coverage", coverage),
        appendClear(Name("tests/task_graph/avboit_typed_clear_depth_warp"), "AVBOIT Clear Depth Warp", depthWarp),
        appendClear(Name("tests/task_graph/avboit_typed_clear_control"), "AVBOIT Clear Control", control),
        appendClear(Name("tests/task_graph/avboit_typed_clear_extinction"), "AVBOIT Clear Extinction", extinction),
        appendClear(Name("tests/task_graph/avboit_typed_clear_extinction_overflow"), "AVBOIT Clear Extinction Overflow", extinctionOverflow),
        appendClear(Name("tests/task_graph/avboit_typed_clear_transmittance"), "AVBOIT Clear Transmittance", transmittance),
    };
    for(const Graphics::GpuTaskId& task : clearTasks)
        ASSERT_TRUE(task.valid());

    Graphics::GpuTaskResourceUse occupancyUses[LengthOf(clearResources)] = {};
    for(usize resourceIndex = 0u; resourceIndex < LengthOf(clearResources); ++resourceIndex){
        occupancyUses[resourceIndex].resource = clearResources[resourceIndex];
        occupancyUses[resourceIndex].requiredState = Graphics::ResourceStates::UnorderedAccess;
        occupancyUses[resourceIndex].access = Graphics::GpuTaskResourceAccess::ReadWrite;
    }
    Graphics::GpuTaskSchedulingHint occupancyScheduling;
    occupancyScheduling.cost = Graphics::GpuTaskCostHint::Large;
    occupancyScheduling.forceSubmissionBoundary = false;
    occupancyScheduling.allowPacketMerge = true;
    occupancyScheduling.mergeWithPrevious = true;
    Graphics::GpuTaskDesc occupancyDesc;
    occupancyDesc
        .setIdentity(Name("tests/task_graph/avboit_typed_clear_occupancy"))
        .setMarkerLabel("AVBOIT Occupancy")
        .setQueue(graphicsTransferQueue)
        .setScheduling(occupancyScheduling)
        .setDependencies(&previousClear, 1u)
        .setResourceUses(occupancyUses, LengthOf(occupancyUses))
    ;
    const Graphics::GpuTaskId occupancy = graph.addTask(occupancyDesc);
    ASSERT_TRUE(occupancy.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    ASSERT_EQ(compiledPlan.packetCount(), 1u);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(occupancy);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuSubmissionPacket& compiledPacket = *compiledPlan.packet(packet).plan;
    ASSERT_EQ(compiledPacket.taskCount, LengthOf(clearTasks) + 1u);
    const Graphics::GpuTaskId* const packetTasks = compiledPlan.packet(packet).tasks;
    ASSERT_NE(packetTasks, nullptr);
    for(usize taskIndex = 0u; taskIndex < LengthOf(clearTasks); ++taskIndex){
        EXPECT_EQ(packetTasks[taskIndex], clearTasks[taskIndex]);
        EXPECT_TRUE(compiledPlan.tasksSharePacket(clearTasks[taskIndex], occupancy));
    }
    EXPECT_EQ(packetTasks[LengthOf(clearTasks)], occupancy);

    const Graphics::GpuCompiledTask* const compiledOccupancy = compiledPlan.findTask(occupancy).plan;
    ASSERT_NE(compiledOccupancy, nullptr);
    ASSERT_EQ(compiledOccupancy->prologueBarrierCount, LengthOf(clearResources));
    const Graphics::GpuCompiledBarrier* const occupancyBarriers = compiledPlan.findTask(occupancy).prologueBarriers;
    ASSERT_NE(occupancyBarriers, nullptr);
    for(const Graphics::GpuGraphResourceId& resource : clearResources){
        bool foundTransition = false;
        for(usize barrierIndex = 0u; barrierIndex < compiledOccupancy->prologueBarrierCount; ++barrierIndex){
            const Graphics::GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
            if(
                barrier.resource == resource
                && barrier.before == Graphics::ResourceStates::CopyDest
                && barrier.after == Graphics::ResourceStates::UnorderedAccess
            ){
                foundTransition = true;
                break;
            }
        }
        EXPECT_TRUE(foundTransition);
    }
}

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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskQueueAssignment* const finalizerAssignment = assignments.find(finalizer);
    const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
    ASSERT_NE(finalizerAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    EXPECT_EQ(finalizerAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Compute);

    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId finalizerPacket = compiledPlan.packetForTask(finalizer);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledPlan.packetForTask(composite);
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(finalizerPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_EQ(finalizerPacket, accumulationPacket);
    EXPECT_NE(compositePacket, finalizerPacket);

    const Graphics::GpuCompiledTask* const compiledFinalizer = compiledPlan.findTask(finalizer).plan;
    const Graphics::GpuCompiledTask* const compiledComposite = compiledPlan.findTask(composite).plan;
    ASSERT_NE(compiledFinalizer, nullptr);
    ASSERT_NE(compiledComposite, nullptr);
    ASSERT_EQ(compiledFinalizer->prologueBarrierCount, 3u);
    const Graphics::GpuCompiledBarrier* const finalizerBarriers = compiledPlan.findTask(finalizer).prologueBarriers;
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

    const Graphics::GpuCompiledBarrier* const compositeBarriers = compiledPlan.findTask(composite).prologueBarriers;
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
        false,
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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskQueueAssignment* const finalizerAssignment = assignments.find(finalizer);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    ASSERT_NE(finalizerAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    EXPECT_EQ(finalizerAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(lightingAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
    EXPECT_NE(FindEdge(analysis, finalizer, lighting), nullptr);

    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId finalizerPacket = compiledPlan.packetForTask(finalizer);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(finalizerPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    EXPECT_EQ(finalizerPacket, accumulationPacket);
    EXPECT_NE(lightingPacket, finalizerPacket);

    const Graphics::GpuCompiledTask* const compiledFinalizer = compiledPlan.findTask(finalizer).plan;
    ASSERT_NE(compiledFinalizer, nullptr);
    const Graphics::GpuCompiledBarrier* const finalizerBarriers = compiledPlan.findTask(finalizer).prologueBarriers;
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

    const Graphics::GpuSubmissionPacket& lightingPacketInfo = *compiledPlan.packet(lightingPacket).plan;
    ASSERT_GT(lightingPacketInfo.dependencyCount, 0u);
    const Graphics::GpuPacketDependency* const lightingPacketDependencies = compiledPlan.packet(lightingPacket).dependencies;
    ASSERT_NE(lightingPacketDependencies, nullptr);
    bool lightingWaitsForFinalizer = false;
    for(u32 dependencyIndex = 0u; dependencyIndex < lightingPacketInfo.dependencyCount; ++dependencyIndex)
        lightingWaitsForFinalizer = lightingWaitsForFinalizer
            || lightingPacketDependencies[dependencyIndex].producer == finalizerPacket;
    EXPECT_TRUE(lightingWaitsForFinalizer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

