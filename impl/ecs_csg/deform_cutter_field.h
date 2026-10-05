// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns cutter classification and per-vertex signed distances with the shared epsilon snap, so walls and caps observe identical distances in preview and commit.
namespace CsgDeformShapeKind{
    enum Enum : u8{
        Invalid,
        Plane,
        Box,
        Sphere,
        Capsule,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgDeformCutterField final : NoCopy{
public:
    // SIMD-domain cores: inputs and outputs stay on vector lanes (replicated distance), never touch storage.
    [[nodiscard]] static SIMDVector planeSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static SIMDVector boxSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static SIMDVector sphereSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static SIMDVector capsuleSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static CsgDeformShapeKind::Enum classifyDeformShape(const Name& shapeType);
    [[nodiscard]] static f32 planeSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static f32 boxSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static f32 sphereSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static f32 capsuleSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static bool shapeDistances(
        const CsgDeformShape& shape,
        const CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices,
        const f32 epsilon,
        Vector<f32, Core::Alloc::ScratchArena>& outDistances,
        CsgDeformViabilityReason::Enum& outReason
    );


public:
    CsgDeformCutterField() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

