// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model_test_context.h"

#include <impl/ecs_skeleton/components.h>

#include <global/arena_memory.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_attachment_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


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
    const f32 objectTranslation = 0.0f
){
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
    const f32 localTranslation = 0.0f
){
    const auto entity = context.makeObject(owner);
    auto& attachment = context.testWorld.world.entity(entity).addComponent<ModelStaticMeshAttachmentComponent>();
    attachment.parentEntity = parent;
    attachment.parentJointIndex = jointIndex;
    attachment.localTransform = Translation(localTranslation);
    return entity;
}

TEST(ModelAttachment, RevalidatesPoseHierarchyAndParentBindingOnEveryUpdate){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto owner = context.makeOwner();
    const auto parent = MakeSkeleton(context, owner, s_ExpectedDualCount, 1.0f, 5.0f);
    const auto replacement = MakeSkeleton(context, owner, s_ExpectedDualCount, 4.0f, 10.0f);
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
    const auto parent = MakeSkeleton(context, owner, s_ExpectedDualCount);
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
    const auto first = MakeAttachment(context, owner, Core::ECS::s_InvalidEntityId, Limit<u32>::s_Max, 2.0f);
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
    const auto parent = MakeSkeleton(context, owner, s_ExpectedDualCount);
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
    const auto parent = MakeSkeleton(context, owner, s_ExpectedDualCount);
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
    const auto firstParent = MakeSkeleton(context, owner, s_ExpectedDualCount, 1.0f);
    const auto secondParent = MakeSkeleton(context, owner, s_ExpectedDualCount, 3.0f);
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
    const auto replacement = MakeSkeleton(context, owner, s_ExpectedDualCount, 5.0f);
    ASSERT_EQ(replacement.index(), firstParent.index());
    ASSERT_NE(replacement.generation(), firstParent.generation());
    world.entity(third).getComponent<ModelStaticMeshAttachmentComponent>().parentEntity = replacement;
    context.system.update(world, 0.0f);
    EXPECT_FLOAT_EQ(world.entity(first).getComponent<Scene::TransformComponent>().position.x, 2.0f);
    EXPECT_FLOAT_EQ(world.entity(second).getComponent<Scene::TransformComponent>().position.x, 3.0f);
    EXPECT_FLOAT_EQ(world.entity(third).getComponent<Scene::TransformComponent>().position.x, 5.0f);
    EXPECT_FLOAT_EQ(world.entity(fourth).getComponent<Scene::TransformComponent>().position.x, 6.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

