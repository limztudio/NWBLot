// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BINDLESS_BINDING_SLOTS_H
#define NWB_GRAPHICS_BINDLESS_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Global descriptor heap - shader-side binding contract (Phase 1, Backend A = descriptor indexing).
//
// These numbers ARE the contract between the shader and the host. They must match, exactly:
//   - the set indices GpuDescriptorHeap binds at (m_resourceSetIndex = 8, m_samplerSetIndex = 9; Phase 2 reserved the
//     high sets so the heap never collides with a migrated pipeline's own low sets - the pipeline layout gap-fills
//     sets 0..7 with an empty descriptor set layout), and
//   - the per-class register-space binding numbers GpuDescriptorHeap::getRegisterSlot() adds to each table.
// createBindlessLayout() sets binding.binding = item.slot directly (no classic 128/256/384 offset), so the resource
// table is one set carrying five flat bindings, one per non-sampler class; the sampler table is a second set.
#define NWB_BINDLESS_HEAP_RESOURCE_SET 8
#define NWB_BINDLESS_HEAP_SAMPLER_SET  9

#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE  0   // Texture2D              (GpuDescriptorClass::SampledImage)
#define NWB_BINDLESS_HEAP_BINDING_STORAGE_IMAGE  1   // RWTexture2D            (GpuDescriptorClass::StorageImage)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_BUFFER 2   // Buffer                 (GpuDescriptorClass::SampledBuffer)
#define NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER 3   // RWByteAddressBuffer    (GpuDescriptorClass::StorageBuffer)
#define NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER 4   // ConstantBuffer         (GpuDescriptorClass::UniformBuffer)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLER        0   // SamplerState (set 9)   (GpuDescriptorClass::Sampler)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Round-trip self-test contract (see impl/ecs_render/raytrace/gpu_descriptor_heap_selftest.cpp).
//
// One invocation runs, so every descriptor-array index is dynamically uniform - no NonUniformResourceIndex is needed
// and no per-type non-uniform-indexing device feature is required. The kernel bootstraps from a params raw-buffer at
// a fixed global slot, then reads a storage buffer / CBV / typed buffer at HOST-CHOSEN runtime slots and writes their
// sum to the output slot. The free-half/realloc pass simply rewrites params with different slots, so the runtime-slot
// path (the whole point of a handle) is what gets re-proven.
//
// Phase 2 P3 additionally cross-checks the two read-only aliased views over the StorageBuffer class: the kernel re-reads
// the storage slot through the read-only raw alias (must equal the RW-view read) and reads one host-planted NwbBvhNode
// through the StructuredBuffer<NwbBvhNode> alias at the node slot (must equal the sentinels below). This proves the
// aliased-binding codegen - the genuinely novel P3 risk - decodes correctly on the target; the indices stay uniform so
// the check is orthogonal to P4's non-uniform-indexing feature. A miss poisons the sum, so the existing host assert on
// the expected sum is the single failure detector for both the round-trip and the alias check.
#define NWB_BINDLESS_TEST_GROUP_SIZE 1

// The params raw-buffer lives at this fixed global resource slot. The host allocates it FIRST from the freshly
// initialized heap (fresh allocation bumps from 0) and asserts handle.slot() == this, so the kernel can find it
// without a second indirection.
#define NWB_BINDLESS_TEST_PARAMS_SLOT 0

// Byte offsets of the fields the host packs into the params raw-buffer (RWByteAddressBuffer, 4 bytes each).
#define NWB_BINDLESS_TEST_PARAM_STORAGE_SLOT 0    // global slot of a StorageBuffer holding a known uint
#define NWB_BINDLESS_TEST_PARAM_UNIFORM_SLOT 4    // global slot of a UniformBuffer (CBV) holding a known uint
#define NWB_BINDLESS_TEST_PARAM_TYPED_SLOT   8    // global slot of a SampledBuffer (typed R32_UINT) holding a known uint
#define NWB_BINDLESS_TEST_PARAM_OUTPUT_SLOT  12   // global slot of the StorageBuffer the kernel writes the sum to
#define NWB_BINDLESS_TEST_PARAM_NODE_SLOT    16   // global slot of a StorageBuffer holding one known NwbBvhNode (P3 structured-alias check)
#define NWB_BINDLESS_TEST_PARAMS_BYTES       20

// Sentinel integer fields of the NwbBvhNode the host plants at the node slot; the kernel reads them back through the
// StructuredBuffer<NwbBvhNode> alias and asserts both. Distinct non-zero values (and leftChild carrying NO leaf flag,
// so it is a plain child index) catch a swapped/misaligned/mis-strided structured decode of the shared descriptor.
#define NWB_BINDLESS_TEST_NODE_LEFTCHILD  0x5A5A0001u
#define NWB_BINDLESS_TEST_NODE_RIGHTCHILD 0x00027A7Au


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

