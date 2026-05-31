// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compute_common.slangi"
#include "binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_AVBOIT_DEPTH_WARP_BINDING_COVERAGE_WORDS, NWB_AVBOIT_COMPUTE_SET)]] StructuredBuffer<uint, Std430DataLayout> g_CoverageWords;
[[vk::binding(NWB_AVBOIT_DEPTH_WARP_BINDING_DEPTH_WARP, NWB_AVBOIT_COMPUTE_SET)]] RWStructuredBuffer<uint, Std430DataLayout> g_DepthWarp;
[[vk::binding(NWB_AVBOIT_DEPTH_WARP_BINDING_CONTROL, NWB_AVBOIT_COMPUTE_SET)]] RWStructuredBuffer<uint, Std430DataLayout> g_Control;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbAvboitVirtualSliceOccupied(uint virtualSlice){
    return (g_CoverageWords[virtualSlice >> NWB_AVBOIT_COVERAGE_WORD_SHIFT] & (1u << (virtualSlice & NWB_AVBOIT_COVERAGE_BIT_MASK))) != 0u;
}

bool nwbAvboitCoarseBinOccupied(uint coarseBin, uint compressionShift){
    const uint beginSlice = coarseBin << compressionShift;
    const uint endSlice = min(beginSlice + (1u << compressionShift), nwbAvboitVirtualSliceCount());
    for(uint virtualSlice = beginSlice; virtualSlice < endSlice; ++virtualSlice){
        if(nwbAvboitVirtualSliceOccupied(virtualSlice))
            return true;
    }
    return false;
}

uint nwbAvboitCountOccupiedBins(uint compressionShift){
    const uint binCount = (nwbAvboitVirtualSliceCount() + (1u << compressionShift) - 1u) >> compressionShift;
    uint occupiedCount = 0u;
    for(uint bin = 0u; bin < binCount; ++bin){
        if(nwbAvboitCoarseBinOccupied(bin, compressionShift))
            ++occupiedCount;
    }
    return occupiedCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbAvboitDepthWarpMain(){
    uint compressionShift = 0u;
    uint occupiedCount = nwbAvboitCountOccupiedBins(compressionShift);
    while(occupiedCount > nwbAvboitPhysicalSliceCount() && ((1u << compressionShift) < nwbAvboitVirtualSliceCount())){
        ++compressionShift;
        occupiedCount = nwbAvboitCountOccupiedBins(compressionShift);
    }

    const uint binCount = (nwbAvboitVirtualSliceCount() + (1u << compressionShift) - 1u) >> compressionShift;
    uint physicalIndex = 0u;
    uint previousPhysicalIndex = NWB_AVBOIT_WARP_INVALID;

    for(uint bin = 0u; bin < binCount; ++bin){
        const bool occupied = nwbAvboitCoarseBinOccupied(bin, compressionShift);
        const uint mappedIndex = occupied ? min(physicalIndex, nwbAvboitPhysicalSliceCount() - 1u) : previousPhysicalIndex;
        const uint beginSlice = bin << compressionShift;
        const uint endSlice = min(beginSlice + (1u << compressionShift), nwbAvboitVirtualSliceCount());

        for(uint virtualSlice = beginSlice; virtualSlice < endSlice; ++virtualSlice)
            g_DepthWarp[virtualSlice] = mappedIndex;

        if(occupied){
            previousPhysicalIndex = mappedIndex;
            ++physicalIndex;
        }
    }

    if(previousPhysicalIndex == NWB_AVBOIT_WARP_INVALID){
        for(uint virtualSlice = 0u; virtualSlice < nwbAvboitVirtualSliceCount(); ++virtualSlice)
            g_DepthWarp[virtualSlice] = 0u;
        occupiedCount = 1u;
        compressionShift = 0u;
    }
    else{
        for(uint virtualSlice = 0u; virtualSlice < nwbAvboitVirtualSliceCount(); ++virtualSlice){
            if(g_DepthWarp[virtualSlice] == NWB_AVBOIT_WARP_INVALID)
                g_DepthWarp[virtualSlice] = 0u;
        }
    }

    g_Control[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES] = min(max(occupiedCount, 1u), nwbAvboitPhysicalSliceCount());
    g_Control[NWB_AVBOIT_CONTROL_COMPRESSION_SHIFT] = compressionShift;
    g_Control[NWB_AVBOIT_CONTROL_VIRTUAL_SLICE_COUNT] = nwbAvboitVirtualSliceCount();
    g_Control[NWB_AVBOIT_CONTROL_PHYSICAL_SLICE_COUNT] = nwbAvboitPhysicalSliceCount();
    g_Control[NWB_AVBOIT_CONTROL_LOW_WIDTH] = nwbAvboitLowWidth();
    g_Control[NWB_AVBOIT_CONTROL_LOW_HEIGHT] = nwbAvboitLowHeight();
    g_Control[NWB_AVBOIT_CONTROL_FULL_WIDTH] = nwbAvboitFullWidth();
    g_Control[NWB_AVBOIT_CONTROL_FULL_HEIGHT] = nwbAvboitFullHeight();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(1, 1, 1)]
void main(){
    nwbAvboitDepthWarpMain();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

