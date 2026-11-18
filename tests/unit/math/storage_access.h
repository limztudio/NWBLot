// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <global/simdmath.h>
#include <global/simd/collision_triangle.h>
#include <global/simd/result.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::MathCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] u8* StorageBytes(T& value)noexcept{ return reinterpret_cast<u8*>(AddressOf(value)); }

template<typename T>
[[nodiscard]] const void* ConstStorageBytes(const T& value)noexcept{ return AddressOf(value); }

template<typename T>
class StorageSnapshot{
public:
    void capture(const T& value)noexcept{ FOUNDATION_MEMCPY(m_bytes.data(), m_bytes.size(), AddressOf(value), sizeof(T)); }
    [[nodiscard]] u8 operator[](const usize index)const noexcept{ return m_bytes[index]; }

private:
    Array<u8, sizeof(T)> m_bytes{};
};

template<typename T>
[[nodiscard]] T ZeroStorage()noexcept{ return T{}; }

template<typename T>
[[nodiscard]] T& BeginStorage(void* address)noexcept{ return *::new(address) T{}; }

template<typename T>
void EndStorage(T& value)noexcept{ value.~T(); }

template<typename T, typename Value>
void SetStorageLane(T& record, const usize index, const Value value)noexcept{
    record.raw[index] = static_cast<RemoveCVRef_T<decltype(record.raw[index])>>(value);
}

template<typename T>
[[nodiscard]] auto StorageLane(const T& record, const usize index)noexcept{ return record.raw[index]; }

template<typename Element, usize Count>
struct RawEqualityStorage{
    Array<Element, Count> raw{};
    friend b8 operator==(const RawEqualityStorage& lhs, const RawEqualityStorage& rhs)noexcept{
        return MathTypeDetail::EqualElements<Element, Count>(MakeNotNull<const Element*>(lhs.raw.data()), MakeNotNull<const Element*>(rhs.raw.data()));
    }
};

#if defined(__SIZEOF_INT128__)
using WideStorageInteger = __int128;
inline constexpr b8 s_HasInt128Storage = true;
#else
using WideStorageInteger = i64;
inline constexpr b8 s_HasInt128Storage = false;
#endif

#if defined(FOUNDATION_HAS_SSE2)
inline constexpr b8 s_Sse2Storage = true;
#else
inline constexpr b8 s_Sse2Storage = false;
#endif

#if defined(__linux__)
inline constexpr b8 s_LinuxStorage = true;
#else
inline constexpr b8 s_LinuxStorage = false;
#endif

[[nodiscard]] inline WideStorageInteger WideStorageBit(const usize bit)noexcept{ return WideStorageInteger{1} << bit; }
[[nodiscard]] inline WideStorageInteger AddWide(const WideStorageInteger left, const WideStorageInteger right)noexcept{ return left + right; }
[[nodiscard]] inline WideStorageInteger NegateWide(const WideStorageInteger value)noexcept{ return -value; }

template<typename T>
FOUNDATION_NOINLINE b8 EqualStorage(const T& lhs, const T& rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    return lhs == rhs;
}

template<typename T>
FOUNDATION_NOINLINE b8 StrictLaneEqual(const T& lhs, const T& rhs, const usize lane)noexcept{
#pragma STDC FENV_ACCESS ON
    return lhs.raw[lane] == rhs.raw[lane];
}

template<typename T>
FOUNDATION_NOINLINE b8 DefaultLaneEqual(const T& lhs, const T& rhs, const usize lane)noexcept{
#pragma STDC FENV_ACCESS OFF
    return lhs.raw[lane] == rhs.raw[lane];
}

template<typename Output, typename Input>
[[nodiscard]] usize TransformTriple(Output& output, const usize outStride, const Input& input, const usize inStride, const usize count, const SIMDMatrixDouble& matrix)noexcept{
    return reinterpret_cast<usize>(VectorDouble3TransformCoordStream(MakeNotNull(AddressOf(output)), outStride, MakeNotNull(AddressOf(input)), inStride, count, matrix).get());
}

template<typename Output, typename Input>
[[nodiscard]] usize TransformPair(Output& output, const usize outStride, const Input& input, const usize inStride, const usize count, const SIMDMatrixDouble& matrix)noexcept{
    return reinterpret_cast<usize>(VectorDouble2TransformCoordStream(MakeNotNull(AddressOf(output)), outStride, MakeNotNull(AddressOf(input)), inStride, count, matrix).get());
}

template<typename T>
[[nodiscard]] auto& SingleMatrixRow(T& matrix, const usize row)noexcept{ return matrix.v[row]; }

template<typename Result>
FOUNDATION_NOINLINE auto LoadVectorResult(const Expected<Result>& result)noexcept{ return result->value; }

template<typename Result>
[[nodiscard]] usize VectorValueAddress(const Expected<Result>& result)noexcept{ return reinterpret_cast<usize>(AddressOf(result->value)); }

template<typename VectorT>
struct ThrowingStreamLoadResult{
    operator VectorT()const{ throw u32{0xE208u}; }
};

struct SingleStreamLoadInput{};
struct DoubleStreamLoadInput{};
[[nodiscard]] inline ThrowingStreamLoadResult<SIMDVectorSingle> LoadFloat(const SingleStreamLoadInput&)noexcept{ return {}; }
[[nodiscard]] inline ThrowingStreamLoadResult<SIMDVectorDouble> LoadDouble(const DoubleStreamLoadInput&)noexcept{ return {}; }

#define NWB_TEST_SINGLE_STREAM(output, input, matrix, callback) \
    ::SIMDVectorDetail::VectorTransformStreamImpl(MakeNotNull(AddressOf(output)), sizeof(output), MakeNotNull(AddressOf(input)), sizeof(input), 1u, matrix, callback)
#define NWB_TEST_DOUBLE_STREAM(output, input, matrix, callback) \
    ::SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(MakeNotNull(AddressOf(output)), sizeof(output), MakeNotNull(AddressOf(input)), sizeof(input), 1u, matrix, callback)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

