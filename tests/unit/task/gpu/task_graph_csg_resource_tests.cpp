// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_csg_resource_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


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
    // The metadata fixture mirrors the one-use declarations automatically derived by the two typed rectangular
    // primitives. Native rectangle payload/capture lowering is exercised by the descriptor-buffer smoke below.
    const Graphics::GpuTaskResourceUse intervalIdClearUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = intervalId,
            .range = Graphics::GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskResourceUse receiverEventCountClearUses[] = {
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
    const Graphics::GpuTaskId opaqueIntervalIdClear = AddTask(
        graph,
        Name("tests/task_graph/csg_opaque_interval_id_clear"),
        "Opaque CSG Interval Id Clear",
        nullptr,
        0u,
        intervalIdClearUses,
        LengthOf(intervalIdClearUses)
    );
    const Graphics::GpuTaskId opaqueReceiverEventCountClear = AddTask(
        graph,
        Name("tests/task_graph/csg_opaque_receiver_event_count_clear"),
        "Opaque CSG Receiver Event Count Clear",
        &opaqueIntervalIdClear,
        1u,
        receiverEventCountClearUses,
        LengthOf(receiverEventCountClearUses)
    );
    const Graphics::GpuTaskId opaqueProducer = AddTask(
        graph,
        Name("tests/task_graph/csg_opaque_interval_producer"),
        "Opaque CSG Interval Producer",
        &opaqueReceiverEventCountClear,
        1u,
        intervalProducerUses,
        LengthOf(intervalProducerUses)
    );
    const Graphics::GpuTaskId transparentIntervalIdClear = AddTask(
        graph,
        Name("tests/task_graph/csg_transparent_interval_id_clear"),
        "Transparent CSG Interval Id Clear",
        &opaqueProducer,
        1u,
        intervalIdClearUses,
        LengthOf(intervalIdClearUses)
    );
    const Graphics::GpuTaskId transparentReceiverEventCountClear = AddTask(
        graph,
        Name("tests/task_graph/csg_transparent_receiver_event_count_clear"),
        "Transparent CSG Receiver Event Count Clear",
        &transparentIntervalIdClear,
        1u,
        receiverEventCountClearUses,
        LengthOf(receiverEventCountClearUses)
    );
    const Graphics::GpuTaskId transparentProducer = AddTask(
        graph,
        Name("tests/task_graph/csg_transparent_interval_producer"),
        "Transparent CSG Interval Producer",
        &transparentReceiverEventCountClear,
        1u,
        intervalProducerUses,
        LengthOf(intervalProducerUses)
    );
    ASSERT_TRUE(opaqueIntervalIdClear.valid());
    ASSERT_TRUE(opaqueReceiverEventCountClear.valid());
    ASSERT_TRUE(opaqueProducer.valid());
    ASSERT_TRUE(transparentIntervalIdClear.valid());
    ASSERT_TRUE(transparentReceiverEventCountClear.valid());
    ASSERT_TRUE(transparentProducer.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.taskAt(opaqueIntervalIdClear.index).resourceUseCount, 1u);
        EXPECT_EQ(declarations.taskAt(opaqueReceiverEventCountClear.index).resourceUseCount, 1u);
        EXPECT_EQ(declarations.taskAt(opaqueProducer.index).resourceUseCount, 11u);
        EXPECT_EQ(declarations.taskAt(transparentIntervalIdClear.index).resourceUseCount, 1u);
        EXPECT_EQ(declarations.taskAt(transparentReceiverEventCountClear.index).resourceUseCount, 1u);
        EXPECT_EQ(declarations.taskAt(transparentProducer.index).resourceUseCount, 11u);
    }

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

    EXPECT_NE(FindEdge(analysis, opaqueIntervalIdClear, opaqueReceiverEventCountClear), nullptr);
    EXPECT_NE(FindEdge(analysis, opaqueReceiverEventCountClear, opaqueProducer), nullptr);
    EXPECT_NE(FindEdge(analysis, transparentIntervalIdClear, transparentReceiverEventCountClear), nullptr);
    EXPECT_NE(FindEdge(analysis, transparentReceiverEventCountClear, transparentProducer), nullptr);
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaqueIntervalIdClear,
        opaqueProducer,
        intervalId,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        opaqueReceiverEventCountClear,
        opaqueProducer,
        receiverEventCount,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentIntervalIdClear,
        transparentProducer,
        intervalId,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));
    EXPECT_TRUE(HasInferredHazard(
        analysis,
        transparentReceiverEventCountClear,
        transparentProducer,
        receiverEventCount,
        Graphics::GpuTaskHazardType::WriteAfterWrite
    ));

    const Graphics::GpuCompiledBarrier* const opaqueProducerBarriers = compiledPlan.findTask(opaqueProducer).prologueBarriers;
    const Graphics::GpuCompiledBarrier* const transparentProducerBarriers = compiledPlan.findTask(
        transparentProducer
    ).prologueBarriers;
    const Graphics::GpuCompiledTask* const compiledOpaqueProducer = compiledPlan.findTask(opaqueProducer).plan;
    const Graphics::GpuCompiledTask* const compiledTransparentProducer = compiledPlan.findTask(transparentProducer).plan;
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
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        EXPECT_EQ(declarations.taskAt(csgClipTask.index).resourceUseCount, 4u);
    }

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
    const Graphics::GpuSubmissionPacketId csgClipPacket = compiledPlan.packetForTask(csgClipTask);
    ASSERT_TRUE(csgClipPacket.valid());
    EXPECT_EQ(compiledPlan.packet(csgClipPacket).plan->dependencyCount, 0u);
    const Graphics::GpuCompiledTask* const compiledCsgClip = compiledPlan.findTask(csgClipTask).plan;
    ASSERT_NE(compiledCsgClip, nullptr);
    EXPECT_EQ(compiledCsgClip->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledCsgClip->prologueBarrierCount, 4u);
    const Graphics::GpuCompiledBarrier* const csgClipBarriers = compiledPlan.findTask(csgClipTask).prologueBarriers;
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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuCompiledTask* const compiledMaterial = compiledPlan.findTask(materialTask).plan;
    ASSERT_NE(compiledMaterial, nullptr);
    ASSERT_EQ(compiledMaterial->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledMaterial->prologueBarrierCount, 3u);
    const Graphics::GpuCompiledBarrier* const materialBarriers = compiledPlan.findTask(materialTask).prologueBarriers;
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

    const Graphics::GpuGraphResourceSetId materialGeometrySet = graph.importResourceSet(
        Graphics::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/task_graph/material_geometry_set"))
            .setMarkerLabel("Prepared Material Geometry")
            .setMembers(&geometry, 1u)
    );
    ASSERT_TRUE(materialGeometrySet.valid());
    const Graphics::GpuTaskResourceSetUse materialSetUses[] = {
        Graphics::GpuTaskResourceSetUse{
            .resourceSet = materialGeometrySet,
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
        .setResourceSetUses(materialSetUses, LengthOf(materialSetUses))
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
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

    const Graphics::GpuCompiledTask* const compiledMaterial = compiledPlan.findTask(materialTask).plan;
    ASSERT_NE(compiledMaterial, nullptr);
    ASSERT_EQ(compiledMaterial->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledMaterial->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const materialBarrier = compiledPlan.findTask(materialTask).prologueBarriers;
    ASSERT_NE(materialBarrier, nullptr);
    EXPECT_EQ(materialBarrier[0].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(materialBarrier[0].resource, geometry);
    EXPECT_EQ(materialBarrier[0].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(materialBarrier[0].after, Graphics::ResourceStates::ShaderResource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

