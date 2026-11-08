// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/shadow_material_context_policy.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_material_context_policy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ShadowMaterialContextPolicy, RejectedAndCrossRouteUploadsDoNotAdmitStaleCache){
    const ShadowMaterialContextPolicy policy;
    u64 hardwareHash = 11u;
    u64 softwareHash = 22u;
    b8 hardwareValid = true;
    b8 softwareValid = true;
    EXPECT_FALSE(policy.publishAcceptedUpload(false, true, PreparedShadowMaterialContextRoute::Hardware, 33u, true, hardwareHash, hardwareValid, softwareHash, softwareValid));
    EXPECT_EQ(hardwareHash, 11u);
    EXPECT_EQ(softwareHash, 22u);
    EXPECT_TRUE(hardwareValid);
    EXPECT_TRUE(softwareValid);
    EXPECT_TRUE(policy.publishAcceptedUpload(true, true, PreparedShadowMaterialContextRoute::Hardware, 33u, true, hardwareHash, hardwareValid, softwareHash, softwareValid));
    EXPECT_EQ(hardwareHash, 33u);
    EXPECT_TRUE(hardwareValid);
    EXPECT_FALSE(softwareValid);
    EXPECT_FALSE(policy.softwareSnapshotCovers(true, PreparedShadowMaterialContextRoute::Hardware, 2u, 2u, 2u));
    EXPECT_FALSE(policy.softwareSnapshotCovers(true, PreparedShadowMaterialContextRoute::Software, 2u, 1u, 2u));
    EXPECT_FALSE(policy.softwareSnapshotAccepted(false, true, false, 22u, 22u, 2u, 2u));
    EXPECT_FALSE(policy.softwareSnapshotAccepted(false, true, true, 22u, 23u, 2u, 2u));
    EXPECT_FALSE(policy.softwareSnapshotAccepted(false, true, true, 22u, 22u, 2u, 3u));
    EXPECT_TRUE(policy.softwareSnapshotAccepted(false, true, true, 22u, 22u, 2u, 2u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

