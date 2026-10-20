// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_LIGHT_SPACE_CSG_SHADE
#define NWB_LIGHT_SPACE_CSG_SHADE 0
#endif

#include "light_space_scene.slangi"
#include "light_space_interpolation.slangi"
#include "shadow_surface.slangi"
#include "shadow/generated/surface_dispatch.slangi"
#include "shadow_integrate.slangi"
#if NWB_LIGHT_SPACE_CSG_SHADE
#include "csg_shadow_shade.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbLightSpaceInvalidateCrossing(const uint address, const uint instance, const uint primitive){
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(address, uint4(NWB_FLOAT_QUIET_NAN_BITS, instance, primitive, 0u));
    g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(address + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET, 0u);
}

// Receiver threads consume instance ASC, depth DESC, primitive ASC records after both shading and cap conversion.
void nwbLightSpaceSortEvents(const uint pixelIndex, const uint count){
    for(uint index = 1u; index < count; ++index){
        const uint address = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + index) * NWB_LIGHT_SPACE_EVENT_BYTES;
        const uint4 value = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load4(address);
        const uint optical = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load(address + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET);
        const uint instance = value.y & NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
        uint destination = index;
        while(destination > 0u){
            const uint previousAddress = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + destination - 1u) * NWB_LIGHT_SPACE_EVENT_BYTES;
            const uint4 previous = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load4(previousAddress);
            const uint previousInstance = previous.y & NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
            const bool before = instance < previousInstance || (instance == previousInstance && (asfloat(value.x) > asfloat(previous.x) || (asfloat(value.x) == asfloat(previous.x) && value.z < previous.z)));
            if(!before)
                break;
            const uint previousOptical = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load(previousAddress + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET);
            g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(previousAddress + NWB_LIGHT_SPACE_EVENT_BYTES, previous);
            g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(previousAddress + NWB_LIGHT_SPACE_EVENT_BYTES + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET, previousOptical);
            --destination;
        }
        const uint destinationAddress = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + destination) * NWB_LIGHT_SPACE_EVENT_BYTES;
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(destinationAddress, value);
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(destinationAddress + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET, optical);
    }
}

// Capture and shading use the same bounded records. The graph orders raw crossing writes before this in-place optical conversion.
[numthreads(NWB_LIGHT_SPACE_SHADE_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint pixelIndex = dispatchThreadID.x + dispatchThreadID.y * (NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X * NWB_LIGHT_SPACE_SHADE_GROUP_SIZE);
    if(pixelIndex >= g_NwbLightSpacePush.width)
        return;
    const uint count = nwbHeapRawBuffer(g_NwbLightSpacePush.countsSlot).Load(pixelIndex * NWB_LIGHT_SPACE_PIXEL_WORD_BYTES);
#if NWB_LIGHT_SPACE_CSG_SHADE
    if(nwbCsgShadowEnabled()){
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.csgOpaqueDepthSlot].Store(pixelIndex * NWB_LIGHT_SPACE_PIXEL_WORD_BYTES, NWB_FLOAT_MAX_BITS);
        if(count > NWB_LIGHT_SPACE_EVENTS_PER_TEXEL)
            nwbCsgShadowConservativeTexel(pixelIndex);
    }
#endif
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
    const uint viewPixelIndex = pixelIndex - view.map.z;
    const uint2 pixel = view.map.x > 0u ? uint2(viewPixelIndex % view.map.x, viewPixelIndex / view.map.x) : uint2(0u);
    for(uint record = 0u; record < count; ++record){
        const uint address = (pixelIndex * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL + record) * NWB_LIGHT_SPACE_EVENT_BYTES;
        const uint4 raw = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load4(address);
        const float depth = asfloat(raw.x);
        const uint instanceIndex = raw.w;
        const uint primitive = g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Load(address + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET);
        if(instanceIndex >= g_NwbLightSpacePush.instanceCount || !isfinite(depth) || view.depthRange.w == 0.0){
            nwbLightSpaceInvalidateCrossing(address, instanceIndex, primitive);
            continue;
        }
        const NwbRtInstanceMaterial material = nwbLightSpaceMaterial(instanceIndex);
        const NwbSwBvhInstance instance = nwbLightSpaceInstance(instanceIndex);
        if(primitive >= instance.primitiveCount){
            nwbLightSpaceInvalidateCrossing(address, instanceIndex, primitive);
            continue;
        }
        const uint3 indices = nwbHeapRawBuffer(material.indexSlot).Load3(primitive * NWB_RAYTRACE_TRIANGLE_INDEX_BYTES);
        const float3 p0 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(indices.x * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
        const float3 p1 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(indices.y * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
        const float3 p2 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(indices.z * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
        const float3 geometric = cross(p1 - p0, p2 - p0);
#if NWB_LIGHT_SPACE_CSG_SHADE
        if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT) == 0u){
            const float3 towardLight = (view.light.w & NWB_LIGHT_SPACE_FLAG_POINT) != 0u ? view.originOrDirection.xyz - nwbLightSpaceWorldPosition(p0, instanceIndex) : view.originOrDirection.xyz;
            const float3 objectDirection = nwbSwBvhTransformVector(instance, towardLight);
            if(!all(isfinite(geometric)) || !all(isfinite(objectDirection))){
                nwbLightSpaceInvalidateCrossing(address, instanceIndex, primitive);
                continue;
            }
            const uint facing = dot(geometric, objectDirection) < 0.0 ? NWB_LIGHT_SPACE_EVENT_ENTERING : 0u;
            g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(address, uint4(raw.x, instanceIndex | facing, primitive, 0u));
            g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(address + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET, 0u);
            continue;
        }
#endif
        float3 weights;
        const float4 clip0 = nwbLightSpaceProject(view, nwbLightSpaceWorldPosition(p0, instanceIndex));
        const float4 clip1 = nwbLightSpaceProject(view, nwbLightSpaceWorldPosition(p1, instanceIndex));
        const float4 clip2 = nwbLightSpaceProject(view, nwbLightSpaceWorldPosition(p2, instanceIndex));
        if(!nwbLightSpaceInterpolateBarycentrics(pixel, view.map.xy, clip0, clip1, clip2, weights)){
            nwbLightSpaceInvalidateCrossing(address, instanceIndex, primitive);
            continue;
        }
        const NwbRayTraceVertexAttrib a0 = nwbSwShadowLoadVertexAttrib(material.attributeSlot, primitive * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT);
        const NwbRayTraceVertexAttrib a1 = nwbSwShadowLoadVertexAttrib(material.attributeSlot, primitive * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT + 1u);
        const NwbRayTraceVertexAttrib a2 = nwbSwShadowLoadVertexAttrib(material.attributeSlot, primitive * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT + 2u);
        const float3 local = p0 * weights.x + p1 * weights.y + p2 * weights.z;
        const float3 world = nwbLightSpaceWorldPosition(local, instanceIndex);
        const float3 n0 = float3(a0.normal);
        const float3 n1 = float3(a1.normal);
        const float3 n2 = float3(a2.normal);
        const float3 normal = n0 * weights.x + n1 * weights.y + n2 * weights.z;
        const float spread = max(max(dot(n0 - n1, n0 - n1), dot(n1 - n2, n1 - n2)), dot(n2 - n0, n2 - n0));
        const float3 towardLight = (view.light.w & NWB_LIGHT_SPACE_FLAG_POINT) != 0u ? normalize(view.originOrDirection.xyz - world) : view.originOrDirection.xyz;
        const float3 objectDirection = nwbSwBvhTransformVector(instance, towardLight);
        NwbShadowHit hit;
        hit.meshInstanceIndex = material.meshInstanceIndex;
        hit.surfaceDispatchId = material.surfaceDispatchId;
        hit.materialConstantByteOffset = material.materialConstantByteOffset;
        hit.uv0 = a0.uv0 * weights.x + a1.uv0 * weights.y + a2.uv0 * weights.z;
        hit.worldNormal = nwbSwShadowTransformNormal(instance, normal);
        hit.worldPosition = world;
        hit.rayDirection = towardLight;
        const NwbMeshSurface surface = nwbShadowDispatchSurface(material.surfaceDispatchId, hit);
        const half cosI = nwbShadowCosineToRay(nwbSwBvhTransformNormal(instance, geometric), towardLight);
        const half smoothCosI = nwbShadowCosineToRay(hit.worldNormal, towardLight);
        const half fresnel = nwbShadowFresnelTransmittedFraction(surface.refractionIor, lerp(cosI, smoothCosI, half(smoothstep(NWB_SHADOW_SMOOTH_NORMAL_SPREAD_MIN, NWB_SHADOW_SMOOTH_NORMAL_SPREAD_MAX, spread))));
        const half3 absorption = nwbShadowAbsorptionCoefficient(surface.shadowAbsorptionTint);
        const bool valid = all(isfinite(world)) && isfinite(depth) && all(isfinite(float3(absorption))) && isfinite(float(fresnel));
        // A nonfinite depth invalidates this texel at resolve, with the same software fallback as capture overflow.
        const uint facing = dot(geometric, objectDirection) < 0.0 ? NWB_LIGHT_SPACE_EVENT_ENTERING : 0u;
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store4(address, uint4(valid ? asuint(depth) : NWB_FLOAT_QUIET_NAN_BITS, instanceIndex | facing, primitive, f32tof16(float(absorption.x)) | (f32tof16(float(absorption.y)) << NWB_HALF_BIT_COUNT)));
        g_NwbHeapStorageBuffers[g_NwbLightSpacePush.eventsSlot].Store(address + NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET, f32tof16(float(absorption.z)) | (f32tof16(float(fresnel)) << NWB_HALF_BIT_COUNT));
    }
    nwbLightSpaceSortEvents(pixelIndex, count);
#if NWB_LIGHT_SPACE_CSG_SHADE
    if(nwbCsgShadowEnabled()){
        bool converted;
        if(!nwbCsgShadowConvertEvents(pixelIndex, view, pixel, count, converted))
            nwbCsgShadowConservativeTexel(pixelIndex);
        else if(converted)
            nwbLightSpaceSortEvents(pixelIndex, nwbHeapRawBuffer(g_NwbLightSpacePush.countsSlot).Load(pixelIndex * NWB_LIGHT_SPACE_PIXEL_WORD_BYTES));
    }
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

