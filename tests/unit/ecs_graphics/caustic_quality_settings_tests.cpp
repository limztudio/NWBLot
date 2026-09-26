// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/caustic/settings.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustic_quality_settings_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(CausticQualitySettings, FullQualityIsTheDefaultAndOnlyExplicitDivisorsAreAccepted){
    CausticQualitySettings settings;
    EXPECT_EQ(settings.photonGridDivisor, 1u);
    EXPECT_TRUE(ValidateCausticQualitySettings(settings));
    for(const u32 divisor : { 0u, 3u, 5u, 8u, 65536u, Limit<u32>::s_Max }){
        settings.photonGridDivisor = divisor;
        EXPECT_FALSE(ValidateCausticQualitySettings(settings));
    }
}

TEST(CausticQualitySettings, BothBaseGridsKeepEveryTemporalPhaseAndExactDispatchCoverage){
    for(const u32 baseGrid : { 128u, 512u }){
        for(const u32 divisor : { 1u, 2u, 4u }){
            const CausticQualitySettings settings{ divisor };
            ASSERT_TRUE(ValidateCausticQualitySettings(settings));
            for(const u32 phases : { 1u, 2u, 4u }){
                const CausticPhotonBudget budget = MakeCausticPhotonBudget(settings, baseGrid, phases);
                EXPECT_GE(budget.gridSide, 32u);
                EXPECT_EQ(budget.gridSide * divisor, baseGrid);
                EXPECT_EQ(budget.gridSide % phases, 0u);
                EXPECT_EQ(budget.fullGridCount * divisor * divisor, baseGrid * baseGrid);
                EXPECT_EQ(budget.photonsPerFrame * phases, budget.fullGridCount);
                // DispatchRays is a rectangle; compute dispatch rounds to 64 lanes and guards excess lanes.
                EXPECT_EQ(budget.gridSide * (budget.gridSide / phases), budget.photonsPerFrame);
                const u32 groups = (budget.photonsPerFrame + 63u) / 64u;
                EXPECT_GE(groups * 64u, budget.photonsPerFrame);
                EXPECT_LT(groups * 64u - budget.photonsPerFrame, 64u);
                EXPECT_GT(budget.photonsPerFrame, 0u);
            }
        }
    }
}

TEST(CausticQualitySettings, BootstrapConvergedAndUnreusedPhotonCountsScaleTogether){
    const u32 expectedFullGrid[] = { 262144u, 65536u, 16384u };
    const u32 divisors[] = { 1u, 2u, 4u };
    for(u32 index = 0u; index < 3u; ++index){
        const CausticQualitySettings settings{ divisors[index] };
        EXPECT_EQ(MakeCausticPhotonBudget(settings, 512u, 1u).photonsPerFrame, expectedFullGrid[index]);
        EXPECT_EQ(MakeCausticPhotonBudget(settings, 512u, 2u).photonsPerFrame, expectedFullGrid[index] / 2u);
        EXPECT_EQ(MakeCausticPhotonBudget(settings, 512u, 4u).photonsPerFrame, expectedFullGrid[index] / 4u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

