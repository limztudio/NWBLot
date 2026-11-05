// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_wall_builder.h"

#include "deform_cutter_field.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Keep side is distance >= 0; caller snaps |distance| <= boundaryEpsilon to zero first.
Expected<void, CsgDeformViabilityReason::Enum> CsgDeformWallBuilder::ClipShell(
    Core::Alloc::ScratchArena& scratchArena,
    const CsgDeformShape& shape,
    const f32 boundaryEpsilon,
    const CsgShapeRegistry* shapeRegistry,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& scratchKept,
    Vector<f32, Core::Alloc::ScratchArena>& scratchDistances,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
){
    const auto shapeDistances = CsgDeformCutterField::ShapeDistances(shape, inOutVertices, boundaryEpsilon, shapeRegistry, scratchDistances);
    if(!shapeDistances)
        return MakeUnexpected(shapeDistances.error());

    CsgDeformEdgeSplitMap edgeSplits(0, CsgDeformEdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    const auto clipped = ClipCsgDeformShellWithDistances(scratchDistances, edgeSplits, scratchKept, inOutVertices, inOutTriangles);
    if(!clipped)
        return MakeUnexpected(clipped.error());
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
