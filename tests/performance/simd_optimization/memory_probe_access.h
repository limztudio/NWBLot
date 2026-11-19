// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/virtual_pages.h>
#include <global/simd/type.h>

#include <cstring>
#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SimdOptimizationBenchmark::MemoryProbe{

using NWB::Tests::ByteAddress;
using NWB::Tests::ReadAddress;

#if defined(FOUNDATION_HAS_AVX)
inline constexpr b8 s_HasAvx = true;
#else
inline constexpr b8 s_HasAvx = false;
#endif

#if defined(FOUNDATION_PLATFORM_LINUX)
inline constexpr b8 s_Linux = true;
#else
inline constexpr b8 s_Linux = false;
#endif

class AlignedProbeBytes final : NoCopy{
public:
    explicit AlignedProbeBytes(usize size)
        : m_data(::new(::operator new(size, AlignValT{64u})) u8[size])
    {}
    ~AlignedProbeBytes()noexcept{ ::operator delete(m_data, AlignValT{64u}); }
    [[nodiscard]] ByteAddress data()const noexcept{ return m_data; }

private:
    ByteAddress m_data;
};

[[nodiscard]] inline usize Address(ReadAddress ptr)noexcept{ return reinterpret_cast<usize>(ptr); }
[[nodiscard]] inline ByteAddress AssumeAligned(ReadAddress ptr)noexcept{
    return static_cast<ByteAddress>(__builtin_assume_aligned(const_cast<void*>(ptr), s_SIMDMemoryCopyAlignment));
}
[[nodiscard]] inline ByteAddress BeginWords(ByteAddress ptr, usize count)noexcept{
    return reinterpret_cast<ByteAddress>(::new(ptr) u64[count]);
}
[[nodiscard]] inline u64& Word(ByteAddress ptr, usize lane)noexcept{ return reinterpret_cast<u64*>(ptr)[lane]; }
[[nodiscard]] inline Span<const u8> ByteSpan(ByteAddress ptr, usize count)noexcept{ return { ptr, count }; }

template<usize Count>
inline void CopyPacket(ByteAddress dst, ReadAddress src)noexcept{
#if defined(FOUNDATION_HAS_AVX)
    const auto* in = static_cast<const u8*>(src);
    if constexpr(Count == 128u){
        const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in));
        const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 32u));
        const __m256i c = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 64u));
        const __m256i d = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + 96u));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), a);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 32u), b);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 64u), c);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 96u), d);
    }
    else if constexpr(Count == 32u)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in)));
    else if constexpr(Count == 16u)
        _mm_store_si128(reinterpret_cast<__m128i*>(dst), _mm_load_si128(reinterpret_cast<const __m128i*>(in)));
    else
#endif
        __builtin_memcpy(dst, src, Count);
}

template<usize Words>
[[nodiscard]] inline b8 CompareWords(ByteAddress lhs, ByteAddress rhs)noexcept{
    return MathTypeDetail::EqualElements<u64, Words>(MakeNotNull(reinterpret_cast<const u64*>(lhs)), MakeNotNull(reinterpret_cast<const u64*>(rhs)));
}

template<usize Words>
[[nodiscard]] inline b8 TestWordBits(ByteAddress lhs, ByteAddress rhs)noexcept{
#if defined(FOUNDATION_HAS_AVX)
    static_assert(Words == 4u);
    const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
    const __m256i right = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs));
    const __m256i bits = _mm256_castps_si256(_mm256_xor_ps(_mm256_castsi256_ps(left), _mm256_castsi256_ps(right)));
    return _mm256_testz_si256(bits, bits) != 0;
#else
    return CompareWords<Words>(lhs, rhs);
#endif
}

template<typename Callback>
[[nodiscard]] inline b8 OpaqueEquality(Callback callback, ByteAddress left, ByteAddress right){
    auto* volatile function = +callback;
    return function(left, right);
}

inline void FlushProbeStdout(){ FOUNDATION_COUT.flush(); }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

