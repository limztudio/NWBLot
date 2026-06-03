// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_cap_private.h"

#include <impl/assets/graphics/csg/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgCapDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool BuiltInCutterSupportsCap(const u32 shapeType){
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

[[nodiscard]] static bool EvaluateBuiltInShapeDistance(
    const CsgCutterGpuData& cutter,
    const SIMDVector shapePosition,
    SIMDVector& outSignedDistance
)noexcept{
    const SIMDVector parameters = LoadFloat(cutter.parameter0);
    switch(cutter.shapeType){
    case NWB_CSG_SHAPE_PLANE:
        outSignedDistance = SdfTests::Plane(shapePosition, parameters);
        return true;
    case NWB_CSG_SHAPE_BOX:
        outSignedDistance = SdfTests::Box(shapePosition, parameters);
        return true;
    case NWB_CSG_SHAPE_SPHERE:
        outSignedDistance = SdfTests::Sphere(shapePosition, parameters);
        return true;
    case NWB_CSG_SHAPE_CAPSULE:
        outSignedDistance = SdfTests::CapsuleY(shapePosition, parameters);
        return true;
    default:
        outSignedDistance = VectorReplicate(1.0f);
        return false;
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

[[nodiscard]] static bool EvaluateProjectShapeCap(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition,
    const SIMDVector fallbackWorldNormal,
    SIMDVector& outSignedDistance,
    SIMDVector& outWorldNormal
){
    if(!cutterEval.shapeType || !cutterEval.shapeType->desc.capEvalCallback)
        return false;
    return cutterEval.shapeType->desc.capEvalCallback(
        cutterEval.worldToShape,
        cutterEval.shapeToWorld,
        cutterEval.parameterBytes,
        cutterEval.parameterByteSize,
        worldPosition,
        fallbackWorldNormal,
        outSignedDistance,
        outWorldNormal
    );
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


bool CutterSupportsCap(const CapCutterEval& cutterEval){
    if(BuiltInCutterSupportsCap(cutterEval.cutter.shapeType))
        return true;
    return
        cutterEval.shapeType
        && cutterEval.shapeType->desc.supportsCapGeneration
        && cutterEval.shapeType->desc.capEvalCallback
    ;
}

bool EvaluateShapeDistance(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition,
    const SIMDVector fallbackWorldNormal,
    SIMDVector& outSignedDistance
){
    outSignedDistance = VectorReplicate(1.0f);
    if(BuiltInCutterSupportsCap(cutterEval.cutter.shapeType)){
        const SIMDVector shapePosition = Vector3Transform(worldPosition, cutterEval.worldToShape);
        return EvaluateBuiltInShapeDistance(cutterEval.cutter, shapePosition, outSignedDistance);
    }

    SIMDVector unusedWorldNormal = fallbackWorldNormal;
    return EvaluateProjectShapeCap(cutterEval, worldPosition, fallbackWorldNormal, outSignedDistance, unusedWorldNormal);
}

bool EvaluateWorldCapNormal(
    const CapCutterEval& cutterEval,
    const SIMDVector worldPosition,
    const SIMDVector fallback,
    SIMDVector& outWorldNormal
){
    outWorldNormal = fallback;
    if(BuiltInCutterSupportsCap(cutterEval.cutter.shapeType)){
        const SIMDVector shapeNormal = EvaluateShapeNormal(cutterEval, worldPosition);
        outWorldNormal = VectorNegate(TransformShapeNormalToWorld(cutterEval.worldToShape, shapeNormal, fallback));
        return true;
    }

    SIMDVector unusedDistance = VectorReplicate(1.0f);
    if(!EvaluateProjectShapeCap(cutterEval, worldPosition, fallback, unusedDistance, outWorldNormal))
        return false;

    outWorldNormal = Vector3NormalizeOr(outWorldNormal, fallback, s_NormalizeMinLengthSquared);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

