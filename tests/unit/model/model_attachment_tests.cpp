// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model_test_context.h"

#include <impl/ecs_skeleton/components.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_attachment_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::ModelTests;


[[nodiscard]] static SkeletonJointMatrix Translation(const f32 x){
    SkeletonJointMatrix matrix = Float34Identity();
    matrix._14 = x;
    return matrix;
}

[[nodiscard]] static Core::ECS::EntityID MakeSkeleton(
    RuntimeContext& context,
    const Core::ECS::EntityID owner,
    const usize jointCount,
    const f32 jointTranslation = 1.0f,
    const f32 objectTranslation = 0.0f){
    auto& world = context.testWorld.world;
    const auto skeleton = context.makeObject(owner);
    auto& object = world.entity(skeleton).getComponent<ModelObjectComponent>();
    object.kind = ModelObjectKind::Skeleton;
    object.localTransform = Translation(objectTranslation);
    auto& pose = world.entity(skeleton).addComponent<SkeletonPoseComponent>(context.testWorld.arena);
    pose.parentJoints.reserve(jointCount);
    pose.localJoints.reserve(jointCount);
    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        pose.parentJoints.push_back(jointIndex == 0u ? s_SkeletonRootParent : static_cast<u32>(jointIndex - 1u));
        pose.localJoints.push_back(Translation(jointTranslation));
    }
    return skeleton;
}

[[nodiscard]] static Core::ECS::EntityID MakeAttachment(
    RuntimeContext& context,
    const Core::ECS::EntityID owner,
    const Core::ECS::EntityID parent,
    const u32 jointIndex,
    const f32 localTranslation = 0.0f){
    const auto entity = context.makeObject(owner);
    auto& attachment = context.testWorld.world.entity(entity).addComponent<ModelStaticMeshAttachmentComponent>();
    attachment.parentEntity = parent;
    attachment.parentJointIndex = jointIndex;
    attachment.localTransform = Translation(localTranslation);
    return entity;
}

TEST(ModelAttachment, AppliesOwnerObjectAndJointTransformsAcrossSharedParents){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    world.entity(owner).getComponent<Scene::TransformComponent>().position.x = 10.0f;
    const auto firstParent = MakeSkeleton(context, owner, 4u, 1.0f, 5.0f);
    const auto secondParent = MakeSkeleton(context, owner, 4u, 2.0f, 15.0f);
    const auto unattached = MakeAttachment(context, owner, Core::ECS::ENTITY_ID_INVALID, Limit<u32>::s_Max, 2.0f);
    const auto objectParent = MakeAttachment(context, owner, firstParent, Limit<u32>::s_Max, 2.0f);
    const auto firstJoint = MakeAttachment(context, owner, firstParent, 2u, 2.0f);
    const auto secondJoint = MakeAttachment(context, owner, firstParent, 0u, 3.0f);
    const auto differentParent = MakeAttachment(context, owner, secondParent, 1u, 1.0f);

    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(unattached).getComponent<Scene::TransformComponent>().position.x, 12.0f);
    EXPECT_FLOAT_EQ(world.entity(objectParent).getComponent<Scene::TransformComponent>().position.x, 17.0f);
    EXPECT_FLOAT_EQ(world.entity(firstJoint).getComponent<Scene::TransformComponent>().position.x, 20.0f);
    EXPECT_FLOAT_EQ(world.entity(secondJoint).getComponent<Scene::TransformComponent>().position.x, 19.0f);
    EXPECT_FLOAT_EQ(world.entity(differentParent).getComponent<Scene::TransformComponent>().position.x, 30.0f);
}

TEST(ModelAttachment, RevalidatesPoseHierarchyAndParentBindingOnEveryUpdate){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    const auto parent = MakeSkeleton(context, owner, 2u, 1.0f, 5.0f);
    const auto replacement = MakeSkeleton(context, owner, 2u, 4.0f, 10.0f);
    const auto attached = MakeAttachment(context, owner, parent, 1u, 2.0f);
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 9.0f);

    world.entity(parent).getComponent<SkeletonPoseComponent>().localJoints[1u] = Translation(3.0f);
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 11.0f);
    world.entity(parent).getComponent<SkeletonPoseComponent>().parentJoints[1u] = s_SkeletonRootParent;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 10.0f);
    world.entity(attached).getComponent<ModelStaticMeshAttachmentComponent>().parentEntity = replacement;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 20.0f);

    world.entity(replacement).removeComponent<SkeletonPoseComponent>();
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 20.0f);
    auto& restored = world.entity(replacement).addComponent<SkeletonPoseComponent>(context.testWorld.arena);
    restored.parentJoints = { s_SkeletonRootParent, 0u };
    restored.localJoints = { Translation(2.0f), Translation(3.0f) };
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 17.0f);
}

TEST(ModelAttachment, InvalidUnrequestedJointsRejectWholePoseAndRecover){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    const auto parent = MakeSkeleton(context, owner, 2u);
    const auto attached = MakeAttachment(context, owner, parent, 0u, 2.0f);
    world.entity(attached).getComponent<Scene::TransformComponent>().position.x = 123.0f;
    world.entity(parent).getComponent<SkeletonPoseComponent>().localJoints[1u]._11 = 0.0f;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 123.0f);
    world.entity(parent).getComponent<SkeletonPoseComponent>().localJoints[1u] = Translation(1.0f);
    world.entity(parent).getComponent<SkeletonPoseComponent>().parentJoints[1u] = 1u;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 123.0f);
    world.entity(parent).getComponent<SkeletonPoseComponent>().parentJoints[1u] = 0u;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 3.0f);
}

TEST(ModelAttachment, PreservesOriginalAttachmentTransformWriteOrder){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    world.entity(owner).getComponent<Scene::TransformComponent>().position.x = 10.0f;
    const auto first = MakeAttachment(context, owner, Core::ECS::ENTITY_ID_INVALID, Limit<u32>::s_Max, 2.0f);
    const auto second = MakeAttachment(context, owner, first, Limit<u32>::s_Max, 3.0f);
    auto& pose = world.entity(first).addComponent<SkeletonPoseComponent>(context.testWorld.arena);
    pose.parentJoints = { s_SkeletonRootParent };
    pose.localJoints = { Translation(1.0f) };
    const auto jointFirst = MakeAttachment(context, owner, first, 0u, 4.0f);
    const auto jointSecond = MakeAttachment(context, owner, first, 0u, 5.0f);
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(first).getComponent<Scene::TransformComponent>().position.x, 12.0f);
    EXPECT_FLOAT_EQ(world.entity(second).getComponent<Scene::TransformComponent>().position.x, 15.0f);
    EXPECT_FLOAT_EQ(world.entity(jointFirst).getComponent<Scene::TransformComponent>().position.x, 17.0f);
    EXPECT_FLOAT_EQ(world.entity(jointSecond).getComponent<Scene::TransformComponent>().position.x, 18.0f);
    world.entity(first).getComponent<ModelStaticMeshAttachmentComponent>().localTransform = Translation(7.0f);
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(second).getComponent<Scene::TransformComponent>().position.x, 20.0f);
    EXPECT_FLOAT_EQ(world.entity(jointFirst).getComponent<Scene::TransformComponent>().position.x, 22.0f);
    EXPECT_FLOAT_EQ(world.entity(jointSecond).getComponent<Scene::TransformComponent>().position.x, 23.0f);
}

TEST(ModelAttachment, GroupedQueriesRejectShrunkAndEmptyPalettesAndRecover){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    const auto parent = MakeSkeleton(context, owner, 2u);
    const auto firstJoint = MakeAttachment(context, owner, parent, 0u);
    const auto secondJoint = MakeAttachment(context, owner, parent, 1u);
    context.system.update(world, 0.0f);
    world.entity(secondJoint).getComponent<Scene::TransformComponent>().position.x = 123.0f;
    auto& pose = world.entity(parent).getComponent<SkeletonPoseComponent>();
    pose.parentJoints.resize(1u);
    pose.localJoints.erase(pose.localJoints.begin() + 1, pose.localJoints.end());
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(firstJoint).getComponent<Scene::TransformComponent>().position.x, 1.0f);
    EXPECT_FLOAT_EQ(world.entity(secondJoint).getComponent<Scene::TransformComponent>().position.x, 123.0f);

    pose.parentJoints.clear();
    pose.localJoints.clear();
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(firstJoint).getComponent<Scene::TransformComponent>().position.x, 1.0f);
    EXPECT_FLOAT_EQ(world.entity(secondJoint).getComponent<Scene::TransformComponent>().position.x, 123.0f);
    pose.parentJoints = { s_SkeletonRootParent, 0u };
    pose.localJoints = { Translation(2.0f), Translation(3.0f) };
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(firstJoint).getComponent<Scene::TransformComponent>().position.x, 2.0f);
    EXPECT_FLOAT_EQ(world.entity(secondJoint).getComponent<Scene::TransformComponent>().position.x, 5.0f);
    pose.skinningMode = Limit<u32>::s_Max;
    pose.localJoints[0u] = Translation(10.0f);
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(firstJoint).getComponent<Scene::TransformComponent>().position.x, 2.0f);
    EXPECT_FLOAT_EQ(world.entity(secondJoint).getComponent<Scene::TransformComponent>().position.x, 5.0f);
    pose.skinningMode = SkeletonSkinningMode::LinearBlend;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(firstJoint).getComponent<Scene::TransformComponent>().position.x, 10.0f);
    EXPECT_FLOAT_EQ(world.entity(secondJoint).getComponent<Scene::TransformComponent>().position.x, 13.0f);
}

TEST(ModelAttachment, SingleQueryRejectsMissingJointBoundsAndRetainsReusableCapacity){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    const auto parent = MakeSkeleton(context, owner, 2u);
    const auto attached = MakeAttachment(context, owner, parent, 1u);
    context.system.update(world, 0.0f);
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    for(usize iteration = 0u; iteration < 32u; ++iteration)
        context.system.update(world, 0.0f);
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    auto& pose = world.entity(parent).getComponent<SkeletonPoseComponent>();
    pose.parentJoints.resize(1u);
    pose.localJoints.erase(pose.localJoints.begin() + 1, pose.localJoints.end());
    world.entity(attached).getComponent<Scene::TransformComponent>().position.x = 123.0f;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 123.0f);
    pose.parentJoints.clear();
    pose.localJoints.clear();
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 123.0f);
    pose.parentJoints = { s_SkeletonRootParent, 0u };
    pose.localJoints = { Translation(2.0f), Translation(3.0f) };
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, 5.0f);
}

TEST(ModelAttachment, InterleavedParentsKeepIndependentQueriesAndEntityGenerations){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    const auto firstParent = MakeSkeleton(context, owner, 2u, 1.0f);
    const auto secondParent = MakeSkeleton(context, owner, 2u, 3.0f);
    const auto first = MakeAttachment(context, owner, firstParent, 1u);
    const auto second = MakeAttachment(context, owner, secondParent, 0u);
    const auto third = MakeAttachment(context, owner, firstParent, 0u);
    const auto fourth = MakeAttachment(context, owner, secondParent, 1u);
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(first).getComponent<Scene::TransformComponent>().position.x, 2.0f);
    EXPECT_FLOAT_EQ(world.entity(second).getComponent<Scene::TransformComponent>().position.x, 3.0f);
    EXPECT_FLOAT_EQ(world.entity(third).getComponent<Scene::TransformComponent>().position.x, 1.0f);
    EXPECT_FLOAT_EQ(world.entity(fourth).getComponent<Scene::TransformComponent>().position.x, 6.0f);

    world.destroyEntity(firstParent);
    const auto replacement = MakeSkeleton(context, owner, 2u, 5.0f);
    ASSERT_EQ(replacement.index(), firstParent.index());
    ASSERT_NE(replacement.generation(), firstParent.generation());
    world.entity(third).getComponent<ModelStaticMeshAttachmentComponent>().parentEntity = replacement;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(first).getComponent<Scene::TransformComponent>().position.x, 2.0f);
    EXPECT_FLOAT_EQ(world.entity(second).getComponent<Scene::TransformComponent>().position.x, 3.0f);
    EXPECT_FLOAT_EQ(world.entity(third).getComponent<Scene::TransformComponent>().position.x, 5.0f);
    EXPECT_FLOAT_EQ(world.entity(fourth).getComponent<Scene::TransformComponent>().position.x, 6.0f);
}

static void BenchmarkAttachments(const usize parentCount, const usize attachmentCount, const usize jointCount, const usize iterations){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena> parents(context.testWorld.arena);
    Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena> attachedObjects(context.testWorld.arena);
    parents.reserve(parentCount);
    attachedObjects.reserve(attachmentCount);
    for(usize parentIndex = 0u; parentIndex < parentCount; ++parentIndex)
        parents.push_back(MakeSkeleton(context, owner, jointCount));
    for(usize attachmentIndex = 0u; attachmentIndex < attachmentCount; ++attachmentIndex){
        attachedObjects.push_back(MakeAttachment(
            context, owner, parents[attachmentIndex % parentCount], static_cast<u32>(jointCount - 1u)
        ));
    }
    context.system.update(world, 0.0f);
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < iterations; ++iteration)
        context.system.update(world, 0.0f);
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    for(const auto attached : attachedObjects)
        EXPECT_FLOAT_EQ(world.entity(attached).getComponent<Scene::TransformComponent>().position.x, static_cast<f32>(jointCount));

    char elapsedText[32u] = {};
    char iterationsText[32u] = {};
    char parentsText[32u] = {};
    char attachmentsText[32u] = {};
    char jointsText[32u] = {};
    char allocationsText[32u] = {};
    testing::Test::RecordProperty("model_attachment_ns", FormatDecimal(elapsed, elapsedText).data());
    testing::Test::RecordProperty("model_attachment_iterations", FormatDecimal(iterations, iterationsText).data());
    testing::Test::RecordProperty("model_attachment_parent_count", FormatDecimal(parentCount, parentsText).data());
    testing::Test::RecordProperty("model_attachment_count", FormatDecimal(attachmentCount, attachmentsText).data());
    testing::Test::RecordProperty("model_attachment_joint_count", FormatDecimal(jointCount, jointsText).data());
    testing::Test::RecordProperty(
        "model_attachment_backing_allocations", FormatDecimal(after.allocationCount - before.allocationCount, allocationsText).data()
    );
}

// Opt in with --gtest_also_run_disabled_tests and a ModelAttachmentBenchmark.* filter.
TEST(ModelAttachmentBenchmark, DISABLED_SharedSkeletonAttachments){
    BenchmarkAttachments(1u, 1024u, 128u, 4u);
}

TEST(ModelAttachmentBenchmark, DISABLED_DistinctSkeletonAttachments){
    BenchmarkAttachments(256u, 256u, 32u, 4u);
}

TEST(ModelAttachmentBenchmark, DISABLED_SingleSkeletonAttachment){
    BenchmarkAttachments(1u, 1u, 16u, 256u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

