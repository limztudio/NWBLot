// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This is cooked-asset ABI shared by the CSG cooker and the runtime registry.  It intentionally lives with the
// authored CSG asset schema, rather than either implementation, so neither side owns the other.
using CsgShapeTypeId = u32;

inline constexpr CsgShapeTypeId s_InvalidCsgShapeTypeId = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CSG evaluator modules cook independently from runtime registration. Keep the GPU dispatch ID a deterministic
// projection of the Name hash, not an insertion ordinal. Cooker and runtime both reject 32-bit collisions,
// so this is a stable ABI.
[[nodiscard]] inline CsgShapeTypeId CsgShapeTypeIdFromName(const Name& shapeName){
    if(!shapeName)
        return s_InvalidCsgShapeTypeId;

    // NameHash lanes are part of the Name ABI; HashValue returns usize and would make the cooked ID host-dependent.
    const NameHash& shapeHash = shapeName.hash();
    u64 foldedHash = shapeHash.qwords[0u];
    for(u32 lane = 1u; lane < NameDetail::s_HashLaneCount; ++lane){
        foldedHash ^= shapeHash.qwords[lane]
            + 0x9e3779b97f4a7c15ull
            + (foldedHash << 6u)
            + (foldedHash >> 2u)
        ;
    }

    const CsgShapeTypeId typeId = static_cast<CsgShapeTypeId>(foldedHash ^ (foldedHash >> 32u));
    return typeId != s_InvalidCsgShapeTypeId ? typeId : Limit<CsgShapeTypeId>::s_Max;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

