// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RGB variant of the soft-shadow a-trous resolve (Stage 5). The a-trous wavelet in shadow_resolve_cs.slang is
// channel-count-parameterized by NWB_SHADOW_RESOLVE_CHANNELS: the default (scalar, =1) denoises the grayscale soft OPAQUE
// visibility; this wrapper compiles the SAME source with =3 so the wavelet denoises the COLORED soft TRANSPARENT
// transmittance (the value/luminance edge-stop uses the luma of the RGB delta). One-file-per-pipeline convention (matching
// the codebase): the engine cooks TWO pipelines from the two .nwb wrappers, the scalar (shadow_resolve_cs) and this RGB
// one, so the common opaque path keeps 1x ALU/LDS while the transparent path denoises color. The fold mode (overwrite vs
// multiply onto the visibility) is a runtime push flag shared by both variants.


#define NWB_SHADOW_RESOLVE_CHANNELS NWB_SHADOW_RESOLVE_CHANNELS_RGB

#include "shadow_resolve_cs.slang"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
