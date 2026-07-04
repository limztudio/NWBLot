// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI atlas border fill (stub). U1 scaffold: no-op placeholder. U4 fills in the edge-texel copy from the interior so
// octahedral sampling at the atlas border does not wrap into the neighbour probe's texels. See .helper/ddgi_plan.md §2/§3 (U4).


#include "binding_slots.h"


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

