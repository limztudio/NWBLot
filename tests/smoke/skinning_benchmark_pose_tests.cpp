// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/smoke/smoke_test_cases.adhd.h>
#include <tests/common/ecs_test_world.h>

#include <impl/ecs_skeleton/components.adhd.h>

#include <core/ecs/entity.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_benchmark_pose_tests{


using namespace NWB;


static void AnimateEntities(Core::ECS::World& world, const Span<const Core::ECS::EntityID> entities, const Span<const Impl::SkeletonJointMatrix> bindJoints, const f32 timeSeconds, const b8 staticPreview)noexcept{
    for(usize entityIndex = 0u; entityIndex < entities.size(); ++entityIndex){
        auto* pose = world.tryGetComponent<Impl::SkeletonPoseComponent>(entities[entityIndex]);
        if(!pose)
            continue;
        Tests::Smoke::AnimateSkinningBenchmarkPoseJoints(bindJoints, static_cast<u32>(entityIndex), timeSeconds, staticPreview, pose->localJoints);
    }
}

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
    AnimateEntities(world, entities, bindJoints, 0.25f, true);
    Tests::Smoke::VerifySkinningBenchmarkPoseStep1(bindJoints, pose.localJoints, firstPose);
    AnimateEntities(world, entities, bindJoints, 0.25f, true);
    Tests::Smoke::VerifySkinningBenchmarkPoseStep2(firstPose, pose.localJoints);
    AnimateEntities(world, entities, bindJoints, 1.0f, true);
    Tests::Smoke::VerifySkinningBenchmarkPoseStep3(firstPose, pose.localJoints);
    AnimateEntities(world, entities, bindJoints, 0.25f, false);
    Tests::Smoke::VerifySkinningBenchmarkPoseStep4(bindJoints, pose.localJoints);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
