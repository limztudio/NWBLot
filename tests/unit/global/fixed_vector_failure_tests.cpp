// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/fixed_vector.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fixed_vector_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ElementAssignmentFailure = 0xE205u;
inline constexpr u32 s_ElementSwapFailure = 0xE206u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingFixedVectorAssignment{
    u32 value = 0u;


public:
    ThrowingFixedVectorAssignment()noexcept = default;
    ThrowingFixedVectorAssignment(const ThrowingFixedVectorAssignment&)noexcept = default;


public:
    ThrowingFixedVectorAssignment& operator=(const ThrowingFixedVectorAssignment&){
        throw s_ElementAssignmentFailure;
    }

    ThrowingFixedVectorAssignment& operator=(ThrowingFixedVectorAssignment&&)noexcept = default;
};

struct ThrowingFixedVectorSwap{
    u32 value = 0u;
};

struct FixedVectorResource{
    u32* live = nullptr;
    b8* failReset = nullptr;

    FixedVectorResource()noexcept = default;
    FixedVectorResource(u32& count, b8& fail)noexcept : live(&count), failReset(&fail){ ++*live; }
    FixedVectorResource(const FixedVectorResource&) = delete;
    FixedVectorResource(FixedVectorResource&& other)noexcept : live(other.live), failReset(other.failReset){ other.live = nullptr; }
    ~FixedVectorResource()noexcept{ if(live) --*live; }

    FixedVectorResource& operator=(FixedVectorResource&& other){
        if(live && failReset && *failReset && !other.live)
            throw s_ElementAssignmentFailure;
        if(live)
            --*live;
        live = other.live;
        failReset = other.failReset;
        other.live = nullptr;
        return *this;
    }
};

struct FixedVectorCopyOnly{
    u32* live = nullptr;
    FixedVectorCopyOnly()noexcept = default;
    explicit FixedVectorCopyOnly(u32& count)noexcept : live(&count){ ++*live; }
    FixedVectorCopyOnly(const FixedVectorCopyOnly& other)noexcept : live(other.live){ if(live) ++*live; }
    FixedVectorCopyOnly& operator=(const FixedVectorCopyOnly& other)noexcept{
        if(other.live)
            ++*other.live;
        if(live)
            --*live;
        live = other.live;
        return *this;
    }
    FixedVectorCopyOnly(FixedVectorCopyOnly&&) = delete;
    FixedVectorCopyOnly& operator=(FixedVectorCopyOnly&&) = delete;
    ~FixedVectorCopyOnly()noexcept{ if(live) --*live; }
};

struct FixedVectorNoDefault{
    FixedVectorNoDefault() = delete;
};

struct FixedVectorNonAssignable{
    FixedVectorNonAssignable()noexcept = default;
    FixedVectorNonAssignable& operator=(const FixedVectorNonAssignable&) = delete;
    FixedVectorNonAssignable& operator=(FixedVectorNonAssignable&&) = delete;
};

void swap([[maybe_unused]] ThrowingFixedVectorSwap& lhs, [[maybe_unused]] ThrowingFixedVectorSwap& rhs){
    throw s_ElementSwapFailure;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NoexceptFailureTests, FixedVectorRemovalSupportsCopyOnlyAndEmptyZeroCapacity){
    using namespace __hidden_fixed_vector_failure_tests;
    u32 live = 0u;
    FixedVector<FixedVectorCopyOnly, 2u> stored;
    FixedVectorCopyOnly value(live);
    stored.push_back(value);
    EXPECT_EQ(live, 2u);
    stored.pop_back();
    EXPECT_EQ(live, 1u);
    stored.push_back(value);
    stored.clear();
    EXPECT_TRUE(stored.empty());
    EXPECT_EQ(live, 1u);
    FixedVector<FixedVectorNoDefault, 0u> empty;
    static_assert(noexcept(empty.clear()));
    empty.clear();
    EXPECT_TRUE(empty.empty());
    FixedVector<FixedVectorNonAssignable, 1u> nonAssignable;
    static_assert(noexcept(nonAssignable.clear()));
    nonAssignable.clear();
    EXPECT_TRUE(nonAssignable.empty());
}

TEST(NoexceptFailureTests, FixedVectorRemovalReleasesResourcesAndRetainsSizeOnResetFailure){
    using namespace __hidden_fixed_vector_failure_tests;
    u32 live = 0u;
    b8 firstFails = false;
    b8 secondFails = false;
    FixedVector<FixedVectorResource, 3u> stored;
    static_assert(!noexcept(stored.clear()));
    stored.emplace_back(live, firstFails);
    stored.emplace_back(live, secondFails);
    ASSERT_EQ(live, 2u);
    stored.pop_back();
    EXPECT_EQ(live, 1u);
    stored.emplace_back(live, secondFails);
    secondFails = true;
    EXPECT_THROW(stored.pop_back(), u32);
    EXPECT_EQ(stored.size(), 2u);
    EXPECT_EQ(live, 2u);
    secondFails = false;
    firstFails = true;
    EXPECT_THROW(stored.clear(), u32);
    EXPECT_EQ(stored.size(), 1u);
    EXPECT_EQ(live, 1u);
    firstFails = false;
    stored.clear();
    EXPECT_TRUE(stored.empty());
    EXPECT_EQ(live, 0u);
}

TEST(NoexceptFailureTests, FixedVectorElementAssignmentFailurePropagatesWithoutPublishingFullSize){
    using namespace __hidden_fixed_vector_failure_tests;
    FixedVector<ThrowingFixedVectorAssignment, 4u> stored;
    stored.resize(1u);
    stored[0u].value = 17u;
    ThrowingFixedVectorAssignment replacement;
    replacement.value = 29u;
    static_assert(!noexcept(stored.fill(replacement)));
    EXPECT_THROW(stored.fill(replacement), u32);
    EXPECT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0u].value, 17u);
}

TEST(NoexceptFailureTests, FixedVectorElementSwapFailurePropagatesWithoutSwappingSizes){
    using namespace __hidden_fixed_vector_failure_tests;
    FixedVector<ThrowingFixedVectorSwap, 4u> first;
    FixedVector<ThrowingFixedVectorSwap, 4u> second;
    first.resize(1u);
    second.resize(2u);
    first[0u].value = 17u;
    second[0u].value = 29u;
    static_assert(!noexcept(first.swap(second)));
    EXPECT_THROW(first.swap(second), u32);
    EXPECT_EQ(first.size(), 1u);
    EXPECT_EQ(second.size(), 2u);
    EXPECT_EQ(first[0u].value, 17u);
    EXPECT_EQ(second[0u].value, 29u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

