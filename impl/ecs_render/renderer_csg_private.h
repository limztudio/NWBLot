// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_types.h"

#include <impl/assets/graphics/csg/constants.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderCsgDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector ComputeWorldToShapeScaleBound(const SIMDMatrix& worldToShape){
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector lengthSquared = VectorAdd(Vector3LengthSq(row0), Vector3LengthSq(row1));
    lengthSquared = VectorAdd(lengthSquared, Vector3LengthSq(row2));
    return VectorSqrt(lengthSquared);
}

[[nodiscard]] inline bool ResolveCsgCutterParameterBytes(
    const CsgShapeTypeInfo& shapeType,
    const CsgCutterComponent& cutter,
    const u8*& outParameterBytes,
    usize& outParameterByteSize
){
    if(cutter.parameterBytes.empty()){
        outParameterBytes = shapeType.desc.defaultParameterBytes.empty() ? nullptr : shapeType.desc.defaultParameterBytes.data();
        outParameterByteSize = shapeType.desc.defaultParameterBytes.size();
    }else{
        outParameterBytes = cutter.parameterBytes.data();
        outParameterByteSize = cutter.parameterBytes.size();
    }

    return outParameterByteSize == static_cast<usize>(shapeType.desc.parameterByteSize)
        && (outParameterByteSize == 0u || outParameterBytes)
    ;
}

inline void CopyCsgCutterInlineParameters(
    const u8* parameterBytes,
    const usize parameterByteSize,
    CsgCutterGpuData& inOutCutter
){
    inOutCutter.parameter0 = Float4(0.f, 0.f, 0.f, 0.f);
    inOutCutter.parameter1 = Float4(0.f, 0.f, 0.f, 0.f);
    if(!parameterBytes)
        return;

    const usize parameter0Bytes = Min(parameterByteSize, sizeof(Float4));
    if(parameter0Bytes > 0u)
        NWB_MEMCPY(&inOutCutter.parameter0, sizeof(Float4), parameterBytes, parameter0Bytes);

    if(parameterByteSize <= sizeof(Float4))
        return;

    const usize parameter1Bytes = Min(parameterByteSize - sizeof(Float4), sizeof(Float4));
    if(parameter1Bytes > 0u)
        NWB_MEMCPY(&inOutCutter.parameter1, sizeof(Float4), parameterBytes + sizeof(Float4), parameter1Bytes);
}

[[nodiscard]] inline CsgBoundsGpuData BuildCsgBoundsGpuData(
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    const bool finiteBounds
){
    CsgBoundsGpuData bounds;
    const i32 flags = s_CsgBoundsValidFlag | (finiteBounds ? s_CsgBoundsFiniteFlag : 0);
    StoreFloatInt(VectorSetW(minBounds, 0.0f), flags, &bounds.minBounds);
    StoreFloatInt(VectorSetW(maxBounds, 0.0f), 0, &bounds.maxBounds);
    return bounds;
}

[[nodiscard]] inline bool BuildCsgReceiverWorldToLocal(
    const Scene::TransformComponent* transform,
    SIMDMatrix& outWorldToLocal
){
    if(!transform){
        outWorldToLocal = MatrixIdentity();
        return true;
    }

    const SIMDVector scale = LoadFloat(transform->scale);
    const SIMDVector rotation = LoadFloat(transform->rotation);
    const SIMDVector translation = LoadFloat(transform->position);
    const SIMDMatrix localToWorld = MatrixAffineTransformation(scale, VectorZero(), rotation, translation);
    SIMDVector determinant;
    outWorldToLocal = MatrixInverse(&determinant, localToWorld);
    const f32 det = VectorGetX(determinant);
    return IsFinite(det) && Abs(det) > 0.0f;
}

[[nodiscard]] inline bool BuildCsgReceiverWorldBounds(
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
){
    if(!receiverBounds.valid())
        return false;

    const SIMDVector localMinBounds = LoadFloatInt(receiverBounds.minBounds);
    const SIMDVector localMaxBounds = LoadFloatInt(receiverBounds.maxBounds);
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return false;

    if(!transform){
        outMinBounds = localMinBounds;
        outMaxBounds = localMaxBounds;
        return true;
    }

    const SIMDVector scale = LoadFloat(transform->scale);
    const SIMDVector rotation = LoadFloat(transform->rotation);
    const SIMDVector translation = LoadFloat(transform->position);
    const SIMDMatrix localToWorld = MatrixAffineTransformation(scale, VectorZero(), rotation, translation);
    return AabbTests::Transform(localToWorld, localMinBounds, localMaxBounds, outMinBounds, outMaxBounds);
}

[[nodiscard]] inline bool BuildCsgCutterComponentWorldBounds(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
){
    CsgShapeTypeInfo shapeType;
    if(!shapeRegistry.findShapeType(cutter.shapeType, shapeType))
        return false;

    const u8* parameterBytes = nullptr;
    usize parameterByteSize = 0u;
    if(!ResolveCsgCutterParameterBytes(shapeType, cutter, parameterBytes, parameterByteSize))
        return false;

    return shapeRegistry.buildShapeBounds(
        shapeType.id,
        LoadFloat(cutter.shapeToWorld),
        parameterBytes,
        parameterByteSize,
        outMinBounds,
        outMaxBounds,
        outFiniteBounds
    );
}

[[nodiscard]] inline bool BuildCsgCutterGpuData(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    CsgCutterGpuData& outCutter
){
    if(cutter.operation != CsgOperation::Subtract)
        return false;

    CsgShapeTypeInfo shapeType;
    if(!shapeRegistry.findShapeType(cutter.shapeType, shapeType))
        return false;

    outCutter = CsgCutterGpuData{};
    outCutter.operation = NWB_CSG_OPERATION_SUBTRACT;
    const SIMDMatrix worldToShape = LoadFloat(cutter.worldToShape);
    StoreFloat(worldToShape, &outCutter.worldToShape);
    const f32 worldToShapeScaleBound = VectorGetX(ComputeWorldToShapeScaleBound(worldToShape));
    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    const u8* sourceParameterBytes = nullptr;
    usize sourceParameterByteSize = 0u;
    if(!ResolveCsgCutterParameterBytes(shapeType, cutter, sourceParameterBytes, sourceParameterByteSize))
        return false;

    outCutter.shapeType = shapeType.id;
    CopyCsgCutterInlineParameters(sourceParameterBytes, sourceParameterByteSize, outCutter);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

