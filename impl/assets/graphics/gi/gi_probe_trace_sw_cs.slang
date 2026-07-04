// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI probe trace (SOFTWARE backend stub). U1 scaffold: the kernel is a no-op placeholder so the cook registers the
// shader + the pipeline can be created. U3 fills in the Fibonacci + R2 rotation + closest-hit SW-BVH traversal +
// Lambert hit shade + prev-atlas bounce + the D4 dominant-light shadow ray. See .helper/ddgi_plan.md §2/§3 (U3).


#include "binding_slots.h"


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

