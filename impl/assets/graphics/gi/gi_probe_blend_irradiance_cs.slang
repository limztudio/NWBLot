// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DDGI irradiance atlas blend (stub). U1 scaffold: no-op placeholder. U4 fills in the gather-style cosine-weighted
// EMA blend (thread-per-atlas-texel, NO float image atomics -- caustic accumulator precedent) that folds this frame's
// ray-data into the front irradiance atlas, writing the back atlas. See .helper/ddgi_plan.md §2/§3 (U4).


#include "binding_slots.h"


[numthreads(NWB_GI_GROUP_SIZE, NWB_GI_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

