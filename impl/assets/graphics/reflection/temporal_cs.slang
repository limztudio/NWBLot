// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "temporal_constants.h"
#include "history_precision.slangi"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbReflectionTemporalPushConstants{
#define NWB_REFLECTION_TEMPORAL_FIELD(name) uint name;
    NWB_REFLECTION_TEMPORAL_UINT_FIELDS(NWB_REFLECTION_TEMPORAL_FIELD)
#undef NWB_REFLECTION_TEMPORAL_FIELD
};
[[vk::push_constant]] ConstantBuffer<NwbReflectionTemporalPushConstants> g_NwbReflectionTemporal;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_REFLECTION_TEMPORAL_GROUP_SIZE, NWB_REFLECTION_TEMPORAL_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadId.xy;
    if(pixel.x >= g_NwbReflectionTemporal.width || pixel.y >= g_NwbReflectionTemporal.height)
        return;
    // Invalid history has no graph read dependency, so this branch must precede every previous-history load.
    if(g_NwbReflectionTemporal.historyValid == 0u || g_NwbReflectionTemporal.previousSampleCount == 0u)
        return;
    const float4 specular = NwbHeapSampledImage2D(g_NwbReflectionTemporal.opaqueSpecularSlot).Load(int3(pixel, 0));
    if(!isfinite(specular.a) || specular.a <= 0.0 || !any(specular.rgb > 0.0))
        return;
    const float4 current = NwbHeapStorageImage2DFloat4(g_NwbReflectionTemporal.currentRadianceUavSlot)[pixel];
    if(current.a <= 0.0 || !all(isfinite(current.rgb)))
        return;
    const float4 previous = NwbHeapSampledImage2D(g_NwbReflectionTemporal.previousRadianceSrvSlot).Load(int3(pixel, 0));
    if(previous.a <= 0.0 || !all(isfinite(previous.rgb)))
        return;
    const uint limit = max(g_NwbReflectionTemporal.maxSampleCount, 1u);
    const uint count = min(g_NwbReflectionTemporal.previousSampleCount, limit - 1u) + 1u;
    if(count == 1u)
        return;
    // The camera and complete content stamp are unchanged. Every pixel contributes one sample, including true
    // zero and environment samples. This pointwise update has no neighboring read/write race or spatial feedback.
    // The difference form keeps equal finite-half endpoints exact, including 65504 at the upper storage limit.
    const float3 radiance = previous.rgb + (current.rgb - previous.rgb) * (1.0 / float(count));
    const float3 roundedRadiance = nwbReflectionRoundHistory(
        radiance, pixel, g_NwbReflectionTemporal.sampleIndex, g_NwbReflectionTemporal.samplingSeed
    );
    NwbHeapStorageImage2DFloat4(g_NwbReflectionTemporal.currentRadianceUavSlot)[pixel] = float4(roundedRadiance, current.a);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

