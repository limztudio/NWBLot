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


template<typename ParameterT>
[[nodiscard]] inline bool LoadCsgCutterParameters(const CsgCutterComponent& cutter, ParameterT& outParameters){
    outParameters = ParameterT{};
    if(cutter.parameterBytes.empty())
        return true;
    if(cutter.parameterBytes.size() != sizeof(ParameterT))
        return false;

    NWB_MEMCPY(&outParameters, sizeof(ParameterT), cutter.parameterBytes.data(), sizeof(ParameterT));
    return true;
}

[[nodiscard]] inline bool AppendCsgParameterBytes(
    CsgParameterByteDataVector& parameterBytes,
    const void* sourceBytes,
    const usize byteSize,
    u32& outByteOffset,
    u32& outByteSize
){
    outByteOffset = 0u;
    outByteSize = 0u;
    if(!sourceBytes || byteSize == 0u)
        return true;

    usize alignedBegin = 0u;
    if(!AlignUpChecked(parameterBytes.size(), sizeof(u32), alignedBegin))
        return false;
    if(alignedBegin > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(byteSize > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(alignedBegin > Limit<usize>::s_Max - byteSize)
        return false;

    if(parameterBytes.size() < alignedBegin)
        parameterBytes.resize(alignedBegin, 0u);

    const usize byteBegin = parameterBytes.size();
    const usize byteEnd = byteBegin + byteSize;
    parameterBytes.resize(byteEnd);
    NWB_MEMCPY(parameterBytes.data() + byteBegin, byteSize, sourceBytes, byteSize);

    usize alignedEnd = 0u;
    if(!AlignUpChecked(parameterBytes.size(), sizeof(u32), alignedEnd))
        return false;
    if(parameterBytes.size() < alignedEnd)
        parameterBytes.resize(alignedEnd, 0u);

    outByteOffset = static_cast<u32>(byteBegin);
    outByteSize = static_cast<u32>(byteSize);
    return true;
}

template<typename ParameterT>
[[nodiscard]] inline bool AppendCsgCutterParameters(
    CsgParameterByteDataVector* parameterBytes,
    const ParameterT& parameters,
    CsgCutterGpuData& inOutCutter
){
    if(!parameterBytes){
        inOutCutter.parameterByteSize = static_cast<u32>(sizeof(ParameterT));
        return true;
    }

    return AppendCsgParameterBytes(
        *parameterBytes,
        &parameters,
        sizeof(ParameterT),
        inOutCutter.parameterByteOffset,
        inOutCutter.parameterByteSize
    );
}

[[nodiscard]] inline SIMDVector ComputeWorldToShapeScaleBound(const SIMDMatrix& worldToShape){
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector lengthSquared = VectorAdd(Vector3LengthSq(row0), Vector3LengthSq(row1));
    lengthSquared = VectorAdd(lengthSquared, Vector3LengthSq(row2));
    return VectorSqrt(lengthSquared);
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
    const u8* parameterBytes = cutter.parameterBytes.empty() ? nullptr : cutter.parameterBytes.data();
    return shapeRegistry.buildShapeBounds(
        cutter.shapeType,
        LoadFloat(cutter.shapeToWorld),
        parameterBytes,
        cutter.parameterBytes.size(),
        outMinBounds,
        outMaxBounds,
        outFiniteBounds
    );
}

[[nodiscard]] inline bool BuildCsgCutterGpuData(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    CsgParameterByteDataVector* parameterBytes,
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
    const SIMDMatrix shapeToWorld = LoadFloat(cutter.shapeToWorld);
    StoreFloat(worldToShape, &outCutter.worldToShape);
    StoreFloat(shapeToWorld, &outCutter.shapeToWorld);
    const f32 worldToShapeScaleBound = VectorGetX(ComputeWorldToShapeScaleBound(worldToShape));
    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    if(cutter.shapeType == s_CsgPlaneShapeName){
        CsgPlaneShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_PLANE;
        outCutter.parameter0 = parameters.normalDistance;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }
    if(cutter.shapeType == s_CsgBoxShapeName){
        CsgBoxShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_BOX;
        outCutter.parameter0 = parameters.halfExtents;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }
    if(cutter.shapeType == s_CsgSphereShapeName){
        CsgSphereShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_SPHERE;
        outCutter.parameter0 = parameters.radius;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }
    if(cutter.shapeType == s_CsgCapsuleShapeName){
        CsgCapsuleShapeParameters parameters;
        if(!LoadCsgCutterParameters(cutter, parameters))
            return false;
        outCutter.shapeType = NWB_CSG_SHAPE_CAPSULE;
        outCutter.parameter0 = parameters.radiusHalfHeight;
        return AppendCsgCutterParameters(parameterBytes, parameters, outCutter);
    }

    if(shapeType.id < NWB_CSG_SHAPE_PROJECT_BEGIN)
        return false;
    if(cutter.parameterBytes.size() != static_cast<usize>(shapeType.desc.parameterByteSize))
        return false;

    outCutter.shapeType = shapeType.id;
    if(!parameterBytes){
        outCutter.parameterByteSize = shapeType.desc.parameterByteSize;
        return true;
    }
    return AppendCsgParameterBytes(
        *parameterBytes,
        cutter.parameterBytes.data(),
        cutter.parameterBytes.size(),
        outCutter.parameterByteOffset,
        outCutter.parameterByteSize
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

