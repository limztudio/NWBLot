// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/ecs_csg/deform_types.adhd.h>
#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns cutter classification and per-vertex distances with the shared epsilon snap; walls and caps agree.
class CsgDeformCutterField final : NoCopy{
public:
    [[nodiscard]] static CsgDeformShapeKind::Enum ClassifyDeformShape(const Name& shapeType)noexcept;
    // SIMD-domain cores: inputs and outputs stay on vector lanes (replicated distance), never touch storage.
    [[nodiscard]] static SIMDVectorSingle PlaneSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept;
    [[nodiscard]] static SIMDVectorSingle BoxSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept;
    [[nodiscard]] static SIMDVectorSingle SphereSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept;
    [[nodiscard]] static SIMDVectorSingle CapsuleSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept;
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> ShapeDistances(const CsgDeformShape& shape, Span<const CsgDeformVertex> vertices, const SIMDVectorSingle epsilon, Vector<f32, Core::Alloc::ScratchArena>& outDistances);


public:
    CsgDeformCutterField() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
