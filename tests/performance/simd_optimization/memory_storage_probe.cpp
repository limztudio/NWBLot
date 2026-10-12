// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/allocation_size.h>
#include <global/atomic.h>
#include <global/simd/memory.h>
#include <global/simd/type.h>
#include <global/timer.h>

#include <fenv.h>
#include <iostream>
#include <new>

#if defined(NWB_PLATFORM_LINUX)
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_memory_storage_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_Alignment = 64u;
inline constexpr usize s_StreamingBytes = 64u * 1024u * 1024u;
inline constexpr u8 s_Poison = 0xA5u;
inline constexpr int s_AbortExit = 81;
inline constexpr usize s_QualifyLengths[]{
    0u, 1u, 2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u, 47u, 48u, 49u, 63u, 64u, 65u,
    127u, 128u, 129u, 255u, 256u, 257u, 511u, 512u, 513u, 4095u, 4096u, 4097u, 5120u, 8191u, 8192u, 8193u, 16384u,
};
inline constexpr usize s_BenchLengths[]{ 16u, 31u, 32u, 33u, 64u, 128u, 129u, 256u, 512u, 1024u, 2048u, 4096u, 5120u, 8191u, 8192u };
volatile u64 g_Checksum = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AlignedBytes final{
    void* storage;
    u8* data;

    explicit AlignedBytes(usize count)
        : storage(::operator new(count, AlignValT{ s_Alignment }))
        , data(::new(storage) u8[count])
    {}
    ~AlignedBytes()noexcept{ ::operator delete(storage, AlignValT{ s_Alignment }); }
    AlignedBytes(const AlignedBytes&) = delete;
    AlignedBytes& operator=(const AlignedBytes&) = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr u8 PatternByte(usize index)noexcept{ return static_cast<u8>((index * 37u + (index >> 8u) * 13u + 11u) & 0xFFu); }

// This candidate changes the internal AVX packet alignment choice; admission, cache policy, CRT threshold, and exact tails match the public operation.
NWB_INLINE void CandidateCopy(void* destination, usize capacity, const void* source, usize count)noexcept{
#if defined(NWB_HAS_AVX)
    if(count == 0u)
        return;
    if(count > capacity || destination == nullptr || source == nullptr)
        ::abort();
    const usize destinationAddress = reinterpret_cast<usize>(destination), sourceAddress = reinterpret_cast<usize>(source);
    if(((destinationAddress | sourceAddress) & (s_SIMDMemoryCopyAlignment - 1u)) != 0u)
        ::abort();
    const usize separation = destinationAddress >= sourceAddress ? destinationAddress - sourceAddress : sourceAddress - destinationAddress;
    if(separation < count)
        ::abort();

    auto* dst = static_cast<u8*>(__builtin_assume_aligned(destination, s_SIMDMemoryCopyAlignment));
    const auto* src = static_cast<const u8*>(__builtin_assume_aligned(source, s_SIMDMemoryCopyAlignment));
    if(count > 8192u){
        __builtin_memcpy(dst, src, count);
        return;
    }
    while(count >= 128u){
        const __m256i packet0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
        const __m256i packet1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + 32u));
        const __m256i packet2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + 64u));
        const __m256i packet3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + 96u));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), packet0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 32u), packet1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 64u), packet2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 96u), packet3);
        dst += 128u;
        src += 128u;
        count -= 128u;
    }
    while(count >= 32u){
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src)));
        dst += 32u;
        src += 32u;
        count -= 32u;
    }
    if(count >= 16u){
        _mm_store_si128(reinterpret_cast<__m128i*>(dst), _mm_load_si128(reinterpret_cast<const __m128i*>(src)));
        dst += 16u;
        src += 16u;
        count -= 16u;
    }
    if(count >= 8u){
        __builtin_memcpy(dst, src, 8u);
        dst += 8u;
        src += 8u;
        count -= 8u;
    }
    if(count >= 4u){
        __builtin_memcpy(dst, src, 4u);
        dst += 4u;
        src += 4u;
        count -= 4u;
    }
    if(count >= 2u){
        __builtin_memcpy(dst, src, 2u);
        dst += 2u;
        src += 2u;
        count -= 2u;
    }
    if(count != 0u)
        __builtin_memcpy(dst, src, 1u);
#else
    SIMDMemoryCopy(destination, capacity, source, count);
#endif
}

NWB_INLINE bool CandidateEqual(const u64* lhs, const u64* rhs)noexcept{
#if defined(NWB_HAS_AVX)
    const __m256i left = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs));
    const __m256i right = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs));
    const __m256 difference = _mm256_xor_ps(_mm256_castsi256_ps(left), _mm256_castsi256_ps(right));
    const __m256i bits = _mm256_castps_si256(difference);
    // Integer VPTEST covers every bit; floating VTESTPS only examines sign bits.
    return _mm256_testz_si256(bits, bits) != 0;
#else
    return MathTypeDetail::EqualElements<u64, 4u>(lhs, rhs);
#endif
}

// Preserve an inexpensive first-word rejection before the full-width exact-bit reduction.
NWB_INLINE bool CandidateEqualFirstWord(const u64* lhs, const u64* rhs)noexcept{
    if(lhs[0] != rhs[0])
        return false;
    return CandidateEqual(lhs, rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool CheckRange(const u8* bytes, usize length, usize start, usize count, usize sourceStart)noexcept{
    for(usize index = 0u; index < length; ++index){
        const u8 expected = index >= start && index - start < count ? PatternByte(sourceStart + index - start) : s_Poison;
        if(bytes[index] != expected)
            return false;
    }
    return true;
}

[[nodiscard]] bool QualifyCopyBytes(){
    constexpr usize s_Bytes = 16640u;
    AlignedBytes source(s_Bytes), destination(s_Bytes);
    for(usize index = 0u; index < s_Bytes; ++index)
        source.data[index] = PatternByte(index);
    for(const usize count : s_QualifyLengths){
        for(usize sourceOffset = 0u; sourceOffset < 128u; sourceOffset += 16u){
            for(usize destinationOffset = 0u; destinationOffset < 128u; destinationOffset += 16u){
                NWB_MEMSET(destination.data, s_Poison, s_Bytes);
                CandidateCopy(destination.data + destinationOffset, count, source.data + sourceOffset, count);
                if(!CheckRange(destination.data, s_Bytes, destinationOffset, count, sourceOffset))
                    return false;
            }
        }
    }
    for(usize index = 0u; index < s_Bytes; ++index){
        if(source.data[index] != PatternByte(index))
            return false;
    }
    for(const usize count : { 16u, 32u, 48u, 64u, 128u, 4096u }){
        for(const bool destinationFirst : { false, true }){
            for(usize index = 0u; index < s_Bytes; ++index)
                destination.data[index] = PatternByte(index);
            const usize destinationStart = destinationFirst ? 0u : count, sourceStart = destinationFirst ? count : 0u;
            CandidateCopy(destination.data + destinationStart, count, destination.data + sourceStart, count);
            for(usize index = 0u; index < count; ++index){
                if(destination.data[destinationStart + index] != PatternByte(sourceStart + index))
                    return false;
            }
        }
    }
    CandidateCopy(nullptr, 0u, nullptr, 0u);
    CandidateCopy(destination.data + 1u, 0u, source.data + 3u, 0u);
    return true;
}

#if defined(NWB_PLATFORM_LINUX)
struct GuardedBytes final{
    void* mapping = nullptr;
    u8* data = nullptr;
    u8* lowerGuard = nullptr;
    u8* upperGuard = nullptr;
    usize pageBytes = 0u;
    usize accessibleBytes = 0u;
    usize mappingBytes = 0u;

    GuardedBytes(){
        const isize systemPageBytes = static_cast<isize>(sysconf(_SC_PAGESIZE));
        if(systemPageBytes <= 0)
            ::abort();
        pageBytes = static_cast<usize>(systemPageBytes);
        accessibleBytes = pageBytes * 4u;
        mappingBytes = pageBytes * 6u;
        mapping = mmap(nullptr, mappingBytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(mapping == MAP_FAILED || mprotect(static_cast<u8*>(mapping) + pageBytes, accessibleBytes, PROT_READ | PROT_WRITE) != 0)
            ::abort();
        data = ::new(static_cast<u8*>(mapping) + pageBytes) u8[accessibleBytes];
        lowerGuard = static_cast<u8*>(mapping);
        upperGuard = data + accessibleBytes;
    }
    ~GuardedBytes()noexcept{
        if(munmap(mapping, mappingBytes) != 0)
            ::abort();
    }
    GuardedBytes(const GuardedBytes&) = delete;
    GuardedBytes& operator=(const GuardedBytes&) = delete;
};

static void ExitOnAbort(int)noexcept{ _exit(s_AbortExit); }

[[nodiscard]] bool RejectedBeforeAccess(void* destination, usize capacity, const void* source, usize count){
    const pid_t child = fork();
    if(child < 0)
        return false;
    if(child == 0){
        if(signal(SIGABRT, ExitOnAbort) == SIG_ERR)
            _exit(82);
        CandidateCopy(destination, capacity, source, count);
        _exit(83);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == s_AbortExit;
}

[[nodiscard]] bool QualifyProtectedRanges(){
    GuardedBytes source, destination;
    if(source.accessibleBytes < s_QualifyLengths[sizeof(s_QualifyLengths) / sizeof(s_QualifyLengths[0]) - 1u])
        return false;
    for(usize index = 0u; index < source.accessibleBytes; ++index)
        source.data[index] = PatternByte(index);
    for(const usize count : s_QualifyLengths){
        for(const bool sourceAtEnd : { false, true }){
            for(const bool destinationAtEnd : { false, true }){
                NWB_MEMSET(destination.data, s_Poison, destination.accessibleBytes);
                const usize sourceStart = sourceAtEnd ? (source.accessibleBytes - count) & ~usize{ 15u } : 0u;
                const usize destinationStart = destinationAtEnd ? (destination.accessibleBytes - count) & ~usize{ 15u } : 0u;
                CandidateCopy(destination.data + destinationStart, count, source.data + sourceStart, count);
                if(!CheckRange(destination.data, destination.accessibleBytes, destinationStart, count, sourceStart))
                    return false;
            }
        }
    }
    for(const bool sourceAtEnd : { false, true }){
        for(const bool destinationAtEnd : { false, true }){
            auto* left = ::new(sourceAtEnd ? source.upperGuard - 32u : source.data) u64[4u];
            auto* right = ::new(destinationAtEnd ? destination.upperGuard - 32u : destination.data) u64[4u];
            for(usize lane = 0u; lane < 4u; ++lane)
                left[lane] = right[lane] = 0x7FF0000000000001ull + lane;
            if(!CandidateEqual(left, right) || !CandidateEqualFirstWord(left, right))
                return false;
            right[3u] ^= 1u;
            if(CandidateEqual(left, right) || CandidateEqualFirstWord(left, right))
                return false;
        }
    }
    CandidateCopy(source.lowerGuard, 0u, destination.upperGuard, 0u);
    if(
        !RejectedBeforeAccess(destination.lowerGuard, 15u, source.upperGuard, 16u)
        || !RejectedBeforeAccess(nullptr, 16u, source.upperGuard, 16u)
        || !RejectedBeforeAccess(destination.lowerGuard, 16u, nullptr, 16u)
        || !RejectedBeforeAccess(destination.lowerGuard, 32u, destination.lowerGuard, 32u)
        || !RejectedBeforeAccess(destination.lowerGuard, 32u, destination.lowerGuard + 16u, 32u)
        || !RejectedBeforeAccess(destination.lowerGuard + 16u, 32u, destination.lowerGuard, 32u)
    )
        return false;
    for(usize offset = 1u; offset < 16u; ++offset){
        if(!RejectedBeforeAccess(destination.lowerGuard + offset, 16u, source.upperGuard, 16u) || !RejectedBeforeAccess(destination.lowerGuard, 16u, source.upperGuard + offset, 16u))
            return false;
    }
    return true;
}
#endif

[[nodiscard]] bool QualifyEquality()noexcept{
#pragma STDC FENV_ACCESS ON
    fenv_t original{};
    if(feholdexcept(&original) != 0)
        return false;
    alignas(64) u64 lhs[8]{}, rhs[8]{};
    bool valid = true;
    constexpr u64 s_Patterns[]{ 0u, ~u64{ 0u }, 0x8000000000000000ull, 0x7FF0000000000001ull, 0x0000000000000001ull, 0x0123456789ABCDEFull };
    for(const u64 pattern : s_Patterns){
        for(usize offset = 0u; offset < 4u; ++offset){
            for(usize lane = 0u; lane < 4u; ++lane)
                lhs[offset + lane] = rhs[offset + lane] = pattern ^ static_cast<u64>(lane);
            if(!CandidateEqual(lhs + offset, rhs + offset) || !CandidateEqualFirstWord(lhs + offset, rhs + offset))
                valid = false;
            for(usize lane = 0u; lane < 4u; ++lane){
                for(u32 bit = 0u; bit < 64u; ++bit){
                    rhs[offset + lane] ^= u64{ 1u } << bit;
                    if(CandidateEqual(lhs + offset, rhs + offset) || CandidateEqualFirstWord(lhs + offset, rhs + offset) || MathTypeDetail::EqualElements<u64, 4u>(lhs + offset, rhs + offset))
                        valid = false;
                    rhs[offset + lane] ^= u64{ 1u } << bit;
                }
            }
        }
    }
    if(fetestexcept(FE_ALL_EXCEPT) != 0 || feraiseexcept(FE_DIVBYZERO) != 0)
        valid = false;
    // Opaque call targets prevent either predicate from being removed between the pre-existing flag and its observation.
    bool(*volatile wideFunction)(const u64*, const u64*)noexcept = CandidateEqual;
    bool(*volatile firstWordFunction)(const u64*, const u64*)noexcept = CandidateEqualFirstWord;
    const bool wideEqual = wideFunction(lhs, rhs);
    const i32 wideFlags = fetestexcept(FE_ALL_EXCEPT);
    const bool firstWordEqual = firstWordFunction(lhs, rhs);
    const i32 firstWordFlags = fetestexcept(FE_ALL_EXCEPT);
    if(!wideEqual || !firstWordEqual || wideFlags != FE_DIVBYZERO || firstWordFlags != FE_DIVBYZERO)
        valid = false;
    if(fesetenv(&original) != 0)
        valid = false;
    return valid;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hot operations inline into the batch so per-call vzeroupper/ABI overhead does not substitute for the production callsite behavior.
template<bool Candidate, usize FixedCount = 0u>
NWB_NOINLINE void CopyBatch(u8* destination, const u8* source, usize slots, usize stride, usize bytes, usize iterations)noexcept{
    usize slot = 0u;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        AtomicSignalFence(MemoryOrder::seq_cst);
        const usize count = FixedCount != 0u ? FixedCount : bytes;
        if constexpr(Candidate)
            CandidateCopy(destination + slot * stride, count, source + slot * stride, count);
        else
            SIMDMemoryCopy(destination + slot * stride, count, source + slot * stride, count);
        if(++slot == slots)
            slot = 0u;
    }
}

template<bool Candidate, bool FirstWord = false>
NWB_NOINLINE u64 EqualityBatch(const u64* lhs, const u64* rhs, usize slots, usize stride, usize iterations)noexcept{
    usize slot = 0u;
    u64 total = 0u;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        AtomicSignalFence(MemoryOrder::seq_cst);
        if constexpr(Candidate){
            if constexpr(FirstWord)
                total += CandidateEqualFirstWord(lhs + slot * stride, rhs + slot * stride) ? 1u : 0u;
            else
                total += CandidateEqual(lhs + slot * stride, rhs + slot * stride) ? 1u : 0u;
        }
        else
            total += MathTypeDetail::EqualElements<u64, 4u>(lhs + slot * stride, rhs + slot * stride) ? 1u : 0u;
        if(++slot == slots)
            slot = 0u;
    }
    return total;
}

using CopyBatchFunction = void(*)(u8*, const u8*, usize, usize, usize, usize)noexcept;
using EqualityBatchFunction = u64(*)(const u64*, const u64*, usize, usize, usize)noexcept;

[[nodiscard]] f64 TimeCopy(CopyBatchFunction function, u8* destination, const u8* source, usize slots, usize stride, usize bytes, usize iterations)noexcept{
    const Timer start = TimerNow();
    function(destination, source, slots, stride, bytes, iterations);
    const Timer end = TimerNow();
    g_Checksum = g_Checksum + destination[(iterations - 1u) % slots * stride];
    return DurationInNS<f64>(end, start) / static_cast<f64>(iterations);
}

[[nodiscard]] f64 TimeEquality(EqualityBatchFunction function, const u64* lhs, const u64* rhs, usize slots, usize stride, usize iterations)noexcept{
    const Timer start = TimerNow();
    const u64 result = function(lhs, rhs, slots, stride, iterations);
    const Timer end = TimerNow();
    g_Checksum = g_Checksum + result;
    return DurationInNS<f64>(end, start) / static_cast<f64>(iterations);
}

void BenchCopy(usize samples){
    NWB_COUT << "kind,cache,bytes,source_offset,destination_offset,sample,baseline_ns,candidate_ns,candidate_over_baseline\n";
    for(const bool streaming : { false, true }){
        for(const usize bytes : s_BenchLengths){
            const usize stride = (bytes + 127u) & ~usize{ 63u };
            const usize slots = streaming ? s_StreamingBytes / stride : 1u;
            AlignedBytes source(slots * stride + 128u), destination(slots * stride + 128u);
            for(usize index = 0u; index < slots * stride + 128u; ++index)
                source.data[index] = PatternByte(index);
            NWB_MEMSET(destination.data, s_Poison, slots * stride + 128u);
            usize iterations = s_StreamingBytes / bytes;
            if(iterations < 65536u)
                iterations = 65536u;
            const CopyBatchFunction baseline = bytes == 5120u ? CopyBatch<false, 5120u> : CopyBatch<false>;
            const CopyBatchFunction candidate = bytes == 5120u ? CopyBatch<true, 5120u> : CopyBatch<true>;
            for(const usize sourceOffset : { 16u, 32u }){
                for(const usize destinationOffset : { 16u, 32u }){
                    baseline(destination.data + destinationOffset, source.data + sourceOffset, slots, stride, bytes, 1024u);
                    candidate(destination.data + destinationOffset, source.data + sourceOffset, slots, stride, bytes, 1024u);
                    for(usize sample = 0u; sample < samples; ++sample){
                        f64 baselineNs, candidateNs;
                        if((sample & 1u) == 0u){
                            baselineNs = TimeCopy(baseline, destination.data + destinationOffset, source.data + sourceOffset, slots, stride, bytes, iterations);
                            candidateNs = TimeCopy(candidate, destination.data + destinationOffset, source.data + sourceOffset, slots, stride, bytes, iterations);
                        }
                        else{
                            candidateNs = TimeCopy(candidate, destination.data + destinationOffset, source.data + sourceOffset, slots, stride, bytes, iterations);
                            baselineNs = TimeCopy(baseline, destination.data + destinationOffset, source.data + sourceOffset, slots, stride, bytes, iterations);
                        }
                        NWB_COUT << (bytes == 5120u ? "copy_fixed" : "copy_dynamic") << ',' << (streaming ? "streaming" : "warm") << ',' << bytes << ',' << sourceOffset << ',' << destinationOffset << ',' << sample << ',';
                        NWB_COUT << baselineNs << ',' << candidateNs << ',' << candidateNs / baselineNs << '\n';
                        NWB_COUT.flush();
                    }
                }
            }
        }
    }
}

void BenchEquality(usize samples, bool firstWord){
    const EqualityBatchFunction candidate = firstWord ? EqualityBatch<true, true> : EqualityBatch<true>;
    NWB_COUT << "kind,cache,case,offset,sample,baseline_ns,candidate_ns,candidate_over_baseline\n";
    for(const bool streaming : { false, true }){
        const usize slots = streaming ? s_StreamingBytes / 64u : 1u;
        AlignedBytes left(slots * 64u + 64u), right(slots * 64u + 64u);
        auto* lhs = ::new(left.data) u64[slots * 8u + 8u];
        auto* rhs = ::new(right.data) u64[slots * 8u + 8u];
        for(usize comparisonCase = 0u; comparisonCase < (firstWord ? 5u : 3u); ++comparisonCase){
            for(const usize offset : { 0u, 1u, 2u, 3u }){
                for(usize slot = 0u; slot < slots; ++slot){
                    for(usize lane = 0u; lane < 4u; ++lane)
                        lhs[slot * 8u + offset + lane] = rhs[slot * 8u + offset + lane] = static_cast<u64>(slot) * 0x0123456789ABCDEFull + lane;
                    if(comparisonCase != 0u)
                        rhs[slot * 8u + offset + (firstWord ? comparisonCase - 1u : comparisonCase == 1u ? 0u : 3u)] ^= 1u;
                }
                constexpr usize s_Iterations = 4u * 1024u * 1024u;
                const u64 baselineWarmup = EqualityBatch<false>(lhs + offset, rhs + offset, slots, 8u, 1024u);
                const u64 candidateWarmup = candidate(lhs + offset, rhs + offset, slots, 8u, 1024u);
                const u64 expectedWarmup = comparisonCase == 0u ? 1024u : 0u;
                if(baselineWarmup != expectedWarmup || candidateWarmup != expectedWarmup){
                    NWB_CERR << "equality warm-up produced an incorrect result\n";
                    ::abort();
                }
                g_Checksum = g_Checksum + baselineWarmup + candidateWarmup;
                for(usize sample = 0u; sample < samples; ++sample){
                    f64 baselineNs, candidateNs;
                    if((sample & 1u) == 0u){
                        baselineNs = TimeEquality(EqualityBatch<false>, lhs + offset, rhs + offset, slots, 8u, s_Iterations);
                        candidateNs = TimeEquality(candidate, lhs + offset, rhs + offset, slots, 8u, s_Iterations);
                    }
                    else{
                        candidateNs = TimeEquality(candidate, lhs + offset, rhs + offset, slots, 8u, s_Iterations);
                        baselineNs = TimeEquality(EqualityBatch<false>, lhs + offset, rhs + offset, slots, 8u, s_Iterations);
                    }
                    constexpr StringView s_FirstWordCases[]{ "equal", "word0_mismatch", "word1_mismatch", "word2_mismatch", "word3_mismatch" };
                    const StringView caseName = firstWord ? s_FirstWordCases[comparisonCase] : comparisonCase == 0u ? "equal" : comparisonCase == 1u ? "early_mismatch" : "late_mismatch";
                    NWB_COUT << "equality," << (streaming ? "streaming" : "warm") << ',' << caseName << ',' << offset * sizeof(u64) << ',' << sample << ',' << baselineNs << ',' << candidateNs << ',' << candidateNs / baselineNs << '\n';
                    NWB_COUT.flush();
                }
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char** argv){
    using namespace __hidden_memory_storage_probe;
    if(argc < 2 || (NWB_STRCMP(argv[1], "--qualify") != 0 && NWB_STRCMP(argv[1], "--bench-copy") != 0 && NWB_STRCMP(argv[1], "--bench-equality") != 0 && NWB_STRCMP(argv[1], "--bench-equality-first-word") != 0)){
        NWB_CERR << "usage: memory_storage_probe --qualify|--bench-copy|--bench-equality|--bench-equality-first-word [samples]\n";
        return 2;
    }
    if(!QualifyCopyBytes() || !QualifyEquality()){
        NWB_CERR << "byte/equality qualification failed\n";
        return 1;
    }
#if defined(NWB_PLATFORM_LINUX)
    if(!QualifyProtectedRanges()){
        NWB_CERR << "protected-range qualification failed\n";
        return 1;
    }
#endif
    if(NWB_STRCMP(argv[1], "--qualify") == 0){
        NWB_COUT << "qualification passed: exact bytes/tails, adjacent ranges, page boundaries, admission, integer bits and FENV\n";
        return 0;
    }
    usize samples = 7u;
    if(argc >= 3){
        char* end = nullptr;
        const u64 parsed = static_cast<u64>(strtoul(argv[2], &end, 10));
        if(end == argv[2] || *end != '\0' || parsed == 0u || parsed > 100u)
            return 2;
        samples = static_cast<usize>(parsed);
    }
    if(NWB_STRCMP(argv[1], "--bench-copy") == 0)
        BenchCopy(samples);
    else
        BenchEquality(samples, NWB_STRCMP(argv[1], "--bench-equality-first-word") == 0);
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

