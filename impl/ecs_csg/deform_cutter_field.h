// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <impl/ecs_csg/deform_types.adhd.h>
#include <impl/ecs_csg/shape_registry.h>
#include <core/alloc/scratch.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns cutter dispatch and per-vertex distances with the shared epsilon snap; walls and caps agree.
class CsgDeformCutterField final : NoCopy{
public:
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> ShapeDistances(
        const CsgDeformShape& shape,
        Span<const CsgDeformVertex> vertices,
        SIMDVectorSingle epsilon,
        const CsgShapeRegistry* shapeRegistry,
        Vector<f32, Core::Alloc::ScratchArena>& outDistances
    );
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> ShapeDistances(
        const CsgDeformShape& shape,
        const Span<const CsgDeformVertex> vertices,
        const SIMDVectorSingle epsilon,
        Vector<f32, Core::Alloc::ScratchArena>& outDistances
    ){
        return ShapeDistances(shape, vertices, epsilon, nullptr, outDistances);
    }


public:
    CsgDeformCutterField() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


