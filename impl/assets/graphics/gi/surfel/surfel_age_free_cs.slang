// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel age-free (U1 recycling): one thread per pool slot. A live surfel whose cell has not been "seen" (covered by a
// visible spawn tile) for more than maxAge frames is freed -- alive = 0 -- and its slot id is PUSHED onto the free-list
// LIFO stack (InterlockedAdd on counter[FREE_TOP], one integer atomic, no float atomics). The spawn pass POPS these ids
// before bump-allocating, so the pool stays bounded by the visible working set instead of growing monotonically. This
// pass runs FIRST each frame (before the cell-head clear + hash-build), reading lastSeenFrame written by the PREVIOUS
// frame's spawn keep-alive. See .helper/surfel_gi_plan.md (U1).


#include "surfel_record.slangi"
#include "surfel_constants.slangi"
#include "surfel_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_BINDING_CONSTANTS, NWB_SURFEL_SET)]]
ConstantBuffer<NwbSurfelConstants> g_NwbSurfelConstants;

[[vk::binding(NWB_SURFEL_BINDING_POOL, NWB_SURFEL_SET)]]
RWStructuredBuffer<NwbSurfel> g_NwbSurfelPool;

[[vk::binding(NWB_SURFEL_BINDING_COUNTER, NWB_SURFEL_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelCounter;

[[vk::binding(NWB_SURFEL_BINDING_FREE_LIST, NWB_SURFEL_SET)]]
RWStructuredBuffer<uint> g_NwbSurfelFreeList;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint surfelIndex = dispatchThreadID.x;
    const uint poolCapacity = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.y);
    if(surfelIndex >= poolCapacity)
        return;

    NwbSurfel surfel = g_NwbSurfelPool[surfelIndex];
    if(surfel.alive == 0u)
        return;

    const uint frameIndex = uint(g_NwbSurfelConstants.hashPoolFrameDivisor.z);
    const uint maxAge = uint(g_NwbSurfelConstants.ageRaysTileScreen.x);

    // lastSeenFrame <= frameIndex always (set with the current frame index at spawn/keep-alive, and this pass runs before
    // this frame's spawn), so the unsigned age is exact. A surfel unseen for more than maxAge frames is recycled: mark it
    // dead and push its slot id onto the free-list for the spawn to reuse. Age-free pushes each dead slot exactly once
    // (the alive == 0 early-out above prevents a re-push), so the free-list holds no duplicates.
    if(frameIndex - surfel.lastSeenFrame > maxAge){
        g_NwbSurfelPool[surfelIndex].alive = 0u;
        uint destination = 0u;
        InterlockedAdd(g_NwbSurfelCounter[NWB_SURFEL_COUNTER_FREE_TOP], 1u, destination);
        g_NwbSurfelFreeList[destination] = surfelIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
