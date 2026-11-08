// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/raytracing_scene_identity_builder.adhd.h>
#include <impl/ecs_render/raytrace/raytracing_build_policy.adhd.h>
#include <impl/ecs_render/raytrace/refractive_bounds_isolation.adhd.h>
#include <impl/ecs_render/raytrace/scene_bvh_upload_policy.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_raytracing_scene_identity_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RayTracingSceneIdentityTests, UntrustedScenesNeverPublishTrustedContentAndAbsentCsgIsIgnored){
    const RayTracingSceneIdentityBuilder builder;
    const RayTracingSceneContentStamp complete = builder.buildSoftwareContentStamp(7u, 11u, 13u, false, 17u, true, true);
    const RayTracingSceneContentStamp ignored = builder.buildSoftwareContentStamp(7u, 11u, 13u, false, 19u, true, true);
    const RayTracingSceneContentStamp incomplete = builder.buildSoftwareContentStamp(7u, 11u, 13u, false, 17u, true, false);
    const RayTracingSceneContentStamp dynamic = builder.buildSoftwareContentStamp(7u, 11u, 13u, false, 17u, false, true);
    const RayTracingSceneContentStamp csg = builder.buildSoftwareContentStamp(7u, 11u, 13u, true, 17u, true, true);

    EXPECT_TRUE(complete.trusted);
    EXPECT_EQ(complete.geometry, 7u);
    EXPECT_EQ(complete.material, ignored.material);
    EXPECT_FALSE(incomplete.trusted);
    EXPECT_FALSE(dynamic.trusted);
    EXPECT_NE(complete.material, csg.material);
}


TEST(SceneBvhUploadPolicy, StaleReuseAndUnacceptedUploadCannotPublishCacheIdentity){
    const SceneBvhUploadPolicy policy;
    u64 acceptedHash = 41u;
    b8 acceptedValid = true;
    EXPECT_FALSE(policy.publishAcceptedUpload(false, true, true, 99u, acceptedHash, acceptedValid));
    EXPECT_EQ(acceptedHash, 41u);
    EXPECT_TRUE(acceptedValid);
    EXPECT_FALSE(policy.publishAcceptedUpload(true, false, true, 99u, acceptedHash, acceptedValid));
    EXPECT_EQ(acceptedHash, 41u);
    EXPECT_TRUE(acceptedValid);
    EXPECT_FALSE(policy.snapshotAccepted(false, true, false, 41u, 41u, 3u, 3u));
    EXPECT_FALSE(policy.snapshotAccepted(false, true, true, 41u, 42u, 3u, 3u));
    EXPECT_FALSE(policy.snapshotAccepted(false, true, true, 41u, 41u, 3u, 4u));
    EXPECT_FALSE(policy.snapshotAccepted(false, false, true, 41u, 41u, 3u, 3u));
    EXPECT_TRUE(policy.snapshotAccepted(false, true, true, 41u, 41u, 3u, 3u));
    EXPECT_TRUE(policy.snapshotAccepted(true, false, false, 41u, 42u, 3u, 4u));
    EXPECT_TRUE(policy.publishAcceptedUpload(true, true, false, 99u, acceptedHash, acceptedValid));
    EXPECT_EQ(acceptedHash, 41u);
    EXPECT_FALSE(acceptedValid);
}


TEST(RayTracingBuildPolicy, StaleTlasCompletionCannotPublishTheOldSceneOrClearFreshBacking){
    const RayTracingBuildPolicy policy;
    u64 cachedHash = 41u;
    b8 cacheValid = true;
    b8 backingFresh = true;
    policy.publishConfirmedSceneTlasBuild(false, true, 99u, cachedHash, cacheValid, backingFresh);
    EXPECT_EQ(cachedHash, 41u);
    EXPECT_FALSE(cacheValid);
    EXPECT_TRUE(backingFresh);
    policy.publishConfirmedSceneTlasBuild(true, true, 99u, cachedHash, cacheValid, backingFresh);
    EXPECT_EQ(cachedHash, 99u);
    EXPECT_TRUE(cacheValid);
    EXPECT_FALSE(backingFresh);
    policy.publishConfirmedSceneTlasBuild(true, false, 111u, cachedHash, cacheValid, backingFresh);
    EXPECT_EQ(cachedHash, 99u);
    EXPECT_FALSE(cacheValid);
    EXPECT_FALSE(backingFresh);
}


TEST(RefractiveBoundsIsolation, TouchingTheCertifiedMarginIsNotIsolated){
    const RefractiveBoundsIsolation policy;
    Array<RayTracingInstanceWorldBounds, 2> bounds{};
    bounds[0].minimum = Float3U(0.0f, 0.0f, 0.0f);
    bounds[0].maximum = Float3U(1.0f, 1.0f, 1.0f);
    bounds[1].minimum = Float3U(1.0f + s_RefractionCertifiedBoundsMargin, 0.0f, 0.0f);
    bounds[1].maximum = Float3U(2.0f, 1.0f, 1.0f);
    EXPECT_FALSE(policy.isIsolated(bounds, 0u));
    EXPECT_FALSE(policy.isIsolated(bounds, 2u));
    bounds[1].minimum.x = 1.0f + s_RefractionCertifiedBoundsMargin * 2.0f;
    EXPECT_TRUE(policy.isIsolated(bounds, 0u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

