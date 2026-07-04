// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI distance atlas blend (stub). U1 scaffold: no-op placeholder. U4 fills in the mean/mean-squared distance blend
// (hitT clamped ~1.5x grid diagonal) that feeds the Chebyshev leak rejection + self-shadow bias. See
// .helper/ddgi_plan.md §2/§3 (U4).


#include "binding_slots.h"


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

