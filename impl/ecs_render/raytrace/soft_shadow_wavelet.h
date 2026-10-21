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


public:
    [[nodiscard]] bool valid()const noexcept;
};

// Soft resolve either overwrites visibility or multiplies transparent transmittance.
namespace SoftShadowUpsampleFold{
    static constexpr auto s_SoftShadowUpsampleFoldOverwriteBase = 0u;
    enum Enum : u32{
        Overwrite = s_SoftShadowUpsampleFoldOverwriteBase,
        Multiply,
    };
};

// Resolve resources pair backing targets with their pushed heap slots.
struct SoftShadowResolvePassResources{
    Core::Texture* softHalfTexture = nullptr;
    Core::Texture* inputColorTexture = nullptr;
    Core::Texture* momentsTexture = nullptr;
    Core::Texture* outputTexture = nullptr;
    u32 softHalf = 0u;
    u32 inputColor = 0u;
    u32 moments = 0u;
    u32 outputStorage = 0u;
};
// Heap-only soft resolve dispatch description.
struct SoftShadowResolveDispatch{
    Core::ComputePipeline* waveletPipeline = nullptr;
    Core::ComputePipeline* upsamplePipeline = nullptr;
    SoftShadowResolvePassResources firstWaveletResources;
    SoftShadowResolvePassResources outputHalfAResources;
    SoftShadowResolvePassResources outputHalfBResources;
    SoftShadowResolvePassResources upsampleResources;
    Core::Texture* visibilityTexture = nullptr;
    u32 visibilityStorage = 0u;
    u32 sceneShading = 0u;
    // Must be odd so the selected upsample input is the final ping-pong result.
    u32 waveletPassCount = 1u;
    bool temporalMomentsValid = false;
    // The prepared graph may already lower a transparent trace or temporal-merge output before the first wavelet.
    bool graphOwnsFirstWaveletInputState = false;
    // A split transparent temporal merge also publishes the selected moments input for the first wavelet.
    bool graphOwnsWaveletMomentsEntryState = false;
    // The split opaque first-wavelet callback inherits its output UAV state from the graph.
    bool graphOwnsFirstWaveletOutputState = false;
    // The transparent fold can inherit its geometry read from the graph; opaque geometry still transitions locally after its in-callback downsample.
    bool graphOwnsWaveletGeometryEntryState = false;
    // Both prepared opaque and transparent resolve callbacks inherit these descriptor-visible upsample reads.
    bool graphOwnsUpsampleStaticEntryStates = false;
    // The one-wavelet opaque resolve tail inherits both of these exact states from the preceding callback.
    bool graphOwnsUpsampleInputColorEntryState = false;
    bool graphOwnsUpsampleVisibilityOutputState = false;
    bool firstWaveletWritesHalfA = true;
    SoftShadowUpsampleFold::Enum fold = SoftShadowUpsampleFold::Overwrite;
};

[[nodiscard]] inline bool CanCombineSoftShadowWavelets(const bool combinedUpsample, const bool pipelineReady, const bool opaqueTemporalReady, const bool transparentTemporalReady, const u32 opaquePassCount, const u32 transparentPassCount)noexcept{
    return combinedUpsample && pipelineReady && opaqueTemporalReady && transparentTemporalReady && opaquePassCount == 1u && transparentPassCount == 1u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

