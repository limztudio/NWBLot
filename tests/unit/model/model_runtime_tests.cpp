// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "model_test_context.h"

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_runtime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::ModelTests;


TEST(ModelRuntime, RemovingMultipleModelsDestroysEveryOwnedObjectInOneSync){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto retainedOwner = context.makeOwner();
    const auto retainedObject = context.makeObject(retainedOwner);
    const auto unrelated = world.createEntity().id();
    Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena> removedOwners(context.testWorld.arena);
    Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena> removedObjects(context.testWorld.arena);
    for(const usize objectCount : { 1u, 0u, 64u, 3u }){
        const auto owner = context.makeOwner();
        removedOwners.push_back(owner);
        for(usize objectIndex = 0u; objectIndex < objectCount; ++objectIndex)
            removedObjects.push_back(context.makeObject(owner));
        world.entity(owner).removeComponent<ModelComponent>();
    }

    context.system.prepare(world);
    context.system.update(world, 0.0f);
    for(const auto owner : removedOwners){
        EXPECT_TRUE(world.entity(owner).alive());
        EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(owner), nullptr);
    }
    for(const auto object : removedObjects)
        EXPECT_FALSE(world.entity(object).alive());
    EXPECT_TRUE(world.entity(retainedObject).alive());
    EXPECT_TRUE(world.entity(unrelated).alive());
    ASSERT_NE(world.tryGetComponent<ModelRuntimeComponent>(retainedOwner), nullptr);
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(retainedOwner)->objectCount, 1u);
    EXPECT_EQ(world.entityCount(), removedOwners.size() + 3u);
    EXPECT_EQ(context.source.readCount, 0u);

    context.system.syncModelRuntimes();
    EXPECT_EQ(world.entityCount(), removedOwners.size() + 3u);
    EXPECT_TRUE(world.entity(retainedObject).alive());
}

TEST(ModelRuntime, InvalidBindingsAndRemovedModelsUseIndependentCleanupBatches){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto invalidOwner = context.makeOwner();
    const auto invalidObject = context.makeObject(invalidOwner);
    const auto removedOwner = context.makeOwner();
    const auto removedObject = context.makeObject(removedOwner);
    const auto retainedOwner = context.makeOwner();
    const auto retainedObject = context.makeObject(retainedOwner);
    world.entity(invalidOwner).getComponent<ModelComponent>().model.reset();
    world.entity(removedOwner).removeComponent<ModelComponent>();

    context.system.syncModelRuntimes();
    context.system.update(world, 0.0f);
    EXPECT_FALSE(world.entity(invalidObject).alive());
    EXPECT_FALSE(world.entity(removedObject).alive());
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(invalidOwner), nullptr);
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(removedOwner), nullptr);
    EXPECT_NE(world.tryGetComponent<ModelComponent>(invalidOwner), nullptr);
    EXPECT_TRUE(world.entity(retainedObject).alive());
    EXPECT_EQ(context.source.readCount, 0u);
}

TEST(ModelRuntime, PrunesDestroyedOwnerGenerationWithoutTouchingItsReplacement){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto oldOwner = context.makeOwner();
    const auto oldObject = context.makeObject(oldOwner);
    world.destroyEntity(oldOwner);
    const auto replacementOwner = context.makeOwner();
    const auto replacementObject = context.makeObject(replacementOwner);
    ASSERT_EQ(oldOwner.index(), replacementOwner.index());
    ASSERT_NE(oldOwner.generation(), replacementOwner.generation());

    context.system.syncModelRuntimes();
    EXPECT_FALSE(world.entity(oldObject).alive());
    EXPECT_TRUE(world.entity(replacementOwner).alive());
    EXPECT_TRUE(world.entity(replacementObject).alive());
    ASSERT_NE(world.tryGetComponent<ModelRuntimeComponent>(replacementOwner), nullptr);
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(replacementOwner)->objectCount, 1u);
    EXPECT_EQ(context.source.readCount, 0u);
}

TEST(ModelRuntime, SteadyStateAndEmptyRuntimeCleanupPerformNoBackingAllocations){
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto retainedOwner = context.makeOwner();
    const auto retainedObject = context.makeObject(retainedOwner);
    const auto emptyOwner = context.makeOwner();
    world.entity(emptyOwner).getComponent<ModelComponent>().model.reset();
    context.system.syncModelRuntimes();
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    for(usize iteration = 0u; iteration < 32u; ++iteration)
        context.system.syncModelRuntimes();
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_TRUE(world.entity(retainedObject).alive());
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(emptyOwner), nullptr);
    EXPECT_EQ(context.source.readCount, 0u);
}

TEST(ModelRuntime, FailedReplacementLoadClearsOnlyThatOwnersObjectsAndCanRetry){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    RuntimeContext context;
    auto& world = context.testWorld.world;
    const auto changedOwner = context.makeOwner();
    const auto changedObject = context.makeObject(changedOwner);
    const auto retainedOwner = context.makeOwner();
    const auto retainedObject = context.makeObject(retainedOwner);
    world.entity(changedOwner).getComponent<ModelComponent>().model = Core::Assets::AssetRef<Model>("tests/model_runtime/missing");

    context.system.syncModelRuntimes();
    EXPECT_FALSE(world.entity(changedObject).alive());
    EXPECT_TRUE(world.entity(retainedObject).alive());
    ASSERT_NE(world.tryGetComponent<ModelRuntimeComponent>(changedOwner), nullptr);
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(changedOwner)->model, NAME_NONE);
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(changedOwner)->objectCount, 0u);
    EXPECT_EQ(context.source.readCount, 1u);

    context.system.syncModelRuntimes();
    EXPECT_EQ(context.source.readCount, 2u);
    EXPECT_TRUE(world.entity(retainedObject).alive());
    world.entity(changedOwner).removeComponent<ModelComponent>();
    context.system.syncModelRuntimes();
    EXPECT_EQ(world.tryGetComponent<ModelRuntimeComponent>(changedOwner), nullptr);
    EXPECT_EQ(context.source.readCount, 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

