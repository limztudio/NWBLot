// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/frame_execution_plan.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_frame_execution_plan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FrameExecutionPlan = NWB::Impl::ECSRenderDetail::FrameExecutionPlan;
using FrameExecutionPlanInput = NWB::Impl::ECSRenderDetail::FrameExecutionPlanInput;
using FrameExecutionPlanSubmissionState = NWB::Impl::ECSRenderDetail::FrameExecutionPlanSubmissionState;
namespace FrameExecutionPacket = NWB::Impl::ECSRenderDetail::FrameExecutionPacket;
namespace FrameExecutionTimingTicket = NWB::Impl::ECSRenderDetail::FrameExecutionTimingTicket;
namespace FrameExecutionWork = NWB::Impl::ECSRenderDetail::FrameExecutionWork;
namespace RenderLane = NWB::Core::RenderLane;
namespace CommandQueue = NWB::Core::CommandQueue;


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
    EXPECT_EQ(
        fallbackPlan.packet(FrameExecutionPacket::GraphicsFallback).timingTicket,
        FrameExecutionTimingTicket::Frame
    );
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
    EXPECT_EQ(
        splitPlan.packet(FrameExecutionPacket::GraphicsPrefix).timingTicket,
        FrameExecutionTimingTicket::Prefix
    );
    EXPECT_EQ(
        splitPlan.packet(FrameExecutionPacket::AsyncRayEffects).timingTicket,
        FrameExecutionTimingTicket::RayEffects
    );
    EXPECT_EQ(
        splitPlan.packet(FrameExecutionPacket::GraphicsAvboitPre).timingTicket,
        FrameExecutionTimingTicket::AvboitPre
    );
    EXPECT_EQ(
        splitPlan.packet(FrameExecutionPacket::AsyncAvboitIntegration).timingTicket,
        FrameExecutionTimingTicket::AvboitIntegration
    );
    EXPECT_EQ(
        splitPlan.packet(FrameExecutionPacket::DeferredLighting).timingTicket,
        FrameExecutionTimingTicket::DeferredLighting
    );
    EXPECT_EQ(
        splitPlan.packet(FrameExecutionPacket::AsyncLaggedLightingStash).timingTicket,
        FrameExecutionTimingTicket::None
    );

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
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::GraphicsEffects).timingTicket,
        FrameExecutionTimingTicket::Effects
    );
    EXPECT_EQ(
        laggedPlan.packet(FrameExecutionPacket::DeferredLighting).timingTicket,
        FrameExecutionTimingTicket::DeferredLighting
    );
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
