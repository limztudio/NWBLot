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


TEST(EcsGraphics, FrameExecutionPlanRoutesOnlyRemainingWorkThroughGraphics){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
    });

    for(usize workIndex = 0u; workIndex < FrameExecutionWork::kCount; ++workIndex){
        const FrameExecutionWork::Enum work = static_cast<FrameExecutionWork::Enum>(workIndex);
        ASSERT_TRUE(plan.hasWork(work));
        EXPECT_EQ(plan.laneForWork(work), RenderLane::Graphics);
        EXPECT_EQ(plan.expectedQueueForWork(work), CommandQueue::Graphics);
        EXPECT_TRUE(plan.workMatchesExpectedQueue(work, CommandQueue::Graphics));
        EXPECT_FALSE(plan.workMatchesExpectedQueue(work, CommandQueue::Compute));
    }
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsPrefix).enabled);
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsPresent).enabled);
    EXPECT_TRUE(plan.packetWaitsForExternalToken(
        FrameExecutionPacket::GraphicsPresent,
        FrameExecutionExternalWait::DeferredComposite
    ));
    ExpectSubmissionBatchesResolvePacketDependencies(plan);
}


TEST(EcsGraphics, GpuTaskGraphScheduleRetainsOnlyPlanParityAndSelectsAvboitRoute){
    const GpuTaskGraphFrameScheduleInput inputs[] = {
        GpuTaskGraphFrameScheduleInput{ false, true, true, true, true, true },
        GpuTaskGraphFrameScheduleInput{ true, false, false, false, false, false },
        GpuTaskGraphFrameScheduleInput{ true, false, false, false, true, false },
        GpuTaskGraphFrameScheduleInput{ true, true, true, false, true, true },
        GpuTaskGraphFrameScheduleInput{ true, true, true, true, true, true },
    };
    for(const GpuTaskGraphFrameScheduleInput& input : inputs){
        const GpuTaskGraphFrameSchedule schedule(input);
        const FrameExecutionPlan plan(FrameExecutionPlanInput{
            input.dedicatedAsyncCompute,
            input.frameLaggedAsyncLightingEnabled,
            input.laggedLightingHistoryReady,
            input.laggedLightingHistoryAccepted,
        });
        for(usize workIndex = 0u; workIndex < FrameExecutionWork::kCount; ++workIndex){
            const FrameExecutionWork::Enum work = static_cast<FrameExecutionWork::Enum>(workIndex);
            EXPECT_EQ(schedule.hasWork(work), plan.hasWork(work));
            for(usize producerIndex = 0u; producerIndex < FrameExecutionWork::kCount; ++producerIndex){
                EXPECT_FALSE(schedule.workDependsOn(
                    work,
                    static_cast<FrameExecutionWork::Enum>(producerIndex)
                ));
            }
        }
    }

    const GpuTaskGraphFrameSchedule splitAvboit(GpuTaskGraphFrameScheduleInput{
        true, false, false, false, true, true,
    });
    EXPECT_TRUE(splitAvboit.usesAsyncAvboit());

    const GpuTaskGraphFrameSchedule laggedLighting(GpuTaskGraphFrameScheduleInput{
        true, true, true, true, true, true,
    });
    EXPECT_TRUE(laggedLighting.usesLaggedLightingHistory());
    EXPECT_TRUE(laggedLighting.capturesLaggedLightingHistory());
    EXPECT_FALSE(laggedLighting.usesAsyncAvboit());

    const GpuTaskGraphFrameSchedule sharedQueue(GpuTaskGraphFrameScheduleInput{
        false, false, false, false, true, false,
    });
    EXPECT_FALSE(sharedQueue.usesAsyncAvboit());
}


TEST(EcsGraphics, FrameExecutionPlanOwnsOnlyPrefixAndPresentBatches){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true, false, false, false,
    });
    const FrameExecutionSubmissionBatch::Enum expectedBatches[] = {
        FrameExecutionSubmissionBatch::GraphicsPrefix,
        FrameExecutionSubmissionBatch::GraphicsPresent,
    };
    ASSERT_EQ(plan.submissionBatchCount(), LengthOf(expectedBatches));
    for(usize batchIndex = 0u; batchIndex < LengthOf(expectedBatches); ++batchIndex)
        EXPECT_EQ(plan.submissionBatchID(batchIndex), expectedBatches[batchIndex]);

    const FrameExecutionPacket::Enum prefixPackets[] = { FrameExecutionPacket::GraphicsPrefix };
    const FrameExecutionPacket::Enum presentPackets[] = { FrameExecutionPacket::GraphicsPresent };
    ExpectSubmissionBatch(
        plan,
        FrameExecutionSubmissionBatch::GraphicsPrefix,
        prefixPackets,
        LengthOf(prefixPackets)
    );
    ExpectSubmissionBatch(
        plan,
        FrameExecutionSubmissionBatch::GraphicsPresent,
        presentPackets,
        LengthOf(presentPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(plan);
}


TEST(EcsGraphics, FrameExecutionPlanImportsGraphCompletionsForPresentation){
    const FrameExecutionPlan bootstrapPlan(FrameExecutionPlanInput{
        true, true, true, false,
    });
    const auto& bootstrapPresent = bootstrapPlan.packet(FrameExecutionPacket::GraphicsPresent);
    EXPECT_EQ(bootstrapPresent.waitPacketCount, 0u);
    ASSERT_EQ(bootstrapPresent.externalWaitCount, 1u);
    EXPECT_EQ(bootstrapPresent.externalWaits[0], FrameExecutionExternalWait::DeferredComposite);

    const FrameExecutionPlan activeLaggedPlan(FrameExecutionPlanInput{
        true, true, true, true,
    });
    const auto& activePresent = activeLaggedPlan.packet(FrameExecutionPacket::GraphicsPresent);
    EXPECT_EQ(activePresent.waitPacketCount, 0u);
    ASSERT_EQ(activePresent.externalWaitCount, 2u);
    EXPECT_EQ(activePresent.externalWaits[0], FrameExecutionExternalWait::DeferredComposite);
    EXPECT_EQ(activePresent.externalWaits[1], FrameExecutionExternalWait::SurfelGi);
}


TEST(EcsGraphics, FrameExecutionPacketCommandListsRouteOnlyRemainingPlanWork){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true, false, false, false,
    });
    FrameExecutionPacketCommandLists commandLists(plan);
    NWB::Core::CommandList* const prefix = TestCommandList(1u);
    NWB::Core::CommandList* const present = TestCommandList(2u);
    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, prefix },
        { FrameExecutionWork::GraphicsPresent, present },
    };
    ASSERT_TRUE(commandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));

    const auto prefixLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPrefix);
    ASSERT_EQ(prefixLists.commandListCount, 1u);
    EXPECT_EQ(prefixLists.commandLists[0], prefix);
    const auto presentLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPresent);
    ASSERT_EQ(presentLists.commandListCount, 1u);
    EXPECT_EQ(presentLists.commandLists[0], present);
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateWaitsForGraphCompositeBeforePresent){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false, false, false, false,
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


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateRetainsGraphCompletions){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true, true, true, true,
    });
    FrameExecutionPlanSubmissionState submissions(plan);
    NWB::Core::QueueSubmissionToken waitTokens[FrameExecutionPlan::s_MaxSubmissionWaits] = {};
    NWB::Core::QueueSubmissionDesc submitDesc;

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
    ASSERT_EQ(submitDesc.waitTokenCount, 2u);
    ExpectSubmissionToken(waitTokens[0], compositeToken);
    ExpectSubmissionToken(waitTokens[1], surfelGiToken);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken(), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
