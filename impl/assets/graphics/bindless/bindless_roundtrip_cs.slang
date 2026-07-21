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
#include "bindless_heap_bvh.slangi"   // Phase 2 P3: g_NwbHeapBvhNodeBuffers + NwbBvhNode for the structured-alias cross-check


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
    const uint nodeSlot    = params.Load(NWB_BINDLESS_TEST_PARAM_NODE_SLOT);

    // Fetch one known value from each resource class through its handle slot.
    const uint storageValue = g_NwbHeapStorageBuffers[storageSlot].Load(0u);
    const uint uniformValue = g_NwbHeapUniformBuffers[uniformSlot].value;
    const uint typedValue   = g_NwbHeapSampledBuffers[typedSlot][0u];

    uint sum = storageValue + uniformValue + typedValue;

    // Phase 2 P3: cross-check the read-only aliased views over the StorageBuffer class. All indices here are dynamically
    // uniform (one invocation), so what is under test is the aliasing/stride codegen, not non-uniform indexing (P4).
    //  - the read-only raw alias must read the same storage word as the RW view above, and
    //  - the structured alias must decode the NwbBvhNode the host planted (leftChild at byte 12, rightChild at byte 28).
    // Any miss poisons the sum, so the host's expected-sum assert is the single detector for both.
    const uint storageValueRaw = g_NwbHeapRawBuffers[storageSlot].Load(0u);
    const NwbBvhNode node = g_NwbHeapBvhNodeBuffers[nodeSlot][0u];
    const bool aliasesOk =
        (storageValueRaw == storageValue)
        && (node.leftChild  == NWB_BINDLESS_TEST_NODE_LEFTCHILD)
        && (node.rightChild == NWB_BINDLESS_TEST_NODE_RIGHTCHILD);
    if(!aliasesOk)
        sum = ~sum;

    g_NwbHeapStorageBuffers[outputSlot].Store(0u, sum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
