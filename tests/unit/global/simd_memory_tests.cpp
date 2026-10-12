// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <global/containers.h>
#include <global/simd/memory.h>
#include <global/generic.h>
#include <global/limit.h>

#include <gtest/gtest.h>

#include <new>
#include <signal.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <process.h>
#include <Windows.h>
#elif defined(NWB_PLATFORM_LINUX)
#include <sys/mman.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_simd_memory_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_BufferBytes = 9216u;
inline constexpr usize s_GuardBytes = 64u;
inline constexpr u8 s_Poison = 0xA5u;
inline constexpr int s_AbortExitCode = 81;
inline constexpr int s_HandlerFailureExitCode = 82;
inline constexpr int s_ReturnedExitCode = 83;
inline constexpr usize s_Lengths[]{ 0u, 1u, 2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u, 47u, 48u, 49u, 63u, 64u, 65u, 127u, 128u, 129u, 255u, 256u, 257u, 511u, 512u, 513u, 4095u, 4096u, 4097u, 8191u, 8192u, 8193u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(16) AlignedPacket{
    u64 raw[4]{};
};

struct AlignedPacketView{
    using value_type = AlignedPacket;


    const AlignedPacket* values = nullptr;
    usize valueCount = 0u;

    [[nodiscard]] bool empty()const noexcept{ return valueCount == 0u; }

    [[nodiscard]] usize size()const noexcept{ return valueCount; }

    [[nodiscard]] const AlignedPacket* data()const noexcept{ return values; }
};


#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
struct GuardedBytes final : NoCopy{
    void* mapping = nullptr;
    u8* data = nullptr;
    usize pageBytes = 0u;
    usize accessibleBytes = 0u;
    usize mappingBytes = 0u;

    GuardedBytes()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        pageBytes = static_cast<usize>(systemInfo.dwPageSize);
#else
        const long systemPageBytes = sysconf(_SC_PAGESIZE);
        if(systemPageBytes <= 0)
            return;
        pageBytes = static_cast<usize>(systemPageBytes);
#endif
        if(pageBytes == 0u || pageBytes % s_SIMDMemoryCopyAlignment != 0u || pageBytes > Limit<usize>::s_Max / 6u)
            return;
        accessibleBytes = pageBytes * 4u;
        mappingBytes = pageBytes * 6u;
#if defined(NWB_PLATFORM_WINDOWS)
        mapping = VirtualAlloc(nullptr, mappingBytes, MEM_RESERVE, PAGE_NOACCESS);
        if(mapping == nullptr)
            return;
        void* const committed = VirtualAlloc(static_cast<u8*>(mapping) + pageBytes, accessibleBytes, MEM_COMMIT, PAGE_READWRITE);
        if(committed == nullptr)
            return;
#else
        void* const allocated = mmap(nullptr, mappingBytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(allocated == MAP_FAILED)
            return;
        mapping = allocated;
        if(mprotect(static_cast<u8*>(mapping) + pageBytes, accessibleBytes, PROT_READ | PROT_WRITE) != 0)
            return;
#endif
        // The copy API writes existing byte objects, including at both ends of this accessible region.
        data = ::new(static_cast<u8*>(mapping) + pageBytes) u8[accessibleBytes];
    }
    ~GuardedBytes()noexcept{
        if(mapping == nullptr)
            return;
#if defined(NWB_PLATFORM_WINDOWS)
        if(VirtualFree(mapping, 0u, MEM_RELEASE) == 0)
            abort();
#else
        if(munmap(mapping, mappingBytes) != 0)
            abort();
#endif
    }
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr u64 PacketLaneBits(usize packetIndex, usize laneIndex)noexcept{
    return 0xDEADBEEF80000000ull ^ (static_cast<u64>(packetIndex + 1u) << 32u) ^ (static_cast<u64>(laneIndex + 1u) * 0x01020304ull);
}

[[nodiscard]] constexpr u8 PatternByte(usize index)noexcept{
    return static_cast<u8>((index * 37u + (index >> 8u) * 13u + 11u) & 0xFFu);
}

#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
[[nodiscard]] u8* GuardAddress(const void* base, usize byteOffset)noexcept{
    return reinterpret_cast<u8*>(reinterpret_cast<usize>(base) + byteOffset);
}

void ExitOnAbort(int)noexcept{
    _exit(s_AbortExitCode);
}

void ExpectRejectedCopy(void* destination, usize capacity, const void* source, usize count){
    // An access violation cannot satisfy this predicate, so rejection must happen before either range is accessed.
    EXPECT_EXIT({
        if(signal(SIGABRT, ExitOnAbort) == SIG_ERR)
            _exit(s_HandlerFailureExitCode);
        SIMDMemoryCopy(destination, capacity, source, count);
        _exit(s_ReturnedExitCode);
    }, testing::ExitedWithCode(s_AbortExitCode), "");
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SIMDMemory, AlignedCacheLineOffsetsAndVectorBoundaryTailsPreserveExactRanges){
    using namespace __hidden_simd_memory_tests;
    alignas(64) u8 source[s_BufferBytes]{};
    alignas(64) u8 destination[s_BufferBytes]{};
    u8 expected[s_BufferBytes]{};
    u8 original[s_BufferBytes]{};
    for(usize index = 0u; index < s_BufferBytes; ++index)
        source[index] = original[index] = PatternByte(index);

    for(const usize count : s_Lengths){
        for(usize sourceOffset = 0u; sourceOffset < 128u; sourceOffset += s_SIMDMemoryCopyAlignment){
            for(usize destinationOffset = 0u; destinationOffset < 128u; destinationOffset += s_SIMDMemoryCopyAlignment){
                NWB_MEMSET(destination, s_Poison, sizeof(destination));
                NWB_MEMSET(expected, s_Poison, sizeof(expected));
                const usize sourceStart = s_GuardBytes + sourceOffset;
                const usize destinationStart = s_GuardBytes + destinationOffset;
                for(usize index = 0u; index < count; ++index)
                    expected[destinationStart + index] = original[sourceStart + index];
                volatile usize runtimeCount = count;
                NWB_SIMD_MEMCPY(destination + destinationStart, count, source + sourceStart, runtimeCount);
                EXPECT_EQ(NWB_MEMCMP(destination, expected, sizeof(destination)), 0) << count << ':' << sourceOffset << ':' << destinationOffset;
            }
        }
    }
    EXPECT_EQ(NWB_MEMCMP(source, original, sizeof(source)), 0);
}

TEST(SIMDMemory, AlignedVectorAliasedAppendReallocationAndOverlappingAssignmentPreserveRawPackets){
    using namespace __hidden_simd_memory_tests;
    using PacketVector = NWB::Tests::TestVector<AlignedPacket>;
    NWB::Tests::TestArena<AlignedPacket> owner;
    PacketVector packets{ PacketVector::allocator_type(owner.arena) };
    packets.reserve(32u);
    const usize initialSize = packets.capacity();
    packets.resize(initialSize);
    for(usize packetIndex = 0u; packetIndex < initialSize; ++packetIndex){
        for(usize laneIndex = 0u; laneIndex < LengthOf(packets[packetIndex].raw); ++laneIndex)
            packets[packetIndex].raw[laneIndex] = PacketLaneBits(packetIndex, laneIndex);
    }

    const usize oldAddress = reinterpret_cast<usize>(packets.data());
    const AlignedPacketView appended{ packets.data() + 3u, 16u };
    AppendTriviallyCopyableVector(packets, appended);
    ASSERT_EQ(packets.size(), initialSize + appended.valueCount);
    EXPECT_GT(packets.capacity(), initialSize);
    EXPECT_NE(reinterpret_cast<usize>(packets.data()), oldAddress);
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const usize originalIndex = packetIndex < initialSize ? packetIndex : 3u + packetIndex - initialSize;
        for(usize laneIndex = 0u; laneIndex < LengthOf(packets[packetIndex].raw); ++laneIndex)
            EXPECT_EQ(packets[packetIndex].raw[laneIndex], PacketLaneBits(originalIndex, laneIndex)) << packetIndex << ':' << laneIndex;
    }

    const usize middleOffset = initialSize - 8u;
    const AlignedPacketView assignedMiddle{ packets.data() + middleOffset, 16u };
    AssignTriviallyCopyableVector(packets, assignedMiddle);
    ASSERT_EQ(packets.size(), assignedMiddle.valueCount);
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const usize priorIndex = middleOffset + packetIndex;
        const usize originalIndex = priorIndex < initialSize ? priorIndex : 3u + priorIndex - initialSize;
        for(usize laneIndex = 0u; laneIndex < LengthOf(packets[packetIndex].raw); ++laneIndex)
            EXPECT_EQ(packets[packetIndex].raw[laneIndex], PacketLaneBits(originalIndex, laneIndex)) << packetIndex << ':' << laneIndex;
    }
}


TEST(SIMDMemory, AdjacentRangesInOneLiveByteArrayAreAcceptedWithoutClobberingGuards){
    using namespace __hidden_simd_memory_tests;
    alignas(64) u8 bytes[s_BufferBytes]{};
    u8 expected[s_BufferBytes]{};
    const usize lengths[]{ 16u, 32u, 48u, 64u, 128u, 256u, 4096u };
    for(const usize count : lengths){
        for(const bool destinationFirst : { false, true }){
            for(usize index = 0u; index < s_BufferBytes; ++index)
                bytes[index] = expected[index] = PatternByte(index);
            const usize destinationStart = s_GuardBytes + (destinationFirst ? 0u : count);
            const usize sourceStart = s_GuardBytes + (destinationFirst ? count : 0u);
            for(usize index = 0u; index < count; ++index)
                expected[destinationStart + index] = PatternByte(sourceStart + index);
            volatile usize runtimeCount = count;
            SIMDMemoryCopy(bytes + destinationStart, count, bytes + sourceStart, runtimeCount);
            EXPECT_EQ(NWB_MEMCMP(bytes, expected, sizeof(bytes)), 0) << count << ':' << destinationFirst;
        }
    }
}

TEST(SIMDMemory, ProtectedPageEdgesRejectOverreadsUnderreadsAndOutOfRangeStores){
#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
    using namespace __hidden_simd_memory_tests;
    GuardedBytes source;
    GuardedBytes destination;
    ASSERT_NE(source.data, nullptr);
    ASSERT_NE(destination.data, nullptr);
    ASSERT_EQ(source.accessibleBytes, destination.accessibleBytes);
    ASSERT_GE(source.accessibleBytes, s_Lengths[LengthOf(s_Lengths) - 1u]);
    for(usize index = 0u; index < source.accessibleBytes; ++index)
        source.data[index] = PatternByte(index);

    for(usize lengthIndex = 1u; lengthIndex <= LengthOf(s_Lengths); ++lengthIndex){
        const usize count = lengthIndex == LengthOf(s_Lengths) ? source.accessibleBytes : s_Lengths[lengthIndex];
        for(const bool sourceAtEnd : { false, true }){
            for(const bool destinationAtEnd : { false, true }){
                NWB_MEMSET(destination.data, s_Poison, destination.accessibleBytes);
                // Nonmultiple tails leave less than one vector of poison before the upper guard to keep both starts aligned.
                const usize sourceStart = sourceAtEnd ? (source.accessibleBytes - count) & ~(s_SIMDMemoryCopyAlignment - 1u) : 0u;
                const usize destinationStart = destinationAtEnd ? (destination.accessibleBytes - count) & ~(s_SIMDMemoryCopyAlignment - 1u) : 0u;
                volatile usize runtimeCount = count;
                SIMDMemoryCopy(destination.data + destinationStart, count, source.data + sourceStart, runtimeCount);
                for(usize index = 0u; index < destination.accessibleBytes; ++index){
                    const u8 expected = index >= destinationStart && index - destinationStart < count ? PatternByte(sourceStart + index - destinationStart) : s_Poison;
                    EXPECT_EQ(destination.data[index], expected) << count << ':' << sourceAtEnd << ':' << destinationAtEnd << ':' << index;
                }
            }
        }
    }
    for(usize index = 0u; index < source.accessibleBytes; ++index)
        EXPECT_EQ(source.data[index], PatternByte(index)) << index;
#else
    GTEST_SKIP() << "Protected-page fixtures require Windows or Linux virtual memory APIs.";
#endif
}

TEST(SIMDMemory, ZeroLengthAllowsNullOnePastAndInaccessiblePointersWithoutAccess){
    using namespace __hidden_simd_memory_tests;
    alignas(16) u8 bytes[]{ 0x12u, 0x34u, 0x56u };
    volatile usize zero = 0u;
    SIMDMemoryCopy(nullptr, 0u, nullptr, zero);
    NWB_SIMD_MEMCPY(bytes + sizeof(bytes), 0u, nullptr, zero);
    SIMDMemoryCopy(nullptr, Limit<usize>::s_Max, bytes + sizeof(bytes), zero);
    SIMDMemoryCopy(bytes, 0u, bytes, zero);
#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
    GuardedBytes guarded;
    ASSERT_NE(guarded.data, nullptr);
    NWB_SIMD_MEMCPY(guarded.mapping, 0u, guarded.data + guarded.accessibleBytes, zero);
    SIMDMemoryCopy(guarded.data + guarded.accessibleBytes, Limit<usize>::s_Max, guarded.mapping, zero);
    SIMDMemoryCopy(GuardAddress(guarded.mapping, 1u), 0u, GuardAddress(guarded.data, guarded.accessibleBytes + 8u), zero);
#endif
    EXPECT_EQ(bytes[0], 0x12u);
    EXPECT_EQ(bytes[1], 0x34u);
    EXPECT_EQ(bytes[2], 0x56u);
}

TEST(SIMDMemory, InvalidCapacityNullAlignmentHugeCountsAndOverlapAbortBeforeRangeAccess){
#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
    using namespace __hidden_simd_memory_tests;
    GuardedBytes guarded;
    ASSERT_NE(guarded.data, nullptr);
    u8* const inaccessible = static_cast<u8*>(guarded.mapping);
    u8* const upperGuard = guarded.data + guarded.accessibleBytes;
    ExpectRejectedCopy(inaccessible, 0u, upperGuard, 1u);
    ExpectRejectedCopy(inaccessible, 15u, upperGuard, 16u);
    ExpectRejectedCopy(inaccessible, 1u, upperGuard, Limit<usize>::s_Max);
    ExpectRejectedCopy(inaccessible, Limit<usize>::s_Max, upperGuard, Limit<usize>::s_Max);
    ExpectRejectedCopy(nullptr, 1u, upperGuard, 1u);
    ExpectRejectedCopy(inaccessible, 1u, nullptr, 1u);
    ExpectRejectedCopy(inaccessible, 32u, inaccessible, 32u);
    ExpectRejectedCopy(GuardAddress(inaccessible, 16u), 32u, inaccessible, 32u);
    ExpectRejectedCopy(inaccessible, 32u, GuardAddress(inaccessible, 16u), 32u);
    for(usize offset = 1u; offset < 32u; ++offset){
        if(offset % s_SIMDMemoryCopyAlignment == 0u)
            continue;
        ExpectRejectedCopy(GuardAddress(inaccessible, offset), 16u, upperGuard, 16u);
        ExpectRejectedCopy(inaccessible, 16u, GuardAddress(upperGuard, offset), 16u);
    }
    ExpectRejectedCopy(GuardAddress(inaccessible, 8u), 16u, GuardAddress(upperGuard, 8u), 16u);
#else
    GTEST_SKIP() << "Abort-before-access fixtures require Windows or Linux virtual memory APIs.";
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

