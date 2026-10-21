// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "macro.h"
#include "../type.h"
#include "../not_null.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_SIMDMemoryCopyAlignment = 16u;
constexpr usize s_SIMDMemoryCopyWideAlignment = 32u;
constexpr usize s_SIMDMemoryCopyCrtThreshold = 8192u;
constexpr usize s_SIMDMemoryCopyPacketBytes = 16u;
constexpr usize s_SIMDMemoryCopyBatchBytes = 64u;
constexpr usize s_SIMDMemoryCopyWidePacketBytes = 32u;
constexpr usize s_SIMDMemoryCopyWideBatchBytes = 128u;
constexpr usize s_SIMDMemoryCopyQuadWordBytes = 8u;
constexpr usize s_SIMDMemoryCopyWordBytes = 4u;
constexpr usize s_SIMDMemoryCopyHalfWordBytes = 2u;

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

    NotNull<u8*> dst(static_cast<u8*>(__builtin_assume_aligned(destination, s_SIMDMemoryCopyAlignment)));
    NotNull<const u8*> src(static_cast<const u8*>(__builtin_assume_aligned(source, s_SIMDMemoryCopyAlignment)));

#if defined(NWB_HAS_NEON) || defined(NWB_HAS_SSE2)
    if(byteCount > s_SIMDMemoryCopyCrtThreshold){
        __builtin_memcpy(dst.get(), src.get(), byteCount);
        return;
    }

#if defined(NWB_HAS_NEON)
    while(byteCount >= s_SIMDMemoryCopyBatchBytes){
        const uint8x16_t packet0 = vld1q_u8(src.get());
        const uint8x16_t packet1 = vld1q_u8(src.get() + s_SIMDMemoryCopyPacketBytes);
        const uint8x16_t packet2 = vld1q_u8(src.get() + s_SIMDMemoryCopyPacketBytes * 2u);
        const uint8x16_t packet3 = vld1q_u8(src.get() + s_SIMDMemoryCopyPacketBytes * 3u);
        vst1q_u8(dst.get(), packet0);
        vst1q_u8(dst.get() + s_SIMDMemoryCopyPacketBytes, packet1);
        vst1q_u8(dst.get() + s_SIMDMemoryCopyPacketBytes * 2u, packet2);
        vst1q_u8(dst.get() + s_SIMDMemoryCopyPacketBytes * 3u, packet3);
        dst = dst.get() + s_SIMDMemoryCopyBatchBytes;
        src = src.get() + s_SIMDMemoryCopyBatchBytes;
        byteCount -= s_SIMDMemoryCopyBatchBytes;
    }
    while(byteCount >= s_SIMDMemoryCopyPacketBytes){
        vst1q_u8(dst.get(), vld1q_u8(src.get()));
        dst = dst.get() + s_SIMDMemoryCopyPacketBytes;
        src = src.get() + s_SIMDMemoryCopyPacketBytes;
        byteCount -= s_SIMDMemoryCopyPacketBytes;
    }
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if(((destinationAddress | sourceAddress) & (s_SIMDMemoryCopyWideAlignment - 1u)) == 0u){
        NotNull<u8*> dst32(static_cast<u8*>(__builtin_assume_aligned(dst.get(), s_SIMDMemoryCopyWideAlignment)));
        NotNull<const u8*> src32(static_cast<const u8*>(__builtin_assume_aligned(src.get(), s_SIMDMemoryCopyWideAlignment)));
        while(byteCount >= s_SIMDMemoryCopyWideBatchBytes){
            const __m256i packet0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32.get()));
            const __m256i packet1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32.get() + s_SIMDMemoryCopyWidePacketBytes));
            const __m256i packet2 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32.get() + s_SIMDMemoryCopyWidePacketBytes * 2u));
            const __m256i packet3 = _mm256_load_si256(reinterpret_cast<const __m256i*>(src32.get() + s_SIMDMemoryCopyWidePacketBytes * 3u));
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32.get()), packet0);
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32.get() + s_SIMDMemoryCopyWidePacketBytes), packet1);
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32.get() + s_SIMDMemoryCopyWidePacketBytes * 2u), packet2);
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32.get() + s_SIMDMemoryCopyWidePacketBytes * 3u), packet3);
            dst32 = dst32.get() + s_SIMDMemoryCopyWideBatchBytes;
            src32 = src32.get() + s_SIMDMemoryCopyWideBatchBytes;
            byteCount -= s_SIMDMemoryCopyWideBatchBytes;
        }
        while(byteCount >= s_SIMDMemoryCopyWidePacketBytes){
            _mm256_store_si256(reinterpret_cast<__m256i*>(dst32.get()), _mm256_load_si256(reinterpret_cast<const __m256i*>(src32.get())));
            dst32 = dst32.get() + s_SIMDMemoryCopyWidePacketBytes;
            src32 = src32.get() + s_SIMDMemoryCopyWidePacketBytes;
            byteCount -= s_SIMDMemoryCopyWidePacketBytes;
        }
        dst = dst32;
        src = src32;
    }
#endif
    while(byteCount >= s_SIMDMemoryCopyBatchBytes){
        const __m128i packet0 = _mm_load_si128(reinterpret_cast<const __m128i*>(src.get()));
        const __m128i packet1 = _mm_load_si128(reinterpret_cast<const __m128i*>(src.get() + s_SIMDMemoryCopyPacketBytes));
        const __m128i packet2 = _mm_load_si128(reinterpret_cast<const __m128i*>(src.get() + s_SIMDMemoryCopyPacketBytes * 2u));
        const __m128i packet3 = _mm_load_si128(reinterpret_cast<const __m128i*>(src.get() + s_SIMDMemoryCopyPacketBytes * 3u));
        _mm_store_si128(reinterpret_cast<__m128i*>(dst.get()), packet0);
        _mm_store_si128(reinterpret_cast<__m128i*>(dst.get() + s_SIMDMemoryCopyPacketBytes), packet1);
        _mm_store_si128(reinterpret_cast<__m128i*>(dst.get() + s_SIMDMemoryCopyPacketBytes * 2u), packet2);
        _mm_store_si128(reinterpret_cast<__m128i*>(dst.get() + s_SIMDMemoryCopyPacketBytes * 3u), packet3);
        dst = dst.get() + s_SIMDMemoryCopyBatchBytes;
        src = src.get() + s_SIMDMemoryCopyBatchBytes;
        byteCount -= s_SIMDMemoryCopyBatchBytes;
    }
    while(byteCount >= s_SIMDMemoryCopyPacketBytes){
        _mm_store_si128(reinterpret_cast<__m128i*>(dst.get()), _mm_load_si128(reinterpret_cast<const __m128i*>(src.get())));
        dst = dst.get() + s_SIMDMemoryCopyPacketBytes;
        src = src.get() + s_SIMDMemoryCopyPacketBytes;
        byteCount -= s_SIMDMemoryCopyPacketBytes;
    }
#endif

    if(byteCount >= s_SIMDMemoryCopyQuadWordBytes){
        __builtin_memcpy(dst.get(), src.get(), s_SIMDMemoryCopyQuadWordBytes);
        dst = dst.get() + s_SIMDMemoryCopyQuadWordBytes;
        src = src.get() + s_SIMDMemoryCopyQuadWordBytes;
        byteCount -= s_SIMDMemoryCopyQuadWordBytes;
    }
    if(byteCount >= s_SIMDMemoryCopyWordBytes){
        __builtin_memcpy(dst.get(), src.get(), s_SIMDMemoryCopyWordBytes);
        dst = dst.get() + s_SIMDMemoryCopyWordBytes;
        src = src.get() + s_SIMDMemoryCopyWordBytes;
        byteCount -= s_SIMDMemoryCopyWordBytes;
    }
    if(byteCount >= s_SIMDMemoryCopyHalfWordBytes){
        __builtin_memcpy(dst.get(), src.get(), s_SIMDMemoryCopyHalfWordBytes);
        dst = dst.get() + s_SIMDMemoryCopyHalfWordBytes;
        src = src.get() + s_SIMDMemoryCopyHalfWordBytes;
        byteCount -= s_SIMDMemoryCopyHalfWordBytes;
    }
    if(byteCount != 0u)
        __builtin_memcpy(dst.get(), src.get(), 1u);
#else
    __builtin_memcpy(dst.get(), src.get(), byteCount);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

