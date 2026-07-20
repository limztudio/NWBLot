// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Global descriptor heap round-trip validation kernel (Phase 1, Backend A).
//
// Proves the GpuDescriptorHandle contract end to end: every resource below is reached ONLY through a host-chosen
// runtime slot carried in the params buffer - the kernel hard-codes no resource binding except the fixed bootstrap
// slot of params itself. Reads a storage buffer, a constant buffer, and a typed buffer, then writes their sum to the
// output storage buffer for the host to read back and assert. One invocation runs (group size 1), so all indices are
// dynamically uniform.


#include "bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_BINDLESS_TEST_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    if(dispatchThreadID.x != 0u)
        return;

    // Bootstrap: params lives at the fixed global StorageBuffer slot.
    RWByteAddressBuffer params = g_NwbHeapStorageBuffers[NWB_BINDLESS_TEST_PARAMS_SLOT];
    const uint storageSlot = params.Load(NWB_BINDLESS_TEST_PARAM_STORAGE_SLOT);
    const uint uniformSlot = params.Load(NWB_BINDLESS_TEST_PARAM_UNIFORM_SLOT);
    const uint typedSlot   = params.Load(NWB_BINDLESS_TEST_PARAM_TYPED_SLOT);
    const uint outputSlot  = params.Load(NWB_BINDLESS_TEST_PARAM_OUTPUT_SLOT);

    // Fetch one known value from each resource class through its handle slot.
    const uint storageValue = g_NwbHeapStorageBuffers[storageSlot].Load(0u);
    const uint uniformValue = g_NwbHeapUniformBuffers[uniformSlot].value;
    const uint typedValue   = g_NwbHeapSampledBuffers[typedSlot][0u];

    g_NwbHeapStorageBuffers[outputSlot].Store(0u, storageValue + uniformValue + typedValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
