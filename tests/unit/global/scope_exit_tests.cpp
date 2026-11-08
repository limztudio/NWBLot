// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/global/scope_case_types.adhd.h>

#include <global/scope_exit.h>
#include <global/type.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scope_exit_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MoveOnlyCleanup : NoCopy{
    CleanupState* state;


public:
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


public:
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
    Array<u32, 3u> order = {};
    u32 count = 0u;
    const auto operation = [&](){
        ScopeExit first([&]()noexcept{ order[count] = 1u; ++count; });
        ScopeExit second([&]()noexcept{ order[count] = 2u; ++count; });
        ScopeExit released([&]()noexcept{ order[count] = 3u; ++count; });
        released.release();

        throw ScopeExitFailure{};
    };
    EXPECT_THROW(operation(), ScopeExitFailure);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(order[0u], 2u);
    EXPECT_EQ(order[1u], 1u);
    EXPECT_EQ(order[2u], 0u);
}

TEST(ScopeExit, ResolvesTheFinalResultOnSuccessAndTheInitialResultOnUnwind){
    b8 published = false;
    const auto operation = [&](const b8 fails){
        b8 completed = false;
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
    b8 alive = false;
    b8 observedAlive = false;
    struct Lifetime{
        b8& alive;


    public:
        explicit Lifetime(b8& lifetime)noexcept
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

