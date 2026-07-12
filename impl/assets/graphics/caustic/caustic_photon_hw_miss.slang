
#include "caustic_hw_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The hardware caustic miss. The ray segment escaped to infinity (no surface in [tMin, tMax]); mark it a miss so the
// shared bounce loop drops the photon (caustic_trace.slangi: `if(!hit.hit) return splat;`). The raygen seeds the
// payload before each TraceRay, but clearing here too keeps the contract robust regardless of seed.
[shader("miss")]
void main(inout NwbCausticHwPayload payload){
    payload.hit = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

