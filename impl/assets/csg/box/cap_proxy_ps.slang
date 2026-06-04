// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_MESH_INSTANCE_INDEX 0u

#include "../../graphics/csg/binding_slots.h"

#include "../../graphics/mesh/gbuffer_io.slangi"
#include "../../graphics/mesh/view_buffer.slangi"
#include "../../graphics/csg/cap_proxy/projection.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbCsgCapProxyShapeShrinkWorldPosition(const float3 worldPosition, const NwbCsgCutter cutter){
    return worldPosition;
}

bool nwbCsgCapProxyShapeResolveWorldPosition(
    const float3 proxyWorldPosition,
    const NwbCsgCutter cutter,
    const NwbCsgReceiverRange receiverRange,
    out float3 outWorldPosition
){
    outWorldPosition = nwbCsgCapProxyShapeShrinkWorldPosition(proxyWorldPosition, cutter);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../../graphics/csg/cap_proxy/ps_main.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
