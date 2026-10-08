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


struct CsgResolvedClipCutter{
    SIMDMatrix worldToShape;
    CsgClipWorkBounds workBounds;
    CsgShapeTypeInfo shapeType;
    const CsgCutterComponent* cutter = nullptr;

    [[nodiscard]] const u8* parameterBytes()const noexcept{
        NWB_ASSERT(cutter);
        if(!cutter->parameterBytes.empty())
            return cutter->parameterBytes.data();
        return shapeType.desc.defaultParameterBytes.empty() ? nullptr : shapeType.desc.defaultParameterBytes.data();
    }

    [[nodiscard]] usize parameterByteSize()const noexcept{
        NWB_ASSERT(cutter);
        return cutter->parameterBytes.empty() ? shapeType.desc.defaultParameterBytes.size() : cutter->parameterBytes.size();
    }
};

struct CsgCutterTransforms{
    SIMDMatrix shapeToWorld;
    SIMDMatrix worldToShape;
};

[[nodiscard]] static SIMDVector ComputeWorldToShapeScaleBound(const SIMDMatrix& worldToShape)noexcept{
    const SIMDVector row0 = VectorSetW(worldToShape.v[0], 0.0f);
    const SIMDVector row1 = VectorSetW(worldToShape.v[1], 0.0f);
    const SIMDVector row2 = VectorSetW(worldToShape.v[2], 0.0f);
    SIMDVector lengthSquared = VectorAdd(Vector3LengthSq(row0), Vector3LengthSq(row1));
    lengthSquared = VectorAdd(lengthSquared, Vector3LengthSq(row2));
    return VectorSqrt(lengthSquared);
}

[[nodiscard]] Expected<BinaryByteView> ResolveCsgCutterParameterBytes(
    const CsgShapeTypeInfo& shapeType,
    const CsgCutterComponent& cutter
)noexcept{
    const u8* bytes = cutter.parameterBytes.empty()
        ? (shapeType.desc.defaultParameterBytes.empty() ? nullptr : shapeType.desc.defaultParameterBytes.data())
        : cutter.parameterBytes.data()
    ;
    const usize byteSize = cutter.parameterBytes.empty() ? shapeType.desc.defaultParameterBytes.size() : cutter.parameterBytes.size();
    if(byteSize != static_cast<usize>(shapeType.desc.parameterByteSize) || (byteSize != 0u && !bytes))
        return MakeUnexpected(Failure{});
    return BinaryByteView(bytes, byteSize);
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

[[nodiscard]] Expected<SIMDMatrix> BuildCsgReceiverWorldToLocal(const SIMDMatrix* localToWorld)noexcept{
    if(!localToWorld)
        return MatrixIdentity();

    SIMDVector determinant;
    const SIMDMatrix worldToLocal = MatrixInverse(&determinant, *localToWorld);
    if(!VectorIsFinite(determinant, VectorComponentMask::s_XYZW) || !Vector4Greater(VectorAbs(determinant), VectorZero()))
        return MakeUnexpected(Failure{});
    return worldToLocal;
}

[[nodiscard]] Expected<AabbTests::Bounds> BuildCsgReceiverWorldBounds(
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    const SIMDMatrix* localToWorld
)noexcept{
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return MakeUnexpected(Failure{});
    if(!localToWorld)
        return AabbTests::Bounds{ localMinBounds, localMaxBounds };
    return AabbTests::Transform(*localToWorld, localMinBounds, localMaxBounds);
}

struct CsgReceiverLocalSpace{
    SIMDVector localMinBounds = VectorZero();
    SIMDVector localMaxBounds = VectorZero();
    SIMDMatrix localToWorld = MatrixIdentity();
    bool boundsCanCull = false;
    bool hasLocalToWorld = false;

    [[nodiscard]] const SIMDMatrix* localToWorldPtr()const noexcept{
        return hasLocalToWorld ? &localToWorld : nullptr;
    }
};

[[nodiscard]] static CsgReceiverLocalSpace BuildCsgReceiverLocalSpace(
    const bool boundsCanCull,
    const SIMDVector localMinBounds,
    const SIMDVector localMaxBounds,
    const SIMDMatrix* localToWorld
)noexcept{
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
)noexcept{
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

static void BuildResolvedClipCutterGpuData(
    const CsgResolvedClipCutter& resolvedCutter,
    const f32 worldToShapeScaleBound,
    CsgCutterGpuData& outCutter
){
    NWB_ASSERT(resolvedCutter.cutter);
    NWB_ASSERT(resolvedCutter.parameterByteSize() == static_cast<usize>(resolvedCutter.shapeType.desc.parameterByteSize));
    NWB_ASSERT(resolvedCutter.parameterByteSize() == 0u || resolvedCutter.parameterBytes());

    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    outCutter.shapeType = resolvedCutter.shapeType.id;
    CopyCsgCutterInlineParameters(resolvedCutter.parameterBytes(), resolvedCutter.parameterByteSize(), outCutter);
}


[[nodiscard]] static Expected<CsgResolvedClipCutter> ResolveReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    const SIMDMatrix& cutterShapeToWorld,
    const SIMDMatrix& cutterWorldToShape,
    const SIMDVector receiverLocalMinBounds,
    const SIMDVector receiverLocalMaxBounds,
    const bool receiverBoundsCanCull,
    const SIMDMatrix* receiverLocalToWorld
){
    CsgResolvedClipCutter resolvedCutter;
    auto shapeType = shapeRegistry.findShapeType(cutter.shapeType);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    resolvedCutter.shapeType = Move(*shapeType);
    const auto parameterBytes = ResolveCsgCutterParameterBytes(resolvedCutter.shapeType, cutter);
    if(!parameterBytes)
        return MakeUnexpected(Failure{});
    resolvedCutter.cutter = &cutter;
    resolvedCutter.worldToShape = cutterWorldToShape;

    CsgClipWorkBounds receiverBounds;
    if(receiverBoundsCanCull){
        if(const auto worldBounds = BuildCsgReceiverWorldBounds(receiverLocalMinBounds, receiverLocalMaxBounds, receiverLocalToWorld)){
            receiverBounds.minBounds = worldBounds->minBounds;
            receiverBounds.maxBounds = worldBounds->maxBounds;
            receiverBounds.valid = true;
        }
    }
    if(!resolvedCutter.workBounds.resolveCutter(
        shapeRegistry,
        resolvedCutter.shapeType,
        cutterShapeToWorld,
        parameterBytes->data(),
        parameterBytes->size(),
        receiverBounds
    ))
        return MakeUnexpected(Failure{});

    return resolvedCutter;
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
            const auto resolvedCutter = ResolveReceiverClipCutter(
                shapeRegistry,
                cutter,
                cutterTransforms.shapeToWorld,
                cutterTransforms.worldToShape,
                receiverLocalMinBounds,
                receiverLocalMaxBounds,
                receiverBoundsCanCull,
                receiverLocalToWorld
            );
            if(!resolvedCutter)
                return;

            if(!handler(*resolvedCutter))
                resolved = false;
        }
    );
    return resolved;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CsgClipWorkBounds::resolveCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgShapeTypeInfo& shapeType,
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    const usize parameterByteSize,
    const CsgClipWorkBounds& receiverBounds
){
    *this = CsgClipWorkBounds{};
    if(!receiverBounds.valid){
        const Name& shapeName = shapeType.desc.name;
        const bool finiteBuiltIn = !shapeType.desc.shaderModule
            && (shapeName == s_CsgBoxShapeName || shapeName == s_CsgSphereShapeName || shapeName == s_CsgCapsuleShapeName)
        ;
        if(!finiteBuiltIn)
            return true;
    }

    const auto cutterBounds = shapeRegistry.buildShapeBounds(shapeType.id, shapeToWorld, parameterBytes, parameterByteSize);
    if(!cutterBounds)
        return !receiverBounds.valid;
    if(!cutterBounds->finiteBounds){
        *this = receiverBounds;
        return true;
    }

    const SIMDVector cutterMinBounds = cutterBounds->minBounds;
    const SIMDVector cutterMaxBounds = cutterBounds->maxBounds;
    if(!receiverBounds.valid){
        // The cutter bounds contain every removed point even when the receiver's current pose has no CPU bounds.
        minBounds = cutterMinBounds;
        maxBounds = cutterMaxBounds;
        valid = true;
        return true;
    }
    if(!AabbTests::Intersects(receiverBounds.minBounds, receiverBounds.maxBounds, cutterMinBounds, cutterMaxBounds))
        return false;

    minBounds = VectorMax(receiverBounds.minBounds, cutterMinBounds);
    maxBounds = VectorMin(receiverBounds.maxBounds, cutterMaxBounds);
    valid = AabbTests::Valid(minBounds, maxBounds);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CsgFrameWorkRegion::expandWorldBounds(
    const SIMDMatrix& worldToClip,
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    const u32 frameWidth,
    const u32 frameHeight
){
    static constexpr f32 s_MinClipWForWorkRegion = static_cast<f32>(NWB_CSG_HOMOGENEOUS_W_EPSILON);
    static constexpr i32 s_WorkRegionPixelPadding = 2;
    static constexpr u32 s_BoxCornerCount = 8u;

    if(frameWidth == 0u || frameHeight == 0u || !AabbTests::Valid(minBounds, maxBounds)){
        expandFull();
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
    for(u32 corner = 0u; corner < s_BoxCornerCount; ++corner){
        const SIMDVector cornerSelect = VectorSelectControl(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u, 0u);
        const SIMDVector worldPosition = VectorSetW(VectorSelect(minBounds, maxBounds, cornerSelect), 1.0f);
        const SIMDVector clipPosition = Vector4Transform(worldPosition, worldToClip);
        const SIMDVector clipW = VectorSplatW(clipPosition);
        if(
            !VectorIsFinite(clipW, VectorComponentMask::s_XYZW)
            || !Vector4Greater(clipW, VectorReplicate(s_MinClipWForWorkRegion))
        ){
            expandFull();
            return;
        }

        const SIMDVector ndcPosition = VectorDivide(clipPosition, clipW);
        if(!VectorIsFinite(ndcPosition, VectorComponentMask::s_XY)){
            expandFull();
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
        if(!VectorIsFinite(pixelPosition, VectorComponentMask::s_XY)){
            expandFull();
            return;
        }
        minPixel = VectorMin(minPixel, pixelPosition);
        maxPixel = VectorMax(maxPixel, pixelPosition);
    }

    const f32 minPixelX = VectorGetX(minPixel);
    const f32 minPixelY = VectorGetY(minPixel);
    const f32 maxPixelX = VectorGetX(maxPixel);
    const f32 maxPixelY = VectorGetY(maxPixel);

    if(maxPixelX < 0.0f || maxPixelY < 0.0f || minPixelX > static_cast<f32>(frameWidth) || minPixelY > static_cast<f32>(frameHeight))
        return;

    const SIMDVector workPixelMin = VectorMax(VectorSet(minPixelX, minPixelY, 0.0f, 0.0f), VectorZero());
    const SIMDVector workFrameExtent = VectorSet(static_cast<f32>(frameWidth), static_cast<f32>(frameHeight), 0.0f, 0.0f);
    const SIMDVector workPixelMax = VectorMin(VectorSet(maxPixelX, maxPixelY, 0.0f, 0.0f), workFrameExtent);
    const SIMDVector flooredWorkMin = VectorFloor(workPixelMin);
    const SIMDVector ceiledWorkMax = VectorCeiling(workPixelMax);
    expandClamped(
        static_cast<i32>(VectorGetX(flooredWorkMin)) - s_WorkRegionPixelPadding,
        static_cast<i32>(VectorGetX(ceiledWorkMax)) + s_WorkRegionPixelPadding,
        static_cast<i32>(VectorGetY(flooredWorkMin)) - s_WorkRegionPixelPadding,
        static_cast<i32>(VectorGetY(ceiledWorkMax)) + s_WorkRegionPixelPadding,
        frameWidth,
        frameHeight
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CsgReceiverClipDrawInfo> RendererCsgSystem::resolveCsgReceiverClipDrawInfo(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform
)const{
    CsgReceiverClipDrawInfo info;
    const __hidden_csg_clip_resolve::CsgReceiverLocalSpace receiverLocalSpace =
        __hidden_csg_clip_resolve::BuildCsgReceiverLocalSpace(receiverBounds, transform)
    ;

    const bool resolved = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
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
                if(!info.evaluatorVariant)
                    info.evaluatorVariant = resolvedCutter.shapeType.desc.shaderModule;
                else if(info.evaluatorVariant != resolvedCutter.shapeType.desc.shaderModule){
                    return false;
                }
            }
            if(info.cutterCount < Limit<u32>::s_Max)
                ++info.cutterCount;
            return true;
        }
    );
    if(!resolved)
        return MakeUnexpected(Failure{});
    return info;
}

Expected<CsgReceiverRangeGpuData> RendererCsgSystem::appendCsgReceiverClipData(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform,
    const u32 frameWidth,
    const u32 frameHeight,
    CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::MeshViewGpuData* const csgWorkRegionMeshViewState
)const{
    CsgReceiverRangeGpuData range;
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    if(!receiverBounds.valid())
        return MakeUnexpected(Failure{});
    const __hidden_csg_clip_resolve::CsgReceiverLocalSpace receiverLocalSpace =
        __hidden_csg_clip_resolve::BuildCsgReceiverLocalSpace(receiverBounds, transform)
    ;

    const auto worldToReceiver = __hidden_csg_clip_resolve::BuildCsgReceiverWorldToLocal(receiverLocalSpace.localToWorldPtr());
    if(!worldToReceiver)
        return MakeUnexpected(Failure{});

    bool meshViewReady = false;
    SIMDMatrix worldToClip;
    if(csgWorkRegionMeshViewState){
        worldToClip = LoadFloat(csgWorkRegionMeshViewState->worldToClip);
        meshViewReady = !MatrixIsNaN(worldToClip) && !MatrixIsInfinite(worldToClip);
    }
    else{
        if(const auto acceptedWorldToClip = m_meshSystem.snapshotAcceptedMeshViewWorldToClip()){
            worldToClip = LoadFloat(*acceptedWorldToClip);
            meshViewReady = !MatrixIsNaN(worldToClip) && !MatrixIsInfinite(worldToClip);
        }
    }

    StoreFloat(*worldToReceiver, range.worldToReceiver);
    range.localBounds = receiverBounds;
    range.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    CsgFrameWorkRegion receiverWorkRegion;
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

            if(meshViewReady && resolvedCutter.workBounds.valid){
                receiverWorkRegion.expandWorldBounds(
                    worldToClip,
                    resolvedCutter.workBounds.minBounds,
                    resolvedCutter.workBounds.maxBounds,
                    frameWidth,
                    frameHeight
                );
            }
            else{
                receiverWorkRegion.expandFull();
            }

            csgFrameData.cutters.push_back(cutterGpuData);
            ++range.cutterCount;
            return true;
        }
    );

    const Core::Rect receiverRect = receiverWorkRegion.resolveRect(frameWidth, frameHeight);
    range.screenWorkRect = { { {
        static_cast<u32>(receiverRect.minX), static_cast<u32>(receiverRect.minY),
        static_cast<u32>(receiverRect.maxX), static_cast<u32>(receiverRect.maxY)
    } } };
    if(receiverWorkRegion.fullFrame)
        csgFrameData.workRegion.expandFull();
    else if(receiverWorkRegion.bounded()){
        csgFrameData.workRegion.expandClamped(
            receiverRect.minX, receiverRect.maxX, receiverRect.minY, receiverRect.maxY, frameWidth, frameHeight
        );
    }
    if(!appended || range.cutterCount == 0u)
        return MakeUnexpected(Failure{});
    return range;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

