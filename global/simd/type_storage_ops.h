// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_matrix_storage.h"
#include "type_register.h"
#include "type_vector_aligned.h"
#include "type_vector_unaligned.h"

#include "../hash_utils.h"
#include "../not_null.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MathTypeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE4) || defined(NWB_HAS_NEON) || defined(NWB_HAS_SSE2)
inline constexpr bool s_NativeEqualityPackets = true;
#else
inline constexpr bool s_NativeEqualityPackets = false;
#endif
inline constexpr i32 s_ByteMovemaskAllEqual = 0xFFFF;

template<typename Element>
using EqualityPacket = Element __attribute__((ext_vector_type(s_SIMDVectorBytes / sizeof(Element))));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool EqualFloatPacket(const EqualityPacket<f32> lhs, const EqualityPacket<f32> rhs)noexcept{
#pragma STDC FENV_ACCESS ON
#if defined(NWB_HAS_SSE2)
    return _mm_movemask_ps(_mm_cmpeq_ps(BitCast<__m128>(lhs), BitCast<__m128>(rhs))) == 0xFu;
#elif defined(NWB_HAS_NEON)
    const uint64x2_t equal = vreinterpretq_u64_u32(vceqq_f32(BitCast<float32x4_t>(lhs), BitCast<float32x4_t>(rhs)));
    return vgetq_lane_u64(equal, 0) == ~u64{ 0u } && vgetq_lane_u64(equal, 1) == ~u64{ 0u };
#else
    return __builtin_reduce_and(lhs == rhs) != 0;
#endif
}

template<typename Element>
[[nodiscard]] NWB_INLINE bool EqualIntegralPacket(const EqualityPacket<Element> lhs, const EqualityPacket<Element> rhs)noexcept{
#if defined(NWB_HAS_SSE2)
    return _mm_movemask_epi8(_mm_cmpeq_epi8(BitCast<__m128i>(lhs), BitCast<__m128i>(rhs))) == s_ByteMovemaskAllEqual;
#elif defined(NWB_HAS_NEON)
    const uint64x2_t equal = vreinterpretq_u64_u8(vceqq_u8(BitCast<uint8x16_t>(lhs), BitCast<uint8x16_t>(rhs)));
    return vgetq_lane_u64(equal, 0) == ~u64{ 0u } && vgetq_lane_u64(equal, 1) == ~u64{ 0u };
#else
    return __builtin_reduce_and(lhs == rhs) != 0;
#endif
}

template<typename Element, usize Count>
[[nodiscard]] NWB_INLINE bool EqualElements(const NotNull<const Element*> lhs, const NotNull<const Element*> rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    // Clang bool vectors use packed bits; wide integers are not supported native lane elements.
    if constexpr(s_NativeEqualityPackets && (IsSame_V<Element, f32> || (IsIntegral_V<Element> && !IsSame_V<Element, bool> && sizeof(Element) <= sizeof(u64)))){
        constexpr usize s_Lanes = s_SIMDVectorBytes / sizeof(Element);
        for(usize first = 0u; first < Count; first += s_Lanes){
            const usize count = Count - first < s_Lanes ? Count - first : s_Lanes;
            EqualityPacket<Element> lhsPacket{}, rhsPacket{};
            // Exact-sized loads support borrowed unaligned arrays and two/three-lane tails.
            __builtin_memcpy(&lhsPacket, lhs.get() + first, count * sizeof(Element));
            __builtin_memcpy(&rhsPacket, rhs.get() + first, count * sizeof(Element));
            if constexpr(IsSame_V<Element, f32>){
                using UInt4 = EqualityPacket<u32>;
                const UInt4 lhsMagnitude = BitCast<UInt4>(lhsPacket) & UInt4{ 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu };
                const UInt4 rhsMagnitude = BitCast<UInt4>(rhsPacket) & UInt4{ 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu };
                const UInt4 infinity{ 0x7F800000u, 0x7F800000u, 0x7F800000u, 0x7F800000u };
                const UInt4 minimumNormal{ 0x00800000u, 0x00800000u, 0x00800000u, 0x00800000u };
                const auto exceptional = (lhsMagnitude > infinity) | (rhsMagnitude > infinity) | ((lhsMagnitude != UInt4{}) & (lhsMagnitude < minimumNormal)) | ((rhsMagnitude != UInt4{}) & (rhsMagnitude < minimumNormal));
                if(__builtin_reduce_or(exceptional) != 0){
                    // Preserve skipped NaN/denormal flags and traps after an earlier mismatch.
                    for(usize lane = 0u; lane < count; ++lane){
                        if(lhs.get()[first + lane] != rhs.get()[first + lane])
                            return false;
                    }
                }
                else if(!EqualFloatPacket(lhsPacket, rhsPacket))
                    return false;
            }
            else if(!EqualIntegralPacket(lhsPacket, rhsPacket))
                return false;
        }
        return true;
    }
    else{
        for(usize i = 0u; i < Count; ++i){
            if(lhs.get()[i] != rhs.get()[i])
                return false;
        }
        return true;
    }
}

template<typename Value, usize Count>
[[nodiscard]] NWB_INLINE bool EqualArray(const Value& lhs, const Value& rhs)noexcept{
    using Element = RemoveCVRef_T<decltype(lhs.raw[0])>;
    return EqualElements<Element, Count>(MakeNotNull<const Element*>(lhs.raw), MakeNotNull<const Element*>(rhs.raw));
}

template<typename Value>
[[nodiscard]] NWB_INLINE bool EqualMixedFloat3(const Value& lhs, const Value& rhs)noexcept{
    f32 lhsValues[3], rhsValues[3];
    // Copy XYZ as storage so an unused signaling NaN never crosses a scalar FP ABI.
    __builtin_memcpy(lhsValues, &lhs, sizeof(lhsValues));
    __builtin_memcpy(rhsValues, &rhs, sizeof(rhsValues));
    return EqualElements<f32, 3u>(MakeNotNull<const f32*>(lhsValues), MakeNotNull<const f32*>(rhsValues)) && lhs.w == rhs.w;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Value, usize Count>
[[nodiscard]] NWB_INLINE usize HashArray(const Value& value)noexcept{
    using Element = RemoveCVRef_T<decltype(value.raw[0])>;
    usize seed = 0u;
    for(usize i = 0u; i < Count; ++i){
        if constexpr(IsSame_V<Element, f32> || IsSame_V<Element, f64>)
            ::HashCombineFloating(seed, value.raw[i]);
        else
            ::HashCombine(seed, value.raw[i]);
    }
    return seed;
}

template<typename Value>
[[nodiscard]] NWB_INLINE usize HashMixed3(const Value& value)noexcept{
    usize seed = 0u;
    ::HashCombineFloating(seed, value.x);
    ::HashCombineFloating(seed, value.y);
    ::HashCombineFloating(seed, value.z);
    ::HashCombine(seed, value.w);
    return seed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
concept MathStorageValue =
    IsSame_V<T, Half2U> || IsSame_V<T, Half4U> ||
    IsSame_V<T, Double2> || IsSame_V<T, Double3> ||
    IsSame_V<T, Float4> || IsSame_V<T, Double4> ||
    IsSame_V<T, Double33> || IsSame_V<T, Float34> ||
    IsSame_V<T, Double34> || IsSame_V<T, Float44> ||
    IsSame_V<T, Double44> || IsSame_V<T, Int4> ||
    IsSame_V<T, Int64x4> || IsSame_V<T, UInt4> ||
    IsSame_V<T, UInt64x4> || IsSame_V<T, Float3Int> ||
    IsSame_V<T, Double3Int> || IsSame_V<T, Float3UInt> ||
    IsSame_V<T, Double3UInt> || IsSame_V<T, Float2U> ||
    IsSame_V<T, Double2U> || IsSame_V<T, Float3U> ||
    IsSame_V<T, Double3U> || IsSame_V<T, Float4U> ||
    IsSame_V<T, Double4U> || IsSame_V<T, Float33U> ||
    IsSame_V<T, Double33U> || IsSame_V<T, Float34U> ||
    IsSame_V<T, Double34U> || IsSame_V<T, Float44U> ||
    IsSame_V<T, Double44U> || IsSame_V<T, Int2U> ||
    IsSame_V<T, Int64x2U> || IsSame_V<T, Int3U> ||
    IsSame_V<T, Int64x3U> || IsSame_V<T, Int4U> ||
    IsSame_V<T, Int64x4U> || IsSame_V<T, UInt2U> ||
    IsSame_V<T, UInt64x2U> || IsSame_V<T, UInt3U> ||
    IsSame_V<T, UInt64x3U> || IsSame_V<T, UInt4U> ||
    IsSame_V<T, UInt64x4U>
;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<Half2U>{
    usize operator()(const Half2U& value)const noexcept{ return MathTypeDetail::HashArray<Half2U, 2u>(value); }
};

template<>
struct hash<Half4U>{
    usize operator()(const Half4U& value)const noexcept{ return MathTypeDetail::HashArray<Half4U, 4u>(value); }
};

template<>
struct hash<Double2>{
    usize operator()(const Double2& value)const noexcept{ return MathTypeDetail::HashArray<Double2, 2u>(value); }
};

template<>
struct hash<Double3>{
    usize operator()(const Double3& value)const noexcept{ return MathTypeDetail::HashArray<Double3, 3u>(value); }
};

template<>
struct hash<Float4>{
    usize operator()(const Float4& value)const noexcept{ return MathTypeDetail::HashArray<Float4, 4u>(value); }
};

template<>
struct hash<Double4>{
    usize operator()(const Double4& value)const noexcept{ return MathTypeDetail::HashArray<Double4, 4u>(value); }
};

template<>
struct hash<Double33>{
    usize operator()(const Double33& value)const noexcept{ return MathTypeDetail::HashArray<Double33, 9u>(value); }
};

template<>
struct hash<Float34>{
    usize operator()(const Float34& value)const noexcept{ return MathTypeDetail::HashArray<Float34, 12u>(value); }
};

template<>
struct hash<Double34>{
    usize operator()(const Double34& value)const noexcept{ return MathTypeDetail::HashArray<Double34, 12u>(value); }
};

template<>
struct hash<Float44>{
    usize operator()(const Float44& value)const noexcept{ return MathTypeDetail::HashArray<Float44, 16u>(value); }
};

template<>
struct hash<Double44>{
    usize operator()(const Double44& value)const noexcept{ return MathTypeDetail::HashArray<Double44, 16u>(value); }
};

template<>
struct hash<Int4>{
    usize operator()(const Int4& value)const noexcept{ return MathTypeDetail::HashArray<Int4, 4u>(value); }
};

template<>
struct hash<Int64x4>{
    usize operator()(const Int64x4& value)const noexcept{ return MathTypeDetail::HashArray<Int64x4, 4u>(value); }
};

template<>
struct hash<UInt4>{
    usize operator()(const UInt4& value)const noexcept{ return MathTypeDetail::HashArray<UInt4, 4u>(value); }
};

template<>
struct hash<UInt64x4>{
    usize operator()(const UInt64x4& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x4, 4u>(value); }
};

template<>
struct hash<Float3Int>{
    usize operator()(const Float3Int& value)const noexcept{ return MathTypeDetail::HashMixed3(value); }
};

template<>
struct hash<Double3Int>{
    usize operator()(const Double3Int& value)const noexcept{ return MathTypeDetail::HashMixed3(value); }
};

template<>
struct hash<Float3UInt>{
    usize operator()(const Float3UInt& value)const noexcept{ return MathTypeDetail::HashMixed3(value); }
};

template<>
struct hash<Double3UInt>{
    usize operator()(const Double3UInt& value)const noexcept{ return MathTypeDetail::HashMixed3(value); }
};

template<>
struct hash<Float2U>{
    usize operator()(const Float2U& value)const noexcept{ return MathTypeDetail::HashArray<Float2U, 2u>(value); }
};

template<>
struct hash<Double2U>{
    usize operator()(const Double2U& value)const noexcept{ return MathTypeDetail::HashArray<Double2U, 2u>(value); }
};

template<>
struct hash<Float3U>{
    usize operator()(const Float3U& value)const noexcept{ return MathTypeDetail::HashArray<Float3U, 3u>(value); }
};

template<>
struct hash<Double3U>{
    usize operator()(const Double3U& value)const noexcept{ return MathTypeDetail::HashArray<Double3U, 3u>(value); }
};

template<>
struct hash<Float4U>{
    usize operator()(const Float4U& value)const noexcept{ return MathTypeDetail::HashArray<Float4U, 4u>(value); }
};

template<>
struct hash<Double4U>{
    usize operator()(const Double4U& value)const noexcept{ return MathTypeDetail::HashArray<Double4U, 4u>(value); }
};

template<>
struct hash<Float33U>{
    usize operator()(const Float33U& value)const noexcept{ return MathTypeDetail::HashArray<Float33U, 9u>(value); }
};

template<>
struct hash<Double33U>{
    usize operator()(const Double33U& value)const noexcept{ return MathTypeDetail::HashArray<Double33U, 9u>(value); }
};

template<>
struct hash<Float34U>{
    usize operator()(const Float34U& value)const noexcept{ return MathTypeDetail::HashArray<Float34U, 12u>(value); }
};

template<>
struct hash<Double34U>{
    usize operator()(const Double34U& value)const noexcept{ return MathTypeDetail::HashArray<Double34U, 12u>(value); }
};

template<>
struct hash<Float44U>{
    usize operator()(const Float44U& value)const noexcept{ return MathTypeDetail::HashArray<Float44U, 16u>(value); }
};

template<>
struct hash<Double44U>{
    usize operator()(const Double44U& value)const noexcept{ return MathTypeDetail::HashArray<Double44U, 16u>(value); }
};

template<>
struct hash<Int2U>{
    usize operator()(const Int2U& value)const noexcept{ return MathTypeDetail::HashArray<Int2U, 2u>(value); }
};

template<>
struct hash<Int64x2U>{
    usize operator()(const Int64x2U& value)const noexcept{ return MathTypeDetail::HashArray<Int64x2U, 2u>(value); }
};

template<>
struct hash<Int3U>{
    usize operator()(const Int3U& value)const noexcept{ return MathTypeDetail::HashArray<Int3U, 3u>(value); }
};

template<>
struct hash<Int64x3U>{
    usize operator()(const Int64x3U& value)const noexcept{ return MathTypeDetail::HashArray<Int64x3U, 3u>(value); }
};

template<>
struct hash<Int4U>{
    usize operator()(const Int4U& value)const noexcept{ return MathTypeDetail::HashArray<Int4U, 4u>(value); }
};

template<>
struct hash<Int64x4U>{
    usize operator()(const Int64x4U& value)const noexcept{ return MathTypeDetail::HashArray<Int64x4U, 4u>(value); }
};

template<>
struct hash<UInt2U>{
    usize operator()(const UInt2U& value)const noexcept{ return MathTypeDetail::HashArray<UInt2U, 2u>(value); }
};

template<>
struct hash<UInt64x2U>{
    usize operator()(const UInt64x2U& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x2U, 2u>(value); }
};

template<>
struct hash<UInt3U>{
    usize operator()(const UInt3U& value)const noexcept{ return MathTypeDetail::HashArray<UInt3U, 3u>(value); }
};

template<>
struct hash<UInt64x3U>{
    usize operator()(const UInt64x3U& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x3U, 3u>(value); }
};

template<>
struct hash<UInt4U>{
    usize operator()(const UInt4U& value)const noexcept{ return MathTypeDetail::HashArray<UInt4U, 4u>(value); }
};

template<>
struct hash<UInt64x4U>{
    usize operator()(const UInt64x4U& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x4U, 4u>(value); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Matrix>
    requires(MathStorageValue<Matrix> && requires(Matrix value){
        value.raw[0];
        value._11;
        value._22;
        value._33;
    }) [[nodiscard]] constexpr Matrix MatrixIdentity()noexcept(noexcept(Matrix{})){
    Matrix matrix = {};
    using Element = RemoveCVRef_T<decltype(matrix._11)>;
    matrix._11 = Element{ 1 };
    matrix._22 = Element{ 1 };
    matrix._33 = Element{ 1 };
    if constexpr(requires{ matrix._44; })
        matrix._44 = Element{ 1 };
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<MathStorageValue Value>
[[nodiscard]] NWB_INLINE bool operator==(const Value& lhs, const Value& rhs)noexcept{
    if constexpr(requires{ lhs.raw; }){
        constexpr usize s_Count = sizeof(lhs.raw) / sizeof(lhs.raw[0]);
        return MathTypeDetail::EqualArray<Value, s_Count>(lhs, rhs);
    }
    else if constexpr(IsSame_V<Value, Float3Int> || IsSame_V<Value, Float3UInt>)
        return MathTypeDetail::EqualMixedFloat3(lhs, rhs);
    else
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

template<MathStorageValue Value>
[[nodiscard]] NWB_INLINE bool operator!=(const Value& lhs, const Value& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

