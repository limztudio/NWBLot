// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hash build: live surfels InterlockedExchange into per-cell lists (one integer atomic). Heads CPU-cleared to
// INVALID before this pass, so linking starts empty.


#include "surfel_runtime.slangi"
#include "surfel_hash.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Heap-selected current-frame hash heads.
#define g_NwbSurfelCellHead g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.cellHeadHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_LINEAR_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint surfelIndex = dispatchThreadID.x;
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    if(surfelIndex >= poolCapacity)
        return;

    const NwbSurfel surfel = g_NwbSurfelPool[surfelIndex];
    if(surfel.alive == 0u)
        return;

    const float cellSize = g_NwbSurfelConstants.cameraPositionCellSize.w;
    const uint cellCount = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.x);
    const uint bucket = nwbSurfelHashCell(nwbSurfelCellCoord(surfel.position, cellSize), cellCount);

    // Push onto the cell's linked list: this surfel becomes the new head; its next is the previous head. Each slot is
    // written by exactly one thread (surfelIndex), so the single-field nextInCell store is race-free.
    uint previousHead = NWB_SURFEL_CELL_INVALID;
    InterlockedExchange(g_NwbSurfelCellHead[bucket], surfelIndex, previousHead);
    g_NwbSurfelPool[surfelIndex].nextInCell = previousHead;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

