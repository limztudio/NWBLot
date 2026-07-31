// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_execution_plan.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_frame_execution_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FrameExecutionPlan = NWB::Impl::ECSRenderDetail::FrameExecutionPlan;
using FrameExecutionPlanInput = NWB::Impl::ECSRenderDetail::FrameExecutionPlanInput;
using FrameExecutionPacketCommandLists = NWB::Impl::ECSRenderDetail::FrameExecutionPacketCommandLists;
using FrameExecutionPlanSubmissionState = NWB::Impl::ECSRenderDetail::FrameExecutionPlanSubmissionState;
namespace FrameExecutionPacket = NWB::Impl::ECSRenderDetail::FrameExecutionPacket;
namespace FrameExecutionWork = NWB::Impl::ECSRenderDetail::FrameExecutionWork;
namespace RenderLane = NWB::Core::RenderLane;
namespace CommandQueue = NWB::Core::CommandQueue;


// The collector stores opaque command-list pointers and never dereferences them. Distinct non-null sentinels keep
// these plan-only tests independent from a graphics device.
[[nodiscard]] NWB::Core::CommandList* TestCommandList(const usize identity){
    return reinterpret_cast<NWB::Core::CommandList*>(identity);
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
    EXPECT_FALSE(plan.capturesLaggedLightingHistory());
    EXPECT_FALSE(plan.usesAsyncAvboit());
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
    EXPECT_FALSE(opaquePlan.capturesLaggedLightingHistory());
    EXPECT_FALSE(opaquePlan.usesAsyncAvboit());
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
    EXPECT_TRUE(bootstrapPlan.capturesLaggedLightingHistory());
    EXPECT_TRUE(bootstrapPlan.usesAsyncAvboit());
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
    EXPECT_TRUE(laggedPlan.capturesLaggedLightingHistory());
    EXPECT_FALSE(laggedPlan.usesAsyncAvboit());
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
    for(usize prefixListIndex = 0u; prefixListIndex < 5u; ++prefixListIndex){
        ASSERT_TRUE(commandLists.appendForWork(
            FrameExecutionWork::GraphicsPrefix,
            recordedLists[prefixListIndex]
        ));
    }
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::RayEffects, recordedLists[5u]));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::Caustics, recordedLists[6u]));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::SurfelGi, recordedLists[7u]));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AvboitRaster, recordedLists[8u]));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::DeferredLighting, recordedLists[9u]));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::DeferredComposite, recordedLists[10u]));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::GraphicsPresent, recordedLists[11u]));

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

    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::GraphicsPrefix, prefix));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::RayEffects, shadow));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::Caustics, caustics));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::SurfelGi, surfelGi));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AsyncEffectsTiming, timingBegin));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AvboitRaster, avboitRaster));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AsyncEffectsTiming, timingEnd));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AvboitDepthWarp, depthWarp));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AvboitExtinction, extinction));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AvboitIntegration, integration));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::AvboitAccumulation, accumulation));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::DeferredLighting, lighting));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::DeferredComposite, composite));
    ASSERT_TRUE(commandLists.appendForWork(FrameExecutionWork::GraphicsPresent, present));

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
    ASSERT_TRUE(laggedCommandLists.appendForWork(FrameExecutionWork::AsyncEffectsTiming, timingBegin));
    ASSERT_TRUE(laggedCommandLists.appendForWork(FrameExecutionWork::AvboitRaster, avboitRaster));
    ASSERT_TRUE(laggedCommandLists.appendForWork(FrameExecutionWork::AsyncEffectsTiming, timingEnd));
    EXPECT_FALSE(laggedCommandLists.appendForWork(FrameExecutionWork::AvboitDepthWarp, depthWarp));
    const auto graphicsEffectsLists = laggedCommandLists.commandLists(FrameExecutionPacket::GraphicsEffects);
    ASSERT_EQ(graphicsEffectsLists.commandListCount, 3u);
    EXPECT_EQ(graphicsEffectsLists.commandLists[0], timingBegin);
    EXPECT_EQ(graphicsEffectsLists.commandLists[1], avboitRaster);
    EXPECT_EQ(graphicsEffectsLists.commandLists[2], timingEnd);
}


TEST(EcsGraphics, FrameExecutionPlanSubmissionStateResolvesAcceptedDependenciesAndRecoveryToken){
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

    EXPECT_TRUE(submissions.prepareSubmission(
        FrameExecutionPacket::GraphicsPrefix,
        submitDesc,
        waitTokens,
        LengthOf(waitTokens)
    ));
    EXPECT_EQ(submitDesc.waitTokenCount, 0u);

    const NWB::Core::QueueSubmissionToken prefixToken{ CommandQueue::Graphics, 11u };
    submissions.acceptSubmission(FrameExecutionPacket::GraphicsPrefix, prefixToken);
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
    EXPECT_EQ(submissions.latestAcceptedToken(RenderLane::Graphics).value, 33u);
    EXPECT_EQ(submissions.latestAcceptedToken(RenderLane::AsyncCompute).value, 22u);

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
    EXPECT_EQ(submissions.latestAcceptedToken(RenderLane::AsyncCompute).value, 77u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
