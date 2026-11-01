// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cutter_field.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, CsgDeformViabilityReason::Enum> CsgDeformCutterField::ShapeDistances(
    const CsgDeformShape& shape,
    const Span<const CsgDeformVertex> vertices,
    const SIMDVectorSingle epsilon,
    const CsgShapeRegistry* shapeRegistry,
    Vector<f32, ScratchArena>& outDistances
){
    const CsgShapeInlineParameters inlineParameters = MakeCsgDeformShapeInlineParameters(shape);
    CsgShapeSignedDistanceCallback registryDistanceCallback = nullptr;
    FixedVector<u8, s_CsgShapeInlineParameterMaxBytes> packedParameterBytes;

    if(shapeRegistry){
        const auto shapeType = shapeRegistry->findShapeType(shape.shapeType);
        if(shapeType && shapeType->desc.signedDistanceCallback){
            if(!PackCsgShapeDefaultParameterBytes(shapeType->desc.parameterByteSize, inlineParameters, packedParameterBytes))
                return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
            registryDistanceCallback = shapeType->desc.signedDistanceCallback;
        }
    }

    if(!registryDistanceCallback){
        f32 probeDistance = 0.0f;
        if(!TryEvaluateBuiltInCsgShapeSignedDistance(shape.shapeType, s_CsgZeroShapeParameter, inlineParameters, probeDistance))
            return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
    }

    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, s_KeepDistanceZero);

    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);
    const Span<const u8> parameterSpan(packedParameterBytes.data(), packedParameterBytes.size());

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices[vertexIndex];
        const SIMDVectorSingle shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);

        f32 distance = 0.0f;
        if(registryDistanceCallback){
            const auto evaluated = registryDistanceCallback(shapePosition, parameterSpan);
            if(!evaluated)
                return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
            distance = *evaluated;
        }
        else{
            Float4 shapePositionFloat4;
            StoreFloat(shapePosition, shapePositionFloat4);
            if(!TryEvaluateBuiltInCsgShapeSignedDistance(shape.shapeType, shapePositionFloat4, inlineParameters, distance))
                return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
        }

        if(!IsFinite(distance))
            return MakeUnexpected(CsgDeformViabilityReason::NonFiniteInput);

        const SIMDVectorSingle distanceValue = VectorReplicate(distance);
        if(Vector4LessOrEqual(VectorAbs(distanceValue), epsilon))
            distance = s_KeepDistanceZero;
        outDistances[vertexIndex] = distance;
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


