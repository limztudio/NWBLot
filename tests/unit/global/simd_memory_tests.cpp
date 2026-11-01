// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/simd_memory_guard_fixture.h>
#include <tests/common/test_context.h>

#include <global/containers.h>
#include <global/generic.h>
#include <global/limit.h>
#include <global/process.h>
#include <global/simd/memory.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_simd_memory_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::ExitOnAbort;
using NWB::Tests::GuardAddress;
#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
using NWB::Tests::GuardedBytes;
#endif
using NWB::Tests::PatternByte;
using NWB::Tests::s_SIMDMemoryAbortExitCode;
using NWB::Tests::s_SIMDMemoryHandlerFailureExitCode;
using NWB::Tests::s_SIMDMemoryReturnedExitCode;

inline constexpr usize s_BufferBytes = 9216u;
inline constexpr usize s_GuardBytes = 64u;
inline constexpr u8 s_Poison = 0xA5u;
inline constexpr Array<usize, 34> s_Lengths = {
    0u, 1u, 2u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 32u, 33u, 47u, 48u, 49u, 63u, 64u,
    65u, 127u, 128u, 129u, 255u, 256u, 257u, 511u, 512u, 513u, 4095u, 4096u, 4097u, 8191u, 8192u, 8193u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(16) AlignedPacket{
    Array<u64, 4> raw = {};
};

struct AlignedPacketView{
    using value_type = AlignedPacket;


public:
    const AlignedPacket* values = nullptr;
    usize valueCount = 0u;


public:
    [[nodiscard]] b8 empty()const noexcept{ return valueCount == 0u; }

    [[nodiscard]] usize size()const noexcept{ return valueCount; }

    [[nodiscard]] const AlignedPacket* data()const noexcept{ return values; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr u64 PacketLaneBits(const usize packetIndex, const usize laneIndex)noexcept{
    return 0xDEADBEEF80000000ull ^ (static_cast<u64>(packetIndex + 1u) << 32u) ^ (static_cast<u64>(laneIndex + 1u) * 0x01020304ull);
}

#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
void ExpectRejectedCopy(void* destination, const usize capacity, const void* source, const usize count){
    // An access violation cannot satisfy this predicate, so rejection must happen before either range is accessed.
    EXPECT_EXIT({
        if(signal(SIGABRT, ExitOnAbort) == SIG_ERR)
            _exit(s_SIMDMemoryHandlerFailureExitCode);
        SIMDMemoryCopy(destination, capacity, source, count);
        _exit(s_SIMDMemoryReturnedExitCode);
    }, testing::ExitedWithCode(s_SIMDMemoryAbortExitCode), ""
    );
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SIMDMemory, AlignedCacheLineOffsetsAndVectorBoundaryTailsPreserveExactRanges){
    using namespace __hidden_simd_memory_tests;
    alignas(64) Array<u8, s_BufferBytes> source = {};
    alignas(64) Array<u8, s_BufferBytes> destination = {};
    Array<u8, s_BufferBytes> expected = {};
    Array<u8, s_BufferBytes> original = {};
    for(usize index = 0u; index < s_BufferBytes; ++index)
        source[index] = original[index] = PatternByte(index);

    for(const usize count : s_Lengths){
        for(usize sourceOffset = 0u; sourceOffset < 128u; sourceOffset += s_SIMDMemoryCopyAlignment){
            for(usize destinationOffset = 0u; destinationOffset < 128u; destinationOffset += s_SIMDMemoryCopyAlignment){
                NWB_MEMSET(destination.data(), s_Poison, destination.size());
                NWB_MEMSET(expected.data(), s_Poison, expected.size());
                const usize sourceStart = s_GuardBytes + sourceOffset;
                const usize destinationStart = s_GuardBytes + destinationOffset;
                for(usize index = 0u; index < count; ++index)
                    expected[destinationStart + index] = original[sourceStart + index];
                volatile usize runtimeCount = count;
                NWB_SIMD_MEMCPY(destination.data() + destinationStart, count, source.data() + sourceStart, runtimeCount);
                EXPECT_EQ(NWB_MEMCMP(destination.data(), expected.data(), destination.size()), 0) << count << ':' << sourceOffset << ':' << destinationOffset;
            }
        }
    }
    EXPECT_EQ(NWB_MEMCMP(source.data(), original.data(), source.size()), 0);
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
        for(usize laneIndex = 0u; laneIndex < packets[packetIndex].raw.size(); ++laneIndex)
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
        for(usize laneIndex = 0u; laneIndex < packets[packetIndex].raw.size(); ++laneIndex)
            EXPECT_EQ(packets[packetIndex].raw[laneIndex], PacketLaneBits(originalIndex, laneIndex)) << packetIndex << ':' << laneIndex;
    }

    const usize middleOffset = initialSize - 8u;
    const AlignedPacketView assignedMiddle{ packets.data() + middleOffset, 16u };
    AssignTriviallyCopyableVector(packets, assignedMiddle);
    ASSERT_EQ(packets.size(), assignedMiddle.valueCount);
    for(usize packetIndex = 0u; packetIndex < packets.size(); ++packetIndex){
        const usize priorIndex = middleOffset + packetIndex;
        const usize originalIndex = priorIndex < initialSize ? priorIndex : 3u + priorIndex - initialSize;
        for(usize laneIndex = 0u; laneIndex < packets[packetIndex].raw.size(); ++laneIndex)
            EXPECT_EQ(packets[packetIndex].raw[laneIndex], PacketLaneBits(originalIndex, laneIndex)) << packetIndex << ':' << laneIndex;
    }
}


TEST(SIMDMemory, AdjacentRangesInOneLiveByteArrayAreAcceptedWithoutClobberingGuards){
    using namespace __hidden_simd_memory_tests;
    alignas(64) Array<u8, s_BufferBytes> bytes = {};
    Array<u8, s_BufferBytes> expected = {};
    constexpr Array<usize, 7> s_AdjacentLengths = { 16u, 32u, 48u, 64u, 128u, 256u, 4096u };
    for(const usize count : s_AdjacentLengths){
        for(const b8 destinationFirst : { false, true }){
            for(usize index = 0u; index < s_BufferBytes; ++index)
                bytes[index] = expected[index] = PatternByte(index);
            const usize destinationStart = s_GuardBytes + (destinationFirst ? 0u : count);
            const usize sourceStart = s_GuardBytes + (destinationFirst ? count : 0u);
            for(usize index = 0u; index < count; ++index)
                expected[destinationStart + index] = PatternByte(sourceStart + index);
            volatile usize runtimeCount = count;
            SIMDMemoryCopy(bytes.data() + destinationStart, count, bytes.data() + sourceStart, runtimeCount);
            EXPECT_EQ(NWB_MEMCMP(bytes.data(), expected.data(), bytes.size()), 0) << count << ':' << destinationFirst;
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
    ASSERT_GE(source.accessibleBytes, s_Lengths[s_Lengths.size() - 1u]);
    for(usize index = 0u; index < source.accessibleBytes; ++index)
        source.data[index] = PatternByte(index);

    for(usize lengthIndex = 1u; lengthIndex <= s_Lengths.size(); ++lengthIndex){
        const usize count = lengthIndex == s_Lengths.size() ? source.accessibleBytes : s_Lengths[lengthIndex];
        for(const b8 sourceAtEnd : { false, true }){
            for(const b8 destinationAtEnd : { false, true }){
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
    alignas(16) Array<u8, 3> bytes = { 0x12u, 0x34u, 0x56u };
    volatile usize zero = 0u;
    SIMDMemoryCopy(nullptr, 0u, nullptr, zero);
    NWB_SIMD_MEMCPY(bytes.data() + bytes.size(), 0u, nullptr, zero);
    SIMDMemoryCopy(nullptr, Limit<usize>::s_Max, bytes.data() + bytes.size(), zero);
    SIMDMemoryCopy(bytes.data(), 0u, bytes.data(), zero);
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
    u8* const inaccessible = guarded.lowerGuard;
    u8* const upperGuard = guarded.upperGuard;
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

