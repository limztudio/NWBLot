// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <core/alloc/scratch.h>
#include <global/append_rollback.h>
#include <global/binary_little_endian.h>
#include <global/bit_matrix.h>
#include <global/fixed_vector.h>
#include <global/hash_utils.h>
#include <global/hierarchical_index_set.h>
#include <global/inplace_function.h>
#include <global/filesystem/operations_file_io.h>
#include <global/sha1.h>
#include <global/numeric_text.h>
#include <tests/unit/global/fixed_vector_case_types.adhd.h>
#include <tests/unit/global/hex_case_types.adhd.h>
#include <tests/unit/global/name_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_binary_shared_tests{

struct ThrowingByteSize{};
struct ThrowingByteSizeComparison{
public:
    explicit operator b8()const{ throw RuntimeException("byte size comparison failure"); }
};
[[nodiscard]] FOUNDATION_INLINE ThrowingByteSizeComparison operator>(const usize, const ThrowingByteSize)noexcept{ return {}; }

struct ThrowingByteContainer{
    [[nodiscard]] usize size()const noexcept{ return 4u; }
    u8& operator[](usize){ throw RuntimeException("byte assignment failure"); }
};

struct ThrowingLvalueByteContainer{
    u8 byte = 0u;
    [[nodiscard]] usize size()const noexcept{ return 4u; }
    u8& operator[](const usize&){ throw RuntimeException("lvalue byte index failure"); }
    u8& operator[](usize&&)noexcept{ return byte; }
};

struct ThrowingSizeByteContainer{
    u8 byte = 0u;
    [[nodiscard]] ThrowingByteSize size()const noexcept{ return {}; }
    u8& operator[](usize)noexcept{ return byte; }
};

struct CharSink{
    Array<char, Sha1Digest::s_HexTextLength> data{};
    usize size = 0u;
    CharSink& operator+=(const char ch)noexcept{ data[size++] = ch; return *this; }
};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_noexcept_failure_tests{

inline constexpr u32 s_PlacementFailure = 0xE201u;
inline constexpr u32 s_ReadFailure = 0xE202u;
inline constexpr u32 s_HashFailure = 0xE203u;

struct ThrowingPlacementCallable{
public:
    static void* operator new(usize, void*){
        throw s_PlacementFailure;
    }

    void operator()()const noexcept{}
};

struct ThrowingPodAddress{
public:
    u32 value = 0u;


public:
    [[nodiscard]] void* operator&(){ throw s_ReadFailure; }
};
static_assert(IsTriviallyCopyable_V<ThrowingPodAddress>);

struct ThrowingMutableByteSource{
public:
    [[nodiscard]] u8* data(){ throw s_ReadFailure; }
};

struct ThrowingDiagnosticView{
public:
    u32& calls;


public:
    operator AStringView()const{
        ++calls;
        throw s_ReadFailure;
    }
};

struct ThrowingDiagnosticTemporary{
public:
    ~ThrowingDiagnosticTemporary()noexcept(false){ throw s_ReadFailure; }


public:
    operator AStringView()const noexcept{ return "unsafe"; }
};

struct ThrowingDiagnosticCleanup{
public:
    u32& calls;


public:
    [[nodiscard]] ThrowingDiagnosticTemporary view()const noexcept{
        ++calls;
        return {};
    }
};

struct StatefulPlacementCallable{
public:
    inline static u32 s_Allocations = 0u;
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
public:
    operator usize()const{
        throw s_HashFailure;
    }
};

struct ThrowingByteSource{
public:
    using value_type = u8;


public:
    [[nodiscard]] usize size()const noexcept{ return sizeof(u32); }

    [[nodiscard]] const u8* data()const{
        throw s_ReadFailure;
    }
};

using FunctionStorage = InplaceFunction<64u>;
inline void ResetPlacements(){ StatefulPlacementCallable::s_Allocations = 0u; }
[[nodiscard]] inline u32 Placements(){ return StatefulPlacementCallable::s_Allocations; }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{

template<>
struct hash<__hidden_noexcept_failure_tests::HashProbe>{
    [[nodiscard]] __hidden_noexcept_failure_tests::ThrowingHashValue operator()(const __hidden_noexcept_failure_tests::HashProbe&)const noexcept{
        return {};
    }
};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_hex_failure_tests{

struct ThrowingHexCapacityPredicate{
public:
    explicit operator b8()const{
        throw s_HexCapacityFailure;
    }
};

[[nodiscard]] FOUNDATION_INLINE ThrowingHexCapacityPredicate operator>(HexCapacityValue, HexCapacityValue)noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE usize operator-(const HexCapacityValue lhs, const HexCapacityValue rhs)noexcept{ return lhs.value - rhs.value; }

struct ThrowingHexCapacityString{
public:
    using value_type = char;


public:
    ACompactString text;


public:
    [[nodiscard]] HexCapacityValue size()const noexcept{ return { .value = text.size() }; }

    [[nodiscard]] HexCapacityValue max_size()const noexcept{ return { .value = text.max_size() }; }

    void push_back(const char ch)noexcept{ text.push_back(ch); }
};

struct ThrowingHexAppendString{
public:
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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fixed_vector_failure_tests{

struct ThrowingFixedVectorAssignment{
public:
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

struct FixedVectorResource{
public:
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
public:
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
public:
    FixedVectorNoDefault() = delete;
};

struct FixedVectorNonAssignable{
public:
    FixedVectorNonAssignable()noexcept = default;
    FixedVectorNonAssignable& operator=(const FixedVectorNonAssignable&) = delete;
    FixedVectorNonAssignable& operator=(FixedVectorNonAssignable&&) = delete;
};

FOUNDATION_INLINE void swap([[maybe_unused]] ThrowingFixedVectorSwap& lhs, [[maybe_unused]] ThrowingFixedVectorSwap& rhs){
    throw s_ElementSwapFailure;
}

using EmptyFixed = FixedVector<FixedVectorNoDefault, 0u>;

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename U>
inline void AssignCaseStorage(T& target, U& value)noexcept(noexcept(target = value)){ target = value; }

template<typename T>
[[nodiscard]] T MoveCaseValue(T& value)noexcept(noexcept(T(Move(value)))){ return T(Move(value)); }

template<typename T>
[[nodiscard]] b8 NonemptyCallable(const T& callable)noexcept{ return static_cast<bool>(callable); }

template<typename Count>
void FillCaseBytes(Span<u8> out, const Count count, Span<const u8> pattern){ FillRepeatingBytes(count, pattern, out.data()); }

inline void FillNullDestination(usize count, Span<const u8> pattern){ FillRepeatingBytes(count, pattern, nullptr); }

template<typename Container, typename T>
void PushTestCopy(Container& container, const T& value){ container.push_back(value); }

template<typename Container, typename Callback>
[[nodiscard]] auto WatchAppend(Container& container, Callback&& callback){
    return AppendedContainerRollbackScope(container, [fn = Forward<Callback>(callback)]()noexcept{ fn(); });
}

using CaseIndexSet = ::HierarchicalIndexSet<NWB::Core::Alloc::ScratchArena>;
inline void CallStoredFunction(__hidden_noexcept_failure_tests::FunctionStorage& value){ value(); }
[[noreturn]] inline void ThrowTestRuntime(const char* text){ throw RuntimeException(text); }

static_assert(s_BitsPerU64 == 64u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

