// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#version 460

#include "avboit_compute_common.glsli"

layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly buffer NwbAvboitExtinctionBuffer{
    uint g_Extinction[];
};

layout(std430, binding = 1) buffer NwbAvboitTransmittanceBuffer{
    uint g_Transmittance[];
};

layout(std430, binding = 2) readonly buffer NwbAvboitControlBuffer{
    uint g_Control[];
};

void main(){
    const uint lowPixelIndex = gl_GlobalInvocationID.x;
    const uint lowPixelCount = nwbAvboitLowWidth() * nwbAvboitLowHeight();
    if(lowPixelIndex >= lowPixelCount)
        return;

    const uvec2 lowPixel = uvec2(lowPixelIndex % nwbAvboitLowWidth(), lowPixelIndex / nwbAvboitLowWidth());
    const uint activePhysicalSlices = clamp(g_Control[0], 1u, nwbAvboitPhysicalSliceCount());
    float accumulatedExtinction = 0.0;

    for(uint physicalSlice = 0u; physicalSlice < nwbAvboitPhysicalSliceCount(); ++physicalSlice){
        const uint volumeIndex = nwbAvboitVolumeIndex(lowPixel, physicalSlice);
        const float transmittance = exp(-min(accumulatedExtinction, 16.0));
        g_Transmittance[volumeIndex] = uint(clamp(round(transmittance * 65535.0), 0.0, 65535.0));

        if(physicalSlice < activePhysicalSlices)
            accumulatedExtinction += nwbAvboitUnpackExtinction(g_Extinction[volumeIndex]);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
