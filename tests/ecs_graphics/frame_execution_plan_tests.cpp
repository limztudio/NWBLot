// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_execution_plan.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_frame_execution_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FrameExecutionPlan = NWB::Impl::ECSRenderDetail::FrameExecutionPlan;
using FrameExecutionPlanInput = NWB::Impl::ECSRenderDetail::FrameExecutionPlanInput;
using FrameExecutionLaneCommandListPair = NWB::Impl::ECSRenderDetail::FrameExecutionLaneCommandListPair;
using FrameExecutionPacketCommandLists = NWB::Impl::ECSRenderDetail::FrameExecutionPacketCommandLists;
using FrameExecutionWorkCommandListBinding = NWB::Impl::ECSRenderDetail::FrameExecutionWorkCommandListBinding;
using FrameExecutionPlanSubmissionState = NWB::Impl::ECSRenderDetail::FrameExecutionPlanSubmissionState;
namespace FrameExecutionPacket = NWB::Impl::ECSRenderDetail::FrameExecutionPacket;
namespace FrameExecutionSubmissionBatch = NWB::Impl::ECSRenderDetail::FrameExecutionSubmissionBatch;
namespace FrameExecutionWork = NWB::Impl::ECSRenderDetail::FrameExecutionWork;
namespace RenderLane = NWB::Core::RenderLane;
namespace CommandQueue = NWB::Core::CommandQueue;


// The collector stores opaque command-list pointers and never dereferences them. Distinct non-null sentinels keep
// these plan-only tests independent from a graphics device.
[[nodiscard]] NWB::Core::CommandList* TestCommandList(const usize identity){
    return reinterpret_cast<NWB::Core::CommandList*>(identity);
}


void ExpectSubmissionBatch(
    const FrameExecutionPlan& plan,
    const FrameExecutionSubmissionBatch::Enum batchID,
    const FrameExecutionPacket::Enum* const expectedPackets,
    const usize expectedPacketCount
){
    const auto& batch = plan.submissionBatch(batchID);
    ASSERT_EQ(batch.packetCount, expectedPacketCount);
    for(usize packetIndex = 0u; packetIndex < expectedPacketCount; ++packetIndex)
        EXPECT_EQ(batch.packets[packetIndex], expectedPackets[packetIndex]);
}


void ExpectSubmissionBatchOrder(
    const FrameExecutionPlan& plan,
    const FrameExecutionSubmissionBatch::Enum* const expectedBatches,
    const usize expectedBatchCount
){
    ASSERT_EQ(plan.submissionBatchCount(), expectedBatchCount);
    for(usize batchIndex = 0u; batchIndex < expectedBatchCount; ++batchIndex)
        EXPECT_EQ(plan.submissionBatchID(batchIndex), expectedBatches[batchIndex]);
}


void ExpectSubmissionBatchesResolvePacketDependencies(const FrameExecutionPlan& plan){
    bool scheduledPackets[FrameExecutionPacket::kCount] = {};
    for(usize batchIndex = 0u; batchIndex < plan.submissionBatchCount(); ++batchIndex){
        const auto& batch = plan.submissionBatch(plan.submissionBatchID(batchIndex));
        for(u8 packetIndex = 0u; packetIndex < batch.packetCount; ++packetIndex){
            const FrameExecutionPacket::Enum packet = batch.packets[packetIndex];
            const auto& packetPlan = plan.packet(packet);
            EXPECT_TRUE(packetPlan.enabled);
            EXPECT_FALSE(scheduledPackets[static_cast<usize>(packet)]);
            for(u8 waitPacketIndex = 0u; waitPacketIndex < packetPlan.waitPacketCount; ++waitPacketIndex){
                EXPECT_TRUE(scheduledPackets[static_cast<usize>(packetPlan.waitPackets[waitPacketIndex])]);
            }
            scheduledPackets[static_cast<usize>(packet)] = true;
        }
    }
    for(usize packetIndex = 0u; packetIndex < FrameExecutionPacket::kCount; ++packetIndex){
        const FrameExecutionPacket::Enum packet = static_cast<FrameExecutionPacket::Enum>(packetIndex);
        if(packet == FrameExecutionPacket::AsyncLaggedLightingStash)
            EXPECT_FALSE(scheduledPackets[packetIndex]);
        else if(plan.packet(packet).enabled)
            EXPECT_TRUE(scheduledPackets[packetIndex]);
        else
            EXPECT_FALSE(scheduledPackets[packetIndex]);
    }
}


TEST(EcsGraphics, FrameExecutionPlanKeepsGraphicsFallbackAsOnePacket){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });

    EXPECT_TRUE(plan.usesGraphicsFallback());
    EXPECT_FALSE(plan.usesDedicatedAsyncCompute());
    EXPECT_FALSE(plan.usesLaggedAsyncLighting());
    EXPECT_FALSE(plan.hasWork(FrameExecutionWork::LaggedLightingStash));
    EXPECT_FALSE(plan.workRunsOnLane(FrameExecutionWork::AvboitDepthWarp, RenderLane::AsyncCompute));
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsFallback).enabled);
    EXPECT_EQ(plan.packet(FrameExecutionPacket::GraphicsFallback).lane, RenderLane::Graphics);
    EXPECT_FALSE(plan.packet(FrameExecutionPacket::GraphicsPrefix).enabled);
    EXPECT_FALSE(plan.packet(FrameExecutionPacket::AsyncRayEffects).enabled);
    EXPECT_FALSE(plan.packet(FrameExecutionPacket::DeferredLighting).enabled);
}


TEST(EcsGraphics, FrameExecutionPlanDescribesDedicatedBootstrapAndLaggedTopologies){
    const FrameExecutionPlan opaquePlan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        false,
    });

    EXPECT_TRUE(opaquePlan.usesDedicatedAsyncCompute());
    EXPECT_FALSE(opaquePlan.usesLaggedAsyncLighting());
    EXPECT_FALSE(opaquePlan.hasWork(FrameExecutionWork::LaggedLightingStash));
    EXPECT_FALSE(opaquePlan.workRunsOnLane(FrameExecutionWork::AvboitDepthWarp, RenderLane::AsyncCompute));
    EXPECT_TRUE(opaquePlan.packet(FrameExecutionPacket::GraphicsEffects).enabled);
    EXPECT_FALSE(opaquePlan.packet(FrameExecutionPacket::GraphicsAvboitPre).enabled);
    EXPECT_EQ(opaquePlan.packet(FrameExecutionPacket::DeferredLighting).lane, RenderLane::AsyncCompute);
    EXPECT_EQ(opaquePlan.packet(FrameExecutionPacket::DeferredLighting).waitPacketCount, 2u);
    EXPECT_EQ(
        opaquePlan.packet(FrameExecutionPacket::DeferredLighting).waitPackets[0],
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_EQ(
        opaquePlan.packet(FrameExecutionPacket::DeferredLighting).waitPackets[1],
        FrameExecutionPacket::AsyncRayEffects
    );

    const FrameExecutionPlan bootstrapPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        false,
        true,
    });

    EXPECT_TRUE(bootstrapPlan.usesDedicatedAsyncCompute());
    EXPECT_FALSE(bootstrapPlan.usesLaggedAsyncLighting());
    EXPECT_TRUE(bootstrapPlan.hasWork(FrameExecutionWork::LaggedLightingStash));
    EXPECT_TRUE(bootstrapPlan.workRunsOnLane(FrameExecutionWork::AvboitDepthWarp, RenderLane::AsyncCompute));
    EXPECT_EQ(bootstrapPlan.packet(FrameExecutionPacket::AsyncRayEffects).lane, RenderLane::AsyncCompute);
    EXPECT_EQ(bootstrapPlan.packet(FrameExecutionPacket::AsyncRayEffects).waitPacketCount, 1u);
    EXPECT_EQ(
        bootstrapPlan.packet(FrameExecutionPacket::AsyncRayEffects).waitPackets[0],
        FrameExecutionPacket::GraphicsPrefix
    );
    EXPECT_TRUE(bootstrapPlan.packet(FrameExecutionPacket::GraphicsAvboitPre).enabled);
    EXPECT_TRUE(bootstrapPlan.packet(FrameExecutionPacket::AsyncAvboitDepthWarp).enabled);
    EXPECT_EQ(bootstrapPlan.packet(FrameExecutionPacket::DeferredLighting).lane, RenderLane::AsyncCompute);
    EXPECT_EQ(bootstrapPlan.packet(FrameExecutionPacket::DeferredLighting).waitPacketCount, 2u);
    EXPECT_EQ(
        bootstrapPlan.packet(FrameExecutionPacket::DeferredLighting).waitPackets[0],
        FrameExecutionPacket::GraphicsAvboitAccumulation
    );
    EXPECT_EQ(
        bootstrapPlan.packet(FrameExecutionPacket::DeferredLighting).waitPackets[1],
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_FALSE(bootstrapPlan.packet(FrameExecutionPacket::DeferredLighting).waitsForLaggedLightingHistory);
    EXPECT_TRUE(bootstrapPlan.packet(FrameExecutionPacket::AsyncLaggedLightingStash).enabled);

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        true,
    });

    EXPECT_TRUE(laggedPlan.usesDedicatedAsyncCompute());
    EXPECT_TRUE(laggedPlan.usesLaggedAsyncLighting());
    EXPECT_TRUE(laggedPlan.hasWork(FrameExecutionWork::LaggedLightingStash));
    EXPECT_FALSE(laggedPlan.workRunsOnLane(FrameExecutionWork::AvboitDepthWarp, RenderLane::AsyncCompute));
    EXPECT_TRUE(laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).enabled);
    EXPECT_FALSE(laggedPlan.packet(FrameExecutionPacket::GraphicsAvboitPre).enabled);
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::DeferredLighting).lane, RenderLane::Graphics);
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::DeferredLighting).waitPacketCount, 1u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::DeferredLighting).waitPackets[0],
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_TRUE(laggedPlan.packet(FrameExecutionPacket::DeferredLighting).waitsForLaggedLightingHistory);
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::GraphicsPresent).waitPacketCount, 2u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::GraphicsPresent).waitPackets[0],
        FrameExecutionPacket::DeferredComposite
    );
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::GraphicsPresent).waitPackets[1],
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::AsyncLaggedLightingStash).lane, RenderLane::AsyncCompute);
}


TEST(EcsGraphics, FrameExecutionPlanOwnsOrderedSubmissionBatches){
    const FrameExecutionSubmissionBatch::Enum fallbackBatches[] = {
        FrameExecutionSubmissionBatch::GraphicsFallback,
    };
    const FrameExecutionPacket::Enum fallbackPackets[] = {
        FrameExecutionPacket::GraphicsFallback,
    };
    const FrameExecutionPlan fallbackPlan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });
    ExpectSubmissionBatchOrder(fallbackPlan, fallbackBatches, LengthOf(fallbackBatches));
    ExpectSubmissionBatch(
        fallbackPlan,
        FrameExecutionSubmissionBatch::GraphicsFallback,
        fallbackPackets,
        LengthOf(fallbackPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(fallbackPlan);

    const FrameExecutionSubmissionBatch::Enum dedicatedBatches[] = {
        FrameExecutionSubmissionBatch::GraphicsPrefix,
        FrameExecutionSubmissionBatch::AsyncRayEffects,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        FrameExecutionSubmissionBatch::DeferredLighting,
        FrameExecutionSubmissionBatch::DeferredComposite,
        FrameExecutionSubmissionBatch::GraphicsPresent,
    };
    const FrameExecutionPacket::Enum opaqueEffectsPackets[] = {
        FrameExecutionPacket::GraphicsEffects,
    };
    const FrameExecutionPlan opaquePlan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        false,
    });
    ExpectSubmissionBatchOrder(opaquePlan, dedicatedBatches, LengthOf(dedicatedBatches));
    ExpectSubmissionBatch(
        opaquePlan,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        opaqueEffectsPackets,
        LengthOf(opaqueEffectsPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(opaquePlan);

    const FrameExecutionPacket::Enum asyncAvboitEffectsPackets[] = {
        FrameExecutionPacket::GraphicsAvboitPre,
        FrameExecutionPacket::AsyncAvboitDepthWarp,
        FrameExecutionPacket::GraphicsAvboitExtinction,
        FrameExecutionPacket::AsyncAvboitIntegration,
        FrameExecutionPacket::GraphicsAvboitAccumulation,
    };
    const FrameExecutionPlan asyncAvboitPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        false,
        true,
    });
    ExpectSubmissionBatchOrder(asyncAvboitPlan, dedicatedBatches, LengthOf(dedicatedBatches));
    ExpectSubmissionBatch(
        asyncAvboitPlan,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        asyncAvboitEffectsPackets,
        LengthOf(asyncAvboitEffectsPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(asyncAvboitPlan);

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        true,
    });
    ExpectSubmissionBatchOrder(laggedPlan, dedicatedBatches, LengthOf(dedicatedBatches));
    ExpectSubmissionBatch(
        laggedPlan,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        opaqueEffectsPackets,
        LengthOf(opaqueEffectsPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(laggedPlan);
}


TEST(EcsGraphics, FrameExecutionPlanAssignsRecordedWorkAndTimingToPlanPackets){
    const FrameExecutionPlan fallbackPlan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });

    EXPECT_EQ(
        fallbackPlan.packetForWork(FrameExecutionWork::GraphicsPrefix),
        FrameExecutionPacket::GraphicsFallback
    );
    EXPECT_EQ(
        fallbackPlan.packetForWork(FrameExecutionWork::RayEffects),
        FrameExecutionPacket::GraphicsFallback
    );
    EXPECT_EQ(
        fallbackPlan.packetForWork(FrameExecutionWork::AvboitRaster),
        FrameExecutionPacket::GraphicsFallback
    );
    EXPECT_TRUE(fallbackPlan.packet(FrameExecutionPacket::GraphicsFallback).recordsTiming);
    EXPECT_FALSE(fallbackPlan.hasWork(FrameExecutionWork::AsyncEffectsTiming));
    EXPECT_FALSE(fallbackPlan.hasWork(FrameExecutionWork::AvboitDepthWarp));

    const FrameExecutionPlan splitPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        false,
        true,
    });

    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::GraphicsPrefix),
        FrameExecutionPacket::GraphicsPrefix
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::RayEffects),
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::Caustics),
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::SurfelGi),
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::AvboitRaster),
        FrameExecutionPacket::GraphicsAvboitPre
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::AsyncEffectsTiming),
        FrameExecutionPacket::GraphicsAvboitPre
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::AvboitDepthWarp),
        FrameExecutionPacket::AsyncAvboitDepthWarp
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::AvboitExtinction),
        FrameExecutionPacket::GraphicsAvboitExtinction
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::AvboitIntegration),
        FrameExecutionPacket::AsyncAvboitIntegration
    );
    EXPECT_EQ(
        splitPlan.packetForWork(FrameExecutionWork::AvboitAccumulation),
        FrameExecutionPacket::GraphicsAvboitAccumulation
    );
    EXPECT_TRUE(splitPlan.packet(FrameExecutionPacket::GraphicsPrefix).recordsTiming);
    EXPECT_TRUE(splitPlan.packet(FrameExecutionPacket::AsyncRayEffects).recordsTiming);
    EXPECT_TRUE(splitPlan.packet(FrameExecutionPacket::GraphicsAvboitPre).recordsTiming);
    EXPECT_TRUE(splitPlan.packet(FrameExecutionPacket::AsyncAvboitIntegration).recordsTiming);
    EXPECT_TRUE(splitPlan.packet(FrameExecutionPacket::DeferredLighting).recordsTiming);
    EXPECT_FALSE(splitPlan.packet(FrameExecutionPacket::AsyncLaggedLightingStash).recordsTiming);

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        true,
    });

    EXPECT_EQ(
        laggedPlan.packetForWork(FrameExecutionWork::AvboitRaster),
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_EQ(
        laggedPlan.packetForWork(FrameExecutionWork::AsyncEffectsTiming),
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_FALSE(laggedPlan.hasWork(FrameExecutionWork::AvboitDepthWarp));
    EXPECT_TRUE(laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).recordsTiming);
    EXPECT_TRUE(laggedPlan.packet(FrameExecutionPacket::DeferredLighting).recordsTiming);
}


TEST(EcsGraphics, FrameExecutionPlanSelectsWorkCommandListFromResolvedLane){
    NWB::Core::CommandList* const graphicsCommandList = TestCommandList(101u);
    NWB::Core::CommandList* const asyncComputeCommandList = TestCommandList(102u);
    const FrameExecutionLaneCommandListPair commandLists{
        graphicsCommandList,
        asyncComputeCommandList,
    };

    const FrameExecutionPlan fallbackPlan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });
    EXPECT_EQ(fallbackPlan.laneForWork(FrameExecutionWork::Caustics), RenderLane::Graphics);
    EXPECT_TRUE(fallbackPlan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::Graphics));
    EXPECT_FALSE(fallbackPlan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::AsyncCompute));
    EXPECT_EQ(
        fallbackPlan.commandListForWork(FrameExecutionWork::Caustics, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        fallbackPlan.commandListForWork(FrameExecutionWork::SurfelGi, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        fallbackPlan.commandListForWork(FrameExecutionWork::DeferredLighting, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        fallbackPlan.commandListForWork(FrameExecutionWork::DeferredComposite, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        fallbackPlan.commandListForWork(FrameExecutionWork::AsyncEffectsTiming, commandLists),
        nullptr
    );

    const FrameExecutionPlan splitPlan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        true,
    });
    EXPECT_EQ(splitPlan.laneForWork(FrameExecutionWork::RayEffects), RenderLane::AsyncCompute);
    EXPECT_TRUE(splitPlan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::AsyncCompute));
    EXPECT_TRUE(splitPlan.workRunsOnLane(FrameExecutionWork::SurfelGi, RenderLane::AsyncCompute));
    EXPECT_TRUE(splitPlan.workRunsOnLane(FrameExecutionWork::DeferredLighting, RenderLane::AsyncCompute));
    EXPECT_TRUE(splitPlan.workRunsOnLane(FrameExecutionWork::DeferredComposite, RenderLane::AsyncCompute));
    EXPECT_EQ(
        splitPlan.commandListForWork(FrameExecutionWork::Caustics, commandLists),
        asyncComputeCommandList
    );
    EXPECT_EQ(
        splitPlan.commandListForWork(FrameExecutionWork::SurfelGi, commandLists),
        asyncComputeCommandList
    );
    EXPECT_EQ(
        splitPlan.commandListForWork(FrameExecutionWork::DeferredLighting, commandLists),
        asyncComputeCommandList
    );
    EXPECT_EQ(
        splitPlan.commandListForWork(FrameExecutionWork::DeferredComposite, commandLists),
        asyncComputeCommandList
    );

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        true,
    });
    EXPECT_TRUE(laggedPlan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::AsyncCompute));
    EXPECT_TRUE(laggedPlan.workRunsOnLane(FrameExecutionWork::SurfelGi, RenderLane::AsyncCompute));
    EXPECT_EQ(laggedPlan.laneForWork(FrameExecutionWork::DeferredLighting), RenderLane::Graphics);
    EXPECT_TRUE(laggedPlan.workRunsOnLane(FrameExecutionWork::DeferredComposite, RenderLane::Graphics));
    EXPECT_TRUE(laggedPlan.workRunsOnLane(FrameExecutionWork::LaggedLightingStash, RenderLane::AsyncCompute));
    EXPECT_EQ(
        laggedPlan.commandListForWork(FrameExecutionWork::Caustics, commandLists),
        asyncComputeCommandList
    );
    EXPECT_EQ(
        laggedPlan.commandListForWork(FrameExecutionWork::SurfelGi, commandLists),
        asyncComputeCommandList
    );
    EXPECT_EQ(
        laggedPlan.commandListForWork(FrameExecutionWork::DeferredLighting, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        laggedPlan.commandListForWork(FrameExecutionWork::DeferredComposite, commandLists),
        graphicsCommandList
    );
}


TEST(EcsGraphics, FrameExecutionPacketCommandListsKeepsFallbackOrderAndRejectsAbsentWork){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });
    FrameExecutionPacketCommandLists commandLists(plan);
    NWB::Core::CommandList* const recordedLists[] = {
        TestCommandList(1u),
        TestCommandList(2u),
        TestCommandList(3u),
        TestCommandList(4u),
        TestCommandList(5u),
        TestCommandList(6u),
        TestCommandList(7u),
        TestCommandList(8u),
        TestCommandList(9u),
        TestCommandList(10u),
        TestCommandList(11u),
        TestCommandList(12u),
    };

    EXPECT_FALSE(commandLists.appendForWork(FrameExecutionWork::GraphicsPrefix, nullptr));
    EXPECT_FALSE(commandLists.appendForWork(FrameExecutionWork::AsyncEffectsTiming, TestCommandList(13u)));
    EXPECT_EQ(commandLists.commandLists(FrameExecutionPacket::GraphicsFallback).commandListCount, 0u);
    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, recordedLists[0u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[1u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[2u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[3u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[4u] },
        { FrameExecutionWork::RayEffects, recordedLists[5u] },
        { FrameExecutionWork::Caustics, recordedLists[6u] },
        { FrameExecutionWork::SurfelGi, recordedLists[7u] },
        // Fallback does not create this work, so a complete binding table can retain its optional entry.
        { FrameExecutionWork::AsyncEffectsTiming, TestCommandList(13u) },
        { FrameExecutionWork::AvboitRaster, recordedLists[8u] },
        { FrameExecutionWork::DeferredLighting, recordedLists[9u] },
        { FrameExecutionWork::DeferredComposite, recordedLists[10u] },
        { FrameExecutionWork::GraphicsPresent, recordedLists[11u] },
    };
    ASSERT_TRUE(commandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));

    const auto fallbackLists = commandLists.commandLists(FrameExecutionPacket::GraphicsFallback);
    ASSERT_EQ(fallbackLists.commandListCount, LengthOf(recordedLists));
    for(usize commandListIndex = 0u; commandListIndex < LengthOf(recordedLists); ++commandListIndex)
        EXPECT_EQ(fallbackLists.commandLists[commandListIndex], recordedLists[commandListIndex]);

    EXPECT_FALSE(commandLists.appendForWork(FrameExecutionWork::GraphicsPrefix, TestCommandList(14u)));
    EXPECT_EQ(
        commandLists.commandLists(FrameExecutionPacket::GraphicsFallback).commandListCount,
        LengthOf(recordedLists)
    );
}


TEST(EcsGraphics, FrameExecutionPacketCommandListsRouteSplitWorkAndKeepTimingBracketOrdered){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true,
        true,
        true,
        false,
        true,
    });
    FrameExecutionPacketCommandLists commandLists(plan);
    NWB::Core::CommandList* const prefix = TestCommandList(21u);
    NWB::Core::CommandList* const shadow = TestCommandList(22u);
    NWB::Core::CommandList* const caustics = TestCommandList(23u);
    NWB::Core::CommandList* const surfelGi = TestCommandList(24u);
    NWB::Core::CommandList* const timingBegin = TestCommandList(25u);
    NWB::Core::CommandList* const avboitRaster = TestCommandList(26u);
    NWB::Core::CommandList* const timingEnd = TestCommandList(27u);
    NWB::Core::CommandList* const depthWarp = TestCommandList(28u);
    NWB::Core::CommandList* const extinction = TestCommandList(29u);
    NWB::Core::CommandList* const integration = TestCommandList(30u);
    NWB::Core::CommandList* const accumulation = TestCommandList(31u);
    NWB::Core::CommandList* const lighting = TestCommandList(32u);
    NWB::Core::CommandList* const composite = TestCommandList(33u);
    NWB::Core::CommandList* const present = TestCommandList(34u);
    NWB::Core::CommandList* const stash = TestCommandList(35u);

    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, prefix },
        { FrameExecutionWork::RayEffects, shadow },
        { FrameExecutionWork::Caustics, caustics },
        { FrameExecutionWork::SurfelGi, surfelGi },
        { FrameExecutionWork::AsyncEffectsTiming, timingBegin },
        { FrameExecutionWork::AvboitRaster, avboitRaster },
        { FrameExecutionWork::AsyncEffectsTiming, timingEnd },
        { FrameExecutionWork::AvboitDepthWarp, depthWarp },
        { FrameExecutionWork::AvboitExtinction, extinction },
        { FrameExecutionWork::AvboitIntegration, integration },
        { FrameExecutionWork::AvboitAccumulation, accumulation },
        { FrameExecutionWork::DeferredLighting, lighting },
        { FrameExecutionWork::DeferredComposite, composite },
        { FrameExecutionWork::GraphicsPresent, present },
    };
    ASSERT_TRUE(commandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));

    const auto prefixLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPrefix);
    ASSERT_EQ(prefixLists.commandListCount, 1u);
    EXPECT_EQ(prefixLists.commandLists[0], prefix);
    const auto rayEffectsLists = commandLists.commandLists(FrameExecutionPacket::AsyncRayEffects);
    ASSERT_EQ(rayEffectsLists.commandListCount, 3u);
    EXPECT_EQ(rayEffectsLists.commandLists[0], shadow);
    EXPECT_EQ(rayEffectsLists.commandLists[1], caustics);
    EXPECT_EQ(rayEffectsLists.commandLists[2], surfelGi);
    const auto avboitPreLists = commandLists.commandLists(FrameExecutionPacket::GraphicsAvboitPre);
    ASSERT_EQ(avboitPreLists.commandListCount, 3u);
    EXPECT_EQ(avboitPreLists.commandLists[0], timingBegin);
    EXPECT_EQ(avboitPreLists.commandLists[1], avboitRaster);
    EXPECT_EQ(avboitPreLists.commandLists[2], timingEnd);
    const auto depthWarpLists = commandLists.commandLists(FrameExecutionPacket::AsyncAvboitDepthWarp);
    ASSERT_EQ(depthWarpLists.commandListCount, 1u);
    EXPECT_EQ(depthWarpLists.commandLists[0], depthWarp);
    const auto extinctionLists = commandLists.commandLists(FrameExecutionPacket::GraphicsAvboitExtinction);
    ASSERT_EQ(extinctionLists.commandListCount, 1u);
    EXPECT_EQ(extinctionLists.commandLists[0], extinction);
    const auto integrationLists = commandLists.commandLists(FrameExecutionPacket::AsyncAvboitIntegration);
    ASSERT_EQ(integrationLists.commandListCount, 1u);
    EXPECT_EQ(integrationLists.commandLists[0], integration);
    const auto accumulationLists = commandLists.commandLists(FrameExecutionPacket::GraphicsAvboitAccumulation);
    ASSERT_EQ(accumulationLists.commandListCount, 1u);
    EXPECT_EQ(accumulationLists.commandLists[0], accumulation);
    const auto lightingLists = commandLists.commandLists(FrameExecutionPacket::DeferredLighting);
    ASSERT_EQ(lightingLists.commandListCount, 1u);
    EXPECT_EQ(lightingLists.commandLists[0], lighting);
    const auto compositeLists = commandLists.commandLists(FrameExecutionPacket::DeferredComposite);
    ASSERT_EQ(compositeLists.commandListCount, 1u);
    EXPECT_EQ(compositeLists.commandLists[0], composite);
    const auto presentLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPresent);
    ASSERT_EQ(presentLists.commandListCount, 1u);
    EXPECT_EQ(presentLists.commandLists[0], present);

    EXPECT_EQ(commandLists.commandLists(FrameExecutionPacket::AsyncLaggedLightingStash).commandListCount, 0u);
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::LaggedLightingStash, stash));
    const auto stashLists = commandLists.commandLists(FrameExecutionPacket::AsyncLaggedLightingStash);
    ASSERT_EQ(stashLists.commandListCount, 1u);
    EXPECT_EQ(stashLists.commandLists[0], stash);

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        true,
    });
    FrameExecutionPacketCommandLists laggedCommandLists(laggedPlan);
    ASSERT_TRUE(laggedCommandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));
    EXPECT_FALSE(laggedCommandLists.appendForWork(FrameExecutionWork::AvboitDepthWarp, depthWarp));
    const auto graphicsEffectsLists = laggedCommandLists.commandLists(FrameExecutionPacket::GraphicsEffects);
    ASSERT_EQ(graphicsEffectsLists.commandListCount, 3u);
    EXPECT_EQ(graphicsEffectsLists.commandLists[0], timingBegin);
    EXPECT_EQ(graphicsEffectsLists.commandLists[1], avboitRaster);
    EXPECT_EQ(graphicsEffectsLists.commandLists[2], timingEnd);
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateResolvesAcceptedDependenciesAndAsyncRecoveryToken){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        false,
    });
    const NWB::Core::QueueSubmissionToken laggedHistoryToken{ CommandQueue::Compute, 71u };
    FrameExecutionPlanSubmissionState submissions(plan, laggedHistoryToken);
    NWB::Core::QueueSubmissionToken waitTokens[FrameExecutionPlan::s_MaxSubmissionWaits] = {};
    NWB::Core::QueueSubmissionDesc submitDesc;

    EXPECT_EQ(submissions.asyncRecoveryWaitToken(), nullptr);

    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 0u);

    const NWB::Core::QueueSubmissionToken prefixToken{ CommandQueue::Graphics, 11u };
    submissions.acceptSubmission(FrameExecutionPacket::GraphicsPrefix, prefixToken);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 1u);
    EXPECT_EQ(waitTokens[0].queue, CommandQueue::Graphics);
    EXPECT_EQ(waitTokens[0].value, 11u);

    const NWB::Core::QueueSubmissionToken rayEffectsToken{ CommandQueue::Compute, 22u };
    submissions.acceptSubmission(FrameExecutionPacket::AsyncRayEffects, rayEffectsToken);
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 22u);
    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 1u);
    EXPECT_EQ(waitTokens[0].value, 11u);

    const NWB::Core::QueueSubmissionToken graphicsEffectsToken{ CommandQueue::Graphics, 33u };
    submissions.acceptSubmission(FrameExecutionPacket::GraphicsEffects, graphicsEffectsToken);
    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::DeferredLighting,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 2u);
    EXPECT_EQ(waitTokens[0].queue, CommandQueue::Graphics);
    EXPECT_EQ(waitTokens[0].value, 33u);
    EXPECT_EQ(waitTokens[1].queue, CommandQueue::Compute);
    EXPECT_EQ(waitTokens[1].value, 71u);
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->queue, CommandQueue::Compute);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 22u);

    const NWB::Core::QueueSubmissionToken lightingToken{ CommandQueue::Graphics, 44u };
    submissions.acceptSubmission(FrameExecutionPacket::DeferredLighting, lightingToken);
    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::DeferredComposite,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 1u);
    EXPECT_EQ(waitTokens[0].value, 44u);

    const NWB::Core::QueueSubmissionToken compositeToken{ CommandQueue::Graphics, 55u };
    submissions.acceptSubmission(FrameExecutionPacket::DeferredComposite, compositeToken);
    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPresent,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 2u);
    EXPECT_EQ(waitTokens[0].value, 55u);
    EXPECT_EQ(waitTokens[1].value, 22u);

    const NWB::Core::QueueSubmissionToken presentToken{ CommandQueue::Graphics, 66u };
    submissions.acceptSubmission(FrameExecutionPacket::GraphicsPresent, presentToken);
    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::AsyncLaggedLightingStash,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 1u);
    EXPECT_EQ(waitTokens[0].value, 66u);

    const NWB::Core::QueueSubmissionToken stashToken{ CommandQueue::Compute, 77u };
    submissions.acceptSubmission(FrameExecutionPacket::AsyncLaggedLightingStash, stashToken);
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 77u);
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateAsyncRecoveryTracksNewestAcceptedComputePacket){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        true,
    });
    FrameExecutionPlanSubmissionState submissions(plan);

    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 11u }
    );
    EXPECT_EQ(submissions.asyncRecoveryWaitToken(), nullptr);

    submissions.acceptSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Compute, 22u }
    );
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 22u);

    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsAvboitPre,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 33u }
    );
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 22u);

    submissions.acceptSubmission(
        FrameExecutionPacket::AsyncAvboitDepthWarp,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Compute, 44u }
    );
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 44u);

    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsAvboitExtinction,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 55u }
    );
    submissions.acceptSubmission(
        FrameExecutionPacket::AsyncAvboitIntegration,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Compute, 66u }
    );
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 66u);

    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsAvboitAccumulation,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 77u }
    );
    submissions.acceptSubmission(
        FrameExecutionPacket::DeferredLighting,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Compute, 88u }
    );
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 88u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
