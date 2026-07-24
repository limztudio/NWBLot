// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global descriptor heap - backend-agnostic contract.
//
// A resource registered in the heap is addressed everywhere - C++ and shader - by a single opaque 32-bit
// GpuDescriptorHandle. The handle bit-layout and class taxonomy are identical on every backend, so a handle
// minted on the descriptor-indexing path is byte-for-byte valid on an optional descriptor-heap accelerator. See
// docs/design/bindless-phase1-rhi-heap.md for the original design.


// The resource classes a shader must select between. Each class maps to exactly one register space / descriptor
// type on the descriptor-indexing backend, so the class tag alone disambiguates which shader-side array to index.
namespace GpuDescriptorClass{
    enum Enum : u8{
        SampledImage = 0,   // Texture_SRV           -> SAMPLED_IMAGE
        StorageImage,       // Texture_UAV           -> STORAGE_IMAGE
        SampledBuffer,      // TypedBuffer_SRV       -> UNIFORM_TEXEL_BUFFER
        StorageBuffer,      // StructuredBuffer_UAV  -> STORAGE_BUFFER (structured/raw SRV+UAV share one descriptor)
        UniformBuffer,      // ConstantBuffer        -> UNIFORM_BUFFER
        AccelStruct,        // RayTracingAccelStruct -> ACCELERATION_STRUCTURE_KHR (see note below)
        Sampler,            // Sampler               -> SAMPLER (separate index namespace)

        kCount
    };

    // AccelStruct is part of the stable handle ABI but is unsupported by the descriptor-indexing backend.
    // write() reports it as unsupported rather than faking a handle.
};


// One opaque 32-bit integer: the only thing that crosses the C++/shader boundary.
//   bits 31..28 (4)  : class tag  - GpuDescriptorClass::Enum (<= kCount classes, fits with headroom)
//   bits 27..0 (28)  : slot index - global within its namespace (resource heap or sampler heap), max 2^28
//
// Generation/versioning is intentionally not packed here (index bits are more valuable); a debug-only
// side table validates handles instead. Decoding stays confined to descriptorClass()/slot() so a future widen to
// a 64-bit handle is a localized change.
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


// Capacities are hard ceilings: the heap is not auto-grown mid-frame. Effective caps are clamped to
// the device's update-after-bind limits at initialize() time and logged (no silent truncation). Zero means "use
// the backend default".
struct GpuDescriptorHeapDesc{
    u32 resourceCapacity = 0;   // slots shared by all non-sampler classes (one global namespace, see design 3.4)
    u32 samplerCapacity = 0;    // samplers live in their own namespace on both backends

    constexpr GpuDescriptorHeapDesc& setResourceCapacity(u32 value){ resourceCapacity = value; return *this; }
    constexpr GpuDescriptorHeapDesc& setSamplerCapacity(u32 value){ samplerCapacity = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

