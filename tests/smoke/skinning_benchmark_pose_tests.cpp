// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinning_benchmark_pose_animator.h"

#include <tests/common/ecs_test_world.h>

#include <core/ecs/entity.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_benchmark_pose_tests{


using namespace NWB;


TEST(SkinningBenchmarkPose, MissingPoseIsSkippedAndSeededJointsAreDeterministic){
    Tests::EcsTestWorld context;
    auto& world = context.world;
    const auto skeleton = world.createEntity().id();
    const auto missingPose = world.createEntity().id();
    auto& pose = world.entity(skeleton).addComponent<Impl::SkeletonPoseComponent>();
    Vector<Impl::SkeletonJointMatrix, Core::Alloc::GlobalArena> bindJoints(context.arena);
    Vector<Impl::SkeletonJointMatrix, Core::Alloc::GlobalArena> firstPose(context.arena);
    for(u32 i = 0u; i < 64u; ++i){
        bindJoints.push_back(MatrixIdentity<Float34>());
        pose.localJoints.push_back(bindJoints.back());
    }
    const Array<Core::ECS::EntityID, 2> entities = { missingPose, skeleton };
    Tests::Smoke::SkinningBenchmarkPoseAnimator::Animate(world, entities, bindJoints, 0.25f, true);
    EXPECT_EQ(NWB_MEMCMP(&pose.localJoints[0u], &bindJoints[0u], sizeof(bindJoints[0u])), 0);
    u32 changedJoints = 0u;
    for(usize i = 0u; i < bindJoints.size(); ++i){
        firstPose.push_back(pose.localJoints[i]);
        if(NWB_MEMCMP(&pose.localJoints[i], &bindJoints[i], sizeof(bindJoints[i])) != 0)
            ++changedJoints;
    }
    EXPECT_GT(changedJoints, 0u);
    Tests::Smoke::SkinningBenchmarkPoseAnimator::Animate(world, entities, bindJoints, 0.25f, true);
    for(usize i = 0u; i < firstPose.size(); ++i)
        EXPECT_EQ(NWB_MEMCMP(&pose.localJoints[i], &firstPose[i], sizeof(firstPose[i])), 0);
    Tests::Smoke::SkinningBenchmarkPoseAnimator::Animate(world, entities, bindJoints, 1.0f, true);
    b8 changedWithTime = false;
    for(usize i = 1u; i < firstPose.size(); ++i)
        changedWithTime |= NWB_MEMCMP(&pose.localJoints[i], &firstPose[i], sizeof(firstPose[i])) != 0;
    EXPECT_TRUE(changedWithTime);
    Tests::Smoke::SkinningBenchmarkPoseAnimator::Animate(world, entities, bindJoints, 0.25f, false);
    EXPECT_EQ(NWB_MEMCMP(&pose.localJoints[0u], &bindJoints[0u], sizeof(bindJoints[0u])), 0);
    b8 changedInBenchmarkMode = false;
    for(usize i = 1u; i < bindJoints.size(); ++i)
        changedInBenchmarkMode |= NWB_MEMCMP(&pose.localJoints[i], &bindJoints[i], sizeof(bindJoints[i])) != 0;
    EXPECT_TRUE(changedInBenchmarkMode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

