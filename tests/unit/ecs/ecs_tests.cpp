// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/module.h>
#include <core/common/module.h>

#include <tests/common/ecs_test_world.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/atomic.h>
#include <global/compile.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;

struct PositionComponent{
    i32 x = 0;
    i32 y = 0;
};

struct VelocityComponent{
    i32 x = 0;
    i32 y = 0;
};

struct AddressTrapComponent{
public:
    mutable u32 addressCalls = 0u;


public:
    [[nodiscard]] AddressTrapComponent* operator&(){ ++addressCalls; throw 37u; }

    [[nodiscard]] const AddressTrapComponent* operator&()const{ ++addressCalls; throw 41u; }
};

struct alignas(32) OverAlignedComponent{
    Array<u8, 32> value{};
};

template<usize I>
struct PoolSlotComponent{
    usize value = I;
};

template<usize... Is>
Array<usize, sizeof...(Is)> RegisterPoolSlotTypes(IndexSequence<Is...>)noexcept{
    return { NWB::Core::ECS::ComponentType<PoolSlotComponent<Is>>()... };
}

struct MoveOnlyMessage{
    u32 value = 0u;


public:
    explicit MoveOnlyMessage(u32 v)noexcept
        : value(v)
    {}
    MoveOnlyMessage(const MoveOnlyMessage&) = delete;
    MoveOnlyMessage(MoveOnlyMessage&& rhs)noexcept
        : value(rhs.value)
    {
        rhs.value = 0u;
    }


public:
    MoveOnlyMessage& operator=(const MoveOnlyMessage&) = delete;

    MoveOnlyMessage& operator=(MoveOnlyMessage&& rhs)noexcept{
        if(this != &rhs){
            value = rhs.value;
            rhs.value = 0u;
        }
        return *this;
    }
};

TEST(Ecs, ComponentStoragePreservesOverAlignment){
    TestWorld testWorld;
    auto entity = testWorld.world.createEntity();
    auto& aligned = entity.addComponent<OverAlignedComponent>();
    EXPECT_EQ(reinterpret_cast<usize>(&aligned) % alignof(OverAlignedComponent), 0u);
}

TEST(Ecs, StoredComponentLookupBypassesThrowingAddressOperators){
    TestWorld testWorld;
    auto entity = testWorld.world.createEntity();
    AddressTrapComponent& component = entity.addComponent<AddressTrapComponent>();
    AddressTrapComponent* const expected = AddressOf(component);
    const NWB::Core::ECS::World& constWorld = testWorld.world;
    static_assert(noexcept(testWorld.world.tryGetComponent<AddressTrapComponent>(entity.id())));
    static_assert(noexcept(constWorld.tryGetComponent<AddressTrapComponent>(entity.id())));
    EXPECT_EQ(testWorld.world.tryGetComponent<AddressTrapComponent>(entity.id()), expected);
    EXPECT_EQ(constWorld.tryGetComponent<AddressTrapComponent>(entity.id()), expected);
    EXPECT_EQ(component.addressCalls, 0u);
    entity.destroy();
    EXPECT_EQ(testWorld.world.tryGetComponent<AddressTrapComponent>(entity.id()), nullptr);
}

TEST(Ecs, ViewAndMessageConsumersPreserveCallbackExceptionContracts){
    TestWorld testWorld;
    auto entity = testWorld.world.createEntity();
    entity.addComponent<PositionComponent>();
    entity.addComponent<VelocityComponent>();
    const auto single = testWorld.world.view<PositionComponent>();
    const auto multiple = testWorld.world.view<PositionComponent, VelocityComponent>();
    const auto singleNoThrow = [](const NWB::Core::ECS::EntityID&, PositionComponent&)noexcept{};
    const auto singleThrows = [](const NWB::Core::ECS::EntityID&, PositionComponent&){ throw 43u; };
    const auto multipleNoThrow = [](NWB::Core::ECS::EntityID&, PositionComponent&, VelocityComponent&)noexcept{};
    const auto multipleThrows = [](NWB::Core::ECS::EntityID&, PositionComponent&, VelocityComponent&){ throw 47u; };
    static_assert(noexcept(single.each(singleNoThrow)));
    static_assert(!noexcept(single.each(singleThrows)));
    static_assert(noexcept(multiple.each(multipleNoThrow)));
    static_assert(!noexcept(multiple.each(multipleThrows)));
    EXPECT_THROW(single.each(singleThrows), u32);
    EXPECT_THROW(multiple.each(multipleThrows), u32);
    single.each(singleNoThrow);
    multiple.each(multipleNoThrow);

    testWorld.world.postMessage(7u);
    testWorld.world.swapMessageBuffers();
    u32 observed = 0u;
    const auto messageNoThrow = [&observed](const u32& value)noexcept{ observed += value; };
    const auto messageThrows = [](const u32&){ throw 53u; };
    static_assert(noexcept(testWorld.world.consumeMessages<u32>(messageNoThrow)));
    static_assert(!noexcept(testWorld.world.consumeMessages<u32>(messageThrows)));
    testWorld.world.consumeMessages<u32>(messageNoThrow);
    EXPECT_EQ(observed, 7u);
    EXPECT_THROW(testWorld.world.consumeMessages<u32>(messageThrows), u32);
    EXPECT_EQ(testWorld.world.messageCount<u32>(), 1u);
}

TEST(Ecs, EmptyViewDoesNotAllocateComponentPools){
    TestWorld testWorld;
    const auto initialMemory = testWorld.arena.memoryStats();

    usize singleViewCount = 0;
    usize multiViewCount = 0;

    testWorld.world.view<PositionComponent>().each([&singleViewCount](NWB::Core::ECS::EntityID, PositionComponent&)noexcept{
        ++singleViewCount;
    });
    testWorld.world.view<PositionComponent, VelocityComponent>().each([&multiViewCount](NWB::Core::ECS::EntityID, PositionComponent&, VelocityComponent&)noexcept{
        ++multiViewCount;
    });
    EXPECT_TRUE((testWorld.world.view<PositionComponent>().parallelEach(testWorld.world.taskScope(), [&singleViewCount](NWB::Core::ECS::EntityID, PositionComponent&)noexcept{
        ++singleViewCount;
    }
    )));
    EXPECT_TRUE((testWorld.world.view<PositionComponent, VelocityComponent>().parallelEach(testWorld.world.taskScope(), [&multiViewCount](NWB::Core::ECS::EntityID, PositionComponent&, VelocityComponent&)noexcept{
        ++multiViewCount;
    }
    )));

    EXPECT_EQ(singleViewCount, 0u);
    EXPECT_EQ(multiViewCount, 0u);

    const auto finalMemory = testWorld.arena.memoryStats();
    EXPECT_EQ(finalMemory.allocationCount, initialMemory.allocationCount);
    EXPECT_EQ(finalMemory.reallocationCount, initialMemory.reallocationCount);
    EXPECT_EQ(finalMemory.usedBytes, initialMemory.usedBytes);
}

TEST(Ecs, DestroyedEntityGenerationCannotReachRecycledComponents){
    TestWorld testWorld;
    auto entity = testWorld.world.createEntity();
    const auto entityId = entity.id();
    entity.addComponent<PositionComponent>();

    entity.destroy();
    EXPECT_FALSE(entity.alive());
    EXPECT_EQ(testWorld.world.tryGetComponent<PositionComponent>(entityId), nullptr);

    auto recycledEntity = testWorld.world.createEntity();
    EXPECT_NE(recycledEntity.id(), entityId);
}

TEST(Ecs, ExhaustedEntityGenerationNeverRevivesAStaleId){
    TestWorld testWorld;
    auto currentId = testWorld.world.createEntity().id();
    const auto stale = currentId;
    for(u32 generation = 0u; generation <= NWB::Core::ECS::ECSDetail::s_EntityGenerationMask; ++generation){
        testWorld.world.entity(currentId).destroy();
        currentId = testWorld.world.createEntity().id();
    }
    EXPECT_NE(currentId.index(), stale.index());
    EXPECT_FALSE(testWorld.world.entity(stale).alive());
    testWorld.world.clear();
    auto afterClear = testWorld.world.createEntity();
    EXPECT_NE(afterClear.id().index(), stale.index());
    EXPECT_FALSE(testWorld.world.entity(stale).alive());
}

TEST(Ecs, ClearingAtTheLastGenerationRetiresTheIndex){
    TestWorld testWorld;
    auto currentId = testWorld.world.createEntity().id();
    const auto stale = currentId;
    for(u32 generation = 0u; generation <= NWB::Core::ECS::ECSDetail::s_EntityGenerationMask; ++generation){
        testWorld.world.clear();
        testWorld.world.clear();
        currentId = testWorld.world.createEntity().id();
    }
    EXPECT_NE(currentId.index(), stale.index());
    EXPECT_FALSE(testWorld.world.entity(stale).alive());
}

TEST(Ecs, ClearedEntityFacadesCannotReachOrDestroyReplacementComponents){
    TestWorld testWorld;
    Array<NWB::Core::ECS::EntityID, 8u> retiredIds{};
    for(usize round = 0u; round < retiredIds.size(); ++round){
        auto entity = testWorld.world.createEntity();
        entity.addComponent<PositionComponent>().x = static_cast<i32>(round);
        retiredIds[round] = entity.id();
        testWorld.world.clear();
        testWorld.world.clear();
        auto replacement = testWorld.world.createEntity();
        replacement.addComponent<PositionComponent>().x = 73;
        for(usize retired = 0u; retired <= round; ++retired){
            auto stale = testWorld.world.entity(retiredIds[retired]);
            EXPECT_FALSE(stale.alive());
            EXPECT_FALSE(stale.hasComponent<PositionComponent>());
            EXPECT_EQ(testWorld.world.tryGetComponent<PositionComponent>(stale.id()), nullptr);
            stale.removeComponent<PositionComponent>();
            stale.destroy();
        }
        EXPECT_TRUE(replacement.alive());
        EXPECT_EQ(testWorld.world.entityCount(), 1u);
        EXPECT_EQ(replacement.getComponent<PositionComponent>().x, 73);
        testWorld.world.clear();
    }
}

TEST(Ecs, NextGenerationIdForAFreeSlotCannotDestroyOrUnderflowEntityCount){
    TestWorld testWorld;
    auto original = testWorld.world.createEntity();
    const auto originalId = original.id();
    original.destroy();
    auto unallocated = testWorld.world.entity(NWB::Core::ECS::EntityID(originalId.index(), originalId.generation() + 1u));
    EXPECT_FALSE(unallocated.alive());
    EXPECT_EQ(testWorld.world.entityCount(), 0u);
    unallocated.destroy();
    EXPECT_EQ(testWorld.world.entityCount(), 0u);
    auto replacement = testWorld.world.createEntity();
    replacement.addComponent<PositionComponent>().x = 91;
    EXPECT_TRUE(replacement.alive());
    EXPECT_EQ(testWorld.world.entityCount(), 1u);
    EXPECT_EQ(replacement.getComponent<PositionComponent>().x, 91);
}

TEST(Ecs, DuplicateAddAndRemovePreserveComponentMutationVersions){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    entity.addComponent<PositionComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 1u);

    entity.addComponent<PositionComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 1u);

    entity.addComponent<VelocityComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 1u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<VelocityComponent>(), 1u);

    entity.removeComponent<VelocityComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<VelocityComponent>(), 2u);

    entity.removeComponent<VelocityComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<VelocityComponent>(), 2u);

    entity.destroy();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 2u);
}

TEST(Ecs, ComponentPoolsPreserveSparseTypeLookupAndWorldIsolation){
    const auto typeIds = RegisterPoolSlotTypes(IndexSequence<
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u,
        16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u,
        32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u, 46u, 47u,
        48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u
    >{}
    );
    EXPECT_NE(typeIds.front(), typeIds.back());

    TestWorld firstWorld;
    TestWorld secondWorld;
    auto firstEntity = firstWorld.world.createEntity();
    auto secondEntity = secondWorld.world.createEntity();
    auto& firstComponent = firstEntity.addComponent<PoolSlotComponent<0u>>();
    firstComponent.value = 17u;
    const auto firstView = firstWorld.world.view<PoolSlotComponent<0u>>();

    secondEntity.addComponent<PoolSlotComponent<31u>>().value = 29u;
    firstEntity.addComponent<PoolSlotComponent<64u>>().value = 41u;

    EXPECT_EQ(firstWorld.world.tryGetComponent<PoolSlotComponent<0u>>(firstEntity.id()), &firstComponent);
    EXPECT_EQ(firstWorld.world.tryGetComponent<PoolSlotComponent<31u>>(firstEntity.id()), nullptr);
    EXPECT_EQ(secondWorld.world.tryGetComponent<PoolSlotComponent<0u>>(secondEntity.id()), nullptr);
    EXPECT_EQ(secondWorld.world.tryGetComponent<PoolSlotComponent<64u>>(secondEntity.id()), nullptr);
    EXPECT_EQ(firstWorld.world.componentMutationVersion<PoolSlotComponent<31u>>(), 0u);

    const NWB::Core::ECS::World& constFirstWorld = firstWorld.world;
    EXPECT_EQ(constFirstWorld.tryGetComponent<PoolSlotComponent<0u>>(firstEntity.id()), &firstComponent);
    EXPECT_EQ(constFirstWorld.tryGetComponent<PoolSlotComponent<31u>>(firstEntity.id()), nullptr);
    EXPECT_EQ(constFirstWorld.tryGetComponent<VelocityComponent>(firstEntity.id()), nullptr);

    usize visits = 0u;
    firstView.each([&](NWB::Core::ECS::EntityID entityId, PoolSlotComponent<0u>& component){
        EXPECT_EQ(entityId, firstEntity.id());
        EXPECT_EQ(&component, &firstComponent);
        EXPECT_EQ(component.value, 17u);
        ++visits;
    });
    EXPECT_EQ(visits, 1u);

    firstEntity.addComponent<PoolSlotComponent<31u>>().value = 53u;
    EXPECT_EQ(firstEntity.getComponent<PoolSlotComponent<31u>>().value, 53u);
    EXPECT_EQ(secondEntity.getComponent<PoolSlotComponent<31u>>().value, 29u);
}

TEST(Ecs, ComponentPoolsCanBeRecreatedAfterWorldClear){
    TestWorld testWorld;
    auto entity = testWorld.world.createEntity();
    entity.addComponent<PoolSlotComponent<63u>>().value = 19u;
    entity.addComponent<PositionComponent>().x = 23;

    testWorld.world.clear();
    EXPECT_EQ(testWorld.world.entityCount(), 0u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 0u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<PoolSlotComponent<63u>>(), 0u);
    EXPECT_EQ(testWorld.world.tryGetComponent<PoolSlotComponent<63u>>(entity.id()), nullptr);
    EXPECT_EQ(testWorld.world.view<PositionComponent>().candidateCount(), 0u);

    auto recreated = testWorld.world.createEntity();
    recreated.addComponent<PoolSlotComponent<63u>>().value = 37u;
    EXPECT_EQ(recreated.getComponent<PoolSlotComponent<63u>>().value, 37u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<PoolSlotComponent<63u>>(), 1u);
    EXPECT_FALSE(recreated.hasComponent<PositionComponent>());
    EXPECT_EQ(testWorld.world.view<PoolSlotComponent<63u>>().candidateCount(), 1u);
}

TEST(Ecs, ClearingPendingMoveOnlyMessagesPreventsPublication){
    TestWorld testWorld;
    testWorld.world.emplaceMessage<MoveOnlyMessage>(41u);
    testWorld.world.clearMessages();
    testWorld.world.swapMessageBuffers();
    EXPECT_EQ(testWorld.world.messageCount<MoveOnlyMessage>(), 0u);
}

TEST(Ecs, DuplicateComponentAddIsStable){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& first = entity.addComponent<PositionComponent>();
    first.x = 9;
    first.y = 4;

    auto& second = entity.addComponent<PositionComponent>();
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(second.x, 9);
    EXPECT_EQ(second.y, 4);

    usize viewCount = 0u;
    testWorld.world.view<PositionComponent>().each([&viewCount](NWB::Core::ECS::EntityID, PositionComponent&)noexcept{
        ++viewCount;
    });
    EXPECT_EQ(viewCount, 1u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

