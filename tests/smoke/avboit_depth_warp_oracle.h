// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_render/avboit/avboit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitDepthWarpOracle final{
public:
    static constexpr u32 s_MaxVirtualCount = s_AvboitMaxVirtualCount;
    static constexpr u32 s_CoverageWords = (s_MaxVirtualCount + 31u) / 32u;
    static constexpr u32 s_BoundaryCaseCount = s_AvboitBoundaryCaseCount;
    static constexpr u32 s_ExhaustiveCaseCount = s_AvboitExhaustiveCaseCount;
    static constexpr u32 s_RandomCaseCount = s_AvboitRandomCaseCount;
    static constexpr u32 s_TotalCaseCount = s_BoundaryCaseCount + s_ExhaustiveCaseCount + s_RandomCaseCount;

    struct Case{
        Array<u32, s_CoverageWords> coverage{};
        u32 virtualCount = 1u;
        u32 physicalCount = 1u;
        u32 scenario = 0u;
    };

    struct Result{
        Array<u32, s_MaxVirtualCount> warp{};
        Array<u32, NWB_AVBOIT_CONTROL_WORD_COUNT> control{};
    };


public:
    [[nodiscard]] static Impl::RendererAvboitPushConstants BuildPush(const Case& probe, u32 ordinal, u32 slots)noexcept;
    [[nodiscard]] static Result BuildOracle(const Case& probe, const Impl::RendererAvboitPushConstants& push)noexcept;
    static void BuildCases(Vector<Case, Core::Alloc::GlobalArena>& cases);


private:
    [[nodiscard]] static b8 Occupied(const Case& probe, u32 slice)noexcept;
    static void SetOccupied(u32 slice, Case& probe)noexcept;
    static void PoisonUnusedCoverage(Case& probe)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

