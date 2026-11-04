// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cutter_field.h"

#include <global/simd/vector.h>


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
    CsgShapeInlineParameters inlineParameters = MakeCsgShapeInlineParameters(LoadFloat(shape.parameter0), LoadFloat(shape.parameter1));
    CsgShapeSignedDistanceCallback registryDistanceCallback = nullptr;
    CsgBuiltInShapeKind::Enum builtInKind = CsgBuiltInShapeKind::Plane;

    if(shapeRegistry){
        const auto shapeType = shapeRegistry->findShapeType(shape.shapeType);
        if(shapeType && shapeType->desc.signedDistanceCallback){
            const auto packedParameterBytes = PackCsgShapeDefaultParameterBytes(shapeType->desc.parameterByteSize, inlineParameters);
            if(!packedParameterBytes)
                return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
            inlineParameters = ReadCsgShapeInlineParameters(Span<const u8>(packedParameterBytes->data(), packedParameterBytes->size()));
            registryDistanceCallback = shapeType->desc.signedDistanceCallback;
        }
    }

    if(!registryDistanceCallback){
        const auto resolvedBuiltInKind = TryResolveBuiltInCsgShapeKind(shape.shapeType);
        if(!resolvedBuiltInKind)
            return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
        builtInKind = *resolvedBuiltInKind;
    }

    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, s_KeepDistanceZero);

    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices[vertexIndex];
        const SIMDVectorSingle shapePosition = TransformCsgDeformVertexPosition(vertex, worldToShape);

        f32 distance = 0.0f;
        if(registryDistanceCallback){
            const auto evaluated = registryDistanceCallback(shapePosition, inlineParameters);
            if(!evaluated)
                return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
            distance = *evaluated;
        }
        else
            distance = EvaluateBuiltInCsgShapeSignedDistance(builtInKind, shapePosition, inlineParameters);

        const auto snapped = TrySnapCsgDeformSignedDistance(distance, boundaryEpsilon);
        if(!snapped)
            return MakeUnexpected(snapped.error());
        outDistances[vertexIndex] = *snapped;
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

