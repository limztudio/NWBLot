// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/task_graph_schedule.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_schedule_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GpuTaskGraphFrameSchedule = NWB::Impl::ECSRenderDetail::GpuTaskGraphFrameSchedule;
using GpuTaskGraphFrameScheduleInput = NWB::Impl::ECSRenderDetail::GpuTaskGraphFrameScheduleInput;


TEST(EcsGraphics, GpuTaskGraphScheduleDistinguishesLaggedLightingAvailability){
    const GpuTaskGraphFrameSchedule bootstrap(GpuTaskGraphFrameScheduleInput{
        true, true, true, false, true, true,
    });
    EXPECT_TRUE(bootstrap.usesDedicatedAsyncCompute());
    EXPECT_TRUE(bootstrap.capturesLaggedLightingHistory());
    EXPECT_FALSE(bootstrap.usesLaggedLightingHistory());
    EXPECT_TRUE(bootstrap.usesAsyncAvboit());

    const GpuTaskGraphFrameSchedule active(GpuTaskGraphFrameScheduleInput{
        true, true, true, true, true, true,
    });
    EXPECT_TRUE(active.capturesLaggedLightingHistory());
    EXPECT_TRUE(active.usesLaggedLightingHistory());
    EXPECT_FALSE(active.usesAsyncAvboit());
}


TEST(EcsGraphics, GpuTaskGraphScheduleRestrictsAsyncRoutesToDedicatedCompute){
    const GpuTaskGraphFrameSchedule graphicsOnly(GpuTaskGraphFrameScheduleInput{
        false, true, true, true, true, false,
    });
    EXPECT_FALSE(graphicsOnly.usesDedicatedAsyncCompute());
    EXPECT_FALSE(graphicsOnly.capturesLaggedLightingHistory());
    EXPECT_FALSE(graphicsOnly.usesLaggedLightingHistory());
    EXPECT_FALSE(graphicsOnly.usesAsyncAvboit());

    const GpuTaskGraphFrameSchedule transparentDedicated(GpuTaskGraphFrameScheduleInput{
        true, false, false, false, true, false,
    });
    EXPECT_TRUE(transparentDedicated.usesDedicatedAsyncCompute());
    EXPECT_FALSE(transparentDedicated.capturesLaggedLightingHistory());
    EXPECT_TRUE(transparentDedicated.usesAsyncAvboit());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
