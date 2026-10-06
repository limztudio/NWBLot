// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/binary.h>
#include <global/hash_utils.h>
#include <global/inplace_function.h>
#include <global/refcount_ptr.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_noexcept_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_PlacementFailure = 0xE201u;
inline constexpr u32 s_ReadFailure = 0xE202u;
inline constexpr u32 s_HashFailure = 0xE203u;
inline constexpr u32 s_PointerFailure = 0xE204u;


struct ThrowingPlacementCallable{
    static void* operator new(usize, void*){
        throw s_PlacementFailure;
    }

    void operator()()const noexcept{}
};

struct RefCountTarget{};

struct ThrowingFancyPointer{
    RefCountTarget* target = nullptr;

    [[nodiscard]] static ThrowingFancyPointer Borrow(RefCountTarget* value)noexcept{
        ThrowingFancyPointer result;
        result.target = value;
        return result;
    }

    ThrowingFancyPointer()noexcept = default;
    ThrowingFancyPointer(RefCountTarget* value){
        if(value)
            throw s_PointerFailure;
    }

    [[nodiscard]] RefCountTarget& operator*()const{
        throw s_PointerFailure;
    }
    explicit operator bool()const noexcept{ return target != nullptr; }
};

[[nodiscard]] bool operator==(const ThrowingFancyPointer& lhs, const ThrowingFancyPointer& rhs)noexcept{
    return lhs.target == rhs.target;
}

u32 RefCountAddReference(ThrowingFancyPointer)noexcept{ return 1u; }
u32 RefCountRelease(ThrowingFancyPointer)noexcept{ return 1u; }

struct FancyPointerDeleter{
    using pointer = ThrowingFancyPointer;

    void operator()(pointer)const noexcept{}
};

struct HashProbe{};

struct ThrowingHashValue{
    operator usize()const{
        throw s_HashFailure;
    }
};

struct ThrowingByteSource{
    using value_type = u8;

    [[nodiscard]] usize size()const noexcept{ return sizeof(u32); }
    [[nodiscard]] const u8* data()const{
        throw s_ReadFailure;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<__hidden_noexcept_failure_tests::HashProbe>{
    [[nodiscard]] __hidden_noexcept_failure_tests::ThrowingHashValue operator()(const __hidden_noexcept_failure_tests::HashProbe&)const noexcept{
        return {};
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NoexceptFailureTests, FancyPointerDereferenceAndRawConversionFailuresPreserveBorrowedTarget){
    using namespace __hidden_noexcept_failure_tests;
    using Storage = RefCountPtr<RefCountTarget, FancyPointerDeleter>;
    RefCountTarget target;
    Storage stored(ThrowingFancyPointer::Borrow(&target), s_AdoptRef);
    static_assert(!noexcept(*stored));
    static_assert(!noexcept(stored = &target));

    EXPECT_THROW(EXPECT_EQ(&*stored, &target), u32);
    EXPECT_THROW(stored = &target, u32);
    EXPECT_EQ(stored.get().target, &target);
}

TEST(NoexceptFailureTests, HashResultConversionFailurePreservesCombinedSeed){
    using namespace __hidden_noexcept_failure_tests;
    HashProbe value;
    usize seed = 17u;
    static_assert(!noexcept(HashCombine(seed, value)));

    EXPECT_THROW(HashCombine(seed, value), u32);
    EXPECT_EQ(seed, 17u);
}

TEST(NoexceptFailureTests, PlacementAllocationFailurePropagatesThroughCaptureConstructionAndReplacement){
    using namespace __hidden_noexcept_failure_tests;
    using Storage = InplaceFunction<64u>;
    ThrowingPlacementCallable callable;
    static_assert(!noexcept(Storage(callable)));
    static_assert(!noexcept(DeclVal<Storage&>() = callable));

    EXPECT_THROW(EXPECT_TRUE(Storage(callable)), u32);

    u32 previousInvocations = 0u;
    Storage stored([&previousInvocations]()noexcept{ ++previousInvocations; });
    EXPECT_THROW(stored = callable, u32);
    EXPECT_FALSE(stored);
    EXPECT_EQ(previousInvocations, 0u);
}

TEST(NoexceptFailureTests, ThrowingByteGetterPreservesReadOffsetAndDestination){
    using namespace __hidden_noexcept_failure_tests;
    ThrowingByteSource source;
    usize offset = 0u;
    u32 destination = 17u;
    static_assert(!noexcept(ReadPOD(source, offset, destination)));

    EXPECT_THROW(EXPECT_FALSE(ReadPOD(source, offset, destination)), u32);
    EXPECT_EQ(offset, 0u);
    EXPECT_EQ(destination, 17u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

