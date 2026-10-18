// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cutter_field.h"

#include "deform_validator.h"

#include <impl/assets_csg/shape_id.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;
using CsgDeformDistanceFunc = SIMDVectorSingle(*)(SIMDVectorSingle, SIMDVectorSingle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgDeformShapeKind::Enum CsgDeformCutterField::ClassifyDeformShape(const Name& shapeType)noexcept{
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

SIMDVectorSingle CsgDeformCutterField::PlaneSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept{
    return VectorAdd(Vector3Dot(shapePosition, parameter0), VectorSplatW(parameter0));
}

SIMDVectorSingle CsgDeformCutterField::BoxSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept{
    // 3-lane helpers ignore w, so the affine w=1 lane needs no masking. Inside/outside combine stays replicated on lanes.
    const SIMDVectorSingle halfExtents = VectorSetW(parameter0, s_ShapeWMask);
    const SIMDVectorSingle q = VectorSubtract(VectorAbs(shapePosition), halfExtents);
    const SIMDVectorSingle outsideVec = Vector3Length(VectorMax(q, VectorZero()));
    const SIMDVectorSingle insideVec = VectorMin(Vector3MinComponent(q), VectorZero());
    return VectorAdd(outsideVec, insideVec);
}

SIMDVectorSingle CsgDeformCutterField::SphereSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept{
    return VectorSubtract(Vector3Length(shapePosition), VectorSplatX(parameter0));
}

SIMDVectorSingle CsgDeformCutterField::CapsuleSignedDistanceVec(SIMDVectorSingle shapePosition, SIMDVectorSingle parameter0)noexcept{
    const SIMDVectorSingle halfHeight = VectorSplatY(parameter0);
    const SIMDVectorSingle shapeY = VectorSplatY(shapePosition);
    const SIMDVectorSingle clampedY = VectorClamp(shapeY, VectorNegate(halfHeight), halfHeight);
    const SIMDVectorSingle spine = VectorSelect(VectorZero(), clampedY, s_SIMDMaskY);
    const SIMDVectorSingle delta = VectorSubtract(shapePosition, spine);
    return VectorSubtract(Vector3Length(delta), VectorSplatX(parameter0));
}

Expected<void, CsgDeformViabilityReason::Enum> CsgDeformCutterField::ShapeDistances(const CsgDeformShape& shape, const CsgDeformVertexVector<ScratchArena>& vertices, const f32 epsilon, Vector<f32, ScratchArena>& outDistances){
    const CsgDeformShapeKind::Enum shapeKind = CsgDeformCutterField::ClassifyDeformShape(shape.shapeType);
    if(shapeKind == CsgDeformShapeKind::Invalid){
        return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
    }
    const usize vertexCount = vertices.size();
    outDistances.clear();
    outDistances.resize(vertexCount, s_KeepDistanceZero);

    // Cutter dispatch happens once per cut. World-to-shape and SDF eval stay on SIMD lanes
    const SIMDMatrix worldToShape = LoadFloat(shape.worldToShape);
    const SIMDVectorSingle parameter0 = LoadFloat(shape.parameter0);
    CsgDeformDistanceFunc distanceFunc = nullptr;
    switch(shapeKind){
    case CsgDeformShapeKind::Plane:
        distanceFunc = &CsgDeformCutterField::PlaneSignedDistanceVec;
        break;
    case CsgDeformShapeKind::Box:
        distanceFunc = &CsgDeformCutterField::BoxSignedDistanceVec;
        break;
    case CsgDeformShapeKind::Sphere:
        distanceFunc = &CsgDeformCutterField::SphereSignedDistanceVec;
        break;
    case CsgDeformShapeKind::Capsule:
        distanceFunc = &CsgDeformCutterField::CapsuleSignedDistanceVec;
        break;
    default:
        break;
    }
    if(!distanceFunc){
        return MakeUnexpected(CsgDeformViabilityReason::InvalidCutter);
    }
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices[vertexIndex];
        const SIMDVectorSingle shapePosition = Vector4Transform(VectorSetW(LoadFloat(vertex.position), s_AffineW), worldToShape);
        const SIMDVectorSingle distanceValue = distanceFunc(shapePosition, parameter0);
        f32 distance = VectorGetX(distanceValue);
        if(!CsgDeformValidator::FiniteFloat(distance)){
            return MakeUnexpected(CsgDeformViabilityReason::NonFiniteInput);
        }
        if(VectorGetX(VectorAbs(distanceValue)) <= epsilon)
            distance = s_KeepDistanceZero;
        outDistances[vertexIndex] = distance;
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

