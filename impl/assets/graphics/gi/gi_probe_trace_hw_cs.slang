// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI probe trace (HARDWARE RayQuery backend stub). U1 scaffold: no-op placeholder. U6 fills in the inline-RayQuery
// twin of the SW trace (same body behind the traversal #define seam). See .helper/ddgi_plan.md §2/§3 (U6).


#include "binding_slots.h"


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

