// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets/graphics/shadow/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU mirror of trace material ABI; HW and SW share instance IDs.
struct NwbRtInstanceMaterialGpu{
    u32 shadowTransmittanceModelId = Limit<u32>::s_Max;
    u32 flags = 0u;
    u32 shadingModelId = 0u;
    u32 materialConstantByteOffset = 0u;
    u32 meshInstanceIndex = 0u;
    // Global-heap geometry slots; nodeSlot is software-only.
    u32 indexSlot = Limit<u32>::s_Max;
    u32 attributeSlot = Limit<u32>::s_Max;
    u32 positionSlot = Limit<u32>::s_Max;
    u32 nodeSlot = Limit<u32>::s_Max;
};
static constexpr usize s_NwbRtInstanceMaterialGpuByteSize = 36u;
static_assert(sizeof(NwbRtInstanceMaterialGpu) == s_NwbRtInstanceMaterialGpuByteSize, "NwbRtInstanceMaterialGpu must match the shader NwbRtInstanceMaterial std430 layout (9 x uint)");
static_assert(offsetof(NwbRtInstanceMaterialGpu, shadingModelId) == sizeof(u32) * 2u);

// Shader-mirrored flags: transparent selects transmittance; refractive selects caustics.
namespace RtInstanceMaterialFlag{
    static constexpr auto kRtInstanceMaterialFlagNoneBase = 0u;
    enum Mask : u32{
        None = kRtInstanceMaterialFlagNoneBase,
        Transparent = NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT,
        Refractive = NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

