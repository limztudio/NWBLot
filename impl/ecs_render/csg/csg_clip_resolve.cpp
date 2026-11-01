// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/components.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_clip_resolve{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgResolvedClipCutter{
    SIMDMatrix worldToShape;
    CsgClipWorkBounds workBounds;
    CsgShapeTypeInfo shapeType;
    const CsgCutterComponent& cutter;


public:
    [[nodiscard]] const u8* parameterBytes()const noexcept{
        return cutter.parameterBytes.empty() ? (shapeType.desc.defaultParameterBytes.empty() ? nullptr : shapeType.desc.defaultParameterBytes.data()) : cutter.parameterBytes.data();
    }

    [[nodiscard]] usize parameterByteSize()const noexcept{
        return cutter.parameterBytes.empty() ? shapeType.desc.defaultParameterBytes.size() : cutter.parameterBytes.size();
    }
};

struct CsgReceiverLocalSpace{
    SIMDVectorSingle localMinBounds = VectorZero();
    SIMDVectorSingle localMaxBounds = VectorZero();
    SIMDMatrix localToWorld = MatrixIdentity();
    b8 boundsCanCull = false;
    b8 hasLocalToWorld = false;


public:
    [[nodiscard]] const SIMDMatrix* localToWorldPtr()const noexcept{ return hasLocalToWorld ? &localToWorld : nullptr; }
};

struct CsgCutterTransforms{
    SIMDMatrix shapeToWorld;
    SIMDMatrix worldToShape;
};

[[nodiscard]] Expected<SIMDMatrix> BuildCsgReceiverWorldToLocal(const SIMDMatrix* localToWorld)noexcept{
    if(!localToWorld)
        return MatrixIdentity();

    SIMDVectorSingle determinant;
    const SIMDMatrix worldToLocal = MatrixInverse(&determinant, *localToWorld);
    if(!VectorIsFinite(determinant, VectorComponentMask::s_XYZW) || !Vector4Greater(VectorAbs(determinant), VectorZero()))
        return MakeUnexpected(Failure{});
    return worldToLocal;
}

[[nodiscard]] Expected<AabbTests::Bounds> BuildCsgReceiverWorldBounds(const SIMDVectorSingle localMinBounds, const SIMDVectorSingle localMaxBounds, const SIMDMatrix* localToWorld)noexcept{
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return MakeUnexpected(Failure{});
    if(!localToWorld)
        return AabbTests::Bounds{ localMinBounds, localMaxBounds };

    return AabbTests::Transform(*localToWorld, localMinBounds, localMaxBounds);
}

[[nodiscard]] static CsgReceiverLocalSpace BuildCsgReceiverLocalSpace(const b8 boundsCanCull, const SIMDVectorSingle localMinBounds, const SIMDVectorSingle localMaxBounds, const SIMDMatrix* localToWorld)noexcept{
    CsgReceiverLocalSpace localSpace;
    localSpace.boundsCanCull = boundsCanCull;
    localSpace.localMinBounds = localMinBounds;
    localSpace.localMaxBounds = localMaxBounds;
    if(localToWorld){
        localSpace.localToWorld = *localToWorld;
        localSpace.hasLocalToWorld = true;
    }
    return localSpace;
}

[[nodiscard]] static CsgReceiverLocalSpace LoadCsgReceiverLocalSpace(const CsgReceiverCpuBounds& receiverBounds, const Scene::TransformComponent* transform)noexcept{
    const b8 boundsCanCull = CsgReceiverBoundsCanCull(receiverBounds);
    const SIMDVectorSingle localMinBounds = boundsCanCull ? LoadFloatInt(receiverBounds.minBounds) : VectorZero();
    const SIMDVectorSingle localMaxBounds = boundsCanCull ? LoadFloatInt(receiverBounds.maxBounds) : VectorZero();
    SIMDMatrix localToWorld;
    const SIMDMatrix* localToWorldPtr = nullptr;
    if(transform){
        localToWorld = MatrixAffineTransformation(LoadFloat(transform->scale), VectorZero(), LoadFloat(transform->rotation), LoadFloat(transform->position));
        localToWorldPtr = &localToWorld;
    }
    return BuildCsgReceiverLocalSpace(boundsCanCull, localMinBounds, localMaxBounds, localToWorldPtr);
}

static void BuildResolvedClipCutterGpuData(const CsgResolvedClipCutter& resolvedCutter, const f32 worldToShapeScaleBound, CsgCutterGpuData& outCutter)noexcept{
    NWB_ASSERT(resolvedCutter.parameterByteSize() == static_cast<usize>(resolvedCutter.shapeType.desc.parameterByteSize));
    NWB_ASSERT(resolvedCutter.parameterByteSize() == 0u || resolvedCutter.parameterBytes());

    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    outCutter.shapeType = resolvedCutter.shapeType.id;
    CopyCsgCutterInlineParameters(Span<const u8>(resolvedCutter.parameterBytes(), resolvedCutter.parameterByteSize()), outCutter);
}

[[nodiscard]] static Expected<CsgResolvedClipCutter> ResolveReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    const SIMDMatrix& cutterShapeToWorld,
    const SIMDMatrix& cutterWorldToShape,
    const SIMDVectorSingle receiverLocalMinBounds,
    const SIMDVectorSingle receiverLocalMaxBounds,
    const b8 receiverBoundsCanCull,
    const SIMDMatrix* receiverLocalToWorld
){
    auto shapeType = shapeRegistry.findShapeType(cutter.shapeType);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    CsgResolvedClipCutter resolvedCutter{ .worldToShape = cutterWorldToShape, .workBounds = {}, .shapeType = Move(*shapeType), .cutter = cutter };
    const auto parameterBytes = ResolveCsgCutterParameterBytes(resolvedCutter.shapeType, cutter);
    if(!parameterBytes)
        return MakeUnexpected(Failure{});

    CsgClipWorkBounds receiverBounds;
    if(receiverBoundsCanCull){
        if(const auto worldBounds = BuildCsgReceiverWorldBounds(receiverLocalMinBounds, receiverLocalMaxBounds, receiverLocalToWorld)){
            receiverBounds.minBounds = worldBounds->minBounds;
            receiverBounds.maxBounds = worldBounds->maxBounds;
            receiverBounds.valid = true;
        }
    }
    if(!resolvedCutter.workBounds.resolveCutter(shapeRegistry, resolvedCutter.shapeType, cutterShapeToWorld, *parameterBytes, receiverBounds))
        return MakeUnexpected(Failure{});

    return resolvedCutter;
}

template<typename CutterTransformLoader, typename CutterHandler>
[[nodiscard]] static b8 ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const SIMDVectorSingle receiverLocalMinBounds,
    const SIMDVectorSingle receiverLocalMaxBounds,
    const b8 receiverBoundsCanCull,
    const SIMDMatrix* receiverLocalToWorld,
    CutterTransformLoader&& loadCutterTransforms,
    CutterHandler&& handler
){
    b8 resolved = true;
    receiverLookup.forEachReceiverCutter(receiverDrawState, [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
        if(!resolved)
            return;

        const CsgCutterTransforms cutterTransforms = loadCutterTransforms(cutter);
        const auto resolvedCutter = ResolveReceiverClipCutter(shapeRegistry, cutter, cutterTransforms.shapeToWorld, cutterTransforms.worldToShape, receiverLocalMinBounds, receiverLocalMaxBounds, receiverBoundsCanCull, receiverLocalToWorld);
        if(!resolvedCutter)
            return;

        if(!handler(*resolvedCutter))
            resolved = false;
    });
    return resolved;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<BinaryByteView> ResolveCsgCutterParameterBytes(const CsgShapeTypeInfo& shapeType, const CsgCutterComponent& cutter)noexcept{
    const u8* bytes = cutter.parameterBytes.empty() ? (shapeType.desc.defaultParameterBytes.empty() ? nullptr : shapeType.desc.defaultParameterBytes.data()) : cutter.parameterBytes.data();
    const usize byteSize = cutter.parameterBytes.empty() ? shapeType.desc.defaultParameterBytes.size() : cutter.parameterBytes.size();
    if(byteSize != static_cast<usize>(shapeType.desc.parameterByteSize) || (byteSize != 0u && !bytes))
        return MakeUnexpected(Failure{});
    return BinaryByteView(bytes, byteSize);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgClipWorkBounds::resolveCutter(const CsgShapeRegistry& shapeRegistry, const CsgShapeTypeInfo& shapeType, const SIMDMatrix& shapeToWorld, const BinaryByteView parameterBytes, const CsgClipWorkBounds& receiverBounds){
    *this = CsgClipWorkBounds{};
    if(!receiverBounds.valid){
        const Name& shapeName = shapeType.desc.name;
        const b8 finiteBuiltIn = !shapeType.desc.shaderModule && (shapeName == s_CsgBoxShapeName || shapeName == s_CsgSphereShapeName || shapeName == s_CsgCapsuleShapeName);
        if(!finiteBuiltIn)
            return true;
    }

    const auto cutterBounds = shapeRegistry.buildShapeBounds(shapeType.id, shapeToWorld, parameterBytes);
    if(!cutterBounds)
        return !receiverBounds.valid;
    if(!cutterBounds->finiteBounds){
        *this = receiverBounds;
        return true;
    }

    const SIMDVectorSingle cutterMinBounds = cutterBounds->minBounds;
    const SIMDVectorSingle cutterMaxBounds = cutterBounds->maxBounds;
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


void CsgFrameGpuData::reserve(const usize receiverCapacity, const usize cutterCapacity){
    receiverRanges.reserve(receiverCapacity);
    cutters.reserve(cutterCapacity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CsgReceiverClipDrawInfo> RendererCsgSystem::resolveCsgReceiverClipDrawInfo(
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverCpuBounds& receiverBounds,
    const Scene::TransformComponent* transform
)const{
    CsgReceiverClipDrawInfo info;
    const __hidden_csg_clip_resolve::CsgReceiverLocalSpace receiverLocalSpace = __hidden_csg_clip_resolve::LoadCsgReceiverLocalSpace(receiverBounds, transform);

    const b8 resolved = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverLocalSpace.localMinBounds,
        receiverLocalSpace.localMaxBounds,
        receiverLocalSpace.boundsCanCull,
        receiverLocalSpace.localToWorldPtr(),
        [](const CsgCutterComponent& cutter)noexcept{
            return __hidden_csg_clip_resolve::CsgCutterTransforms{ LoadFloat(cutter.shapeToWorld), LoadFloat(cutter.worldToShape) };
        },
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter)noexcept{
            if(resolvedCutter.shapeType.desc.shaderModule){
                if(!info.evaluatorVariant)
                    info.evaluatorVariant = resolvedCutter.shapeType.desc.shaderModule;
                else if(info.evaluatorVariant != resolvedCutter.shapeType.desc.shaderModule)
                    return false;
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
    const MeshViewGpuData* const csgWorkRegionMeshViewState,
    CsgFrameGpuData& csgFrameData
)const{
    CsgReceiverRangeGpuData range;
    if(csgFrameData.cutters.size() > static_cast<usize>(Limit<u32>::s_Max))
        return MakeUnexpected(Failure{});

    if(!receiverBounds.valid())
        return MakeUnexpected(Failure{});
    const __hidden_csg_clip_resolve::CsgReceiverLocalSpace receiverLocalSpace = __hidden_csg_clip_resolve::LoadCsgReceiverLocalSpace(receiverBounds, transform);

    const auto worldToReceiver = __hidden_csg_clip_resolve::BuildCsgReceiverWorldToLocal(receiverLocalSpace.localToWorldPtr());
    if(!worldToReceiver)
        return MakeUnexpected(Failure{});

    b8 meshViewReady = false;
    Float44 worldToClip{};
    if(csgWorkRegionMeshViewState){
        worldToClip = csgWorkRegionMeshViewState->worldToClip;
        const SIMDMatrix worldToClipSimd = LoadFloat(worldToClip);
        meshViewReady = !MatrixIsNaN(worldToClipSimd) && !MatrixIsInfinite(worldToClipSimd);
    }
    else{
        if(const auto acceptedWorldToClip = m_meshSystem.snapshotAcceptedMeshViewWorldToClip()){
            worldToClip = *acceptedWorldToClip;
            const SIMDMatrix worldToClipSimd = LoadFloat(worldToClip);
            meshViewReady = !MatrixIsNaN(worldToClipSimd) && !MatrixIsInfinite(worldToClipSimd);
        }
    }

    StoreFloat(*worldToReceiver, range.worldToReceiver);
    range.localBounds = receiverBounds;
    range.firstCutter = static_cast<u32>(csgFrameData.cutters.size());
    CsgFrameWorkRegion receiverWorkRegion;
    const b8 appended = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverLocalSpace.localMinBounds,
        receiverLocalSpace.localMaxBounds,
        receiverLocalSpace.boundsCanCull,
        receiverLocalSpace.localToWorldPtr(),
        [](const CsgCutterComponent& cutter)noexcept{
            return __hidden_csg_clip_resolve::CsgCutterTransforms{ LoadFloat(cutter.shapeToWorld), LoadFloat(cutter.worldToShape) };
        },
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter){
            CsgCutterGpuData cutterGpuData;
            if(csgFrameData.cutters.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            const SIMDMatrix worldToShape = resolvedCutter.worldToShape;
            cutterGpuData = CsgCutterGpuData{};
            StoreFloat(worldToShape, cutterGpuData.worldToShape);
            __hidden_csg_clip_resolve::BuildResolvedClipCutterGpuData(resolvedCutter, ComputeWorldToShapeScaleBound(cutterGpuData.worldToShape), cutterGpuData);

            if(meshViewReady && resolvedCutter.workBounds.valid){
                Float4 minBounds;
                Float4 maxBounds;
                StoreFloat(resolvedCutter.workBounds.minBounds, minBounds);
                StoreFloat(resolvedCutter.workBounds.maxBounds, maxBounds);
                receiverWorkRegion.expandWorldBounds(worldToClip, minBounds, maxBounds, frameWidth, frameHeight);
            }
            else{
                receiverWorkRegion.expandFull();
            }

            csgFrameData.cutters.push_back(cutterGpuData);
            ++range.cutterCount;
            return true;
        }
    );

    const CsgWorkRect receiverRect = receiverWorkRegion.resolveRect(frameWidth, frameHeight);
    range.screenWorkRect = UInt4(static_cast<u32>(receiverRect.minX), static_cast<u32>(receiverRect.minY), static_cast<u32>(receiverRect.maxX), static_cast<u32>(receiverRect.maxY));
    if(receiverWorkRegion.fullFrame)
        csgFrameData.workRegion.expandFull();
    else if(receiverWorkRegion.bounded())
        csgFrameData.workRegion.expandClamped(receiverRect.minX, receiverRect.maxX, receiverRect.minY, receiverRect.maxY, frameWidth, frameHeight);
    if(!appended || range.cutterCount == 0u)
        return MakeUnexpected(Failure{});
    return range;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

