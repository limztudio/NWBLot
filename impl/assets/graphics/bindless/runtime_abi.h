// limztudio@gmail.com


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <impl/assets/graphics/bindless/binding_slots.h>


NWB_IMPL_BEGIN


namespace AssetsGraphicsBindless{


// Converts the shared macro-only CPU/GPU ABI into the typed payload consumed by Core::Graphics. This adapter stays
// on the implementation/project side so the renderer never depends directly on asset-owned shader constants.
[[nodiscard]] inline Core::GpuDescriptorHeapAbi MakeGpuDescriptorHeapAbi(){
    Core::GpuDescriptorHeapAbi abi;
    abi.resourceSetIndex = NWB_BINDLESS_HEAP_RESOURCE_SET;
    abi.samplerSetIndex = NWB_BINDLESS_HEAP_SAMPLER_SET;
    abi.accelStructSetIndex = NWB_BINDLESS_HEAP_ACCEL_STRUCT_SET;
    abi.sampledImageBinding = NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE;
    abi.storageImageBinding = NWB_BINDLESS_HEAP_BINDING_STORAGE_IMAGE;
    abi.sampledBufferBinding = NWB_BINDLESS_HEAP_BINDING_SAMPLED_BUFFER;
    abi.storageBufferBinding = NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER;
    abi.uniformBufferBinding = NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER;
    abi.sampledImage2DArrayBinding = NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY;
    abi.sampledImage3DBinding = NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_3D;
    abi.sampledImage2DArrayUintBinding = NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY_UINT;
    abi.samplerBinding = NWB_BINDLESS_HEAP_BINDING_SAMPLER;
    abi.accelStructBinding = NWB_BINDLESS_HEAP_BINDING_ACCEL_STRUCT;
    return abi;
}


};


NWB_IMPL_END


