// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SoftShadowOpaqueResolvePhase{
    enum Enum : u8{
        TemporalAndWavelet,
        TemporalOnly,
        WaveletOnly,
    };
};

// Target creation validates each history selector once; frame recording trusts the owner-created independent allocations.
struct SoftShadowCombinedWaveletInputs{
    Core::Texture* opaqueHistory = nullptr;
    Core::Texture* opaqueMoments = nullptr;
    Core::Texture* transparentHistory = nullptr;
    Core::Texture* transparentMoments = nullptr;
    Core::Texture* geometry = nullptr;
    Core::Texture* opaqueOutput = nullptr;
    Core::Texture* transparentOutput = nullptr;

    [[nodiscard]] bool valid()const noexcept;
};

[[nodiscard]] inline bool CanCombineSoftShadowWavelets(
    const bool combinedUpsample,
    const bool pipelineReady,
    const bool opaqueTemporalReady,
    const bool transparentTemporalReady,
    const u32 opaquePassCount,
    const u32 transparentPassCount)noexcept{
    return combinedUpsample && pipelineReady && opaqueTemporalReady && transparentTemporalReady
        && opaquePassCount == 1u && transparentPassCount == 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

