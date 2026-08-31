// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BINDLESS_BINDING_SLOTS_H
#define NWB_GRAPHICS_BINDLESS_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Global descriptor heap - shader-side binding contract.
//
// These numbers ARE the contract between the shader and the host. They must match, exactly:
//   - the set indices in GpuDescriptorHeapAbi (resourceSetIndex = 0, samplerSetIndex = 1), and
//   - the per-class register-space binding numbers GpuDescriptorHeap::getRegisterSlot() adds to each table.
// Pipeline-local BindingLayout objects carry push constants only and therefore consume no descriptor sets. The global
// heap can occupy the lowest contiguous sets without colliding with local pipeline state.
// createBindlessLayout() sets binding.binding = item.slot directly (no classic 128/256/384 offset), so the resource
// table is one set carrying nine flat bindings, one per non-sampler class; the sampler table is a second set.
// A third, fixed one-descriptor TLAS set is deliberately separate from the resource array because acceleration
// structures are encoded directly into per-generation descriptor-buffer blocks through vkGetDescriptorEXT.
#define NWB_BINDLESS_HEAP_RESOURCE_SET 0
#define NWB_BINDLESS_HEAP_SAMPLER_SET  1
#define NWB_BINDLESS_HEAP_ACCEL_STRUCT_SET 2

#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE  0   // Texture2D              (GpuDescriptorClass::SampledImage)
#define NWB_BINDLESS_HEAP_BINDING_STORAGE_IMAGE  1   // RWTexture2D            (GpuDescriptorClass::StorageImage)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_BUFFER 2   // Buffer                 (GpuDescriptorClass::SampledBuffer)
#define NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER 3   // RWByteAddressBuffer    (GpuDescriptorClass::StorageBuffer)
#define NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER 4   // ConstantBuffer         (GpuDescriptorClass::UniformBuffer)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY 5 // Texture2DArray      (GpuDescriptorClass::SampledImage2DArray)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_3D 6 // Texture3D             (GpuDescriptorClass::SampledImage3D)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY_UINT 7 // Texture2DArray<uint> (GpuDescriptorClass::SampledImage2DArrayUint)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_CUBE 8 // TextureCube         (GpuDescriptorClass::SampledImageCube)
#define NWB_BINDLESS_HEAP_BINDING_SAMPLER        0   // SamplerState (set 1)   (GpuDescriptorClass::Sampler)
#define NWB_BINDLESS_HEAP_BINDING_ACCEL_STRUCT   0   // RaytracingAccelerationStructure (set 2)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

