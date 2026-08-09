// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_execution_plan.h>
#include <impl/ecs_render/kernel/task_graph_schedule.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_frame_execution_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FrameExecutionPlan = NWB::Impl::ECSRenderDetail::FrameExecutionPlan;
using FrameExecutionPlanInput = NWB::Impl::ECSRenderDetail::FrameExecutionPlanInput;
using GpuTaskGraphFrameSchedule = NWB::Impl::ECSRenderDetail::GpuTaskGraphFrameSchedule;
using GpuTaskGraphFrameScheduleInput = NWB::Impl::ECSRenderDetail::GpuTaskGraphFrameScheduleInput;
using FrameExecutionPacketCommandLists = NWB::Impl::ECSRenderDetail::FrameExecutionPacketCommandLists;
using FrameExecutionWorkCommandListBinding = NWB::Impl::ECSRenderDetail::FrameExecutionWorkCommandListBinding;
using FrameExecutionPlanSubmissionState = NWB::Impl::ECSRenderDetail::FrameExecutionPlanSubmissionState;
using FrameExecutionExternalWaitTokens = NWB::Impl::ECSRenderDetail::FrameExecutionExternalWaitTokens;
namespace FrameExecutionPacket = NWB::Impl::ECSRenderDetail::FrameExecutionPacket;
namespace FrameExecutionSubmissionBatch = NWB::Impl::ECSRenderDetail::FrameExecutionSubmissionBatch;
namespace FrameExecutionWork = NWB::Impl::ECSRenderDetail::FrameExecutionWork;
namespace FrameExecutionExternalWait = NWB::Impl::ECSRenderDetail::FrameExecutionExternalWait;
namespace RenderLane = NWB::Core::RenderLane;
namespace CommandQueue = NWB::Core::CommandQueue;


[[nodiscard]] NWB::Core::CommandList* TestCommandList(const usize identity){
    return reinterpret_cast<NWB::Core::CommandList*>(identity);
}


void ExpectSubmissionToken(
    const NWB::Core::QueueSubmissionToken& actual,
    const NWB::Core::QueueSubmissionToken& expected
){
    EXPECT_EQ(actual.queue, expected.queue);
    EXPECT_EQ(actual.value, expected.value);
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


void ExpectSubmissionBatchesResolvePacketDependencies(const FrameExecutionPlan& plan){
    bool scheduledPackets[FrameExecutionPacket::kCount] = {};
    for(usize batchIndex = 0u; batchIndex < plan.submissionBatchCount(); ++batchIndex){
        const auto& batch = plan.submissionBatch(plan.submissionBatchID(batchIndex));
        for(u8 packetIndex = 0u; packetIndex < batch.packetCount; ++packetIndex){
            const FrameExecutionPacket::Enum packet = batch.packets[packetIndex];
            const auto& packetPlan = plan.packet(packet);
            EXPECT_TRUE(packetPlan.enabled);
            EXPECT_FALSE(scheduledPackets[static_cast<usize>(packet)]);
            for(u8 waitIndex = 0u; waitIndex < packetPlan.waitPacketCount; ++waitIndex)
                EXPECT_TRUE(scheduledPackets[static_cast<usize>(packetPlan.waitPackets[waitIndex])]);
            scheduledPackets[static_cast<usize>(packet)] = true;
        }
    }
    for(usize packetIndex = 0u; packetIndex < FrameExecutionPacket::kCount; ++packetIndex){
        const FrameExecutionPacket::Enum packet = static_cast<FrameExecutionPacket::Enum>(packetIndex);
        EXPECT_EQ(scheduledPackets[packetIndex], plan.packet(packet).enabled);
    }
}


[[nodiscard]] bool PacketPathExists(
    const FrameExecutionPlan& plan,
    const FrameExecutionPacket::Enum producer,
    const FrameExecutionPacket::Enum consumer
){
    if(producer == consumer)
        return true;

    bool visited[FrameExecutionPacket::kCount] = {};
    const auto visit = [&](auto&& self, const FrameExecutionPacket::Enum current) -> bool{
        if(current == producer)
            return true;

        const usize currentIndex = static_cast<usize>(current);
        if(visited[currentIndex])
            return false;
        visited[currentIndex] = true;

        const auto& packet = plan.packet(current);
        for(u8 waitIndex = 0u; waitIndex < packet.waitPacketCount; ++waitIndex){
            if(self(self, packet.waitPackets[waitIndex]))
                return true;
        }
        return false;
    };
    return visit(visit, consumer);
}


TEST(EcsGraphics, FrameExecutionPlanRoutesRemainingWorkThroughGraphicsWithoutDedicatedCompute){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
        false,
    });

    for(usize workIndex = 0u; workIndex < FrameExecutionWork::kCount; ++workIndex){
        const FrameExecutionWork::Enum work = static_cast<FrameExecutionWork::Enum>(workIndex);
        if(!plan.hasWork(work))
            continue;
        EXPECT_EQ(plan.laneForWork(work), RenderLane::Graphics);
        EXPECT_EQ(plan.expectedQueueForWork(work), CommandQueue::Graphics);
    }
    EXPECT_FALSE(plan.hasWork(FrameExecutionWork::AvboitDepthWarp));
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsPrefix).enabled);
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::AsyncRayEffects).enabled);
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsEffects).enabled);
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsPresent).enabled);
    EXPECT_TRUE(plan.packetWaitsForExternalToken(
        FrameExecutionPacket::GraphicsPresent,
        FrameExecutionExternalWait::DeferredComposite
    ));
    ExpectSubmissionBatchesResolvePacketDependencies(plan);
}


TEST(EcsGraphics, FrameExecutionPlanKeepsOnlyRemainingWorkInTheParityOracle){
    const FrameExecutionPlan graphicsPlan(FrameExecutionPlanInput{
        false, false, false, false, false, false,
    });
    for(usize workIndex = 0u; workIndex < FrameExecutionWork::kCount; ++workIndex){
        const FrameExecutionWork::Enum work = static_cast<FrameExecutionWork::Enum>(workIndex);
        if(!graphicsPlan.hasWork(work))
            continue;
        EXPECT_TRUE(graphicsPlan.workMatchesExpectedQueue(work, CommandQueue::Graphics));
        EXPECT_FALSE(graphicsPlan.workMatchesExpectedQueue(work, CommandQueue::Compute));
    }

    const FrameExecutionPlan dedicatedPlan(FrameExecutionPlanInput{
        true, false, false, false, true, false,
    });
    EXPECT_EQ(dedicatedPlan.expectedQueueForWork(FrameExecutionWork::RayEffects), CommandQueue::Compute);
    EXPECT_FALSE(dedicatedPlan.hasWork(FrameExecutionWork::HardwareCaustics));
    EXPECT_EQ(dedicatedPlan.expectedQueueForWork(FrameExecutionWork::GraphicsPresent), CommandQueue::Graphics);

    const FrameExecutionPlan hardwarePlan(FrameExecutionPlanInput{
        true, false, false, false, true, true,
    });
    EXPECT_EQ(hardwarePlan.expectedQueueForWork(FrameExecutionWork::HardwareCaustics), CommandQueue::Graphics);
}


TEST(EcsGraphics, GpuTaskGraphFrameScheduleMatchesTheRemainingLegacyPlan){
    const GpuTaskGraphFrameScheduleInput inputs[] = {
        GpuTaskGraphFrameScheduleInput{ false, true, true, true, true, true },
        GpuTaskGraphFrameScheduleInput{ true, false, false, false, false, false },
        GpuTaskGraphFrameScheduleInput{ true, false, false, false, true, false },
        GpuTaskGraphFrameScheduleInput{ true, true, true, false, true, true },
        GpuTaskGraphFrameScheduleInput{ true, true, true, true, true, true },
        GpuTaskGraphFrameScheduleInput{ true, true, true, true, false, false },
    };
    for(const GpuTaskGraphFrameScheduleInput& input : inputs){
        const GpuTaskGraphFrameSchedule schedule(input);
        const FrameExecutionPlan plan(FrameExecutionPlanInput{
            input.dedicatedAsyncCompute,
            input.frameLaggedAsyncLightingEnabled,
            input.laggedLightingHistoryReady,
            input.laggedLightingHistoryAccepted,
            input.hasTransparentRenderers,
            input.hardwareCaustics,
        });
        for(usize workIndex = 0u; workIndex < FrameExecutionWork::kCount; ++workIndex){
            const FrameExecutionWork::Enum work = static_cast<FrameExecutionWork::Enum>(workIndex);
            ASSERT_EQ(schedule.hasWork(work), plan.hasWork(work));
            if(!schedule.hasWork(work))
                continue;

            for(usize producerIndex = 0u; producerIndex < FrameExecutionWork::kCount; ++producerIndex){
                const FrameExecutionWork::Enum producer = static_cast<FrameExecutionWork::Enum>(producerIndex);
                if(!schedule.workDependsOn(work, producer))
                    continue;
                ASSERT_TRUE(plan.hasWork(producer));
                EXPECT_TRUE(PacketPathExists(
                    plan,
                    plan.packetForWork(producer),
                    plan.packetForWork(work)
                ));
            }
            if(schedule.workWaitsForLaggedLightingHistory(work)){
                EXPECT_TRUE(plan.workWaitsForExternalToken(
                    work,
                    FrameExecutionExternalWait::LaggedLightingHistory
                ));
            }
        }
    }

    const GpuTaskGraphFrameSchedule activeHardwareLagged(GpuTaskGraphFrameScheduleInput{
        true, true, true, true, true, true,
    });
    EXPECT_TRUE(activeHardwareLagged.usesLaggedLightingHistory());
    EXPECT_TRUE(activeHardwareLagged.capturesLaggedLightingHistory());
    EXPECT_TRUE(activeHardwareLagged.workWaitsForLaggedLightingHistory(FrameExecutionWork::HardwareCaustics));

    const GpuTaskGraphFrameSchedule activeSoftwareLagged(GpuTaskGraphFrameScheduleInput{
        true, true, true, true, false, false,
    });
    EXPECT_FALSE(activeSoftwareLagged.hasWork(FrameExecutionWork::HardwareCaustics));
    EXPECT_FALSE(activeSoftwareLagged.workWaitsForLaggedLightingHistory(FrameExecutionWork::HardwareCaustics));
}


TEST(EcsGraphics, FrameExecutionPlanOwnsOnlyTheRemainingOrderedSubmissionBatches){
    const FrameExecutionPlan opaquePlan(FrameExecutionPlanInput{
        true, false, false, false, false, false,
    });
    const FrameExecutionSubmissionBatch::Enum expectedBatches[] = {
        FrameExecutionSubmissionBatch::GraphicsPrefix,
        FrameExecutionSubmissionBatch::AsyncRayEffects,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        FrameExecutionSubmissionBatch::GraphicsPresent,
    };
    ASSERT_EQ(opaquePlan.submissionBatchCount(), LengthOf(expectedBatches));
    for(usize batchIndex = 0u; batchIndex < LengthOf(expectedBatches); ++batchIndex)
        EXPECT_EQ(opaquePlan.submissionBatchID(batchIndex), expectedBatches[batchIndex]);

    const FrameExecutionPacket::Enum prefixPackets[] = { FrameExecutionPacket::GraphicsPrefix };
    const FrameExecutionPacket::Enum rayPackets[] = { FrameExecutionPacket::AsyncRayEffects };
    const FrameExecutionPacket::Enum effectsPackets[] = { FrameExecutionPacket::GraphicsEffects };
    const FrameExecutionPacket::Enum presentPackets[] = { FrameExecutionPacket::GraphicsPresent };
    ExpectSubmissionBatch(
        opaquePlan,
        FrameExecutionSubmissionBatch::GraphicsPrefix,
        prefixPackets,
        LengthOf(prefixPackets)
    );
    ExpectSubmissionBatch(
        opaquePlan,
        FrameExecutionSubmissionBatch::AsyncRayEffects,
        rayPackets,
        LengthOf(rayPackets)
    );
    ExpectSubmissionBatch(
        opaquePlan,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        effectsPackets,
        LengthOf(effectsPackets)
    );
    ExpectSubmissionBatch(
        opaquePlan,
        FrameExecutionSubmissionBatch::GraphicsPresent,
        presentPackets,
        LengthOf(presentPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(opaquePlan);

    const FrameExecutionPlan splitPlan(FrameExecutionPlanInput{
        true, false, false, false, true, false,
    });
    const FrameExecutionPacket::Enum splitEffectsPackets[] = {
        FrameExecutionPacket::GraphicsAvboitPre,
        FrameExecutionPacket::AsyncAvboitDepthWarp,
        FrameExecutionPacket::GraphicsAvboitExtinction,
        FrameExecutionPacket::AsyncAvboitIntegration,
        FrameExecutionPacket::GraphicsAvboitAccumulation,
    };
    ExpectSubmissionBatch(
        splitPlan,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        splitEffectsPackets,
        LengthOf(splitEffectsPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(splitPlan);
}


TEST(EcsGraphics, FrameExecutionPlanLeavesGraphOwnedLightingOutOfPacketDependencies){
    const FrameExecutionPlan bootstrapPlan(FrameExecutionPlanInput{
        true, true, true, false, true, false,
    });
    EXPECT_TRUE(bootstrapPlan.packet(FrameExecutionPacket::GraphicsPresent).enabled);
    EXPECT_EQ(bootstrapPlan.packet(FrameExecutionPacket::GraphicsPresent).waitPacketCount, 0u);
    EXPECT_EQ(bootstrapPlan.packet(FrameExecutionPacket::GraphicsPresent).externalWaitCount, 1u);
    EXPECT_EQ(
        bootstrapPlan.packet(FrameExecutionPacket::GraphicsPresent).externalWaits[0],
        FrameExecutionExternalWait::DeferredComposite
    );

    const FrameExecutionPlan activeLaggedPlan(FrameExecutionPlanInput{
        true, true, true, true, false, true,
    });
    const auto& present = activeLaggedPlan.packet(FrameExecutionPacket::GraphicsPresent);
    ASSERT_EQ(present.waitPacketCount, 1u);
    EXPECT_EQ(present.waitPackets[0], FrameExecutionPacket::AsyncRayEffects);
    ASSERT_EQ(present.externalWaitCount, 2u);
    EXPECT_EQ(present.externalWaits[0], FrameExecutionExternalWait::DeferredComposite);
    EXPECT_EQ(present.externalWaits[1], FrameExecutionExternalWait::SurfelGi);

    const auto& effects = activeLaggedPlan.packet(FrameExecutionPacket::GraphicsEffects);
    ASSERT_EQ(effects.externalWaitCount, 1u);
    EXPECT_EQ(effects.externalWaits[0], FrameExecutionExternalWait::LaggedLightingHistory);
}


TEST(EcsGraphics, FrameExecutionPacketCommandListsRouteOnlyRemainingPlanWork){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true, false, false, false, true, false,
    });
    FrameExecutionPacketCommandLists commandLists(plan);
    NWB::Core::CommandList* const prefix = TestCommandList(1u);
    NWB::Core::CommandList* const rayEffects = TestCommandList(2u);
    NWB::Core::CommandList* const avboitPre = TestCommandList(3u);
    NWB::Core::CommandList* const depthWarp = TestCommandList(4u);
    NWB::Core::CommandList* const extinction = TestCommandList(5u);
    NWB::Core::CommandList* const integration = TestCommandList(6u);
    NWB::Core::CommandList* const accumulation = TestCommandList(7u);
    NWB::Core::CommandList* const present = TestCommandList(8u);
    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, prefix },
        { FrameExecutionWork::RayEffects, rayEffects },
        { FrameExecutionWork::AvboitRaster, avboitPre },
        { FrameExecutionWork::AvboitDepthWarp, depthWarp },
        { FrameExecutionWork::AvboitExtinction, extinction },
        { FrameExecutionWork::AvboitIntegration, integration },
        { FrameExecutionWork::AvboitAccumulation, accumulation },
        { FrameExecutionWork::GraphicsPresent, present },
    };
    ASSERT_TRUE(commandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));

    const auto prefixLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPrefix);
    ASSERT_EQ(prefixLists.commandListCount, 1u);
    EXPECT_EQ(prefixLists.commandLists[0], prefix);
    const auto rayLists = commandLists.commandLists(FrameExecutionPacket::AsyncRayEffects);
    ASSERT_EQ(rayLists.commandListCount, 1u);
    EXPECT_EQ(rayLists.commandLists[0], rayEffects);
    const auto accumulationLists = commandLists.commandLists(FrameExecutionPacket::GraphicsAvboitAccumulation);
    ASSERT_EQ(accumulationLists.commandListCount, 1u);
    EXPECT_EQ(accumulationLists.commandLists[0], accumulation);
    const auto presentLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPresent);
    ASSERT_EQ(presentLists.commandListCount, 1u);
    EXPECT_EQ(presentLists.commandLists[0], present);

    const FrameExecutionPlan opaquePlan(FrameExecutionPlanInput{
        true, false, false, false, false, false,
    });
    FrameExecutionPacketCommandLists opaqueLists(opaquePlan);
    const FrameExecutionWorkCommandListBinding absentSplitWork[] = {
        { FrameExecutionWork::AvboitDepthWarp, TestCommandList(9u) },
    };
    EXPECT_TRUE(opaqueLists.appendPlannedWorkCommandLists(absentSplitWork, LengthOf(absentSplitWork)));
    EXPECT_EQ(
        opaqueLists.commandLists(FrameExecutionPacket::GraphicsEffects).commandListCount,
        0u
    );
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateWaitsForGraphCompositeBeforePresent){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false, false, false, false, false, false,
    });
    FrameExecutionPlanSubmissionState submissions(plan);
    NWB::Core::QueueSubmissionToken waitTokens[FrameExecutionPlan::s_MaxSubmissionWaits] = {};
    NWB::Core::QueueSubmissionDesc submitDesc;

    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 11u }
    );
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    submissions.acceptSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 22u }
    );
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsEffects,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 33u }
    );

    EXPECT_FALSE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPresent,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    const NWB::Core::QueueSubmissionToken compositeToken{ CommandQueue::Graphics, 44u };
    submissions.setExternalWaitToken(FrameExecutionExternalWait::DeferredComposite, compositeToken);
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPresent,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    ExpectSubmissionToken(waitTokens[0], compositeToken);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken(), nullptr);
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateRetainsLaggedProducerAndGraphCompletions){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true, true, true, true, false, true,
    });
    FrameExecutionExternalWaitTokens externalWaitTokens;
    const NWB::Core::QueueSubmissionToken historyToken{ CommandQueue::Compute, 71u };
    externalWaitTokens.tokens[static_cast<usize>(FrameExecutionExternalWait::LaggedLightingHistory)] = historyToken;
    FrameExecutionPlanSubmissionState submissions(plan, externalWaitTokens);
    NWB::Core::QueueSubmissionToken waitTokens[FrameExecutionPlan::s_MaxSubmissionWaits] = {};
    NWB::Core::QueueSubmissionDesc submitDesc;

    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 11u }
    );
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    EXPECT_EQ(waitTokens[0].value, 11u);
    const NWB::Core::QueueSubmissionToken rayEffectsToken{ CommandQueue::Compute, 22u };
    submissions.acceptSubmission(FrameExecutionPacket::AsyncRayEffects, rayEffectsToken);

    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 2u);
    EXPECT_EQ(waitTokens[0].value, 11u);
    ExpectSubmissionToken(waitTokens[1], historyToken);
    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsEffects,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 33u }
    );

    const NWB::Core::QueueSubmissionToken compositeToken{ CommandQueue::Graphics, 44u };
    const NWB::Core::QueueSubmissionToken surfelGiToken{ CommandQueue::Compute, 55u };
    submissions.setExternalWaitToken(FrameExecutionExternalWait::DeferredComposite, compositeToken);
    submissions.setExternalWaitToken(FrameExecutionExternalWait::SurfelGi, surfelGiToken);
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPresent,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 3u);
    ExpectSubmissionToken(waitTokens[0], rayEffectsToken);
    ExpectSubmissionToken(waitTokens[1], compositeToken);
    ExpectSubmissionToken(waitTokens[2], surfelGiToken);
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, rayEffectsToken.value);
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateTracksNewestRemainingComputePacket){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true, false, false, false, true, false,
    });
    FrameExecutionPlanSubmissionState submissions(plan);

    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 11u }
    );
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
