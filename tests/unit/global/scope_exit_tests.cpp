// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/scope_exit.h>
#include <global/type.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scope_exit_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ScopeExitFailure{};

struct CleanupState{
    u32 calls = 0u;
    u32 destructions = 0u;
};

struct MoveOnlyCleanup : NoCopy{
    CleanupState* state;

    explicit MoveOnlyCleanup(CleanupState& cleanupState)noexcept
        : state(&cleanupState)
    {}
    MoveOnlyCleanup(MoveOnlyCleanup&& other)noexcept
        : state(other.state)
    {
        other.state = nullptr;
    }
    ~MoveOnlyCleanup()noexcept{
        if(state)
            ++state->destructions;
    }

    void operator()()noexcept{ ++state->calls; }
};


TEST(ScopeExit, ReleaseIsIdempotentAndDestroysTheOwnedCallable){
    CleanupState state;
    {
        ScopeExit cleanup{ MoveOnlyCleanup(state) };
        cleanup.release();
        cleanup.release();
        EXPECT_EQ(state.calls, 0u);
        EXPECT_EQ(state.destructions, 0u);
    }
    EXPECT_EQ(state.calls, 0u);
    EXPECT_EQ(state.destructions, 1u);
}


TEST(ScopeExit, UnwindingRunsNestedCleanupInReverseOrderAndPropagatesFailure){
    u32 order[3u] = {};
    u32 count = 0u;
    const auto operation = [&](){
        ScopeExit first([&]()noexcept{ order[count] = 1u; ++count; });
        ScopeExit second([&]()noexcept{ order[count] = s_ExpectedDualCount; ++count; });
        ScopeExit released([&]()noexcept{ order[count] = 3u; ++count; });
        released.release();

        throw ScopeExitFailure{};
    };
    EXPECT_THROW(operation(), ScopeExitFailure);
    EXPECT_EQ(count, s_ExpectedDualCount);
    EXPECT_EQ(order[0u], s_ExpectedDualCount);
    EXPECT_EQ(order[1u], 1u);
    EXPECT_EQ(order[s_ThirdElementIndex], 0u);
}

TEST(ScopeExit, ResolvesTheFinalResultOnSuccessAndTheInitialResultOnUnwind){
    bool published = false;
    const auto operation = [&](const bool fails){
        bool completed = false;
        ScopeExit publish([&]()noexcept{ published = completed; });

        if(fails)
            throw ScopeExitFailure{};
        completed = true;
        return completed;
    };
    EXPECT_TRUE(operation(false));
    EXPECT_TRUE(published);
    EXPECT_THROW(operation(true), ScopeExitFailure);
    EXPECT_FALSE(published);
}

TEST(ScopeExit, CleanupObservesEarlierCapturedObjectsBeforeTheirDestruction){
    bool alive = false;
    bool observedAlive = false;
    struct Lifetime{
        bool& alive;

        explicit Lifetime(bool& lifetime)noexcept
            : alive(lifetime)
        {
            alive = true;
        }
        ~Lifetime()noexcept{ alive = false; }
    };
    const auto operation = [&](){
        Lifetime lifetime(alive);
        ScopeExit cleanup([&]()noexcept{ observedAlive = alive; });

        throw ScopeExitFailure{};
    };
    EXPECT_THROW(operation(), ScopeExitFailure);
    EXPECT_TRUE(observedAlive);
    EXPECT_FALSE(alive);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

