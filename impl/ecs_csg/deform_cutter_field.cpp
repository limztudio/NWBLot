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
    CsgShapeInlineParameters inlineParameters = MakeCsgShapeInlineParameters(shape.parameter0, shape.parameter1);
    CsgShapeSignedDistanceCallback registryDistanceCallback = nullptr;
    CsgBuiltInShapeKind::Enum builtInKind = CsgBuiltInShapeKind::Plane;

    if(shapeRegistry){
        const auto shapeType = shapeRegistry->findShapeType(shape.shapeType);
        if(shapeType && shapeType->desc.signedDistanceCallback){
            FixedVector<u8, s_CsgShapeInlineParameterMaxBytes> packedParameterBytes;
            if(!PackCsgShapeDefaultParameterBytes(shapeType->desc.parameterByteSize, inlineParameters, packedParameterBytes))
                return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
            inlineParameters = ReadCsgShapeInlineParameters(Span<const u8>(packedParameterBytes.data(), packedParameterBytes.size()));
            registryDistanceCallback = shapeType->desc.signedDistanceCallback;
        }
    }

    if(!registryDistanceCallback && !TryResolveBuiltInCsgShapeKind(shape.shapeType, builtInKind))
        return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);

    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, s_KeepDistanceZero);

    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices[vertexIndex];
        const SIMDVectorSingle shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
        Float4 shapePositionFloat4;
        StoreFloat(shapePosition, shapePositionFloat4);

        f32 distance = 0.0f;
        if(registryDistanceCallback){
            const auto evaluated = registryDistanceCallback(shapePositionFloat4, inlineParameters);
            if(!evaluated)
                return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
            distance = *evaluated;
        }
        else
            distance = EvaluateBuiltInCsgShapeSignedDistance(builtInKind, shapePositionFloat4, inlineParameters);

        if(!IsFinite(distance))
            return MakeUnexpected(CsgDeformViabilityReason::NonFiniteInput);

        if(Abs(distance) <= boundaryEpsilon)
            distance = s_KeepDistanceZero;
        outDistances[vertexIndex] = distance;
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

