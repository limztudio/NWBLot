// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global descriptor-heap contract.
//
// A resource registered in the heap is addressed everywhere - C++ and shader - by a single opaque 32-bit
// GpuDescriptorHandle. The handle bit-layout and class taxonomy are shared by C++ and shaders; the Vulkan renderer
// resolves every handle through its required descriptor-buffer-backed global heap.


// The resource classes a shader must select between. Each class maps to exactly one global-heap register space /
// descriptor type, so the class tag alone disambiguates which shader-side array to index.
namespace GpuDescriptorClass{
    enum Enum : u8{
        SampledImage = 0,   // Texture_SRV           -> SAMPLED_IMAGE
        StorageImage,       // Texture_UAV           -> STORAGE_IMAGE
        SampledBuffer,      // TypedBuffer_SRV       -> UNIFORM_TEXEL_BUFFER
        StorageBuffer,      // StructuredBuffer_UAV  -> STORAGE_BUFFER (structured/raw SRV+UAV share one descriptor)
        UniformBuffer,      // ConstantBuffer        -> UNIFORM_BUFFER
        AccelStruct,        // RayTracingAccelStruct -> ACCELERATION_STRUCTURE_KHR (see note below)
        Sampler,            // Sampler               -> SAMPLER (separate index namespace)
        // Keep this appended so the class tags above remain stable across the C++/shader handle ABI. It is a
        // distinct descriptor array because Texture2D and Texture2DArray have different shader types even though
        // both write VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE descriptors.
        SampledImage2DArray,// Texture2DArray_SRV     -> SAMPLED_IMAGE
        // Keep appended for the same stable handle ABI reason. Texture3D requires its own shader-side array even
        // though Vulkan encodes it with the same sampled-image descriptor type.
        SampledImage3D,     // Texture3D_SRV          -> SAMPLED_IMAGE
        // Keep appended to preserve the stable tags above. A uint Texture2DArray has a distinct shader image type
        // from the floating-point Texture2DArray table, so it must occupy its own descriptor-array binding.
        SampledImage2DArrayUint, // Texture2DArray<uint>_SRV -> SAMPLED_IMAGE
        // Keep appended to preserve the stable handle ABI above. Cubemaps require a cube image view and therefore
        // cannot share the Texture2D sampled-image declaration.
        SampledImageCube,        // TextureCube_SRV -> SAMPLED_IMAGE

        kCount
    };

    // AccelStruct selects a distinct immutable descriptor-buffer block per live TLAS generation.
};


// One opaque 32-bit integer: the only thing that crosses the C++/shader boundary.
//   bits 31..28 (4)  : class tag  - GpuDescriptorClass::Enum (<= kCount classes, fits with headroom)
//   bits 27..0 (28)  : slot index - global within its namespace (resource heap or sampler heap), max 2^28
//
// Generation/versioning is intentionally not packed here (index bits are more valuable); a debug-only
// side table validates handles instead.
struct GpuDescriptorHandle{
    static constexpr u32 s_Invalid = 0xFFFFFFFFu;
    static constexpr u32 s_ClassShift = 28u;
    static constexpr u32 s_SlotMask = (1u << s_ClassShift) - 1u; // 0x0FFFFFFF, max slot index per namespace

    u32 value = s_Invalid;

    constexpr GpuDescriptorHandle() = default;
    constexpr explicit GpuDescriptorHandle(u32 raw) : value(raw){}

    static constexpr GpuDescriptorHandle make(GpuDescriptorClass::Enum cls, u32 slot){
        return GpuDescriptorHandle((static_cast<u32>(cls) << s_ClassShift) | (slot & s_SlotMask));
    }
    static constexpr GpuDescriptorHandle invalid(){ return GpuDescriptorHandle(s_Invalid); }

    [[nodiscard]] constexpr bool valid()const{ return value != s_Invalid; }
    [[nodiscard]] constexpr GpuDescriptorClass::Enum descriptorClass()const{ return static_cast<GpuDescriptorClass::Enum>(value >> s_ClassShift); }
    [[nodiscard]] constexpr u32 slot()const{ return value & s_SlotMask; }
};
inline constexpr bool operator==(const GpuDescriptorHandle lhs, const GpuDescriptorHandle rhs){ return lhs.value == rhs.value; }
inline constexpr bool operator!=(const GpuDescriptorHandle lhs, const GpuDescriptorHandle rhs){ return lhs.value != rhs.value; }
static_assert(sizeof(GpuDescriptorHandle) == 4, "GpuDescriptorHandle is supposed to be a single 32-bit word");


// Project/bootstrap-owned values that connect the renderer's generic descriptor heap to the shared shader ABI.
// Core carries and validates this typed payload without depending on the implementation-owned macro contract that
// supplies it. The Vulkan backend performs its stricter descriptor-layout range and ordering checks at initialize().
struct GpuDescriptorHeapAbi{
    static constexpr u32 s_Unspecified = s_MaxU32;

    u32 resourceSetIndex = s_Unspecified;
    u32 samplerSetIndex = s_Unspecified;
    u32 accelStructSetIndex = s_Unspecified;
    u32 sampledImageBinding = s_Unspecified;
    u32 storageImageBinding = s_Unspecified;
    u32 sampledBufferBinding = s_Unspecified;
    u32 storageBufferBinding = s_Unspecified;
    u32 uniformBufferBinding = s_Unspecified;
    u32 sampledImage2DArrayBinding = s_Unspecified;
    u32 sampledImage3DBinding = s_Unspecified;
    u32 sampledImage2DArrayUintBinding = s_Unspecified;
    u32 sampledImageCubeBinding = s_Unspecified;
    u32 samplerBinding = s_Unspecified;
    u32 accelStructBinding = s_Unspecified;

    [[nodiscard]] constexpr bool valid()const{
        return resourceSetIndex != s_Unspecified
            && samplerSetIndex != s_Unspecified
            && accelStructSetIndex != s_Unspecified
            && sampledImageBinding != s_Unspecified
            && storageImageBinding != s_Unspecified
            && sampledBufferBinding != s_Unspecified
            && storageBufferBinding != s_Unspecified
            && uniformBufferBinding != s_Unspecified
            && sampledImage2DArrayBinding != s_Unspecified
            && sampledImage3DBinding != s_Unspecified
            && sampledImage2DArrayUintBinding != s_Unspecified
            && sampledImageCubeBinding != s_Unspecified
            && samplerBinding != s_Unspecified
            && accelStructBinding != s_Unspecified
        ;
    }
};


// Capacities are hard ceilings: the heap is not auto-grown mid-frame. Effective caps are clamped to the device's
// descriptor-layout limits at initialize() time and logged (no silent truncation). Zero means "use the renderer
// default".
struct GpuDescriptorHeapDesc{
    u32 resourceCapacity = 0;   // slots shared by all non-sampler classes (one global namespace)
    u32 samplerCapacity = 0;    // samplers live in their own global namespace
    GpuDescriptorHeapAbi bindlessHeapAbi;

    constexpr GpuDescriptorHeapDesc& setResourceCapacity(u32 value){ resourceCapacity = value; return *this; }
    constexpr GpuDescriptorHeapDesc& setSamplerCapacity(u32 value){ samplerCapacity = value; return *this; }
    constexpr GpuDescriptorHeapDesc& setBindlessHeapAbi(const GpuDescriptorHeapAbi& value){ bindlessHeapAbi = value; return *this; }
};


// Coherent-by-value lifecycle snapshot for the descriptor-buffer global heap. The Vulkan backend samples every
// field while holding its heap mutex. Resource slots are one shared namespace for every non-sampler, non-TLAS
// descriptor class; TLAS slots select separate immutable per-generation blocks.
struct GpuDescriptorHeapLifecycleStatistics{
    // False after shutdown or before initialization. The remaining counters are ordinarily zero in that state.
    bool initialized = false;
    u32 resourceCapacity = 0u;
    u32 samplerCapacity = 0u;
    u32 accelStructCapacity = 0u;
    // A live slot is allocated and has not yet entered deferred-free quarantine.
    usize resourceLiveSlotCount = 0u;
    usize samplerLiveSlotCount = 0u;
    usize accelStructLiveSlotCount = 0u;
    // Aggregate slots awaiting reuse because a pending CPU snapshot may still record them or a prior heap binding
    // may still need their descriptor/resource.
    usize pendingRetiredSlotCount = 0u;
    // Heap uses with an accepted submission token, retained until collectRetired() reaps their token.
    // This can include work that has physically completed but has not yet been collected.
    usize acceptedHeapUseCount = 0u;
    // Heap uses still owned by a recorded command buffer without an accepted submission token. Rejected or
    // abandoned command buffers stop contributing here before collectRetired() releases their retired slots.
    usize unsubmittedHeapUseCount = 0u;
    // Heap uses whose command buffer was discarded without an accepted token. They have no trustworthy physical
    // queue identity and remain visible only until collectRetired() releases the associated deferred-free slots.
    usize abandonedHeapUseCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

