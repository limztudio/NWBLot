
#pragma once


#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MeshMagic = 0x4D534835u; // MSH5

#pragma pack(push, 1)
struct MeshHeaderBinary{
    u32 magic = s_MeshMagic;
    u32 meshClass = 0;
    u64 positionCount = 0;
    u64 normalCount = 0;
    u64 tangentCount = 0;
    u64 uv0Count = 0;
    u64 colorCount = 0;
    u64 skinCount = 0;
    u64 skeletonJointCount = 0;
    u64 inverseBindMatrixCount = 0;
    u64 meshletCount = 0;
    u64 meshletBoundCount = 0;
    u64 meshletPositionRefDeltaByteCount = 0;
    u64 meshletAttributeRefDeltaByteCount = 0;
    u64 meshletLocalVertexRefCount = 0;
    u64 meshletPrimitiveIndexCount = 0;
};
#pragma pack(pop)
static_assert(sizeof(MeshHeaderBinary) == sizeof(u32) + sizeof(u32) + (sizeof(u64) * 14u), "MeshHeaderBinary layout drifted");
static_assert(alignof(MeshHeaderBinary) == 1u, "MeshHeaderBinary must stay packed");
static_assert(IsStandardLayout_V<MeshHeaderBinary>, "MeshHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<MeshHeaderBinary>, "MeshHeaderBinary must stay binary-serializable");


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

