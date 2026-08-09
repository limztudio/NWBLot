// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_execution_plan.h>
#include <impl/ecs_render/kernel/task_graph_schedule.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_frame_execution_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FrameExecutionPlan = NWB::Impl::ECSRenderDetail::FrameExecutionPlan;
using GpuTaskGraphFrameSchedule = NWB::Impl::ECSRenderDetail::GpuTaskGraphFrameSchedule;
using GpuTaskGraphFrameScheduleInput = NWB::Impl::ECSRenderDetail::GpuTaskGraphFrameScheduleInput;
using FrameExecutionPacketCommandLists = NWB::Impl::ECSRenderDetail::FrameExecutionPacketCommandLists;
using FrameExecutionWorkCommandListBinding = NWB::Impl::ECSRenderDetail::FrameExecutionWorkCommandListBinding;
namespace FrameExecutionPacket = NWB::Impl::ECSRenderDetail::FrameExecutionPacket;
namespace FrameExecutionSubmissionBatch = NWB::Impl::ECSRenderDetail::FrameExecutionSubmissionBatch;
namespace FrameExecutionWork = NWB::Impl::ECSRenderDetail::FrameExecutionWork;
namespace RenderLane = NWB::Core::RenderLane;
namespace CommandQueue = NWB::Core::CommandQueue;


[[nodiscard]] NWB::Core::CommandList* TestCommandList(const usize identity){
    return reinterpret_cast<NWB::Core::CommandList*>(identity);
}


TEST(EcsGraphics, FrameExecutionPlanRoutesOnlyTheLegacyGraphicsPrefix){
    const FrameExecutionPlan plan;

    ASSERT_EQ(FrameExecutionWork::kCount, 1u);
    ASSERT_TRUE(plan.hasWork(FrameExecutionWork::GraphicsPrefix));
    EXPECT_EQ(plan.laneForWork(FrameExecutionWork::GraphicsPrefix), RenderLane::Graphics);
    EXPECT_EQ(plan.expectedQueueForWork(FrameExecutionWork::GraphicsPrefix), CommandQueue::Graphics);
    EXPECT_TRUE(plan.workMatchesExpectedQueue(FrameExecutionWork::GraphicsPrefix, CommandQueue::Graphics));
    EXPECT_FALSE(plan.workMatchesExpectedQueue(FrameExecutionWork::GraphicsPrefix, CommandQueue::Compute));

    ASSERT_EQ(plan.submissionBatchCount(), 1u);
    EXPECT_EQ(plan.submissionBatchID(0u), FrameExecutionSubmissionBatch::GraphicsPrefix);
    const auto& batch = plan.submissionBatch(FrameExecutionSubmissionBatch::GraphicsPrefix);
    ASSERT_EQ(batch.packetCount, 1u);
    EXPECT_EQ(batch.packets[0], FrameExecutionPacket::GraphicsPrefix);
}


TEST(EcsGraphics, GpuTaskGraphScheduleRetainsOnlyPrefixPlanParityAndSelectsAvboitRoute){
    const GpuTaskGraphFrameScheduleInput inputs[] = {
        GpuTaskGraphFrameScheduleInput{ false, true, true, true, true, true },
        GpuTaskGraphFrameScheduleInput{ true, false, false, false, false, false },
        GpuTaskGraphFrameScheduleInput{ true, false, false, false, true, false },
        GpuTaskGraphFrameScheduleInput{ true, true, true, false, true, true },
        GpuTaskGraphFrameScheduleInput{ true, true, true, true, true, true },
    };
    for(const GpuTaskGraphFrameScheduleInput& input : inputs){
        const GpuTaskGraphFrameSchedule schedule(input);
        const FrameExecutionPlan plan;
        EXPECT_EQ(schedule.hasWork(FrameExecutionWork::GraphicsPrefix), plan.hasWork(FrameExecutionWork::GraphicsPrefix));
        EXPECT_FALSE(schedule.workDependsOn(
            FrameExecutionWork::GraphicsPrefix,
            FrameExecutionWork::GraphicsPrefix
        ));
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


TEST(EcsGraphics, FrameExecutionPacketCommandListsRouteOnlyTheLegacyPrefix){
    const FrameExecutionPlan plan;
    FrameExecutionPacketCommandLists commandLists(plan);
    NWB::Core::CommandList* const prefix = TestCommandList(1u);
    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, prefix },
    };
    ASSERT_TRUE(commandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));

    const auto prefixLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPrefix);
    ASSERT_EQ(prefixLists.commandListCount, 1u);
    EXPECT_EQ(prefixLists.commandLists[0], prefix);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
