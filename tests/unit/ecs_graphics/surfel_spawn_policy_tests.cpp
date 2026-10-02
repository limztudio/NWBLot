// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_surfel_spawn_policy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compile and exercise the same predicate used by the spawn shader. The hash/list test must additionally cover
// atomic publication on GPU; these cases pin the geometric decision before any allocation is attempted.
#include <impl/assets/graphics/gi/surfel/surfel_spawn_policy.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr float s_Radius = 0.9f;
constexpr float s_NormalBias = 0.05f;


TEST(SurfelSpawnPolicy, NearbyParallelSurfaceSharesAnExistingSurfel){
    EXPECT_TRUE(nwbSurfelSpawnCoversCandidate(true, 1.0f, 0.1f * 0.1f, 0.01f, s_Radius, s_NormalBias));
    EXPECT_TRUE(nwbSurfelSpawnCoversCandidate(true, 0.95f, 0.2f * 0.2f, 0.04f, s_Radius, s_NormalBias));
}

TEST(SurfelSpawnPolicy, PerpendicularSurfaceInTheSameCellNeedsItsOwnSurfel){
    EXPECT_FALSE(nwbSurfelSpawnCoversCandidate(true, 0.0f, 0.1f * 0.1f, 0.0f, s_Radius, s_NormalBias));
    EXPECT_FALSE(nwbSurfelSpawnCoversCandidate(true, 0.8f, 0.1f * 0.1f, 0.0f, s_Radius, s_NormalBias));
}

TEST(SurfelSpawnPolicy, DifferentCellsSharingAHashBucketDoNotDeduplicate){
    EXPECT_FALSE(nwbSurfelSpawnCoversCandidate(false, 1.0f, 0.0f, 0.0f, s_Radius, s_NormalBias));
}

TEST(SurfelSpawnPolicy, DistantOrOffsetParallelSurfaceNeedsItsOwnSurfel){
    EXPECT_FALSE(nwbSurfelSpawnCoversCandidate(true, 1.0f, 0.8f * 0.8f, 0.0f, s_Radius, s_NormalBias));
    EXPECT_FALSE(nwbSurfelSpawnCoversCandidate(true, 1.0f, 0.1f * 0.1f, 0.08f, s_Radius, s_NormalBias));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

