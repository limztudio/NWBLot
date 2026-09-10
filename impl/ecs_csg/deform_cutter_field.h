// limztudio@gmail.com
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Cutter field evaluation for deformable CSG rebuilds.
//
// Owns cutter classification and per-vertex signed distances (plane/box/
// sphere/capsule, mirroring the GPU SDF evals) with the shared epsilon snap,
// so walls and caps observe identical distances in preview and commit.
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
    CsgDeformCutterField() = delete;


public:
    [[nodiscard]] static CsgDeformShapeKind::Enum ClassifyDeformShape(const Name& shapeType);
    [[nodiscard]] static f32 PlaneSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static f32 BoxSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static f32 SphereSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static f32 CapsuleSignedDistance(SIMDVector shapePosition, SIMDVector parameter0);
    [[nodiscard]] static bool ShapeDistances(
        const CsgDeformShape& shape,
        const CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices,
        const f32 epsilon,
        Vector<f32, Core::Alloc::ScratchArena>& outDistances,
        CsgDeformViabilityReason::Enum& outReason
    );
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

