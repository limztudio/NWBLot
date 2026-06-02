// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_private.h"

#include <impl/assets/graphics/csg/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgCapDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CutterSupportsCap(const u32 shapeType){
    switch(shapeType){
    case NWB_CSG_SHAPE_PLANE:
    case NWB_CSG_SHAPE_BOX:
    case NWB_CSG_SHAPE_SPHERE:
    case NWB_CSG_SHAPE_CAPSULE:
        return true;
    default:
        return false;
    }
}

SIMDVector NormalizeVector3Or(const SIMDVector value, const SIMDVector fallback){
    const SIMDVector lengthSquared = Vector3LengthSq(value);
    const f32 scalarLengthSquared = VectorGetX(lengthSquared);
    if(!IsFinite(scalarLengthSquared) || scalarLengthSquared <= 0.00000001f)
        return fallback;

    return VectorSetW(VectorMultiply(value, VectorReciprocalSqrt(lengthSquared)), 0.0f);
}

SIMDVector EvaluateShapeDistance(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition
){
    const SIMDVector shapePosition = Vector3Transform(worldPosition, cutterEval.worldToShape);
    const SIMDVector parameters = LoadFloat(cutterEval.cutter.parameter0);
    switch(cutterEval.cutter.shapeType){
    case NWB_CSG_SHAPE_PLANE:
        return VectorAdd(Vector3Dot(shapePosition, parameters), VectorSplatW(parameters));
    case NWB_CSG_SHAPE_BOX:{
        const SIMDVector q = VectorSubtract(VectorAbs(shapePosition), parameters);
        const SIMDVector outside = VectorMax(q, VectorZero());
        const f32 insideDistance = Min(Max(VectorGetX(q), Max(VectorGetY(q), VectorGetZ(q))), 0.0f);
        return VectorAdd(Vector3Length(outside), VectorReplicate(insideDistance));
    }
    case NWB_CSG_SHAPE_SPHERE:
        return VectorSubtract(Vector3Length(shapePosition), VectorSplatX(parameters));
    case NWB_CSG_SHAPE_CAPSULE:{
        const f32 halfHeight = VectorGetY(parameters);
        const f32 clampedY = Max(-halfHeight, Min(halfHeight, VectorGetY(shapePosition)));
        const SIMDVector segmentPoint = VectorSet(0.0f, clampedY, 0.0f, 0.0f);
        return VectorSubtract(Vector3Length(VectorSubtract(shapePosition, segmentPoint)), VectorSplatX(parameters));
    }
    default:
        return VectorReplicate(1.0f);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_shape{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDVector EvaluateBoxShapeNormal(
    const SIMDVector shapePosition,
    const SIMDVector halfExtents
){
    const SIMDVector q = VectorSubtract(VectorAbs(shapePosition), halfExtents);
    const f32 x = VectorGetX(shapePosition);
    const f32 y = VectorGetY(shapePosition);
    const f32 z = VectorGetZ(shapePosition);
    const f32 ox = Max(VectorGetX(q), 0.0f);
    const f32 oy = Max(VectorGetY(q), 0.0f);
    const f32 oz = Max(VectorGetZ(q), 0.0f);
    if(ox * ox + oy * oy + oz * oz > 0.00000001f)
        return NormalizeVector3Or(
            VectorSet(x < 0.0f ? -ox : ox, y < 0.0f ? -oy : oy, z < 0.0f ? -oz : oz, 0.0f),
            VectorSet(0.0f, 1.0f, 0.0f, 0.0f)
        );

    const f32 dx = VectorGetX(q);
    const f32 dy = VectorGetY(q);
    const f32 dz = VectorGetZ(q);
    if(dx >= dy && dx >= dz)
        return VectorSet(x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f, 0.0f);
    if(dy >= dz)
        return VectorSet(0.0f, y < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);

    return VectorSet(0.0f, 0.0f, z < 0.0f ? -1.0f : 1.0f, 0.0f);
}

[[nodiscard]] static SIMDVector EvaluateShapeNormal(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition
){
    const SIMDVector shapePosition = Vector3Transform(worldPosition, cutterEval.worldToShape);
    const SIMDVector parameters = LoadFloat(cutterEval.cutter.parameter0);
    switch(cutterEval.cutter.shapeType){
    case NWB_CSG_SHAPE_PLANE:
        return NormalizeVector3Or(VectorSetW(parameters, 0.0f), VectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    case NWB_CSG_SHAPE_BOX:
        return EvaluateBoxShapeNormal(shapePosition, parameters);
    case NWB_CSG_SHAPE_SPHERE:
        return NormalizeVector3Or(shapePosition, VectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    case NWB_CSG_SHAPE_CAPSULE:{
        const f32 halfHeight = VectorGetY(parameters);
        const f32 clampedY = Max(-halfHeight, Min(halfHeight, VectorGetY(shapePosition)));
        return NormalizeVector3Or(
            VectorSubtract(shapePosition, VectorSet(0.0f, clampedY, 0.0f, 0.0f)),
            VectorSet(0.0f, VectorGetY(shapePosition) < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f)
        );
    }
    default:
        return VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }
}

[[nodiscard]] static SIMDVector TransformShapeNormalToWorld(
    const SIMDMatrix& worldToShape,
    const SIMDVector shapeNormal,
    const SIMDVector fallback
){
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector worldNormal = VectorMultiply(row0, VectorSplatX(shapeNormal));
    worldNormal = VectorMultiplyAdd(row1, VectorSplatY(shapeNormal), worldNormal);
    worldNormal = VectorMultiplyAdd(row2, VectorSplatZ(shapeNormal), worldNormal);
    return NormalizeVector3Or(worldNormal, fallback);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVector EvaluateWorldCapNormal(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition,
    const SIMDVector fallback
){
    const SIMDVector shapeNormal = __hidden_csg_cap_shape::EvaluateShapeNormal(cutterEval, worldPosition);
    return VectorNegate(__hidden_csg_cap_shape::TransformShapeNormalToWorld(cutterEval.worldToShape, shapeNormal, fallback));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

