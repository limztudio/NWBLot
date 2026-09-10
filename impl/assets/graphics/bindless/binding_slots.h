// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BINDLESS_BINDING_SLOTS_H
#define NWB_GRAPHICS_BINDLESS_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Global heap contract; pipeline-local layouts use push constants only.
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

