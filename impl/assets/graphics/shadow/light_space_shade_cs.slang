// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_scene.slangi"
#include "shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"
#include "shadow_integrate.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Capture and shading use the same bounded records. The graph orders raw crossing writes before this in-place optical conversion.
[numthreads(NWB_LIGHT_SPACE_SHADE_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint pixelIndex = dispatchThreadID.x
        + dispatchThreadID.y * (NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X * NWB_LIGHT_SPACE_SHADE_GROUP_SIZE);
    if(pixelIndex >= g_NwbLightSpacePush.width)
        return;
    const uint count = NwbHeapRawBuffer(g_NwbLightSpacePush.countsSlot).Load(pixelIndex * 4u);
    if(count == 0u || count > NWB_LIGHT_SPACE_EVENTS_PER_TEXEL)
        return;
    NwbLightSpaceView view = (NwbLightSpaceView)0;
    for(uint index = 0u; index < g_NwbLightSpacePush.viewCount; ++index){
        const NwbLightSpaceView candidate = nwbLightSpaceLoadView(index);
        if(pixelIndex >= candidate.map.z && pixelIndex - candidate.map.z < candidate.map.x * candidate.map.y){
            view = candidate;
            break;
        }
    }
    for(uint record = 0u; record < count; ++record){
        const uint address = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + record) * NWB_LIGHT_SPACE_EVENT_BYTES;
        const uint4 raw = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load4(address);
        const float2 bary = float2(asfloat(raw.w), asfloat(g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load(address + 16u)));
        const uint instanceIndex = raw.y;
        const uint primitive = raw.z;
        const NwbRtInstanceMaterial material = nwbLightSpaceMaterial(instanceIndex);
        const NwbSwBvhInstance instance = nwbLightSpaceInstance(instanceIndex);
        const uint3 indices = NwbHeapRawBuffer(material.indexSlot).Load3(primitive * 12u);
        const float3 p0 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(indices.x * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
        const float3 p1 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(indices.y * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
        const float3 p2 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(indices.z * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
        const NwbRayTraceVertexAttrib a0 = nwbSwShadowLoadVertexAttrib(material.attributeSlot, primitive * 3u);
        const NwbRayTraceVertexAttrib a1 = nwbSwShadowLoadVertexAttrib(material.attributeSlot, primitive * 3u + 1u);
        const NwbRayTraceVertexAttrib a2 = nwbSwShadowLoadVertexAttrib(material.attributeSlot, primitive * 3u + 2u);
        const float3 weights = float3(1.0 - bary.x - bary.y, bary);
        const float3 local = p0 * weights.x + p1 * weights.y + p2 * weights.z;
        const float3 world = nwbLightSpaceWorldPosition(local, instanceIndex);
        const float3 n0 = float3(a0.normal);
        const float3 n1 = float3(a1.normal);
        const float3 n2 = float3(a2.normal);
        const float3 normal = n0 * weights.x + n1 * weights.y + n2 * weights.z;
        const float3 geometric = cross(p1 - p0, p2 - p0);
        const float spread = max(max(dot(n0 - n1, n0 - n1), dot(n1 - n2, n1 - n2)), dot(n2 - n0, n2 - n0));
        const float3 towardLight = (view.light.w & NWB_LIGHT_SPACE_FLAG_POINT) != 0u
            ? normalize(view.originOrDirection.xyz - world) : view.originOrDirection.xyz;
        const float3 objectDirection = nwbSwBvhTransformVector(instance, towardLight);
        NwbShadowHit hit;
        hit.meshInstanceIndex = material.meshInstanceIndex;
        hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
        hit.materialConstantByteOffset = material.materialConstantByteOffset;
        hit.uv0 = a0.uv0 * weights.x + a1.uv0 * weights.y + a2.uv0 * weights.z;
        hit.worldNormal = nwbSwShadowTransformNormal(instance, normal);
        hit.worldPosition = world;
        hit.rayDirection = towardLight;
        const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
        const half cosI = nwbShadowCosineToRay(geometric, objectDirection);
        const half smoothCosI = nwbShadowCosineToRay(normal, objectDirection);
        const half fresnel = nwbShadowFresnelTransmittedFraction(surface.refractionIor, lerp(cosI, smoothCosI, half(smoothstep(1e-5, 2e-3, spread))));
        const half3 absorption = nwbShadowAbsorptionCoefficient(surface.shadowAbsorptionTint);
        const bool valid = view.depthRange.w != 0.0 && isfinite(asfloat(raw.x))
            && all(isfinite(float3(absorption))) && isfinite(float(fresnel));
        // A nonfinite depth invalidates this texel at resolve, with the same software fallback as capture overflow.
        const uint facing = dot(geometric, objectDirection) < 0.0 ? NWB_LIGHT_SPACE_EVENT_ENTERING : 0u;
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(address, uint4(valid ? raw.x : 0x7fc00000u,
            instanceIndex | facing, primitive, f32tof16(float(absorption.x)) | (f32tof16(float(absorption.y)) << 16u)));
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(address + 16u,
            f32tof16(float(absorption.z)) | (f32tof16(float(fresnel)) << 16u));
    }
    // Order each retained texel once. Receiver threads stream these records without private arrays or repeated sorting.
    for(uint index = 1u; index < count; ++index){
        const uint address = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + index) * NWB_LIGHT_SPACE_EVENT_BYTES;
        const uint4 value = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load4(address);
        const uint optical = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load(address + 16u);
        const uint instance = value.y & NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
        uint destination = index;
        while(destination > 0u){
            const uint previousAddress = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + destination - 1u) * NWB_LIGHT_SPACE_EVENT_BYTES;
            const uint4 previous = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load4(previousAddress);
            const uint previousInstance = previous.y & NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
            const bool before = instance < previousInstance || (instance == previousInstance &&
                (asfloat(value.x) > asfloat(previous.x) || (asfloat(value.x) == asfloat(previous.x) && value.z < previous.z)));
            if(!before)
                break;
            const uint previousOptical = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load(previousAddress + 16u);
            g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(previousAddress + NWB_LIGHT_SPACE_EVENT_BYTES, previous);
            g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(previousAddress + NWB_LIGHT_SPACE_EVENT_BYTES + 16u, previousOptical);
            --destination;
        }
        const uint destinationAddress = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + destination) * NWB_LIGHT_SPACE_EVENT_BYTES;
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(destinationAddress, value);
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(destinationAddress + 16u, optical);
    }

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

