// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compute_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The integrated transmittance volume and all source work buffers are global-heap entries selected by the shared
// target-generation payload. Keep the established local names below so the integration algorithm stays readable.
#define g_Transmittance nwbAvboitTransmittanceUav()
#define g_Extinction nwbAvboitExtinction()
#define g_Control nwbAvboitControl()
#define g_ExtinctionOverflowDepth nwbAvboitExtinctionOverflowDepth()


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbAvboitIntegrateMain(uint lowPixelIndex){
    const uint lowWidth = nwbAvboitLowWidth();
    const uint lowHeight = nwbAvboitLowHeight();
    const uint lowPixelCount = lowWidth * lowHeight;
    if(lowPixelIndex >= lowPixelCount)
        return;

    const uint physicalSliceCount = nwbAvboitPhysicalSliceCount();
    const uint activePhysicalSlices = clamp(g_Control[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES], 1u, physicalSliceCount);
    const uint2 lowPixel = uint2(lowPixelIndex % lowWidth, lowPixelIndex / lowWidth);
    const uint overflowSlice = g_ExtinctionOverflowDepth[lowPixelIndex];
    const float saturatedExtinction = NWB_AVBOIT_EXTINCTION_SATURATION;
    float accumulatedExtinction = 0.0;
    uint cachedWordIndex = NWB_AVBOIT_OVERFLOW_INVALID;
    uint cachedPackedWord = 0u;

    for(uint physicalSlice = 0u; physicalSlice < physicalSliceCount; ++physicalSlice){
        const float transmittance = exp(-min(accumulatedExtinction, saturatedExtinction));
        g_Transmittance[uint3(lowPixel.x, lowPixel.y, physicalSlice)] = transmittance;

        if(physicalSlice >= activePhysicalSlices || accumulatedExtinction >= saturatedExtinction)
            continue;

        if(overflowSlice != NWB_AVBOIT_OVERFLOW_INVALID && physicalSlice >= overflowSlice){
            accumulatedExtinction = saturatedExtinction;
            continue;
        }

        const uint wordIndex = nwbAvboitExtinctionWordIndex(lowPixel, physicalSlice);
        if(wordIndex >= nwbAvboitExtinctionWordCount())
            continue;

        if(wordIndex != cachedWordIndex){
            cachedPackedWord = g_Extinction[wordIndex];
            cachedWordIndex = wordIndex;
        }
        accumulatedExtinction = min(
            accumulatedExtinction + nwbAvboitUnpackExtinctionByte(cachedPackedWord, physicalSlice),
            saturatedExtinction
        );
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_AVBOIT_INTEGRATE_GROUP_SIZE_X, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    nwbAvboitIntegrateMain(dispatchThreadID.x);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef g_Transmittance
#undef g_Extinction
#undef g_Control
#undef g_ExtinctionOverflowDepth

