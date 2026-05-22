// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_compute_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(0, 0)]] StructuredBuffer<uint, Std430DataLayout> g_CoverageWords;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint, Std430DataLayout> g_DepthWarp;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint, Std430DataLayout> g_Control;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbAvboitVirtualSliceOccupied(uint virtualSlice){
    return (g_CoverageWords[virtualSlice >> 5u] & (1u << (virtualSlice & 31u))) != 0u;
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

    g_Control[0] = min(max(occupiedCount, 1u), nwbAvboitPhysicalSliceCount());
    g_Control[1] = compressionShift;
    g_Control[2] = nwbAvboitVirtualSliceCount();
    g_Control[3] = nwbAvboitPhysicalSliceCount();
    g_Control[4] = nwbAvboitLowWidth();
    g_Control[5] = nwbAvboitLowHeight();
    g_Control[6] = nwbAvboitFullWidth();
    g_Control[7] = nwbAvboitFullHeight();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(1, 1, 1)]
void main(){
    nwbAvboitDepthWarpMain();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

