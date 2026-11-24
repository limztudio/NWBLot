// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "physics_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_PHYSICS_WORKGROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint tid = dispatchThreadID.x;
    if(tid >= g_NwbPhysicsPushConstants.itemCount)
        return;

    const uint sourceBodyCount = g_NwbPhysicsPushConstants.itemOffset;
    const uint exportMode = g_NwbPhysicsPushConstants.modeOrSecondaryCount;
    const uint sourceMode = g_NwbPhysicsPushConstants.flags;
    const uint bindingSlot = g_NwbPhysicsPushConstants.slots0.x;
    const uint sourceSlot = g_NwbPhysicsPushConstants.slots0.y;
    const uint bodySimSlot = g_NwbPhysicsPushConstants.slots0.z;
    const uint outSlot = g_NwbPhysicsPushConstants.slots0.w;
    const uint strideFloat4 = max(g_NwbPhysicsPushConstants.slots1.x, 3u);
    const uint bindingOffset = (g_NwbPhysicsPushConstants.slots1.y != NWB_PHYSICS_INVALID_INDEX) ? g_NwbPhysicsPushConstants.slots1.y : 0u;

    const NwbPhysicsGpuRenderTransformBinding b = g_NwbPhysicsRenderBindingBuffers[bindingSlot][bindingOffset + tid];
    const uint bodyIdx = b.bodyDestFlags.x;
    const uint dstIdx = b.bodyDestFlags.y;
    if(bodyIdx >= sourceBodyCount)
        return;

    float3 origin = float3(0.0f, 0.0f, 0.0f);
    float4 rot = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float3 linVel = float3(0.0f, 0.0f, 0.0f);
    float sleepTime = 0.0f;

    if(sourceMode == NWB_PHYSICS_EXPORT_SOURCE_BODY_OUT){
        const NwbPhysicsGpuBodyOut bo = g_NwbPhysicsBodyOutBuffers[sourceSlot][bodyIdx];
        const float3 localCenter = g_NwbPhysicsBodySimBuffers[bodySimSlot][bodyIdx].localCenterMinExtent.xyz;
        rot = bo.rotation;
        origin = bo.positionSleepTime.xyz - nwbPhysicsQuatRotate(localCenter, rot);
        if(exportMode != NWB_PHYSICS_EXPORT_MODE_PATCH_INSTANCE_BUFFER){
            linVel = bo.linearVelocityFlags.xyz;
            sleepTime = bo.positionSleepTime.w;
        }
    }
    else{
        const NwbPhysicsGpuBodyPose bp = g_NwbPhysicsBodyPoseBuffers[sourceSlot][bodyIdx];
        origin = bp.originPad.xyz;
        rot = bp.rotation;
    }

    if(exportMode == NWB_PHYSICS_EXPORT_MODE_PATCH_INSTANCE_BUFFER){
        const uint baseIdx = dstIdx * strideFloat4;
        const float4 oldTrans = g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 1u];
        const float4 oldScale = g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 2u];
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 0u] = rot;
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 1u] = float4(origin, oldTrans.w);
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 2u] = float4(b.scalePad.xyz, oldScale.w);
        return;
    }

    NwbPhysicsGpuRenderTransformOut outTrs;
    outTrs.rotation = rot;
    outTrs.translationFlags = float4(origin, asfloat(b.bodyDestFlags.w));
    outTrs.scaleEntityId = float4(b.scalePad.xyz, asfloat(b.bodyDestFlags.z));
    outTrs.linearVelSleep = float4(linVel, sleepTime);
    g_NwbPhysicsRenderTransformOutBuffers[outSlot][dstIdx] = outTrs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

