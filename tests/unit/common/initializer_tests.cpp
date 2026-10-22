// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <global/terminal_entry.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common_initializer_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_InitializerCount = 2u;
inline constexpr u32 s_EventCapacity = 16u;
inline constexpr int s_InitializationFailureCode = 37;
inline constexpr int s_OwnerFailureCode = 53;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InitializerFailure{
    int code;
};

struct InitializerState{
    Array<u32, s_InitializerCount> initializeCounts{};
    Array<u32, s_InitializerCount> finalizeCounts{};
    Array<u32, s_EventCapacity> events{};
    usize eventCount = 0u;
    bool failNext = false;
    bool throwNext = false;


public:
    void record(const u32 event)noexcept{
        if(eventCount >= events.size())
            TerminateInvariant();
        events[eventCount] = event;
        ++eventCount;
    }
};

static InitializerState s_State;

class InitializerProbe final : public NWB::Core::Common::Initializerable{
public:
    explicit InitializerProbe(const u32 index)
        : m_index(index)
    {}


public:
    virtual bool initialize()override{
        ++s_State.initializeCounts[m_index];
        s_State.record(m_index);
        if(m_index == 1u){
            if(s_State.failNext){
                s_State.failNext = false;
                return false;
            }
            if(s_State.throwNext){
                s_State.throwNext = false;
                throw InitializerFailure{ s_InitializationFailureCode };
            }
        }
        return true;
    }

    virtual void finalize()noexcept override{
        ++s_State.finalizeCounts[m_index];
        s_State.record(m_index + s_InitializerCount);
    }


private:
    const u32 m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CommonInitializerLifetime, FailedInitializationRollsBackOnlyTheSuccessfulPrefixAndAllowsReacquire){
    using namespace __hidden_common_initializer_tests;
    s_State = {};
    s_State.failNext = true;
    NWB::Core::Common::InitializerGuard initializer;
    EXPECT_FALSE(initializer.initialize());
    EXPECT_EQ(s_State.eventCount, 3u);
    EXPECT_EQ(s_State.events[0], 0u);
    EXPECT_EQ(s_State.events[1], 1u);
    EXPECT_EQ(s_State.events[2], s_InitializerCount);
    EXPECT_EQ(s_State.finalizeCounts[0], 1u);
    EXPECT_EQ(s_State.finalizeCounts[1], 0u);

    ASSERT_TRUE(initializer.initialize());
    EXPECT_EQ(s_State.initializeCounts[0], 2u);
    EXPECT_EQ(s_State.initializeCounts[1], 2u);
    initializer.finalize();
    EXPECT_EQ(s_State.finalizeCounts[0], 2u);
    EXPECT_EQ(s_State.finalizeCounts[1], 1u);
    EXPECT_EQ(s_State.eventCount, 7u);
    EXPECT_EQ(s_State.events[5], s_InitializerCount);
    EXPECT_EQ(s_State.events[6], s_InitializerCount + 1u);
    initializer.finalize();
    EXPECT_EQ(s_State.eventCount, 7u);
}

TEST(CommonInitializerLifetime, ThrowingInitializationRollsBackOnlyTheSuccessfulPrefixAndAllowsReacquire){
    using namespace __hidden_common_initializer_tests;
    s_State = {};
    s_State.throwNext = true;
    const int result = InvokeTerminalEntry<InitializerFailure>([]()->int{
        NWB::Core::Common::InitializerGuard initializer;
        return initializer.initialize() ? 0 : -1;
    }, [](const InitializerFailure& error)noexcept{ return error.code; }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, s_InitializationFailureCode);
    EXPECT_EQ(s_State.eventCount, 3u);
    EXPECT_EQ(s_State.events[0], 0u);
    EXPECT_EQ(s_State.events[1], 1u);
    EXPECT_EQ(s_State.events[2], s_InitializerCount);
    EXPECT_EQ(s_State.finalizeCounts[0], 1u);
    EXPECT_EQ(s_State.finalizeCounts[1], 0u);

    NWB::Core::Common::InitializerGuard initializer;
    ASSERT_TRUE(initializer.initialize());
    EXPECT_EQ(s_State.initializeCounts[0], 2u);
    EXPECT_EQ(s_State.initializeCounts[1], 2u);
    initializer.finalize();
    EXPECT_EQ(s_State.finalizeCounts[0], 2u);
    EXPECT_EQ(s_State.finalizeCounts[1], 1u);
    EXPECT_EQ(s_State.eventCount, 7u);
}

TEST(CommonInitializerLifetime, NestedGuardsFinalizeOnlyTheLastOwnerAndAllowReacquire){
    using namespace __hidden_common_initializer_tests;
    s_State = {};
    {
        NWB::Core::Common::InitializerGuard outer;
        ASSERT_TRUE(outer.initialize());
        ASSERT_TRUE(outer.initialize());
        {
            NWB::Core::Common::InitializerGuard inner;
            ASSERT_TRUE(inner.initialize());
            ASSERT_TRUE(inner.initialize());
            EXPECT_EQ(s_State.initializeCounts[0], 1u);
            EXPECT_EQ(s_State.initializeCounts[1], 1u);
            outer.finalize();
            EXPECT_EQ(s_State.finalizeCounts[0], 0u);
            EXPECT_EQ(s_State.finalizeCounts[1], 0u);
        }
        EXPECT_EQ(s_State.finalizeCounts[0], 1u);
        EXPECT_EQ(s_State.finalizeCounts[1], 1u);
    }
    EXPECT_EQ(s_State.eventCount, 4u);
    {
        NWB::Core::Common::InitializerGuard reacquired;
        ASSERT_TRUE(reacquired.initialize());
        EXPECT_EQ(s_State.initializeCounts[0], 2u);
        EXPECT_EQ(s_State.initializeCounts[1], 2u);
    }
    EXPECT_EQ(s_State.finalizeCounts[0], 2u);
    EXPECT_EQ(s_State.finalizeCounts[1], 2u);
    EXPECT_EQ(s_State.eventCount, 8u);
}

TEST(CommonInitializerLifetime, GuardUnwindFinalizesAllItemsBeforeTheTerminalEntryHandlesTheOriginalFailure){
    using namespace __hidden_common_initializer_tests;
    s_State = {};
    const int result = InvokeTerminalEntry<InitializerFailure>([]()->int{
        NWB::Core::Common::InitializerGuard initializer;
        if(!initializer.initialize())
            return -1;
        throw InitializerFailure{ s_OwnerFailureCode };
    }, [](const InitializerFailure& error)noexcept{ return error.code; }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, s_OwnerFailureCode);
    EXPECT_EQ(s_State.initializeCounts[0], 1u);
    EXPECT_EQ(s_State.initializeCounts[1], 1u);
    EXPECT_EQ(s_State.finalizeCounts[0], 1u);
    EXPECT_EQ(s_State.finalizeCounts[1], 1u);
    EXPECT_EQ(s_State.eventCount, 4u);
    EXPECT_EQ(s_State.events[2], s_InitializerCount);
    EXPECT_EQ(s_State.events[3], s_InitializerCount + 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This executable owns its initializer guards so the last release is exercised directly.
int main(int argc, char** argv){
    using namespace __hidden_common_initializer_tests;
    return InvokeTerminalEntry<InitializerFailure>([&](){
        static InitializerProbe s_FirstInitializer(0u);
        static InitializerProbe s_SecondInitializer(1u);
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
    }, [](const InitializerFailure&)noexcept{ return -1; }, []()noexcept{ return -1; }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

