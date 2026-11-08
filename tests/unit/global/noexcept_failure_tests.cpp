// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/binary.h>
#include <global/compact_string.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations_file_io.h>
#include <global/hash_utils.h>
#include <global/inplace_function.h>
#include <global/numeric_text.h>
#include <global/refcount_ptr.h>
#include <global/unique_ptr.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_noexcept_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_PlacementFailure = 0xE201u;
inline constexpr u32 s_ReadFailure = 0xE202u;
inline constexpr u32 s_HashFailure = 0xE203u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingPlacementCallable{
    static void* operator new(usize, void*){
        throw s_PlacementFailure;
    }

    void operator()()const noexcept{}
};

struct ThrowingPodAddress{
    u32 value = 0u;


public:
    [[nodiscard]] void* operator&(){ throw s_ReadFailure; }
};
static_assert(IsTriviallyCopyable_V<ThrowingPodAddress>);

struct ThrowingMutableByteSource{
    [[nodiscard]] u8* data(){ throw s_ReadFailure; }
};

struct ThrowingDiagnosticView{
    u32& calls;


public:
    operator AStringView()const{
        ++calls;
        throw s_ReadFailure;
    }
};

struct ThrowingDiagnosticTemporary{
    ~ThrowingDiagnosticTemporary()noexcept(false){ throw s_ReadFailure; }


public:
    operator AStringView()const noexcept{ return "unsafe"; }
};

struct ThrowingDiagnosticCleanup{
    u32& calls;


public:
    [[nodiscard]] ThrowingDiagnosticTemporary view()const noexcept{
        ++calls;
        return {};
    }
};

struct StatefulPlacementCallable{
    static inline u32 s_Allocations = 0u;
    u32& calls;


public:
    static void* operator new(usize, void* storage){
        if(++s_Allocations > 1u)
            throw s_PlacementFailure;
        return storage;
    }

    void operator()()const noexcept{ ++calls; }
};

struct HashProbe{};

struct ThrowingHashValue{
    operator usize()const{
        throw s_HashFailure;
    }
};

struct ThrowingByteSource{
    using value_type = u8;


public:
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

TEST(NoexceptFailureTests, ThrowingByteGetterPreservesReadOffset){
    using namespace __hidden_noexcept_failure_tests;
    ThrowingByteSource source;
    usize offset = 0u;
    static_assert(!noexcept(ReadPOD<u32>(source, offset)));

    EXPECT_THROW(EXPECT_TRUE(ReadPOD<u32>(source, offset)), u32);
    EXPECT_EQ(offset, 0u);
}






TEST(NoexceptFailureTests, PodAddressAndMutableBufferGetterFailuresPropagateBeforeReading){
    using namespace __hidden_noexcept_failure_tests;
    const Array<u8, sizeof(ThrowingPodAddress)> bytes = {};
    const BinaryByteView source{ bytes.data(), bytes.size() };
    usize offset = 0u;
    static_assert(!noexcept(ReadPOD<ThrowingPodAddress>(source, offset)));
    EXPECT_THROW(EXPECT_TRUE(ReadPOD<ThrowingPodAddress>(source, offset)), u32);
    EXPECT_EQ(offset, 0u);

    ThrowingMutableByteSource mutableSource;
    static_assert(!noexcept(GlobalFilesystemDetail::MutableReadBuffer(mutableSource)));
    EXPECT_THROW(EXPECT_EQ(GlobalFilesystemDetail::MutableReadBuffer(mutableSource), nullptr), u32);
}

TEST(NoexceptFailureTests, BoundedDiagnosticsNeverInvokeThrowingConversionOrCleanupAdapters){
    using namespace __hidden_noexcept_failure_tests;
    u32 calls = 0u;
    const ThrowingDiagnosticView conversion{ calls };
    const ThrowingDiagnosticCleanup cleanup{ calls };
    EXPECT_EQ(MakeDiagnosticEventText(AStringView("{}"), conversion).view(), AStringView("<unprintable>"));
    EXPECT_EQ(MakeDiagnosticEventText(AStringView("{}"), cleanup).view(), AStringView("<unprintable>"));
    EXPECT_EQ(calls, 0u);
}

TEST(NoexceptFailureTests, CaptureMoveTransportDoesNotReinvokeClassPlacementAllocation){
    using namespace __hidden_noexcept_failure_tests;
    StatefulPlacementCallable::s_Allocations = 0u;
    u32 calls = 0u;
    StatefulPlacementCallable callable{ calls };
    InplaceFunction<64u> source(callable);
    EXPECT_EQ(StatefulPlacementCallable::s_Allocations, 1u);
    InplaceFunction<64u> moved(Move(source));
    EXPECT_FALSE(source);
    ASSERT_TRUE(moved);
    moved();
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(StatefulPlacementCallable::s_Allocations, 1u);
}







////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

