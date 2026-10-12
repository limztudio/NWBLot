// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "macro.h"
#include "../type.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_SIMDMemoryCopyAlignment = 16u;

#define NWB_SIMD_MEMCPY(destination, destinationCapacity, source, byteCount) ::SIMDMemoryCopy(destination, destinationCapacity, source, byteCount)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Copies exactly byteCount bytes between disjoint ranges in normal CPU-accessible memory; SIMD packets use ordinary cached accesses.
// Requests above 8192 bytes use the CRT, and partial packet tails use fixed-size builtin copies.
// Both pointers must be aligned to s_SIMDMemoryCopyAlignment for a nonzero count.
// Destination must already contain byte storage or live trivially copyable objects; this operation does not establish typed object lifetimes.
// Insufficient capacity, null pointers, misalignment, or overlapping ranges terminate before either range is accessed.
// A zero count does not inspect either range.
// Mapped GPU memory requires the same CPU access contract, with synchronization and visibility managed by the caller.
NWB_INLINE void SIMDMemoryCopy(void* destination, usize destinationCapacity, const void* source, usize byteCount)noexcept{
    if(byteCount == 0u)
        return;
    if(byteCount > destinationCapacity || destination == nullptr || source == nullptr)
        ::abort();

    const usize destinationAddress = reinterpret_cast<usize>(destination);
    const usize sourceAddress = reinterpret_cast<usize>(source);
    if(((destinationAddress | sourceAddress) & (s_SIMDMemoryCopyAlignment - 1u)) != 0u)
        ::abort();
    const usize separation = destinationAddress >= sourceAddress ? destinationAddress - sourceAddress : sourceAddress - destinationAddress;
    if(separation < byteCount)
        ::abort();

    auto* dst = static_cast<u8*>(__builtin_assume_aligned(destination, s_SIMDMemoryCopyAlignment));
    const auto* src = static_cast<const u8*>(__builtin_assume_aligned(source, s_SIMDMemoryCopyAlignment));

#if defined(NWB_HAS_NEON) || defined(NWB_HAS_SSE2)
    if(byteCount > 8192u){
        __builtin_memcpy(dst, src, byteCount);
        return;
    }

#if defined(NWB_HAS_NEON)
    while(byteCount >= 64u){
        const uint8x16_t packet0 = vld1q_u8(src);
        const uint8x16_t packet1 = vld1q_u8(src + 16u);
        const uint8x16_t packet2 = vld1q_u8(src + 32u);
        const uint8x16_t packet3 = vld1q_u8(src + 48u);
        vst1q_u8(dst, packet0);
        vst1q_u8(dst + 16u, packet1);
        vst1q_u8(dst + 32u, packet2);
        vst1q_u8(dst + 48u, packet3);
        dst += 64u;
        src += 64u;
        byteCount -= 64u;
    }
    while(byteCount >= 16u){
        vst1q_u8(dst, vld1q_u8(src));
        dst += 16u;
        src += 16u;
        byteCount -= 16u;
    }
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if(((destinationAddress | sourceAddress) & 31u) == 0u){
        auto* dst32 = static_cast<u8*>(__builtin_assume_aligned(dst, 32u));
        const auto* src32 = static_cast<const u8*>(__builtin_assume_aligned(src, 32u));
        while(byteCount >= 128u){
            const __m256i packet0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32));
            const __m256i packet1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32 + 32u));
            const __m256i packet2 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32 + 64u));
            const __m256i packet3 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32 + 96u));
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32), packet0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32 + 32u), packet1);
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32 + 64u), packet2);
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32 + 96u), packet3);
            dst32 += 128u;
            src32 += 128u;
            byteCount -= 128u;
        }
        while(byteCount >= 32u){
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32), _mm256_load_si256(reinterpret_cast<const __m256i*>(src32)));
            dst32 += 32u;
            src32 += 32u;
            byteCount -= 32u;
        }
        dst = dst32;
        src = src32;
    }
#endif
    while(byteCount >= 64u){
        const __m128i packet0 = _mm_load_si128(reinterpret_cast<const __m128i*>(src));
        const __m128i packet1 = _mm_load_si128(reinterpret_cast<const __m128i*>(src + 16u));
        const __m128i packet2 = _mm_load_si128(reinterpret_cast<const __m128i*>(src + 32u));
        const __m128i packet3 = _mm_load_si128(reinterpret_cast<const __m128i*>(src + 48u));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst), packet0);
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + 16u), packet1);
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + 32u), packet2);
        _mm_store_si128(reinterpret_cast<__m128i*>(dst + 48u), packet3);
        dst += 64u;
        src += 64u;
        byteCount -= 64u;
    }
    while(byteCount >= 16u){
        _mm_store_si128(reinterpret_cast<__m128i*>(dst), _mm_load_si128(reinterpret_cast<const __m128i*>(src)));
        dst += 16u;
        src += 16u;
        byteCount -= 16u;
    }
#endif

    if(byteCount >= 8u){
        __builtin_memcpy(dst, src, 8u);
        dst += 8u;
        src += 8u;
        byteCount -= 8u;
    }
    if(byteCount >= 4u){
        __builtin_memcpy(dst, src, 4u);
        dst += 4u;
        src += 4u;
        byteCount -= 4u;
    }
    if(byteCount >= 2u){
        __builtin_memcpy(dst, src, 2u);
        dst += 2u;
        src += 2u;
        byteCount -= 2u;
    }
    if(byteCount != 0u)
        __builtin_memcpy(dst, src, 1u);
#else
    __builtin_memcpy(dst, src, byteCount);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

