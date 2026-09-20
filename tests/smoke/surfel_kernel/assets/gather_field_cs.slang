// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gi/surfel/surfel_heap_resources.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Use the real live and previous-frame snapshot contracts with immutable synthetic fields.
#define NWB_SURFEL_GATHER_FIELD_POOL g_NwbHeapSurfelReadOnlyPools[g_NwbSurfelHeapPushConstants.poolHeapSlot]
#define NWB_SURFEL_GATHER_FIELD_CELL_HEAD g_NwbHeapSurfelReadOnlyUintBuffers[g_NwbSurfelHeapPushConstants.cellHeadHeapSlot]
#define NWB_SURFEL_GATHER_FIELD_CONSTANTS g_NwbHeapSurfelConstantBuffers[g_NwbSurfelHeapPushConstants.constantsHeapSlot]
#if NWB_SURFEL_TEST_BOUNDED
#define NWB_SURFEL_GATHER_FIELD_INDEX_VALID(id, poolCapacity) ((id) < (poolCapacity))
#else
#define NWB_SURFEL_GATHER_FIELD_INDEX_VALID(id, poolCapacity) ((id) != NWB_SURFEL_CELL_INVALID)
#endif

#if NWB_SURFEL_TEST_REFERENCE
#include "../reference/surfel_gather_field.slangi"
#else
#include "gi/surfel/surfel_gather_field.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID){
    // Only this test wrapper interprets unused gather selectors as query count and output-record offset.
    if(id.x >= g_NwbSurfelHeapPushConstants.halfIrradianceSlot)
        return;
    const ByteAddressBuffer queries = NwbHeapRawBuffer(g_NwbSurfelHeapPushConstants.counterHeapSlot);
    const float3 position = asfloat(queries.Load3(id.x * 32u));
    const float3 normal = asfloat(queries.Load3(id.x * 32u + 16u));
    const uint capacity = queries.Load(id.x * 32u + 28u);
    half3 irradiance;
    const bool covered = nwbSurfelGatherField(position, normal, capacity, irradiance);
    const uint record = g_NwbSurfelHeapPushConstants.freeListHeapSlot + id.x;
    g_NwbHeapStorageBuffers[g_NwbSurfelHeapPushConstants.outputStorageHeapSlot].Store4(
        record * 16u, uint4(covered ? 1u : 0u, f32tof16(float3(irradiance)))
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

