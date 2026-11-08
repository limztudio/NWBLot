// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_depth_warp_oracle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ProbeCase = AvboitDepthWarpOracle::Case;
using ProbeOracle = AvboitDepthWarpOracle::Result;


b8 AvboitDepthWarpOracle::Occupied(const ProbeCase& probe, const u32 slice)noexcept{
    return (probe.coverage[slice / 32u] & (1u << (slice % 32u))) != 0u;
}

void AvboitDepthWarpOracle::SetOccupied(const u32 slice, ProbeCase& probe)noexcept{
    if(slice < probe.virtualCount)
        probe.coverage[slice / 32u] |= 1u << (slice % 32u);
}

void AvboitDepthWarpOracle::PoisonUnusedCoverage(ProbeCase& probe)noexcept{
    // Occupied invalid tail bits must not add a coarse bin.
    for(u32 slice = probe.virtualCount; slice < s_CoverageWords * 32u; ++slice)
        probe.coverage[slice / 32u] |= 1u << (slice % 32u);
}

Impl::RendererAvboitPushConstants AvboitDepthWarpOracle::BuildPush(const ProbeCase& probe, const u32 ordinal, const u32 slots)noexcept{
    Impl::RendererAvboitPushConstants push;
    push.frame[0] = 1000u + ordinal % 997u;
    push.frame[1] = 300u + ordinal % 251u;
    push.frame[2] = (push.frame[0] + 2u) / 3u;
    push.frame[3] = (push.frame[1] + 3u) / 4u;
    push.volume[0] = probe.virtualCount;
    push.volume[1] = probe.physicalCount;
    push.volume[3] = (probe.virtualCount + 31u) / 32u;
    push.heapSlots[0] = slots;
    return push;
}

ProbeOracle AvboitDepthWarpOracle::BuildOracle(const ProbeCase& probe, const Impl::RendererAvboitPushConstants& push)noexcept{
    // The reference uses sorted occupied slice positions, independently of the shader's coverage-word algorithm.
    Array<u32, s_MaxVirtualCount> occupiedSlices{};
    u32 occupiedCount = 0u;
    for(u32 slice = 0u; slice < probe.virtualCount; ++slice){
        if(Occupied(probe, slice))
            occupiedSlices[occupiedCount++] = slice;
    }

    Array<u32, s_MaxVirtualCount> distinctBins{};
    u32 binWidth = 1u;
    u32 shift = 0u;
    u32 distinctCount = 0u;
    for(;;){
        distinctCount = 0u;
        for(u32 index = 0u; index < occupiedCount; ++index){
            const u32 bin = occupiedSlices[index] / binWidth;
            if(distinctCount == 0u || distinctBins[distinctCount - 1u] != bin)
                distinctBins[distinctCount++] = bin;
        }
        if(distinctCount <= probe.physicalCount)
            break;
        binWidth *= 2u;
        ++shift;
    }

    ProbeOracle oracle;
    u32 nextBin = 0u;
    u32 precedingRank = 0u;
    for(u32 slice = 0u; slice < probe.virtualCount; ++slice){
        while(nextBin < distinctCount && distinctBins[nextBin] <= slice / binWidth)
            precedingRank = nextBin++;
        oracle.warp[slice] = precedingRank;
    }
    oracle.control = { distinctCount == 0u ? 1u : distinctCount, shift, probe.virtualCount, probe.physicalCount, push.frame[2], push.frame[3], push.frame[0], push.frame[1], };
    return oracle;
}

void AvboitDepthWarpOracle::BuildCases(Vector<ProbeCase, Core::Alloc::GlobalArena>& cases){
    constexpr Array<u32, 21u> s_VirtualCounts{ 1u, 2u, 3u, 7u, 8u, 9u, 31u, 32u, 33u, 63u, 64u, 65u, 95u, 96u, 97u, 127u, 128u, 129u, 255u, 256u, 257u };
    constexpr Array<u32, 5u> s_PhysicalCounts{ 1u, 2u, 31u, 32u, 64u };
    cases.reserve(s_TotalCaseCount);
    for(const u32 virtualCount : s_VirtualCounts){
        for(const u32 physicalCount : s_PhysicalCounts){
            for(u32 pattern = 0u; pattern < 10u; ++pattern){
                ProbeCase probe;
                probe.virtualCount = virtualCount;
                probe.physicalCount = physicalCount;
                probe.scenario = pattern;
                for(u32 slice = 0u; slice < virtualCount; ++slice){
                    const b8 occupied = pattern == 1u
                        || (pattern == 2u && slice + 1u == virtualCount)
                        || (pattern == 3u && slice >= virtualCount / 2u && slice < virtualCount / 2u + 3u)
                        || (pattern == 4u && slice % 2u == 1u)
                        || (pattern == 5u && (slice % 32u == 0u || slice % 32u == 31u))
                        || (pattern == 6u && slice < 64u)
                        || (pattern == 7u && slice < 65u)
                        || (pattern == 8u && slice + 64u >= virtualCount)
                        || (pattern == 9u && slice == 0u);
                    if(occupied)
                        SetOccupied(slice, probe);
                }
                PoisonUnusedCoverage(probe);
                cases.push_back(probe);
            }
        }
    }
    for(u32 virtualCount = 1u; virtualCount <= 8u; ++virtualCount){
        for(u32 mask = 0u; mask < (1u << virtualCount); ++mask){
            for(const u32 physicalCount : s_PhysicalCounts){
                ProbeCase probe;
                probe.virtualCount = virtualCount;
                probe.physicalCount = physicalCount;
                probe.scenario = 10u;
                probe.coverage[0] = mask;
                PoisonUnusedCoverage(probe);
                cases.push_back(probe);
            }
        }
    }
    constexpr u32 s_RandomCaseInitialSeed = s_AvboitRandomCaseInitialSeed;
    constexpr u32 s_RandomVirtualCountStride = s_AvboitRandomVirtualCountStride;
    constexpr u32 s_RandomScenarioId = s_AvboitRandomScenarioId;
    constexpr u32 s_XorShift1 = s_AvboitXorShift1;
    constexpr u32 s_XorShift2 = s_AvboitXorShift2;
    constexpr u32 s_XorShift3 = s_AvboitXorShift3;
    u32 random = s_RandomCaseInitialSeed;
    for(u32 index = 0u; index < s_RandomCaseCount; ++index){
        ProbeCase probe;
        probe.virtualCount = 1u + index * s_RandomVirtualCountStride % s_MaxVirtualCount;
        probe.physicalCount = s_PhysicalCounts[index % s_PhysicalCounts.size()];
        probe.scenario = s_RandomScenarioId;
        for(u32 slice = 0u; slice < probe.virtualCount; ++slice){
            random ^= random << s_XorShift1;
            random ^= random >> s_XorShift2;
            random ^= random << s_XorShift3;
            if(random % 8u < 1u + index % 7u)
                SetOccupied(slice, probe);
        }
        PoisonUnusedCoverage(probe);
        cases.push_back(probe);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

