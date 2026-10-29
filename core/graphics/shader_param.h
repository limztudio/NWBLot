// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
static constexpr usize s_IndirectInstanceDescByteSize = 64u;
static_assert(sizeof(IndirectInstanceDesc) == s_IndirectInstanceDescByteSize, "IndirectInstanceDesc GPU layout drifted");
static_assert(alignof(IndirectInstanceDesc) >= alignof(Float4), "IndirectInstanceDesc must stay SIMD-aligned");
static constexpr usize s_AlignRemainderZero = 0;
static_assert((offsetof(IndirectInstanceDesc, transform) % alignof(Float4)) == s_AlignRemainderZero, "IndirectInstanceDesc::transform must stay SIMD-aligned");
#endif

struct IndirectArgs{
    u32               clusterCount; // Number of cluster references.
#if defined(__cplusplus)
    u32               clusterReferencesStride = sizeof(GpuVirtualAddress); // Byte stride between references; must be at least 8.
#else
    u32               clusterReferencesStride; // Byte stride between references; must be at least 8.
#endif
    GpuVirtualAddress clusterAddresses; // GPU address of the first constructed CLAS reference.
};
#if defined(__cplusplus)
static_assert(IsStandardLayout_V<IndirectArgs>, "IndirectArgs must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<IndirectArgs>, "IndirectArgs must stay GPU-uploadable");
static constexpr usize s_IndirectArgsByteSize = 16u;
static_assert(sizeof(IndirectArgs) == s_IndirectArgsByteSize, "IndirectArgs GPU layout drifted");
static_assert(alignof(IndirectArgs) == alignof(GpuVirtualAddress), "IndirectArgs GPU alignment drifted");
static constexpr usize s_IndirectArgsClusterCountOffset = 0u;
static_assert(offsetof(IndirectArgs, clusterCount) == s_IndirectArgsClusterCountOffset, "IndirectArgs::clusterCount layout drifted");
static constexpr usize s_IndirectArgsClusterReferencesStrideOffset = 4u;
static_assert(offsetof(IndirectArgs, clusterReferencesStride) == s_IndirectArgsClusterReferencesStrideOffset, "IndirectArgs::clusterReferencesStride layout drifted");
static constexpr usize s_IndirectArgsClusterAddressesOffset = 8u;
static_assert(offsetof(IndirectArgs, clusterAddresses) == s_IndirectArgsClusterAddressesOffset, "IndirectArgs::clusterAddresses layout drifted");
static_assert(IndirectArgs{}.clusterReferencesStride >= sizeof(GpuVirtualAddress), "IndirectArgs default cluster-reference stride is invalid");
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
NWB_CORE_END
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

