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
using FrameExecutionLaneCommandListPair = NWB::Impl::ECSRenderDetail::FrameExecutionLaneCommandListPair;
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


// The collector stores opaque command-list pointers and never dereferences them. Distinct non-null sentinels keep
// these plan-only tests independent from a graphics device.
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
        if(plan.packet(packet).enabled)
            EXPECT_TRUE(scheduledPackets[packetIndex]);
        else
            EXPECT_FALSE(scheduledPackets[packetIndex]);
    }
}


TEST(EcsGraphics, FrameExecutionPlanRoutesNoDedicatedComputeWorkThroughGraphicsPackets){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });

    EXPECT_FALSE(plan.workRunsOnLane(FrameExecutionWork::RayEffects, RenderLane::AsyncCompute));
    EXPECT_TRUE(plan.workRunsOnLane(FrameExecutionWork::RayEffects, RenderLane::Graphics));
    EXPECT_FALSE(plan.workWaitsForExternalToken(
        FrameExecutionWork::DeferredLighting,
        FrameExecutionExternalWait::LaggedLightingHistory
    ));
    EXPECT_FALSE(plan.workRunsOnLane(FrameExecutionWork::AvboitDepthWarp, RenderLane::AsyncCompute));
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsPrefix).enabled);
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::AsyncRayEffects).enabled);
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::GraphicsEffects).enabled);
    EXPECT_TRUE(plan.packet(FrameExecutionPacket::DeferredLighting).enabled);
    EXPECT_EQ(plan.packet(FrameExecutionPacket::AsyncRayEffects).lane, RenderLane::Graphics);
    EXPECT_EQ(plan.packet(FrameExecutionPacket::DeferredLighting).lane, RenderLane::Graphics);
    EXPECT_EQ(plan.packetForWork(FrameExecutionWork::GraphicsPrefix), FrameExecutionPacket::GraphicsPrefix);
    EXPECT_EQ(plan.packetForWork(FrameExecutionWork::RayEffects), FrameExecutionPacket::AsyncRayEffects);
    EXPECT_EQ(plan.packetForWork(FrameExecutionWork::Caustics), FrameExecutionPacket::GraphicsEffects);
    EXPECT_EQ(plan.packetForWork(FrameExecutionWork::SurfelGi), FrameExecutionPacket::AsyncRayEffects);
    for(usize packetIndex = 0u; packetIndex < FrameExecutionPacket::kCount; ++packetIndex){
        const FrameExecutionPacket::Enum packet = static_cast<FrameExecutionPacket::Enum>(packetIndex);
        if(plan.packet(packet).enabled)
            EXPECT_EQ(plan.packet(packet).lane, RenderLane::Graphics);
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


TEST(EcsGraphics, FrameExecutionPlanExposesLegacyPhysicalQueueParityOracle){
    const FrameExecutionPlan graphicsPlan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });
    for(usize workIndex = 0u; workIndex < FrameExecutionWork::kCount; ++workIndex){
        const FrameExecutionWork::Enum work = static_cast<FrameExecutionWork::Enum>(workIndex);
        if(!graphicsPlan.hasWork(work))
            continue;
        EXPECT_EQ(graphicsPlan.expectedQueueForWork(work), CommandQueue::Graphics);
        EXPECT_TRUE(graphicsPlan.workMatchesExpectedQueue(work, CommandQueue::Graphics));
        EXPECT_FALSE(graphicsPlan.workMatchesExpectedQueue(work, CommandQueue::Compute));
    }

    const FrameExecutionPlan dedicatedPlan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        true,
    });
    EXPECT_EQ(dedicatedPlan.expectedQueueForWork(FrameExecutionWork::RayEffects), CommandQueue::Compute);
    EXPECT_EQ(dedicatedPlan.expectedQueueForWork(FrameExecutionWork::Caustics), CommandQueue::Compute);
    EXPECT_EQ(dedicatedPlan.expectedQueueForWork(FrameExecutionWork::SurfelGi), CommandQueue::Compute);
    EXPECT_EQ(dedicatedPlan.expectedQueueForWork(FrameExecutionWork::DeferredLighting), CommandQueue::Compute);
    EXPECT_TRUE(dedicatedPlan.workMatchesExpectedQueue(FrameExecutionWork::DeferredLighting, CommandQueue::Compute));
    EXPECT_FALSE(dedicatedPlan.workMatchesExpectedQueue(FrameExecutionWork::DeferredLighting, CommandQueue::Graphics));

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        false,
    });
    EXPECT_EQ(laggedPlan.expectedQueueForWork(FrameExecutionWork::RayEffects), CommandQueue::Compute);
    EXPECT_EQ(laggedPlan.expectedQueueForWork(FrameExecutionWork::DeferredLighting), CommandQueue::Graphics);
    EXPECT_TRUE(laggedPlan.workMatchesExpectedQueue(FrameExecutionWork::DeferredLighting, CommandQueue::Graphics));
    EXPECT_FALSE(laggedPlan.workMatchesExpectedQueue(FrameExecutionWork::DeferredLighting, CommandQueue::Compute));

    const FrameExecutionLaneCommandListPair commandLists{
        TestCommandList(1u),
        TestCommandList(2u),
    };
    EXPECT_EQ(
        FrameExecutionPlan::commandListForResolvedQueue(CommandQueue::Graphics, commandLists),
        commandLists.graphics
    );
    EXPECT_EQ(
        FrameExecutionPlan::commandListForResolvedQueue(CommandQueue::Compute, commandLists),
        commandLists.asyncCompute
    );
    EXPECT_EQ(
        FrameExecutionPlan::commandListForResolvedQueue(CommandQueue::kCount, commandLists),
        nullptr
    );
}


TEST(EcsGraphics, GpuTaskGraphFrameScheduleUsesSemanticDependenciesCoveredByLegacyPlan){
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
    EXPECT_FALSE(activeHardwareLagged.usesAsyncAvboit());
    EXPECT_TRUE(activeHardwareLagged.capturesLaggedLightingHistory());
    EXPECT_TRUE(activeHardwareLagged.workWaitsForLaggedLightingHistory(FrameExecutionWork::Caustics));
    EXPECT_TRUE(activeHardwareLagged.workWaitsForLaggedLightingHistory(FrameExecutionWork::DeferredLighting));
    EXPECT_FALSE(activeHardwareLagged.workDependsOn(
        FrameExecutionWork::DeferredLighting,
        FrameExecutionWork::RayEffects
    ));

    const GpuTaskGraphFrameSchedule activeSoftwareLagged(GpuTaskGraphFrameScheduleInput{
        true, true, true, true, false, false,
    });
    EXPECT_TRUE(activeSoftwareLagged.usesAsyncCaustics());
    EXPECT_FALSE(activeSoftwareLagged.workWaitsForLaggedLightingHistory(FrameExecutionWork::Caustics));
    EXPECT_TRUE(activeSoftwareLagged.workWaitsForLaggedLightingHistory(FrameExecutionWork::DeferredLighting));
}


TEST(EcsGraphics, FrameExecutionPlanDescribesDedicatedBootstrapAndLaggedTopologies){
    const FrameExecutionPlan opaquePlan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        false,
    });

    EXPECT_TRUE(opaquePlan.workRunsOnLane(FrameExecutionWork::RayEffects, RenderLane::AsyncCompute));
    EXPECT_FALSE(opaquePlan.workWaitsForExternalToken(
        FrameExecutionWork::DeferredLighting,
        FrameExecutionExternalWait::LaggedLightingHistory
    ));
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
    EXPECT_EQ(opaquePlan.packet(FrameExecutionPacket::GraphicsPresent).externalWaitCount, 1u);
    EXPECT_EQ(
        opaquePlan.packet(FrameExecutionPacket::GraphicsPresent).externalWaits[0],
        FrameExecutionExternalWait::DeferredComposite
    );

    const FrameExecutionPlan bootstrapPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        false,
        true,
    });

    EXPECT_TRUE(bootstrapPlan.workRunsOnLane(FrameExecutionWork::RayEffects, RenderLane::AsyncCompute));
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
    EXPECT_FALSE(bootstrapPlan.workWaitsForExternalToken(
        FrameExecutionWork::DeferredLighting,
        FrameExecutionExternalWait::LaggedLightingHistory
    ));

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        true,
    });

    EXPECT_TRUE(laggedPlan.workRunsOnLane(FrameExecutionWork::RayEffects, RenderLane::AsyncCompute));
    EXPECT_FALSE(laggedPlan.workRunsOnLane(FrameExecutionWork::AvboitDepthWarp, RenderLane::AsyncCompute));
    EXPECT_TRUE(laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).enabled);
    EXPECT_FALSE(laggedPlan.packet(FrameExecutionPacket::GraphicsAvboitPre).enabled);
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::DeferredLighting).lane, RenderLane::Graphics);
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::DeferredLighting).waitPacketCount, 1u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::DeferredLighting).waitPackets[0],
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::DeferredLighting).externalWaitCount, 1u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::DeferredLighting).externalWaits[0],
        FrameExecutionExternalWait::LaggedLightingHistory
    );
    EXPECT_TRUE(laggedPlan.workWaitsForExternalToken(
        FrameExecutionWork::DeferredLighting,
        FrameExecutionExternalWait::LaggedLightingHistory
    ));
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::GraphicsPresent).waitPacketCount, 1u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::GraphicsPresent).waitPackets[0],
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::GraphicsPresent).externalWaitCount, 1u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::GraphicsPresent).externalWaits[0],
        FrameExecutionExternalWait::DeferredComposite
    );
}


TEST(EcsGraphics, FrameExecutionPlanOwnsOrderedSubmissionBatches){
    const FrameExecutionSubmissionBatch::Enum standardBatches[] = {
        FrameExecutionSubmissionBatch::GraphicsPrefix,
        FrameExecutionSubmissionBatch::AsyncRayEffects,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        FrameExecutionSubmissionBatch::DeferredLighting,
        FrameExecutionSubmissionBatch::GraphicsPresent,
    };
    const FrameExecutionPacket::Enum opaqueEffectsPackets[] = {
        FrameExecutionPacket::GraphicsEffects,
    };
    const FrameExecutionPlan graphicsPlan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });
    ExpectSubmissionBatchOrder(graphicsPlan, standardBatches, LengthOf(standardBatches));
    ExpectSubmissionBatch(
        graphicsPlan,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        opaqueEffectsPackets,
        LengthOf(opaqueEffectsPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(graphicsPlan);

    const FrameExecutionPlan opaquePlan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        false,
    });
    ExpectSubmissionBatchOrder(opaquePlan, standardBatches, LengthOf(standardBatches));
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
    ExpectSubmissionBatchOrder(asyncAvboitPlan, standardBatches, LengthOf(standardBatches));
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
    ExpectSubmissionBatchOrder(laggedPlan, standardBatches, LengthOf(standardBatches));
    ExpectSubmissionBatch(
        laggedPlan,
        FrameExecutionSubmissionBatch::GraphicsEffects,
        opaqueEffectsPackets,
        LengthOf(opaqueEffectsPackets)
    );
    ExpectSubmissionBatchesResolvePacketDependencies(laggedPlan);
}


TEST(EcsGraphics, FrameExecutionPlanAssignsRecordedWorkAndTimingToPlanPackets){
    const FrameExecutionPlan graphicsPlan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });

    EXPECT_EQ(
        graphicsPlan.packetForWork(FrameExecutionWork::GraphicsPrefix),
        FrameExecutionPacket::GraphicsPrefix
    );
    EXPECT_EQ(
        graphicsPlan.packetForWork(FrameExecutionWork::RayEffects),
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(
        graphicsPlan.packetForWork(FrameExecutionWork::AvboitRaster),
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_TRUE(graphicsPlan.packet(FrameExecutionPacket::GraphicsPrefix).recordsTiming);
    EXPECT_TRUE(graphicsPlan.packet(FrameExecutionPacket::AsyncRayEffects).recordsTiming);
    EXPECT_TRUE(graphicsPlan.packet(FrameExecutionPacket::GraphicsEffects).recordsTiming);
    EXPECT_FALSE(graphicsPlan.hasWork(FrameExecutionWork::AsyncEffectsTiming));
    EXPECT_FALSE(graphicsPlan.hasWork(FrameExecutionWork::AvboitDepthWarp));

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


TEST(EcsGraphics, FrameExecutionPlanKeepsHardwareCausticsInTheGraphicsOverlapPacket){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        true,
        true,
    });

    EXPECT_EQ(
        plan.packetForWork(FrameExecutionWork::RayEffects),
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(
        plan.packetForWork(FrameExecutionWork::SurfelGi),
        FrameExecutionPacket::AsyncRayEffects
    );
    EXPECT_EQ(
        plan.packetForWork(FrameExecutionWork::Caustics),
        FrameExecutionPacket::GraphicsAvboitPre
    );
    EXPECT_TRUE(plan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::Graphics));
    EXPECT_FALSE(plan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::AsyncCompute));
    EXPECT_EQ(
        plan.packetForWork(FrameExecutionWork::AsyncEffectsTiming),
        FrameExecutionPacket::GraphicsAvboitPre
    );
    EXPECT_EQ(plan.packet(FrameExecutionPacket::GraphicsAvboitPre).waitPacketCount, 1u);
    EXPECT_EQ(
        plan.packet(FrameExecutionPacket::GraphicsAvboitPre).waitPackets[0],
        FrameExecutionPacket::GraphicsPrefix
    );

    FrameExecutionPacketCommandLists commandLists(plan);
    NWB::Core::CommandList* const prefix = TestCommandList(201u);
    NWB::Core::CommandList* const timingBegin = TestCommandList(202u);
    NWB::Core::CommandList* const shadow = TestCommandList(203u);
    NWB::Core::CommandList* const caustics = TestCommandList(204u);
    NWB::Core::CommandList* const surfelGi = TestCommandList(205u);
    NWB::Core::CommandList* const avboitRaster = TestCommandList(206u);
    NWB::Core::CommandList* const timingEnd = TestCommandList(207u);
    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, prefix },
        { FrameExecutionWork::AsyncEffectsTiming, timingBegin },
        { FrameExecutionWork::RayEffects, shadow },
        { FrameExecutionWork::Caustics, caustics },
        { FrameExecutionWork::SurfelGi, surfelGi },
        { FrameExecutionWork::AvboitRaster, avboitRaster },
        { FrameExecutionWork::AsyncEffectsTiming, timingEnd },
    };
    ASSERT_TRUE(commandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));

    const auto rayEffectsLists = commandLists.commandLists(FrameExecutionPacket::AsyncRayEffects);
    ASSERT_EQ(rayEffectsLists.commandListCount, 2u);
    EXPECT_EQ(rayEffectsLists.commandLists[0], shadow);
    EXPECT_EQ(rayEffectsLists.commandLists[1], surfelGi);
    const auto graphicsSupportLists = commandLists.commandLists(FrameExecutionPacket::GraphicsAvboitPre);
    ASSERT_EQ(graphicsSupportLists.commandListCount, 4u);
    EXPECT_EQ(graphicsSupportLists.commandLists[0], timingBegin);
    EXPECT_EQ(graphicsSupportLists.commandLists[1], caustics);
    EXPECT_EQ(graphicsSupportLists.commandLists[2], avboitRaster);
    EXPECT_EQ(graphicsSupportLists.commandLists[3], timingEnd);

    const FrameExecutionPlan opaquePlan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        false,
        true,
    });
    EXPECT_EQ(
        opaquePlan.packetForWork(FrameExecutionWork::Caustics),
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_TRUE(opaquePlan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::Graphics));

    const FrameExecutionPlan laggedPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        true,
        true,
    });
    EXPECT_EQ(
        laggedPlan.packetForWork(FrameExecutionWork::Caustics),
        FrameExecutionPacket::GraphicsEffects
    );
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).waitPacketCount, 1u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).waitPackets[0],
        FrameExecutionPacket::GraphicsPrefix
    );
    EXPECT_EQ(laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).externalWaitCount, 1u);
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).externalWaits[0],
        FrameExecutionExternalWait::LaggedLightingHistory
    );
    EXPECT_TRUE(laggedPlan.workWaitsForExternalToken(
        FrameExecutionWork::Caustics,
        FrameExecutionExternalWait::LaggedLightingHistory
    ));
    EXPECT_TRUE(laggedPlan.workWaitsForExternalToken(
        FrameExecutionWork::DeferredLighting,
        FrameExecutionExternalWait::LaggedLightingHistory
    ));
}


TEST(EcsGraphics, FrameExecutionPlanSelectsWorkCommandListFromResolvedLane){
    NWB::Core::CommandList* const graphicsCommandList = TestCommandList(101u);
    NWB::Core::CommandList* const asyncComputeCommandList = TestCommandList(102u);
    const FrameExecutionLaneCommandListPair commandLists{
        graphicsCommandList,
        asyncComputeCommandList,
    };

    const FrameExecutionPlan graphicsPlan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
    });
    EXPECT_EQ(graphicsPlan.laneForWork(FrameExecutionWork::Caustics), RenderLane::Graphics);
    EXPECT_TRUE(graphicsPlan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::Graphics));
    EXPECT_FALSE(graphicsPlan.workRunsOnLane(FrameExecutionWork::Caustics, RenderLane::AsyncCompute));
    EXPECT_EQ(
        graphicsPlan.commandListForWork(FrameExecutionWork::Caustics, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        graphicsPlan.commandListForWork(FrameExecutionWork::SurfelGi, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        graphicsPlan.commandListForWork(FrameExecutionWork::DeferredLighting, commandLists),
        graphicsCommandList
    );
    EXPECT_EQ(
        graphicsPlan.commandListForWork(FrameExecutionWork::AsyncEffectsTiming, commandLists),
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
}


TEST(EcsGraphics, FrameExecutionPacketCommandListsRoutesGraphicsWorkAndRejectsAbsentWork){
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
    };

    EXPECT_FALSE(commandLists.appendForWork(FrameExecutionWork::GraphicsPrefix, nullptr));
    EXPECT_FALSE(commandLists.appendForWork(FrameExecutionWork::AsyncEffectsTiming, TestCommandList(13u)));
    EXPECT_EQ(commandLists.commandLists(FrameExecutionPacket::GraphicsPrefix).commandListCount, 0u);
    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, recordedLists[0u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[1u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[2u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[3u] },
        { FrameExecutionWork::GraphicsPrefix, recordedLists[4u] },
        { FrameExecutionWork::RayEffects, recordedLists[5u] },
        { FrameExecutionWork::Caustics, recordedLists[6u] },
        { FrameExecutionWork::SurfelGi, recordedLists[7u] },
        // This schedule does not create the dedicated timing bracket, so a complete binding table can retain it.
        { FrameExecutionWork::AsyncEffectsTiming, TestCommandList(13u) },
        { FrameExecutionWork::AvboitRaster, recordedLists[8u] },
        { FrameExecutionWork::DeferredLighting, recordedLists[9u] },
        { FrameExecutionWork::GraphicsPresent, recordedLists[10u] },
    };
    ASSERT_TRUE(commandLists.appendPlannedWorkCommandLists(bindings, LengthOf(bindings)));

    const auto prefixLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPrefix);
    ASSERT_EQ(prefixLists.commandListCount, 5u);
    for(usize commandListIndex = 0u; commandListIndex < prefixLists.commandListCount; ++commandListIndex)
        EXPECT_EQ(prefixLists.commandLists[commandListIndex], recordedLists[commandListIndex]);

    const auto rayEffectsLists = commandLists.commandLists(FrameExecutionPacket::AsyncRayEffects);
    ASSERT_EQ(rayEffectsLists.commandListCount, 2u);
    EXPECT_EQ(rayEffectsLists.commandLists[0], recordedLists[5u]);
    EXPECT_EQ(rayEffectsLists.commandLists[1], recordedLists[7u]);

    const auto graphicsEffectsLists = commandLists.commandLists(FrameExecutionPacket::GraphicsEffects);
    ASSERT_EQ(graphicsEffectsLists.commandListCount, 2u);
    EXPECT_EQ(graphicsEffectsLists.commandLists[0], recordedLists[6u]);
    EXPECT_EQ(graphicsEffectsLists.commandLists[1], recordedLists[8u]);

    EXPECT_TRUE(commandLists.appendForWork(FrameExecutionWork::GraphicsPrefix, TestCommandList(14u)));
    EXPECT_EQ(commandLists.commandLists(FrameExecutionPacket::GraphicsPrefix).commandListCount, 6u);
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
    NWB::Core::CommandList* const present = TestCommandList(33u);

    const FrameExecutionWorkCommandListBinding bindings[] = {
        { FrameExecutionWork::GraphicsPrefix, prefix },
        { FrameExecutionWork::AsyncEffectsTiming, timingBegin },
        { FrameExecutionWork::RayEffects, shadow },
        { FrameExecutionWork::Caustics, caustics },
        { FrameExecutionWork::SurfelGi, surfelGi },
        { FrameExecutionWork::AvboitRaster, avboitRaster },
        { FrameExecutionWork::AsyncEffectsTiming, timingEnd },
        { FrameExecutionWork::AvboitDepthWarp, depthWarp },
        { FrameExecutionWork::AvboitExtinction, extinction },
        { FrameExecutionWork::AvboitIntegration, integration },
        { FrameExecutionWork::AvboitAccumulation, accumulation },
        { FrameExecutionWork::DeferredLighting, lighting },
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
    const auto presentLists = commandLists.commandLists(FrameExecutionPacket::GraphicsPresent);
    ASSERT_EQ(presentLists.commandListCount, 1u);
    EXPECT_EQ(presentLists.commandLists[0], present);

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


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateKeepsNoDedicatedPacketsOnGraphics){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        false,
        true,
        true,
        true,
        true,
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
    EXPECT_EQ(submitDesc.waitTokenCount, 0u);

    const NWB::Core::QueueSubmissionToken prefixToken{ CommandQueue::Graphics, 11u };
    submissions.acceptSubmission(FrameExecutionPacket::GraphicsPrefix, prefixToken);
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    ExpectSubmissionToken(waitTokens[0], prefixToken);

    const NWB::Core::QueueSubmissionToken rayEffectsToken{ CommandQueue::Graphics, 22u };
    submissions.acceptSubmission(FrameExecutionPacket::AsyncRayEffects, rayEffectsToken);
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    ExpectSubmissionToken(waitTokens[0], prefixToken);

    const NWB::Core::QueueSubmissionToken graphicsEffectsToken{ CommandQueue::Graphics, 33u };
    submissions.acceptSubmission(FrameExecutionPacket::GraphicsEffects, graphicsEffectsToken);
    ASSERT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::DeferredLighting,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 2u);
    ExpectSubmissionToken(waitTokens[0], graphicsEffectsToken);
    ExpectSubmissionToken(waitTokens[1], rayEffectsToken);

    const NWB::Core::QueueSubmissionToken lightingToken{ CommandQueue::Graphics, 44u };
    submissions.acceptSubmission(FrameExecutionPacket::DeferredLighting, lightingToken);
    // The graph-owned composite has already waited for lighting and publishes only its accepted completion.
    const NWB::Core::QueueSubmissionToken compositeToken{ CommandQueue::Graphics, 55u };
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


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateTracksAcceptedPacketsByBatch){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true,
        false,
        false,
        false,
        true,
    });
    FrameExecutionPlanSubmissionState submissions(plan);

    EXPECT_FALSE(submissions.batchHasAcceptedPacket(FrameExecutionSubmissionBatch::GraphicsEffects));
    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 11u }
    );
    submissions.acceptSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Compute, 22u }
    );
    EXPECT_FALSE(submissions.batchHasAcceptedPacket(FrameExecutionSubmissionBatch::GraphicsEffects));

    submissions.acceptSubmission(
        FrameExecutionPacket::GraphicsAvboitPre,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 33u }
    );
    EXPECT_TRUE(submissions.batchHasAcceptedPacket(FrameExecutionSubmissionBatch::GraphicsEffects));
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateResolvesAcceptedDependenciesAndAsyncRecoveryToken){
    const FrameExecutionPlan plan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        false,
    });
    FrameExecutionExternalWaitTokens externalWaitTokens;
    externalWaitTokens.tokens[static_cast<usize>(FrameExecutionExternalWait::LaggedLightingHistory)] =
        NWB::Core::QueueSubmissionToken{ CommandQueue::Compute, 71u }
    ;
    FrameExecutionPlanSubmissionState submissions(plan, externalWaitTokens);
    NWB::Core::QueueSubmissionToken waitTokens[FrameExecutionPlan::s_MaxSubmissionWaits] = {};
    NWB::Core::QueueSubmissionDesc submitDesc;

    // The plan's external edge must reject submission until RendererSystem provides an accepted history token.
    FrameExecutionPlanSubmissionState missingExternalWaitSubmissions(plan);
    EXPECT_TRUE(missingExternalWaitSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    missingExternalWaitSubmissions.acceptSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 1u }
    );
    EXPECT_TRUE(missingExternalWaitSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    missingExternalWaitSubmissions.acceptSubmission(
        FrameExecutionPacket::GraphicsEffects,
        NWB::Core::QueueSubmissionToken{ CommandQueue::Graphics, 2u }
    );
    EXPECT_FALSE(missingExternalWaitSubmissions.prepareSubmission(
        FrameExecutionPacket::DeferredLighting,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));

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
    const NWB::Core::QueueSubmissionToken compositeToken{ CommandQueue::Graphics, 55u };
    submissions.setExternalWaitToken(FrameExecutionExternalWait::DeferredComposite, compositeToken);
    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPresent,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 2u);
    EXPECT_EQ(waitTokens[0].value, 22u);
    EXPECT_EQ(waitTokens[1].value, 55u);

    const NWB::Core::QueueSubmissionToken presentToken{ CommandQueue::Graphics, 66u };
    submissions.acceptSubmission(FrameExecutionPacket::GraphicsPresent, presentToken);
    ASSERT_NE(submissions.asyncRecoveryWaitToken(), nullptr);
    EXPECT_EQ(submissions.asyncRecoveryWaitToken()->value, 22u);
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateUsesPublishedHistoryTokenAsNextFrameWait){
    // The graph-owned copy publishes this token after bootstrap presentation.  The remaining legacy plan consumes
    // only the external completion, not a retired history-copy packet.
    const FrameExecutionPlan bootstrapPlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        false,
        false,
        true,
    });
    FrameExecutionPlanSubmissionState bootstrapSubmissions(bootstrapPlan);
    NWB::Core::QueueSubmissionToken waitTokens[FrameExecutionPlan::s_MaxSubmissionWaits] = {};
    NWB::Core::QueueSubmissionDesc submitDesc;

    ASSERT_TRUE(bootstrapSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 0u);
    const NWB::Core::QueueSubmissionToken bootstrapPrefixToken{ CommandQueue::Graphics, 11u };
    bootstrapSubmissions.acceptSubmission(FrameExecutionPacket::GraphicsPrefix, bootstrapPrefixToken);

    ASSERT_TRUE(bootstrapSubmissions.prepareSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    ExpectSubmissionToken(waitTokens[0], bootstrapPrefixToken);
    const NWB::Core::QueueSubmissionToken bootstrapRayEffectsToken{ CommandQueue::Compute, 22u };
    bootstrapSubmissions.acceptSubmission(FrameExecutionPacket::AsyncRayEffects, bootstrapRayEffectsToken);

    ASSERT_TRUE(bootstrapSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    ExpectSubmissionToken(waitTokens[0], bootstrapPrefixToken);
    const NWB::Core::QueueSubmissionToken bootstrapEffectsToken{ CommandQueue::Graphics, 33u };
    bootstrapSubmissions.acceptSubmission(FrameExecutionPacket::GraphicsEffects, bootstrapEffectsToken);

    ASSERT_TRUE(bootstrapSubmissions.prepareSubmission(
        FrameExecutionPacket::DeferredLighting,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 2u);
    ExpectSubmissionToken(waitTokens[0], bootstrapEffectsToken);
    ExpectSubmissionToken(waitTokens[1], bootstrapRayEffectsToken);
    const NWB::Core::QueueSubmissionToken bootstrapLightingToken{ CommandQueue::Compute, 44u };
    bootstrapSubmissions.acceptSubmission(FrameExecutionPacket::DeferredLighting, bootstrapLightingToken);
    const NWB::Core::QueueSubmissionToken bootstrapCompositeToken{ CommandQueue::Compute, 55u };
    bootstrapSubmissions.setExternalWaitToken(
        FrameExecutionExternalWait::DeferredComposite,
        bootstrapCompositeToken
    );

    ASSERT_TRUE(bootstrapSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPresent,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    ExpectSubmissionToken(waitTokens[0], bootstrapCompositeToken);
    const NWB::Core::QueueSubmissionToken bootstrapPresentToken{ CommandQueue::Graphics, 66u };
    bootstrapSubmissions.acceptSubmission(FrameExecutionPacket::GraphicsPresent, bootstrapPresentToken);
    const NWB::Core::QueueSubmissionToken bootstrapHistoryToken{ CommandQueue::Compute, 77u };

    const FrameExecutionPlan activePlan(FrameExecutionPlanInput{
        true,
        true,
        true,
        true,
        false,
        true,
    });
    FrameExecutionExternalWaitTokens activeExternalWaitTokens;
    activeExternalWaitTokens.tokens[static_cast<usize>(FrameExecutionExternalWait::LaggedLightingHistory)] =
        bootstrapHistoryToken
    ;
    FrameExecutionPlanSubmissionState activeSubmissions(activePlan, activeExternalWaitTokens);

    ASSERT_TRUE(activeSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 0u);
    const NWB::Core::QueueSubmissionToken activePrefixToken{ CommandQueue::Graphics, 101u };
    activeSubmissions.acceptSubmission(FrameExecutionPacket::GraphicsPrefix, activePrefixToken);

    ASSERT_TRUE(activeSubmissions.prepareSubmission(
        FrameExecutionPacket::AsyncRayEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 1u);
    ExpectSubmissionToken(waitTokens[0], activePrefixToken);
    const NWB::Core::QueueSubmissionToken activeRayEffectsToken{ CommandQueue::Compute, 102u };
    activeSubmissions.acceptSubmission(FrameExecutionPacket::AsyncRayEffects, activeRayEffectsToken);

    ASSERT_TRUE(activeSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsEffects,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 2u);
    ExpectSubmissionToken(waitTokens[0], activePrefixToken);
    ExpectSubmissionToken(waitTokens[1], bootstrapHistoryToken);
    const NWB::Core::QueueSubmissionToken activeEffectsToken{ CommandQueue::Graphics, 103u };
    activeSubmissions.acceptSubmission(FrameExecutionPacket::GraphicsEffects, activeEffectsToken);

    EXPECT_EQ(activePlan.laneForWork(FrameExecutionWork::DeferredLighting), RenderLane::Graphics);
    ASSERT_TRUE(activeSubmissions.prepareSubmission(
        FrameExecutionPacket::DeferredLighting,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 2u);
    ExpectSubmissionToken(waitTokens[0], activeEffectsToken);
    ExpectSubmissionToken(waitTokens[1], bootstrapHistoryToken);
    EXPECT_NE(waitTokens[1].value, activeRayEffectsToken.value);
    const NWB::Core::QueueSubmissionToken activeLightingToken{ CommandQueue::Graphics, 104u };
    activeSubmissions.acceptSubmission(FrameExecutionPacket::DeferredLighting, activeLightingToken);
    const NWB::Core::QueueSubmissionToken activeCompositeToken{ CommandQueue::Graphics, 105u };
    activeSubmissions.setExternalWaitToken(
        FrameExecutionExternalWait::DeferredComposite,
        activeCompositeToken
    );

    ASSERT_TRUE(activeSubmissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPresent,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    ASSERT_EQ(submitDesc.waitTokenCount, 2u);
    ExpectSubmissionToken(waitTokens[0], activeRayEffectsToken);
    ExpectSubmissionToken(waitTokens[1], activeCompositeToken);
    const NWB::Core::QueueSubmissionToken activePresentToken{ CommandQueue::Graphics, 106u };
    activeSubmissions.acceptSubmission(FrameExecutionPacket::GraphicsPresent, activePresentToken);
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

