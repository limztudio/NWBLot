// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cooked-asset ABI shared by the CSG cooker and the runtime registry.
using CsgShapeTypeId = u32;

inline constexpr CsgShapeTypeId s_InvalidCsgShapeTypeId = 0u;
inline constexpr u32 s_CsgShapeHashFoldShiftBits = 32u;


// Canonical built-in cutter names shared by the cooker, the runtime registry, and the CPU deform rebuild, so classification never drifts between duplicated literals.
inline constexpr Name s_CsgPlaneShapeName("engine/csg/plane");
inline constexpr Name s_CsgBoxShapeName("engine/csg/box");
inline constexpr Name s_CsgSphereShapeName("engine/csg/sphere");
inline constexpr Name s_CsgCapsuleShapeName("engine/csg/capsule");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Keep the dispatch ID a deterministic Name-hash projection, not an insertion ordinal.
[[nodiscard]] inline Expected<CsgShapeTypeId> CsgShapeTypeIdFromName(const Name& shapeName){
    if(!shapeName)
        return MakeUnexpected(Failure{});

    // HashValue returns usize and would make the cooked ID host-dependent.
    const NameHash& shapeHash = shapeName.hash();
    u64 foldedHash = shapeHash.qwords[0u];
    for(u32 lane = 1u; lane < NameDetail::s_HashLaneCount; ++lane){
        HashCombineHash(foldedHash, shapeHash.qwords[lane], s_HashCombineGoldenRatio64);
    }

    const CsgShapeTypeId typeId = static_cast<CsgShapeTypeId>(foldedHash ^ (foldedHash >> s_CsgShapeHashFoldShiftBits));
    return typeId != s_InvalidCsgShapeTypeId ? typeId : Limit<CsgShapeTypeId>::s_Max;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

