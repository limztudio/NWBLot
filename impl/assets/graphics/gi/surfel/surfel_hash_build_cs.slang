// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel hash build: one thread per pool slot. A live surfel links itself into its cell's singly-linked list --
// InterlockedExchange the cell head to this surfel id (one integer atomic, no float atomics) and store the previous
// head as this surfel's nextInCell. The gather + the next frame's spawn walk these lists. The cell-head table is
// cleared to NWB_SURFEL_CELL_INVALID by the CPU each frame BEFORE this pass (a buffer clear), so the linking starts
// from empty lists.


#include "surfel_runtime.slangi"
#include "surfel_hash.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_BINDING_CELL_HEAD, NWB_SURFEL_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelCellHead;


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

