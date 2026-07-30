// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_CORE_GRAPHICS_SHADER_PARAM_H
#define NWB_CORE_GRAPHICS_SHADER_PARAM_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/global.h>

#if defined(__cplusplus)
#include <cstddef>
#else
#include <core/common/module.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(__cplusplus)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
NWB_CORE_BEGIN
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef u64 GpuVirtualAddress;
struct GpuVirtualAddressAndStride{
    GpuVirtualAddress startAddress;
    u64 strideInBytes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct IndirectInstanceDesc{
#if !defined(__cplusplus)
    float4 transform[3];
#else
    Float4 transform[3] = {};
#endif
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    u32 flags : 8;
    GpuVirtualAddress blasDeviceAddress;
};
#if defined(__cplusplus)
static_assert(IsStandardLayout_V<IndirectInstanceDesc>, "IndirectInstanceDesc must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<IndirectInstanceDesc>, "IndirectInstanceDesc must stay GPU-uploadable");
static_assert(sizeof(IndirectInstanceDesc) == 64u, "IndirectInstanceDesc GPU layout drifted");
static_assert(alignof(IndirectInstanceDesc) >= alignof(Float4), "IndirectInstanceDesc must stay SIMD-aligned");
static_assert((offsetof(IndirectInstanceDesc, transform) % alignof(Float4)) == 0, "IndirectInstanceDesc::transform must stay SIMD-aligned");
#endif

struct IndirectArgs{
    u32                       clusterCount;     // Number of cluster addresses.
    u32                       reserved;         // Reserved, must be 0
    GpuVirtualAddress         clusterAddresses; // GPU address array of constructed CLAS objects.
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
NWB_CORE_END
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

