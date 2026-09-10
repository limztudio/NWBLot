// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cutter_field.h"

#include "deform_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformShapeKind::Enum CsgDeformCutterField::ClassifyDeformShape(const Name& shapeType){
    static const Name s_PlaneShape("engine/csg/plane");
    static const Name s_BoxShape("engine/csg/box");
    static const Name s_SphereShape("engine/csg/sphere");
    static const Name s_CapsuleShape("engine/csg/capsule");
    if(shapeType == s_PlaneShape)
        return CsgDeformShapeKind::Plane;
    if(shapeType == s_BoxShape)
        return CsgDeformShapeKind::Box;
    if(shapeType == s_SphereShape)
        return CsgDeformShapeKind::Sphere;
    if(shapeType == s_CapsuleShape)
        return CsgDeformShapeKind::Capsule;
    return CsgDeformShapeKind::Invalid;
}

f32 CsgDeformCutterField::PlaneSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(Vector3Dot(shapePosition, parameter0)) + VectorGetW(parameter0);
}

f32 CsgDeformCutterField::BoxSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    // 3-lane helpers ignore w, so the affine w=1 lane needs no masking.
    const SIMDVector halfExtents = VectorSetW(parameter0, s_ShapeWMask);
    const SIMDVector q = VectorSubtract(VectorAbs(shapePosition), halfExtents);
    const SIMDVector outsideVec = VectorMax(q, VectorZero());
    const f32 outside = VectorGetX(Vector3Length(outsideVec));
    const f32 insideComp = VectorGetX(Vector3MinComponent(q));
    const f32 inside = insideComp < s_KeepDistanceZero ? insideComp : s_KeepDistanceZero;
    return outside + inside;
}

f32 CsgDeformCutterField::SphereSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    return VectorGetX(Vector3Length(shapePosition)) - VectorGetX(parameter0);
}

f32 CsgDeformCutterField::CapsuleSignedDistance(SIMDVector shapePosition, SIMDVector parameter0){
    const f32 halfHeight = VectorGetY(parameter0);
    const f32 shapeY = VectorGetY(shapePosition);
    const f32 clampedY = shapeY < -halfHeight ? -halfHeight : (shapeY > halfHeight ? halfHeight : shapeY);
    const SIMDVector delta = VectorSubtract(shapePosition, VectorSet(s_ShapeWMask, clampedY, s_ShapeWMask, s_ShapeWMask));
    return VectorGetX(Vector3Length(delta)) - VectorGetX(parameter0);
}

bool CsgDeformCutterField::ShapeDistances(
    const CsgDeformShape& shape,
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const f32 epsilon,
    Vector<f32, ScratchArena>& outDistances,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    if(!shape.shapeType){
        outReason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    const CsgDeformShapeKind::Enum shapeKind = CsgDeformCutterField::ClassifyDeformShape(shape.shapeType);
    if(shapeKind == CsgDeformShapeKind::Invalid){
        outReason = CsgDeformViabilityReason::InvalidCutter;
        return false;
    }
    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, 0.0f);

    // Cutter dispatch happens once per cut. World-to-shape and SDF eval stay on SIMD lanes; only the snapped distance crosses back to scalar, so preview/commit observe identical distances with no second pass.
    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);
    const SIMDVector parameter0 = LoadFloat(shape.parameter0);
    switch(shapeKind){
    case CsgDeformShapeKind::Plane:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = CsgDeformCutterField::PlaneSignedDistance(shapePosition, parameter0);
            if(!CsgDeformValidator::FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    case CsgDeformShapeKind::Box:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = CsgDeformCutterField::BoxSignedDistance(shapePosition, parameter0);
            if(!CsgDeformValidator::FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    case CsgDeformShapeKind::Sphere:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = CsgDeformCutterField::SphereSignedDistance(shapePosition, parameter0);
            if(!CsgDeformValidator::FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    case CsgDeformShapeKind::Capsule:{
        for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
            const CsgDeformVertex& vertex = vertices[vertexIndex];
            const SIMDVector shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
            f32 distance = CsgDeformCutterField::CapsuleSignedDistance(shapePosition, parameter0);
            if(!CsgDeformValidator::FiniteFloat(distance)){
                outReason = CsgDeformViabilityReason::NonFiniteInput;
                return false;
            }
            if(Abs(distance) <= epsilon)
                distance = s_KeepDistanceZero;
            outDistances[vertexIndex] = distance;
        }
        return true;
    }
    default:
        break;
    }
    outReason = CsgDeformViabilityReason::InvalidCutter;
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

