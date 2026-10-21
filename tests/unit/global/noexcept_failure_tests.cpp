// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/binary.h>
#include <global/compact_string.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations_file_io.h>
#include <global/fixed_vector.h>
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
inline constexpr u32 s_PointerFailure = 0xE204u;
inline constexpr u32 s_ElementAssignmentFailure = 0xE205u;
inline constexpr u32 s_ElementSwapFailure = 0xE206u;
inline constexpr u32 s_ComparisonFailure = 0xE209u;
inline constexpr u32 s_HexCapacityFailure = 0xE20Au;
inline constexpr u32 s_HexAppendFailure = 0xE20Bu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingHexCapacityPredicate{
    explicit operator bool()const{
        throw s_HexCapacityFailure;
    }
};

struct HexCapacityValue{
    usize value = 0u;
};

[[nodiscard]] ThrowingHexCapacityPredicate operator>(HexCapacityValue, HexCapacityValue)noexcept{ return {}; }

[[nodiscard]] usize operator-(const HexCapacityValue lhs, const HexCapacityValue rhs)noexcept{ return lhs.value - rhs.value; }

struct ThrowingHexCapacityString{
    using value_type = char;


public:
    ACompactString text;


public:
    [[nodiscard]] HexCapacityValue size()const noexcept{ return { text.size() }; }

    [[nodiscard]] HexCapacityValue max_size()const noexcept{ return { text.max_size() }; }

    void push_back(const char ch)noexcept{ text.push_back(ch); }
};

struct ThrowingHexAppendString{
    using value_type = char;


public:
    ACompactString text;


public:
    ThrowingHexAppendString& operator+=(const char ch){
        push_back(ch);
        return *this;
    }


public:
    [[nodiscard]] usize size()const noexcept{ return text.size(); }

    [[nodiscard]] usize max_size()const noexcept{ return text.max_size(); }

    void push_back(const char ch){
        if(text.size() == 2u)
            throw s_HexAppendFailure;
        text.push_back(ch);
    }
};

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

void swap(ThrowingFixedVectorSwap& lhs, ThrowingFixedVectorSwap& rhs){
    static_cast<void>(lhs);
    static_cast<void>(rhs);
    throw s_ElementSwapFailure;
}


struct ThrowingPlacementCallable{
    static void* operator new(usize, void*){
        throw s_PlacementFailure;
    }

    void operator()()const noexcept{}
};

struct RefCountTarget{};

struct ThrowingFancyPointer{
    RefCountTarget* target = nullptr;


public:
    [[nodiscard]] static ThrowingFancyPointer Borrow(RefCountTarget* value)noexcept{
        ThrowingFancyPointer result;
        result.target = value;
        return result;
    }


public:
    ThrowingFancyPointer()noexcept = default;
    ThrowingFancyPointer(RefCountTarget* value){
        if(value)
            throw s_PointerFailure;
    }


public:
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


public:
    void operator()(pointer)const noexcept{}
};

struct ThrowingComparisonResult{
    operator bool()const{ throw s_ComparisonFailure; }

    [[nodiscard]] ThrowingComparisonResult operator!()const noexcept{ return {}; }
};

template<typename T>
struct ComparisonProxyDeleter{
    void operator()(T*)const noexcept{}
};

struct ComparisonReferenceTarget{
    u32 references = 1u;


public:
    [[nodiscard]] u32 addReference()noexcept{ return ++references; }

    [[nodiscard]] u32 release()noexcept{ return --references; }
};

using UniqueComparisonOwner = UniquePtr<int, ComparisonProxyDeleter<int>>;
using ReferenceComparisonOwner = RefCountPtr<ComparisonReferenceTarget, ComparisonProxyDeleter<ComparisonReferenceTarget>>;

[[nodiscard]] ThrowingComparisonResult operator<(const UniqueComparisonOwner&, const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(const UniqueComparisonOwner&, decltype(nullptr))noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(decltype(nullptr), const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(const ReferenceComparisonOwner&, const ReferenceComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(const ReferenceComparisonOwner&, decltype(nullptr))noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(decltype(nullptr), const ReferenceComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator!(const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator!(const ReferenceComparisonOwner&)noexcept{ return {}; }

template<typename OwnerT>
void VerifyComparisonResultConversions(){
    OwnerT first;
    OwnerT second;
    static_assert(noexcept(first < second));
    static_assert(noexcept(!(first < second)));
    static_assert(noexcept(!first));
    static_assert(!noexcept(::operator==(first, nullptr)));
    static_assert(!noexcept(::operator>(first, second)));
    static_assert(!noexcept(::operator<=(first, second)));
    static_assert(!noexcept(::operator>=(first, second)));
    static_assert(!noexcept(::operator>(first, nullptr)));
    static_assert(!noexcept(::operator>(nullptr, first)));
    static_assert(!noexcept(::operator<=(first, nullptr)));
    static_assert(!noexcept(::operator<=(nullptr, first)));
    static_assert(!noexcept(::operator>=(first, nullptr)));
    static_assert(!noexcept(::operator>=(nullptr, first)));
    EXPECT_THROW(::operator==(first, nullptr), u32);
    EXPECT_THROW(::operator>(first, second), u32);
    EXPECT_THROW(::operator<=(first, second), u32);
    EXPECT_THROW(::operator>=(first, second), u32);
    EXPECT_THROW(::operator>(first, nullptr), u32);
    EXPECT_THROW(::operator>(nullptr, first), u32);
    EXPECT_THROW(::operator<=(first, nullptr), u32);
    EXPECT_THROW(::operator<=(nullptr, first), u32);
    EXPECT_THROW(::operator>=(first, nullptr), u32);
    EXPECT_THROW(::operator>=(nullptr, first), u32);
    EXPECT_FALSE(first);
    EXPECT_FALSE(second);
}

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

struct ObservationPointer{
    static inline bool s_FailCopy = false;
    RefCountTarget* target = nullptr;


public:
    ObservationPointer()noexcept = default;
    ObservationPointer(decltype(nullptr))noexcept{}
    explicit ObservationPointer(RefCountTarget* value)noexcept
        : target(value)
    {}
    ObservationPointer(const volatile ObservationPointer& value)
        : target(value.target)
    {
        if(s_FailCopy)
            throw s_PointerFailure;
    }
    template<typename Other> requires IsSame_V<RemoveCVRef_T<Other>, ObservationPointer>
    explicit ObservationPointer(Other&& value)noexcept
        : target(value.target)
    {}
    ObservationPointer(ObservationPointer&&)noexcept = default;


public:
    ObservationPointer& operator=(const ObservationPointer&)noexcept = default;

    ObservationPointer& operator=(ObservationPointer&&)noexcept = default;

    explicit operator bool()const noexcept{ return target != nullptr; }
};

[[nodiscard]] bool operator==(const ObservationPointer& first, const ObservationPointer& second)noexcept{ return first.target == second.target; }

u32 RefCountAddReference(ObservationPointer)noexcept{ return 1u; }

u32 RefCountRelease(ObservationPointer)noexcept{ return 1u; }

struct ObservationDeleter{
    using pointer = ObservationPointer;


public:
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

TEST(NoexceptFailureTests, FixedVectorElementAssignmentFailurePropagatesWithoutPublishingFullSize){
    using namespace __hidden_noexcept_failure_tests;
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
    using namespace __hidden_noexcept_failure_tests;
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

TEST(NoexceptFailureTests, ThrowingByteGetterPreservesReadOffset){
    using namespace __hidden_noexcept_failure_tests;
    ThrowingByteSource source;
    usize offset = 0u;
    static_assert(!noexcept(ReadPOD<u32>(source, offset)));

    EXPECT_THROW(EXPECT_TRUE(ReadPOD<u32>(source, offset)), u32);
    EXPECT_EQ(offset, 0u);
}


TEST(NoexceptFailureTests, UniqueOwnerComparisonResultConversionUnwindsThroughEveryBooleanWrapper){
    using namespace __hidden_noexcept_failure_tests;
    UniquePtr<int> first;
    UniquePtr<int> second;
    static_assert(noexcept(::operator==(first, nullptr)));
    static_assert(noexcept(::operator>(first, second)));
    static_assert(noexcept(::operator<=(first, second)));
    static_assert(noexcept(::operator>=(first, second)));
    VerifyComparisonResultConversions<UniqueComparisonOwner>();
}

TEST(NoexceptFailureTests, ReferenceOwnerComparisonResultConversionUnwindsThroughEveryBooleanWrapper){
    using namespace __hidden_noexcept_failure_tests;
    RefCountPtr<ComparisonReferenceTarget> first;
    RefCountPtr<ComparisonReferenceTarget> second;
    static_assert(noexcept(::operator==(first, nullptr)));
    static_assert(noexcept(::operator>(first, second)));
    static_assert(noexcept(::operator<=(first, second)));
    static_assert(noexcept(::operator>=(first, second)));
    VerifyComparisonResultConversions<ReferenceComparisonOwner>();
}

TEST(NoexceptFailureTests, PodAddressAndMutableBufferGetterFailuresPropagateBeforeReading){
    using namespace __hidden_noexcept_failure_tests;
    const u8 bytes[sizeof(ThrowingPodAddress)] = {};
    const BinaryByteView source{ bytes, sizeof(bytes) };
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

TEST(NoexceptFailureTests, OwnerObservationUsesImplicitCopyAndPreservesBorrowedTargetsOnFailure){
    using namespace __hidden_noexcept_failure_tests;
    static_assert(noexcept(ObservationPointer(DeclVal<const ObservationPointer&>())));
    static_assert(!noexcept(DeclVal<void(*)(ObservationPointer)noexcept>()(DeclVal<const ObservationPointer&>())));
    RefCountTarget target;
    UniquePtr<RefCountTarget, ObservationDeleter> unique(ObservationPointer{ &target });
    RefCountPtr<RefCountTarget, ObservationDeleter> shared(ObservationPointer{ &target }, s_AdoptRef);
    ScopeExit disarm([]()noexcept{ ObservationPointer::s_FailCopy = false; });
    static_assert(!noexcept(unique.get()));
    static_assert(!noexcept(unique.operator->()));
    static_assert(!noexcept(shared.get()));
    static_assert(!noexcept(shared.operator->()));
    ObservationPointer::s_FailCopy = true;
    EXPECT_THROW(EXPECT_EQ(unique.get().target, &target), u32);
    EXPECT_THROW(EXPECT_EQ(unique.operator->().target, &target), u32);
    EXPECT_THROW(EXPECT_EQ(shared.get().target, &target), u32);
    EXPECT_THROW(EXPECT_EQ(shared.operator->().target, &target), u32);
    ObservationPointer::s_FailCopy = false;
    EXPECT_EQ(unique.get().target, &target);
    EXPECT_EQ(shared.get().target, &target);
}


TEST(NoexceptFailureTests, HexAppendCapacityAndOutputFailuresPreservePublishedCharacters){
    using namespace __hidden_noexcept_failure_tests;
    ACompactString fixed;
    static_assert(noexcept(AppendHexU32(u32{}, fixed)));
    static_assert(noexcept(AppendHexU64(u64{}, fixed)));
    static_assert(noexcept(AppendHexU32UnsignedLiteral(u32{}, fixed)));
    AppendHexU64(0x0123456789abcdefu, fixed);
    EXPECT_EQ(fixed.view(), AStringView("0123456789abcdef"));
    fixed.clear();
    AppendHexU32UnsignedLiteral(0x89abcdefu, fixed);
    EXPECT_EQ(fixed.view(), AStringView("0x89abcdefu"));

    ThrowingHexCapacityString capacity;
    static_assert(!noexcept(AppendHexU64(u64{}, capacity)));
    EXPECT_THROW(AppendHexU64(1u, capacity), u32);
    EXPECT_TRUE(capacity.text.empty());

    ThrowingHexAppendString output;
    static_assert(!noexcept(AppendHexU64(u64{}, output)));
    EXPECT_THROW(AppendHexU64(0xabcdef0123456789u, output), u32);
    EXPECT_EQ(output.text.view(), AStringView("ab"));
    output.text.clear();
    static_assert(!noexcept(AppendHexU32UnsignedLiteral(u32{}, output)));
    EXPECT_THROW(AppendHexU32UnsignedLiteral(0x89abcdefu, output), u32);
    EXPECT_EQ(output.text.view(), AStringView("0x"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

