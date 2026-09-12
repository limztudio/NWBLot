// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>

#include <impl/assets/graphics/csg/constants.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/ecs_csg/module.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_clip_resolve{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_MinClipWForWorkRegion = static_cast<f32>(NWB_CSG_HOMOGENEOUS_W_EPSILON);
static constexpr i32 s_WorkRegionPixelPadding = 2;

namespace CsgClipCutterResolveResult{
    enum Enum : u8{
        Skipped,
        Ready
    };
};

struct CsgResolvedClipCutter{
    SIMDMatrix worldToShape;
    SIMDVector workMinBounds;
    SIMDVector workMaxBounds;
    CsgShapeTypeInfo shapeType;
    const CsgCutterComponent* cutter = nullptr;
    const u8* parameterBytes = nullptr;
    usize parameterByteSize = 0u;
    bool workBoundsValid = false;
};

struct CsgCutterTransforms{
    SIMDMatrix shapeToWorld;
    SIMDMatrix worldToShape;
};

[[nodiscard]] static SIMDVector ComputeWorldToShapeScaleBound(const SIMDMatrix& worldToShape){
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector lengthSquared = VectorAdd(Vector3LengthSq(row0), Vector3LengthSq(row1));
    lengthSquared = VectorAdd(lengthSquared, Vector3LengthSq(row2));
    return VectorSqrt(lengthSquared);
}

[[nodiscard]] static bool ResolveCsgCutterParameterBytes(
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

static void CopyCsgCutterInlineParameters(
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

[[nodiscard]] static bool BuildCsgReceiverWorldToLocal(
    const SIMDMatrix* localToWorld,
    SIMDMatrix& outWorldToLocal
){
    if(!localToWorld){
        outWorldToLocal = MatrixIdentity();
        return true;
    }

    SIMDVector determinant;
    outWorldToLocal = MatrixInverse(&determinant, *localToWorld);
    return VectorIsFinite(determinant, VectorComponentMask::s_XYZW) && Vector4Greater(VectorAbs(determinant), VectorZero());
}

[[nodiscard]] static bool BuildCsgReceiverWorldBounds(
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    const SIMDMatrix* localToWorld,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
){
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return false;

    if(!localToWorld){
        outMinBounds = localMinBounds;
        outMaxBounds = localMaxBounds;
        return true;
    }

    return AabbTests::Transform(*localToWorld, localMinBounds, localMaxBounds, outMinBounds, outMaxBounds);
}

struct CsgReceiverLocalSpace{
    SIMDVector localMinBounds = VectorZero();
    SIMDVector localMaxBounds = VectorZero();
    SIMDMatrix localToWorld = MatrixIdentity();
    bool boundsCanCull = false;
    bool hasLocalToWorld = false;

    [[nodiscard]] const SIMDMatrix* localToWorldPtr()const{
        return hasLocalToWorld ? &localToWorld : nullptr;
    }
};

[[nodiscard]] static CsgReceiverLocalSpace BuildCsgReceiverLocalSpace(
    const bool boundsCanCull,
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    const SIMDMatrix* localToWorld
){
    CsgReceiverLocalSpace localSpace;
    localSpace.boundsCanCull = boundsCanCull;
    if(boundsCanCull){
        localSpace.localMinBounds = localMinBounds;
        localSpace.localMaxBounds = localMaxBounds;
    }

    if(localToWorld){
        localSpace.localToWorld = *localToWorld;
        localSpace.hasLocalToWorld = true;
    }
    return localSpace;
}

[[nodiscard]] static CsgReceiverLocalSpace BuildCsgReceiverLocalSpace(
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform
){
    const bool boundsCanCull = CsgReceiverBoundsCanCull(receiverBounds);
    const SIMDVector localMinBounds = boundsCanCull ? LoadFloatInt(receiverBounds.minBounds) : VectorZero();
    const SIMDVector localMaxBounds = boundsCanCull ? LoadFloatInt(receiverBounds.maxBounds) : VectorZero();
    SIMDMatrix localToWorld;
    const SIMDMatrix* localToWorldPtr = nullptr;
    if(transform){
        localToWorld = MatrixAffineTransformation(
            LoadFloat(transform->scale),
            VectorZero(),
            LoadFloat(transform->rotation),
            LoadFloat(transform->position)
        );
        localToWorldPtr = &localToWorld;
    }
    return BuildCsgReceiverLocalSpace(boundsCanCull, localMinBounds, localMaxBounds, localToWorldPtr);
}

static void ExpandCsgFrameWorkRegionForWorldBounds(
    CsgFrameGpuData& csgFrameData,
    const SIMDMatrix& worldToClip,
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    const u32 frameWidth,
    const u32 frameHeight
){
    if(frameWidth == 0u || frameHeight == 0u || !AabbTests::Valid(minBounds, maxBounds)){
        csgFrameData.workRegion.expandFull();
        return;
    }

    const SIMDVector frameExtent = VectorSet(
        static_cast<f32>(frameWidth),
        static_cast<f32>(frameHeight),
        0.0f,
        0.0f
    );
    SIMDVector minPixel = frameExtent;
    SIMDVector maxPixel = VectorZero();
    for(u32 corner = 0u; corner < 8u; ++corner){
        const SIMDVector cornerSelect = VectorSelectControl(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u, 0u);
        const SIMDVector worldPosition = VectorSetW(VectorSelect(minBounds, maxBounds, cornerSelect), 1.0f);
        const SIMDVector clipPosition = Vector4Transform(worldPosition, worldToClip);
        const SIMDVector clipW = VectorSplatW(clipPosition);
        if(
            !VectorIsFinite(clipW, VectorComponentMask::s_XYZW)
            || !Vector4Greater(clipW, VectorReplicate(s_MinClipWForWorkRegion))
        ){
            csgFrameData.workRegion.expandFull();
            return;
        }

        const SIMDVector ndcPosition = VectorDivide(clipPosition, clipW);
        if(!VectorIsFinite(ndcPosition, VectorComponentMask::s_XY)){
            csgFrameData.workRegion.expandFull();
            return;
        }

        SIMDVector normalizedPosition = VectorAdd(
            VectorMultiply(ndcPosition, s_SIMDOneHalf),
            s_SIMDOneHalf
        );
        normalizedPosition = VectorSelect(
            normalizedPosition,
            VectorSubtract(s_SIMDOne, normalizedPosition),
            s_SIMDMaskY
        );
        const SIMDVector pixelPosition = VectorAndInt(
            VectorMultiply(normalizedPosition, frameExtent),
            s_SIMDMaskXY
        );
        minPixel = VectorMin(minPixel, pixelPosition);
        maxPixel = VectorMax(maxPixel, pixelPosition);
    }

    const f32 minPixelX = VectorGetX(minPixel);
    const f32 minPixelY = VectorGetY(minPixel);
    const f32 maxPixelX = VectorGetX(maxPixel);
    const f32 maxPixelY = VectorGetY(maxPixel);

    if(maxPixelX < 0.0f || maxPixelY < 0.0f || minPixelX > static_cast<f32>(frameWidth) || minPixelY > static_cast<f32>(frameHeight))
        return;

    csgFrameData.workRegion.expandClamped(
        static_cast<i32>(Floor(minPixelX)) - s_WorkRegionPixelPadding,
        static_cast<i32>(Ceil(maxPixelX)) + s_WorkRegionPixelPadding,
        static_cast<i32>(Floor(minPixelY)) - s_WorkRegionPixelPadding,
        static_cast<i32>(Ceil(maxPixelY)) + s_WorkRegionPixelPadding,
        frameWidth,
        frameHeight
    );
}

static void BuildResolvedClipCutterGpuData(
    const CsgResolvedClipCutter& resolvedCutter,
    const f32 worldToShapeScaleBound,
    CsgCutterGpuData& outCutter
){
    NWB_ASSERT(resolvedCutter.cutter);
    NWB_ASSERT(resolvedCutter.parameterByteSize == static_cast<usize>(resolvedCutter.shapeType.desc.parameterByteSize));
    NWB_ASSERT(resolvedCutter.parameterByteSize == 0u || resolvedCutter.parameterBytes);

    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    outCutter.shapeType = resolvedCutter.shapeType.id;
    CopyCsgCutterInlineParameters(resolvedCutter.parameterBytes, resolvedCutter.parameterByteSize, outCutter);
}


[[nodiscard]] static CsgClipCutterResolveResult::Enum ResolveReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    const SIMDMatrix& cutterShapeToWorld,
    const SIMDMatrix& cutterWorldToShape,
    const SIMDVector receiverLocalMinBounds,
    const SIMDVector receiverLocalMaxBounds,
    const bool receiverBoundsCanCull,
    const SIMDMatrix* receiverLocalToWorld,
    CsgResolvedClipCutter& outCutter
){
    outCutter = CsgResolvedClipCutter{};
    if(!shapeRegistry.findShapeType(cutter.shapeType, outCutter.shapeType))
        return CsgClipCutterResolveResult::Skipped;
    if(!ResolveCsgCutterParameterBytes(outCutter.shapeType, cutter, outCutter.parameterBytes, outCutter.parameterByteSize))
        return CsgClipCutterResolveResult::Skipped;
    outCutter.cutter = &cutter;
    outCutter.worldToShape = cutterWorldToShape;

    SIMDVector receiverMinBounds;
    SIMDVector receiverMaxBounds;
    if(!receiverBoundsCanCull)
        return CsgClipCutterResolveResult::Ready;
    if(!BuildCsgReceiverWorldBounds(
        receiverLocalMinBounds,
        receiverLocalMaxBounds,
        receiverLocalToWorld,
        receiverMinBounds,
        receiverMaxBounds
    ))
        return CsgClipCutterResolveResult::Ready;

    SIMDVector cutterMinBounds;
    SIMDVector cutterMaxBounds;
    bool finiteBounds = false;
    if(!shapeRegistry.buildShapeBounds(
        outCutter.shapeType.id,
        cutterShapeToWorld,
        outCutter.parameterBytes,
        outCutter.parameterByteSize,
        cutterMinBounds,
        cutterMaxBounds,
        finiteBounds
    ))
        return CsgClipCutterResolveResult::Skipped;
    if(!finiteBounds){
        outCutter.workMinBounds = receiverMinBounds;
        outCutter.workMaxBounds = receiverMaxBounds;
        outCutter.workBoundsValid = true;
        return CsgClipCutterResolveResult::Ready;
    }

    if(!AabbTests::Intersects(receiverMinBounds, receiverMaxBounds, cutterMinBounds, cutterMaxBounds))
        return CsgClipCutterResolveResult::Skipped;

    outCutter.workMinBounds = VectorMax(receiverMinBounds, cutterMinBounds);
    outCutter.workMaxBounds = VectorMin(receiverMaxBounds, cutterMaxBounds);
    outCutter.workBoundsValid = AabbTests::Valid(outCutter.workMinBounds, outCutter.workMaxBounds);
    return CsgClipCutterResolveResult::Ready;
}


template<typename CutterTransformLoader, typename CutterHandler>
[[nodiscard]] static bool ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const SIMDVector receiverLocalMinBounds,
    const SIMDVector receiverLocalMaxBounds,
    const bool receiverBoundsCanCull,
    const SIMDMatrix* receiverLocalToWorld,
    CutterTransformLoader&& loadCutterTransforms,
    CutterHandler&& handler
){
    bool resolved = true;
    receiverLookup.forEachReceiverCutter(
        receiverDrawState,
        [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
            if(!resolved)
                return;

            const CsgCutterTransforms cutterTransforms = loadCutterTransforms(cutter);
            CsgResolvedClipCutter resolvedCutter;
            const CsgClipCutterResolveResult::Enum resolveResult = ResolveReceiverClipCutter(
                shapeRegistry,
                cutter,
                cutterTransforms.shapeToWorld,
                cutterTransforms.worldToShape,
                receiverLocalMinBounds,
                receiverLocalMaxBounds,
                receiverBoundsCanCull,
                receiverLocalToWorld,
                resolvedCutter
            );
            if(resolveResult == CsgClipCutterResolveResult::Skipped)
                return;
            NWB_ASSERT(resolveResult == CsgClipCutterResolveResult::Ready);

            if(!handler(resolvedCutter))
                resolved = false;
        }
    );
    return resolved;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::resolveCsgReceiverClipDrawInfo(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    CsgReceiverClipDrawInfo& outInfo
)const{
    outInfo = CsgReceiverClipDrawInfo{};
    const __hidden_csg_clip_resolve::CsgReceiverLocalSpace receiverLocalSpace =
        __hidden_csg_clip_resolve::BuildCsgReceiverLocalSpace(receiverBounds, transform)
    ;

    return __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverLocalSpace.localMinBounds,
        receiverLocalSpace.localMaxBounds,
        receiverLocalSpace.boundsCanCull,
        receiverLocalSpace.localToWorldPtr(),
        [](const CsgCutterComponent& cutter){
            return __hidden_csg_clip_resolve::CsgCutterTransforms{
                LoadFloat(cutter.shapeToWorld),
                LoadFloat(cutter.worldToShape)
            };
        },
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter){
            if(resolvedCutter.shapeType.desc.shaderModule){
                if(!outInfo.evaluatorVariant)
                    outInfo.evaluatorVariant = resolvedCutter.shapeType.desc.shaderModule;
                else if(outInfo.evaluatorVariant != resolvedCutter.shapeType.desc.shaderModule){
                    return false;
                }
            }
            if(outInfo.cutterCount < Limit<u32>::s_Max)
                ++outInfo.cutterCount;
            return true;
        }
    );
}

bool RendererCsgSystem::appendCsgReceiverClipData(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    const u32 frameWidth,
    const u32 frameHeight,
    CsgFrameGpuData& csgFrameData,
    CsgReceiverRangeGpuData& outRange,
    const ECSRenderDetail::MeshViewGpuData* const csgWorkRegionMeshViewState
)const{
    outRange = CsgReceiverRangeGpuData{};
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    if(!receiverBounds.valid())
        return false;
    const __hidden_csg_clip_resolve::CsgReceiverLocalSpace receiverLocalSpace =
        __hidden_csg_clip_resolve::BuildCsgReceiverLocalSpace(receiverBounds, transform)
    ;

    SIMDMatrix worldToReceiver;
    if(!__hidden_csg_clip_resolve::BuildCsgReceiverWorldToLocal(receiverLocalSpace.localToWorldPtr(), worldToReceiver))
        return false;

    bool meshViewReady = false;
    SIMDMatrix worldToClip;
    if(csgWorkRegionMeshViewState){
        worldToClip = LoadFloat(csgWorkRegionMeshViewState->worldToClip);
        meshViewReady = !MatrixIsNaN(worldToClip) && !MatrixIsInfinite(worldToClip);
    }
    else{
        Float44 acceptedWorldToClip = {};
        if(m_meshSystem.snapshotAcceptedMeshViewWorldToClip(acceptedWorldToClip)){
            worldToClip = LoadFloat(acceptedWorldToClip);
            meshViewReady = !MatrixIsNaN(worldToClip) && !MatrixIsInfinite(worldToClip);
        }
    }

    StoreFloat(worldToReceiver, outRange.worldToReceiver);
    outRange.localBounds = receiverBounds;
    outRange.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    const bool appended = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverLocalSpace.localMinBounds,
        receiverLocalSpace.localMaxBounds,
        receiverLocalSpace.boundsCanCull,
        receiverLocalSpace.localToWorldPtr(),
        [](const CsgCutterComponent& cutter){
            return __hidden_csg_clip_resolve::CsgCutterTransforms{
                LoadFloat(cutter.shapeToWorld),
                LoadFloat(cutter.worldToShape)
            };
        },
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter){
            CsgCutterGpuData cutterGpuData;
            if(csgFrameData.cutters.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                return false;
            }
            const SIMDMatrix worldToShape = resolvedCutter.worldToShape;
            cutterGpuData = CsgCutterGpuData{};
            StoreFloat(worldToShape, cutterGpuData.worldToShape);
            __hidden_csg_clip_resolve::BuildResolvedClipCutterGpuData(
                resolvedCutter,
                VectorGetX(__hidden_csg_clip_resolve::ComputeWorldToShapeScaleBound(worldToShape)),
                cutterGpuData
            );

            if(meshViewReady && resolvedCutter.workBoundsValid){
                __hidden_csg_clip_resolve::ExpandCsgFrameWorkRegionForWorldBounds(
                    csgFrameData,
                    worldToClip,
                    resolvedCutter.workMinBounds,
                    resolvedCutter.workMaxBounds,
                    frameWidth,
                    frameHeight
                );
            }
            else{
                csgFrameData.workRegion.expandFull();
            }

            csgFrameData.cutters.push_back(cutterGpuData);
            ++outRange.cutterCount;
            return true;
        }
    );

    return appended && outRange.cutterCount > 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

