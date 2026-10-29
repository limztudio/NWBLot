// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/types.adhd.h>

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    [[nodiscard]] b8 valid()const noexcept;
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
    b8 temporalMomentsValid = false;
    // The prepared graph may already lower a transparent trace or temporal-merge output before the first wavelet.
    b8 graphOwnsFirstWaveletInputState = false;
    // A split transparent temporal merge also publishes the selected moments input for the first wavelet.
    b8 graphOwnsWaveletMomentsEntryState = false;
    // The split opaque first-wavelet callback inherits its output UAV state from the graph.
    b8 graphOwnsFirstWaveletOutputState = false;
    // The transparent fold can inherit its geometry read from the graph; opaque geometry still transitions locally after its in-callback downsample.
    b8 graphOwnsWaveletGeometryEntryState = false;
    // Both prepared opaque and transparent resolve callbacks inherit these descriptor-visible upsample reads.
    b8 graphOwnsUpsampleStaticEntryStates = false;
    // The one-wavelet opaque resolve tail inherits both of these exact states from the preceding callback.
    b8 graphOwnsUpsampleInputColorEntryState = false;
    b8 graphOwnsUpsampleVisibilityOutputState = false;
    b8 firstWaveletWritesHalfA = true;
    SoftShadowUpsampleFold::Enum fold = SoftShadowUpsampleFold::Overwrite;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

