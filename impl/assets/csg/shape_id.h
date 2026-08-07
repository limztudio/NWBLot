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


// CSG evaluator modules are cooked independently from runtime registration.  Keep their compact GPU dispatch ID a
// deterministic projection of the canonical Name hash rather than an insertion-order ordinal.  The cooker and runtime
// both reject the extremely unlikely 32-bit collision, so this is a stable ABI rather than a best-effort hash lookup.
[[nodiscard]] inline CsgShapeTypeId CsgShapeTypeIdFromName(const Name& shapeName){
    if(!shapeName)
        return s_InvalidCsgShapeTypeId;

    // NameHash deliberately exposes its fixed, canonical lanes as part of the Name ABI.  Do not use HashValue here:
    // it returns usize and would make the cooked GPU ID depend on host word size.
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

