// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cutter_field.h"

#include "deform_validator.h"
#include <impl/assets/csg/shape_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;
using CsgDeformDistanceFunc = SIMDVector(*)(SIMDVector, SIMDVector);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformShapeKind::Enum CsgDeformCutterField::classifyDeformShape(const Name& shapeType){
    if(shapeType == s_CsgPlaneShapeName)
        return CsgDeformShapeKind::Plane;
    if(shapeType == s_CsgBoxShapeName)
        return CsgDeformShapeKind::Box;
    if(shapeType == s_CsgSphereShapeName)
        return CsgDeformShapeKind::Sphere;
    if(shapeType == s_CsgCapsuleShapeName)
        return CsgDeformShapeKind::Capsule;
    return CsgDeformShapeKind::Invalid;
}

SIMDVector CsgDeformCutterField::planeSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorAdd(Vector3Dot(shapePosition, parameter0), VectorSplatW(parameter0));
}

SIMDVector CsgDeformCutterField::boxSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0){
    // 3-lane helpers ignore w, so the affine w=1 lane needs no masking. Inside/outside combine stays replicated on lanes.
    const SIMDVector halfExtents = VectorSetW(parameter0, s_ShapeWMask);
    const SIMDVector q = VectorSubtract(VectorAbs(shapePosition), halfExtents);
    const SIMDVector outsideVec = Vector3Length(VectorMax(q, VectorZero()));
    const SIMDVector insideVec = VectorMin(Vector3MinComponent(q), VectorZero());
    return VectorAdd(outsideVec, insideVec);
}

SIMDVector CsgDeformCutterField::sphereSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorSubtract(Vector3Length(shapePosition), VectorSplatX(parameter0));
}

SIMDVector CsgDeformCutterField::capsuleSignedDistanceVec(SIMDVector shapePosition, SIMDVector parameter0){
    const SIMDVector halfHeight = VectorSplatY(parameter0);
    const SIMDVector shapeY = VectorSplatY(shapePosition);
    const SIMDVector clampedY = VectorClamp(shapeY, VectorNegate(halfHeight), halfHeight);
    const SIMDVector spine = VectorSelect(VectorZero(), clampedY, s_SIMDMaskY);
    const SIMDVector delta = VectorSubtract(shapePosition, spine);
    return VectorSubtract(Vector3Length(delta), VectorSplatX(parameter0));
}

f32 CsgDeformCutterField::planeSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(CsgDeformCutterField::planeSignedDistanceVec(shapePosition, parameter0));
}

f32 CsgDeformCutterField::boxSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(CsgDeformCutterField::boxSignedDistanceVec(shapePosition, parameter0));
}

f32 CsgDeformCutterField::sphereSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(CsgDeformCutterField::sphereSignedDistanceVec(shapePosition, parameter0));
}

f32 CsgDeformCutterField::capsuleSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(CsgDeformCutterField::capsuleSignedDistanceVec(shapePosition, parameter0));
}

bool CsgDeformCutterField::shapeDistances(
    const CsgDeformShape& shape,
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const f32 epsilon,
    Vector<f32, ScratchArena>& outDistances,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    const CsgDeformShapeKind::Enum shapeKind = CsgDeformCutterField::classifyDeformShape(shape.shapeType);
    if(shapeKind == CsgDeformShapeKind::Invalid){
        outReason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, s_KeepDistanceZero);

    // Cutter dispatch happens once per cut. World-to-shape and SDF eval stay on SIMD lanes
    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);
    const SIMDVector parameter0 = LoadFloat(shape.parameter0);
    CsgDeformDistanceFunc distanceFunc = nullptr;
    switch(shapeKind){
    case CsgDeformShapeKind::Plane:
        distanceFunc = &CsgDeformCutterField::planeSignedDistanceVec;
        break;
    case CsgDeformShapeKind::Box:
        distanceFunc = &CsgDeformCutterField::boxSignedDistanceVec;
        break;
    case CsgDeformShapeKind::Sphere:
        distanceFunc = &CsgDeformCutterField::sphereSignedDistanceVec;
        break;
    case CsgDeformShapeKind::Capsule:
        distanceFunc = &CsgDeformCutterField::capsuleSignedDistanceVec;
        break;
    default:
        break;
    }
    if(!distanceFunc){
        outReason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices[vertexIndex];
        const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
        f32 distance = VectorGetX(distanceFunc(shapePosition, parameter0));
        if(!CsgDeformValidator::finiteFloat(distance)){
            outReason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
        if(Abs(distance) <= epsilon)
            distance = s_KeepDistanceZero;
        outDistances[vertexIndex] = distance;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

