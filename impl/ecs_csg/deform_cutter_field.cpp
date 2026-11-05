// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cutter_field.h"

#include <impl/ecs_csg/deform_wall_clip.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, CsgDeformViabilityReason::Enum> CsgDeformCutterField::ShapeDistances(
    const CsgDeformShape& shape,
    const Span<const CsgDeformVertex> vertices,
    const f32 boundaryEpsilon,
    const CsgShapeRegistry* shapeRegistry,
    Vector<f32, Core::Alloc::ScratchArena>& outDistances
){
    u32 registryParameterByteSize = 0u;
    Optional<InplaceFunction<0u, Expected<f32>(const SIMDVectorSingle, const CsgShapeInlineParameters&)>> registryDistanceCallback;
    if(shapeRegistry){
        const auto shapeType = shapeRegistry->findShapeType(shape.shapeType);
        if(shapeType && shapeType->desc.signedDistanceCallback){
            registryParameterByteSize = shapeType->desc.parameterByteSize;
            registryDistanceCallback = shapeType->desc.signedDistanceCallback;
        }
    }
    const auto computed = ComputeCsgDeformShapeDistances(shape, vertices, boundaryEpsilon, registryParameterByteSize, registryDistanceCallback, outDistances);
    if(!computed)
        return MakeUnexpected(computed.error());
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
