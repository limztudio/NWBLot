// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hash build: live surfels InterlockedExchange into per-cell lists (one integer atomic). Heads CPU-cleared to INVALID before this pass, so linking starts empty.


#include "surfel_runtime.slangi"
#include "surfel_hash.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSurfelCellHead g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.cellHeadHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_LINEAR_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint surfelIndex = dispatchThreadID.x;
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    if(surfelIndex >= poolCapacity)
        return;

    const NwbSurfel surfel = g_NwbSurfelPool[surfelIndex];
    if(!nwbSurfelIsAlive(surfel.sampleState))
        return;

    const float cellSize = g_NwbSurfelConstants.cellSizeRadiusBiasAccum.x;
    const uint cellCount = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.x);
    const int3 cell = nwbSurfelCellCoord(surfel.position, cellSize);
    const uint bucket = nwbSurfelHashCell(cell, cellCount);

    // Each surfel-index thread owns its exact cell and list link; the following pass observes both after the pool UAV barrier.
    uint previousHead = NWB_SURFEL_CELL_INVALID;
    InterlockedExchange(g_NwbSurfelCellHead[bucket], surfelIndex, previousHead);
    g_NwbSurfelPool[surfelIndex].cell = cell;
    g_NwbSurfelPool[surfelIndex].nextInCell = previousHead;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

