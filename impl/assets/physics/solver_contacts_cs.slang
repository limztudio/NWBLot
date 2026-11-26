// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "solver_contact_helpers.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID){
    uint itemCount = g_NwbPhysicsPushConstants.itemCount;
    uint itemOffset = g_NwbPhysicsPushConstants.itemOffset;
    const uint mode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint flags = g_NwbPhysicsPushConstants.flags;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint bodyStateSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint constraintSlot = g_NwbPhysicsPushConstants.slots0.w;
    const float invH = g_NwbPhysicsPushConstants.invDeltaTime;

    if((flags & NWB_PHYSICS_SOLVER_FLAG_FUSED_COLORS) != 0u){
        const uint sliceMetaSlot = g_NwbPhysicsPushConstants.slots1.w;
        const uint startColor = g_NwbPhysicsUintBuffers[sliceMetaSlot][NWB_PHYSICS_GRAPH_COLOR_COUNT * 2u + 2u];
        const uint localTid = groupThreadID.x;
        const uint parallelFlags = flags & ~NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP;
        [loop] for(uint c = startColor; c < NWB_PHYSICS_OVERFLOW_COLOR_INDEX; ++c){
            const uint cOff = g_NwbPhysicsUintBuffers[sliceMetaSlot][c * 2u + 0u];
            const uint cCnt = g_NwbPhysicsUintBuffers[sliceMetaSlot][c * 2u + 1u];
            if(cCnt == 0u)
                continue;
            [loop] for(uint k = localTid; k < cCnt; k += NWB_PHYSICS_WORKGROUP_SIZE)
                nwbPhysicsSolveSingleContact(cOff + k, mode, bodySimSlot, bodyStateSlot, constraintSlot, invH, parallelFlags, true);
            DeviceMemoryBarrierWithGroupSync();
        }
        const uint ovOff = g_NwbPhysicsUintBuffers[sliceMetaSlot][NWB_PHYSICS_OVERFLOW_COLOR_INDEX * 2u + 0u];
        const uint ovCnt = g_NwbPhysicsUintBuffers[sliceMetaSlot][NWB_PHYSICS_OVERFLOW_COLOR_INDEX * 2u + 1u];
        if(ovCnt > 0u && localTid == 0u){
            [loop] for(uint k = 0u; k < ovCnt; ++k)
                nwbPhysicsSolveSingleContact(ovOff + k, mode, bodySimSlot, bodyStateSlot, constraintSlot, invH, flags | NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP, true);
        }
        return;
    }

    if((flags & NWB_PHYSICS_SOLVER_FLAG_INDIRECT_SLICE) != 0u){
        const uint sliceMetaSlot = g_NwbPhysicsPushConstants.slots1.w;
        const uint colorIdx = g_NwbPhysicsPushConstants.itemOffset;
        itemOffset = g_NwbPhysicsUintBuffers[sliceMetaSlot][colorIdx * 2u + 0u];
        itemCount = g_NwbPhysicsUintBuffers[sliceMetaSlot][colorIdx * 2u + 1u];
        if(itemCount == 0u)
            return;
    }

    const bool isSerial = (flags & NWB_PHYSICS_SOLVER_FLAG_SERIAL_LOOP) != 0u;
    if(isSerial ? (dispatchThreadID.x != 0u) : (dispatchThreadID.x >= itemCount))
        return;

    const uint startIdx = isSerial ? 0u : dispatchThreadID.x;
    const uint endIdx = isSerial ? itemCount : (dispatchThreadID.x + 1u);
    [loop] for(uint k = startIdx; k < endIdx; ++k)
        nwbPhysicsSolveSingleContact(itemOffset + k, mode, bodySimSlot, bodyStateSlot, constraintSlot, invH, flags, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

