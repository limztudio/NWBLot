// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/module.h>

#include <tests/common/ecs_test_world.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_publication_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;

using TestWorld = NWB::Tests::EcsTestWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PublicationPositionComponent{
    i32 x = 0;
};

struct PublicationVelocityComponent{
    i32 x = 0;
};

struct ThrowingConstructionComponent{
    u32 value;


public:
    ThrowingConstructionComponent(bool reject, u32 valueArg)
        : value(valueArg)
    {
        if(reject)
            throw 61u;
    }
};

struct ThrowingMoveAssignmentComponent{
    bool& rejectMove;
    u32 value;


public:
    ThrowingMoveAssignmentComponent(bool& rejectMoveArg, u32 valueArg)noexcept
        : rejectMove(rejectMoveArg)
        , value(valueArg)
    {}
    ThrowingMoveAssignmentComponent(const ThrowingMoveAssignmentComponent&) = delete;
    ThrowingMoveAssignmentComponent(ThrowingMoveAssignmentComponent&&)noexcept = default;


public:
    ThrowingMoveAssignmentComponent& operator=(const ThrowingMoveAssignmentComponent&) = delete;

    ThrowingMoveAssignmentComponent& operator=(ThrowingMoveAssignmentComponent&& rhs){
        if(rejectMove)
            throw 67u;
        value = rhs.value;
        return *this;
    }
};

struct ThrowingMoveMessage{
    bool& rejectMove;
    u32& moveAttempts;
    u32& destructions;


public:
    ThrowingMoveMessage(bool& rejectMoveArg, u32& moveAttemptsArg, u32& destructionsArg)noexcept
        : rejectMove(rejectMoveArg)
        , moveAttempts(moveAttemptsArg)
        , destructions(destructionsArg)
    {}
    ThrowingMoveMessage(const ThrowingMoveMessage&) = delete;
    ThrowingMoveMessage(ThrowingMoveMessage&& rhs)
        : rejectMove(rhs.rejectMove)
        , moveAttempts(rhs.moveAttempts)
        , destructions(rhs.destructions)
    {
        ++moveAttempts;
        if(rejectMove)
            throw 73u;
    }
    ~ThrowingMoveMessage()noexcept{ ++destructions; }


public:
    ThrowingMoveMessage& operator=(const ThrowingMoveMessage&) = delete;

    ThrowingMoveMessage& operator=(ThrowingMoveMessage&&) = delete;
};

struct SystemLifetimeState{
    bool providerAlive = false;
    bool borrowerObservedProvider = false;
    Array<u32, 2u> destructionOrder{};
    usize destructionCount = 0u;
};

template<bool IsProvider>
class LifetimeSystem final : public NWB::Core::ECS::ISystem{
public:
    LifetimeSystem(NWB::Core::Alloc::GlobalArena& arena, SystemLifetimeState& state)noexcept
        : NWB::Core::ECS::ISystem(arena)
        , m_state(state)
    {
        if constexpr(IsProvider)
            m_state.providerAlive = true;
    }
    virtual ~LifetimeSystem()noexcept override{
        if constexpr(IsProvider)
            m_state.providerAlive = false;
        else
            m_state.borrowerObservedProvider = m_state.providerAlive;
        m_state.destructionOrder[m_state.destructionCount++] = IsProvider ? 1u : 2u;
    }


public:
    [[nodiscard]] virtual Expected<void, NWB::Core::CpuTaskAdmissionError::Enum> update(NWB::Core::ECS::World& world, f32 delta)noexcept override{
        static_cast<void>(world);
        static_cast<void>(delta);
        return {};
    }


private:
    SystemLifetimeState& m_state;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Ecs, FailedComponentConstructionDoesNotPublishMembership){
    TestWorld testWorld;
    auto entity = testWorld.world.createEntity();
    entity.addComponent<PublicationPositionComponent>().x = 71;
    EXPECT_THROW(entity.addComponent<ThrowingConstructionComponent>(true, 73u), u32);
    ASSERT_FALSE(entity.hasComponent<ThrowingConstructionComponent>());
    EXPECT_EQ(testWorld.world.tryGetComponent<ThrowingConstructionComponent>(entity.id()), nullptr);
    EXPECT_EQ(testWorld.world.view<ThrowingConstructionComponent>().candidateCount(), 0u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<ThrowingConstructionComponent>(), 0u);
    EXPECT_EQ(entity.getComponent<PublicationPositionComponent>().x, 71);

    EXPECT_EQ(entity.addComponent<ThrowingConstructionComponent>(false, 79u).value, 79u);
    EXPECT_EQ(entity.addComponent<ThrowingConstructionComponent>(true, 83u).value, 79u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<ThrowingConstructionComponent>(), 1u);
    entity.destroy();
    EXPECT_FALSE(entity.alive());
    EXPECT_EQ(testWorld.world.view<ThrowingConstructionComponent>().candidateCount(), 0u);
    EXPECT_EQ(testWorld.world.view<PublicationPositionComponent>().candidateCount(), 0u);
}

TEST(Ecs, FailedComponentMoveAssignmentPreservesEntityMappings){
    bool rejectMove = false;
    TestWorld testWorld;
    auto first = testWorld.world.createEntity();
    auto second = testWorld.world.createEntity();
    first.addComponent<ThrowingMoveAssignmentComponent>(rejectMove, 89u);
    second.addComponent<ThrowingMoveAssignmentComponent>(rejectMove, 97u);
    static_assert(!noexcept(first.removeComponent<ThrowingMoveAssignmentComponent>()));
    rejectMove = true;
    EXPECT_THROW(first.removeComponent<ThrowingMoveAssignmentComponent>(), u32);
    rejectMove = false;
    ASSERT_TRUE(first.hasComponent<ThrowingMoveAssignmentComponent>());
    ASSERT_TRUE(second.hasComponent<ThrowingMoveAssignmentComponent>());
    EXPECT_EQ(first.getComponent<ThrowingMoveAssignmentComponent>().value, 89u);
    EXPECT_EQ(second.getComponent<ThrowingMoveAssignmentComponent>().value, 97u);
    EXPECT_EQ(testWorld.world.view<ThrowingMoveAssignmentComponent>().candidateCount(), s_ExpectedDualCount);
    EXPECT_EQ(testWorld.world.componentMutationVersion<ThrowingMoveAssignmentComponent>(), s_ExpectedDualCount);

    first.removeComponent<ThrowingMoveAssignmentComponent>();
    EXPECT_FALSE(first.hasComponent<ThrowingMoveAssignmentComponent>());
    EXPECT_EQ(second.getComponent<ThrowingMoveAssignmentComponent>().value, 97u);
    EXPECT_EQ(testWorld.world.view<ThrowingMoveAssignmentComponent>().candidateCount(), 1u);
    second.destroy();
    EXPECT_EQ(testWorld.world.view<ThrowingMoveAssignmentComponent>().candidateCount(), 0u);
}

TEST(Ecs, FailedEntityDestructionRetainsRemainingMembershipForRetry){
    bool rejectMove = false;
    TestWorld testWorld;
    auto first = testWorld.world.createEntity();
    auto second = testWorld.world.createEntity();
    first.addComponent<PublicationPositionComponent>().x = 101;
    first.addComponent<ThrowingMoveAssignmentComponent>(rejectMove, 103u);
    first.addComponent<PublicationVelocityComponent>().x = 107;
    second.addComponent<ThrowingMoveAssignmentComponent>(rejectMove, 109u);
    rejectMove = true;
    EXPECT_THROW(first.destroy(), u32);
    rejectMove = false;
    EXPECT_TRUE(first.alive());
    EXPECT_FALSE(first.hasComponent<PublicationVelocityComponent>());
    EXPECT_TRUE(first.hasComponent<PublicationPositionComponent>());

    first.destroy();
    EXPECT_FALSE(first.alive());
    EXPECT_EQ(testWorld.world.tryGetComponent<PublicationPositionComponent>(first.id()), nullptr);
    EXPECT_EQ(testWorld.world.tryGetComponent<ThrowingMoveAssignmentComponent>(first.id()), nullptr);
    EXPECT_EQ(testWorld.world.view<PublicationPositionComponent>().candidateCount(), 0u);
    EXPECT_EQ(testWorld.world.view<ThrowingMoveAssignmentComponent>().candidateCount(), 1u);
    ASSERT_TRUE(second.hasComponent<ThrowingMoveAssignmentComponent>());
    EXPECT_EQ(second.getComponent<ThrowingMoveAssignmentComponent>().value, 109u);
    second.destroy();
    EXPECT_EQ(testWorld.world.entityCount(), 0u);
    EXPECT_EQ(testWorld.world.view<ThrowingMoveAssignmentComponent>().candidateCount(), 0u);
}

TEST(Ecs, WorldUnwindDestroysPendingMessagesWithoutMovingThem){
    bool rejectMove = false;
    u32 moveAttempts = 0u;
    u32 destructions = 0u;
    const auto failWithPendingMessage = [&](){
        TestWorld testWorld;
        testWorld.world.emplaceMessage<ThrowingMoveMessage>(rejectMove, moveAttempts, destructions);
        rejectMove = true;
        throw static_cast<i32>(-79);
    };
    EXPECT_THROW(failWithPendingMessage(), i32);
    EXPECT_EQ(moveAttempts, 0u);
    EXPECT_EQ(destructions, 1u);
}

TEST(Ecs, OrdinaryWorldDestructionDestroysPendingMessagesWithoutMovingThem){
    bool rejectMove = true;
    u32 moveAttempts = 0u;
    u32 destructions = 0u;
    {
        TestWorld testWorld;
        testWorld.world.emplaceMessage<ThrowingMoveMessage>(rejectMove, moveAttempts, destructions);
    }
    EXPECT_EQ(moveAttempts, 0u);
    EXPECT_EQ(destructions, 1u);
}

TEST(Ecs, FailedWorldTaskJoinStillRetiresBorrowingSystemsInOrder){
    SystemLifetimeState state;
    const auto failDuringWorldWait = [&](){
        TestWorld testWorld;
        testWorld.world.addSystem<LifetimeSystem<true>>(state);
        testWorld.world.addSystem<LifetimeSystem<false>>(state);
        const auto admitted = testWorld.world.taskScope().submit([](){ throw static_cast<i32>(-83); });
        ASSERT_TRUE(admitted);
    };
    EXPECT_THROW(failDuringWorldWait(), i32);
    EXPECT_TRUE(state.borrowerObservedProvider);
    EXPECT_FALSE(state.providerAlive);
    EXPECT_EQ(state.destructionCount, s_ExpectedDualCount);
    EXPECT_EQ(state.destructionOrder[0], s_ExpectedDualCount);
    EXPECT_EQ(state.destructionOrder[1], 1u);
}

TEST(Ecs, WorldClearAndDestructionRetireBorrowersBeforeEarlierSystems){
    for(u32 round = 0u; round < s_ExpectedDualCount; ++round){
        SystemLifetimeState state;
        {
            TestWorld testWorld;
            testWorld.world.addSystem<LifetimeSystem<true>>(state);
            testWorld.world.addSystem<LifetimeSystem<false>>(state);
            if(round == 0u)
                testWorld.world.clear();
        }
        EXPECT_TRUE(state.borrowerObservedProvider);
        EXPECT_FALSE(state.providerAlive);
        EXPECT_EQ(state.destructionCount, s_ExpectedDualCount);
        EXPECT_EQ(state.destructionOrder[0], s_ExpectedDualCount);
        EXPECT_EQ(state.destructionOrder[1], 1u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

