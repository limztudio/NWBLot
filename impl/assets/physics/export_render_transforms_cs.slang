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
    const uint poseBase = (g_NwbPhysicsPushConstants.slots1.z != NWB_PHYSICS_INVALID_INDEX) ? g_NwbPhysicsPushConstants.slots1.z : 0u;
    const uint baseFloat4Offset = (g_NwbPhysicsPushConstants.slots1.w != NWB_PHYSICS_INVALID_INDEX) ? g_NwbPhysicsPushConstants.slots1.w : 0u;

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
        if(exportMode == NWB_PHYSICS_EXPORT_MODE_STRUCTURED_TRS){
            linVel = bo.linearVelocityFlags.xyz;
            sleepTime = bo.positionSleepTime.w;
        }
    }
    else{
        const NwbPhysicsGpuBodyPose bp = g_NwbPhysicsBodyPoseBuffers[sourceSlot][poseBase + bodyIdx];
        origin = bp.originPad.xyz;
        rot = bp.rotation;
        if(exportMode == NWB_PHYSICS_EXPORT_MODE_STRUCTURED_TRS){
            linVel = bp.sweepDispFlags.xyz;
            sleepTime = bp.originPad.w;
        }
    }

    if(exportMode == NWB_PHYSICS_EXPORT_MODE_PATCH_INSTANCE_BUFFER){
        const uint baseIdx = baseFloat4Offset + dstIdx * strideFloat4;
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 0u] = rot;
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 1u].xyz = origin;
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 2u].xyz = b.scalePad.xyz;
        return;
    }

    if(exportMode == NWB_PHYSICS_EXPORT_MODE_PATCH_AFFINE_3X4_OBJ_TO_WORLD){
        float3 ax;
        float3 ay;
        float3 az;
        nwbPhysicsQuatToAxes(rot, ax, ay, az);
        const float3 s = b.scalePad.xyz;
        const uint baseIdx = baseFloat4Offset + dstIdx * strideFloat4;
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 0u] = float4(ax.x * s.x, ay.x * s.y, az.x * s.z, origin.x);
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 1u] = float4(ax.y * s.x, ay.y * s.y, az.y * s.z, origin.y);
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 2u] = float4(ax.z * s.x, ay.z * s.y, az.z * s.z, origin.z);
        return;
    }

    if(exportMode == NWB_PHYSICS_EXPORT_MODE_PATCH_AFFINE_3X4_WORLD_TO_OBJ){
        float3 ax;
        float3 ay;
        float3 az;
        nwbPhysicsQuatToAxes(rot, ax, ay, az);
        const float3 rawS = b.scalePad.xyz;
        float3 invS = float3(1.0f, 1.0f, 1.0f);
        if(abs(rawS.x) > 1.0e-6f)
            invS.x = 1.0f / rawS.x;
        if(abs(rawS.y) > 1.0e-6f)
            invS.y = 1.0f / rawS.y;
        if(abs(rawS.z) > 1.0e-6f)
            invS.z = 1.0f / rawS.z;
        const uint baseIdx = baseFloat4Offset + dstIdx * strideFloat4;
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 0u] = float4(ax.x * invS.x, ax.y * invS.x, ax.z * invS.x, -dot(ax, origin) * invS.x);
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 1u] = float4(ay.x * invS.y, ay.y * invS.y, ay.z * invS.y, -dot(ay, origin) * invS.y);
        g_NwbPhysicsFloat4Buffers[outSlot][baseIdx + 2u] = float4(az.x * invS.z, az.y * invS.z, az.z * invS.z, -dot(az, origin) * invS.z);
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

