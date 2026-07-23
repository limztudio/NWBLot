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


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

