// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Age-free recycling: unseen-for-maxAge surfels freed + pushed to the LIFO free-list (one integer atomic).
// Runs first each frame; spawn pops before bump-allocating, bounding the pool to the visible set.


#include "surfel_runtime.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Heap-selected mutable allocation state.
#define g_NwbSurfelCounter g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.counterHeapSlot]
#define g_NwbSurfelFreeList g_NwbHeapSurfelWritableUintBuffers[g_NwbSurfelHeapPushConstants.freeListHeapSlot]


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_LINEAR_GROUP_SIZE, 1, 1)]
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

    // Unsigned age is exact (lastSeen <= frame). Unseen past maxAge: mark dead, push once (alive early-out stops
    // re-push); free-list stays duplicate-free.
    if(frameIndex - surfel.lastSeenFrame > maxAge){
        g_NwbSurfelPool[surfelIndex].alive = 0u;
        uint destination = 0u;
        InterlockedAdd(g_NwbSurfelCounter[NWB_SURFEL_COUNTER_FREE_TOP], 1u, destination);
        g_NwbSurfelFreeList[destination] = surfelIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

