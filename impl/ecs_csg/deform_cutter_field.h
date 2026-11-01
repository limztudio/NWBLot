// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <impl/ecs_csg/deform_types.adhd.h>
#include <core/alloc/scratch.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns cutter classification and per-vertex distances with the shared epsilon snap; walls and caps agree.
class CsgDeformCutterField final : NoCopy{
public:
    // SIMD-domain cores: inputs and outputs stay on vector lanes (replicated distance), never touch storage.
    [[nodiscard]] static SIMDVectorSingle PlaneSignedDistanceVec(const SIMDVectorSingle shapePosition, const SIMDVectorSingle parameter0)noexcept{
        return VectorAdd(Vector3Dot(shapePosition, parameter0), VectorSplatW(parameter0));
    }
    [[nodiscard]] static SIMDVectorSingle BoxSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept;
    [[nodiscard]] static SIMDVectorSingle SphereSignedDistanceVec(const SIMDVectorSingle shapePosition, const SIMDVectorSingle parameter0)noexcept{
        return VectorSubtract(Vector3Length(shapePosition), VectorSplatX(parameter0));
    }
    [[nodiscard]] static SIMDVectorSingle CapsuleSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept;
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> ShapeDistances(const CsgDeformShape& shape, Span<const CsgDeformVertex> vertices, const SIMDVectorSingle epsilon, Vector<f32, Core::Alloc::ScratchArena>& outDistances);


public:
    CsgDeformCutterField() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

