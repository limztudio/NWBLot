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

SIMDVector EvaluateShapeDistance(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition
){
    const SIMDVector shapePosition = Vector3Transform(worldPosition, cutterEval.worldToShape);
    const SIMDVector parameters = LoadFloat(cutterEval.cutter.parameter0);
    switch(cutterEval.cutter.shapeType){
    case NWB_CSG_SHAPE_PLANE:
        return SdfTests::Plane(shapePosition, parameters);
    case NWB_CSG_SHAPE_BOX:
        return SdfTests::Box(shapePosition, parameters);
    case NWB_CSG_SHAPE_SPHERE:
        return SdfTests::Sphere(shapePosition, parameters);
    case NWB_CSG_SHAPE_CAPSULE:
        return SdfTests::CapsuleY(shapePosition, parameters);
    default:
        return VectorReplicate(1.0f);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDVector EvaluateShapeNormal(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition
){
    const SIMDVector shapePosition = Vector3Transform(worldPosition, cutterEval.worldToShape);
    const SIMDVector parameters = LoadFloat(cutterEval.cutter.parameter0);
    constexpr f32 minLengthSquared = s_NormalizeMinLengthSquared;
    const SIMDVector fallbackUp = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    switch(cutterEval.cutter.shapeType){
    case NWB_CSG_SHAPE_PLANE:
        return SdfTests::PlaneNormal(parameters, fallbackUp, minLengthSquared);
    case NWB_CSG_SHAPE_BOX:
        return SdfTests::BoxNormal(shapePosition, parameters, fallbackUp, minLengthSquared);
    case NWB_CSG_SHAPE_SPHERE:
        return SdfTests::SphereNormal(shapePosition, fallbackUp, minLengthSquared);
    case NWB_CSG_SHAPE_CAPSULE:
        return SdfTests::CapsuleYNormal(shapePosition, parameters, minLengthSquared);
    default:
        return fallbackUp;
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
    return Vector3NormalizeOr(worldNormal, fallback, s_NormalizeMinLengthSquared);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVector EvaluateWorldCapNormal(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition,
    const SIMDVector fallback
){
    const SIMDVector shapeNormal = EvaluateShapeNormal(cutterEval, worldPosition);
    return VectorNegate(TransformShapeNormalToWorld(cutterEval.worldToShape, shapeNormal, fallback));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

