// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SoftwareShadowBackend{
    enum Enum : u8{
        Automatic,
        SoftwareTrace,
        LightSpace,
    };
};

namespace SoftwareShadowCoverage{
    enum Enum : u8{
        Reference,
        FittedVolume,
    };
};

namespace SoftwareShadowBlockerSearch{
    enum Enum : u8{
        ReferenceGrid9,
        CompactCross5,
    };
};

inline constexpr u32 s_ShadowDefaultDirectionalResolution = 512u;
inline constexpr u32 s_ShadowDefaultPointResolution = 256u;
inline constexpr u64 s_ShadowDefaultMemoryBudgetBytes = 256ull * 1024ull * 1024ull;
inline constexpr u32 s_ShadowMinResolution = 32u;
inline constexpr u32 s_ShadowMaxResolution = 2048u;


struct SoftwareShadowSettings{
    SoftwareShadowBackend::Enum backend = SoftwareShadowBackend::Automatic;
    SoftwareShadowCoverage::Enum coverage = SoftwareShadowCoverage::Reference;
    SoftwareShadowBlockerSearch::Enum blockerSearch = SoftwareShadowBlockerSearch::ReferenceGrid9;
    u32 directionalResolution = s_ShadowDefaultDirectionalResolution;
    u32 pointResolution = s_ShadowDefaultPointResolution;
    u64 memoryBudgetBytes = s_ShadowDefaultMemoryBudgetBytes;
};

[[nodiscard]] bool ValidateSoftwareShadowSettings(const SoftwareShadowSettings& settings);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

