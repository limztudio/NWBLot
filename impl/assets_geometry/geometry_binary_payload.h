// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GeometryBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_GeometryMagic = 0x47454F32u; // GEO2

#pragma pack(push, 1)
struct GeometryHeaderBinary{
    u32 magic = s_GeometryMagic;
    u32 geometryClass = 0;
    u64 positionCount = 0;
    u64 normalCount = 0;
    u64 tangentCount = 0;
    u64 uv0Count = 0;
    u64 colorCount = 0;
    u64 skinCount = 0;
    u64 skeletonJointCount = 0;
    u64 inverseBindMatrixCount = 0;
    u64 vertexRefCount = 0;
    u64 meshletCount = 0;
    u64 meshletBoundCount = 0;
    u64 meshletVertexRefCount = 0;
    u64 meshletPrimitiveIndexCount = 0;
};
#pragma pack(pop)
static_assert(sizeof(GeometryHeaderBinary) == sizeof(u32) + sizeof(u32) + (sizeof(u64) * 13u), "GeometryHeaderBinary layout drifted");
static_assert(alignof(GeometryHeaderBinary) == 1u, "GeometryHeaderBinary must stay packed");
static_assert(IsStandardLayout_V<GeometryHeaderBinary>, "GeometryHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<GeometryHeaderBinary>, "GeometryHeaderBinary must stay binary-serializable");


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

