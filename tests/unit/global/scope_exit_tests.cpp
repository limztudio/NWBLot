// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/scope_exit.h>
#include <global/type.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scope_exit_tests{


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

struct PotentiallyThrowingCopyCleanup{
    PotentiallyThrowingCopyCleanup() = default;
    PotentiallyThrowingCopyCleanup(const PotentiallyThrowingCopyCleanup&)noexcept(false){}
    PotentiallyThrowingCopyCleanup(PotentiallyThrowingCopyCleanup&&)noexcept = default;

    void operator()()noexcept{}
};


TEST(ScopeExit, RunsExactlyOnceAtNormalExitAndEarlyReturn){
    u32 calls = 0u;
    {
        ScopeExit cleanup([&]()noexcept{ ++calls; });
        EXPECT_EQ(calls, 0u);
    }
    EXPECT_EQ(calls, 1u);
    const auto operation = [&](){
        ScopeExit cleanup([&]()noexcept{ ++calls; });

        return 29u;
    };
    EXPECT_EQ(operation(), 29u);
    EXPECT_EQ(calls, 2u);
}

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

TEST(ScopeExit, OwnsMoveOnlyCallableUntilCleanupCompletes){
    CleanupState state;
    {
        ScopeExit cleanup{ MoveOnlyCleanup(state) };
        EXPECT_EQ(state.calls, 0u);
        EXPECT_EQ(state.destructions, 0u);
    }
    EXPECT_EQ(state.calls, 1u);
    EXPECT_EQ(state.destructions, 1u);
}

TEST(ScopeExit, InvokesMutableCopiedCallableWithoutChangingItsSource){
    u32 observed = 0u;
    auto callback = [value = 7u, &observed]()mutable noexcept{
        ++value;
        observed = value;
    };
    {
        ScopeExit cleanup(callback);
    }
    EXPECT_EQ(observed, 8u);
    callback();
    EXPECT_EQ(observed, 8u);
}

TEST(ScopeExit, UnwindingRunsNestedCleanupInReverseOrderAndPropagatesFailure){
    u32 order[3u] = {};
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

TEST(ScopeExit, GuardCannotTransferOwnershipAndConstructionCannotThrow){
    const auto callback = []()noexcept{};
    using Guard = ScopeExit<Decay_T<decltype(callback)>>;
    static_assert(!IsConstructible_V<Guard, Guard&>);
    static_assert(!IsConstructible_V<Guard, const Guard&>);
    static_assert(!IsConstructible_V<Guard, Guard&&>);
    static_assert(!IsAssignable_V<Guard&, Guard&>);
    static_assert(!IsAssignable_V<Guard&, Guard&&>);
    static_assert(IsNothrowDestructible_V<Guard>);
    static_assert(noexcept(ScopeExit(callback)));
    static_assert(!IsConstructible_V<ScopeExit<PotentiallyThrowingCopyCleanup>, PotentiallyThrowingCopyCleanup&>);
    static_assert(IsConstructible_V<ScopeExit<PotentiallyThrowingCopyCleanup>, PotentiallyThrowingCopyCleanup&&>);
    ScopeExit cleanup(callback);
    static_assert(noexcept(cleanup.release()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

