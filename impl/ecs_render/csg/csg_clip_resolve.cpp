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

[[nodiscard]] Expected<SIMDMatrix> BuildCsgReceiverWorldToLocal(const CsgReceiverLocalSpace& localSpace)noexcept{
    if(!localSpace.hasLocalToWorld)
        return MatrixIdentity();

    SIMDVectorSingle determinant;
    const SIMDMatrix worldToLocal = MatrixInverse(&determinant, LoadFloat(localSpace.localToWorld));
    if(!VectorIsFinite(determinant, VectorComponentMask::s_XYZW) || !Vector4Greater(VectorAbs(determinant), VectorZero()))
        return MakeUnexpected(Failure{});
    return worldToLocal;
}

[[nodiscard]] Expected<AabbTests::Bounds> BuildCsgReceiverWorldBounds(const CsgReceiverLocalSpace& localSpace)noexcept{
    const SIMDVectorSingle localMinBounds = LoadFloat(localSpace.localMinBounds);
    const SIMDVectorSingle localMaxBounds = LoadFloat(localSpace.localMaxBounds);
    if(!AabbTests::Valid(localMinBounds, localMaxBounds))
        return MakeUnexpected(Failure{});
    if(!localSpace.hasLocalToWorld)
        return AabbTests::Bounds{ localMinBounds, localMaxBounds };

    return AabbTests::Transform(LoadFloat(localSpace.localToWorld), localMinBounds, localMaxBounds);
}

[[nodiscard]] static CsgReceiverLocalSpace LoadCsgReceiverLocalSpace(const CsgReceiverCpuBounds& receiverBounds, const Scene::TransformComponent* transform)noexcept{
    const b8 boundsCanCull = CsgReceiverBoundsCanCull(receiverBounds);
    Float4 localMinBounds(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 localMaxBounds(0.0f, 0.0f, 0.0f, 0.0f);
    if(boundsCanCull){
        StoreFloat(LoadFloatInt(receiverBounds.minBounds), localMinBounds);
        StoreFloat(LoadFloatInt(receiverBounds.maxBounds), localMaxBounds);
    }
    Float44 localToWorld{};
    b8 hasLocalToWorld = false;
    if(transform){
        StoreFloat(MatrixAffineTransformation(LoadFloat(transform->scale), VectorZero(), LoadFloat(transform->rotation), LoadFloat(transform->position)), localToWorld);
        hasLocalToWorld = true;
    }
    return BuildCsgReceiverLocalSpace(boundsCanCull, localMinBounds, localMaxBounds, hasLocalToWorld, localToWorld);
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
    const CsgReceiverLocalSpace& receiverLocalSpace
){
    auto shapeType = shapeRegistry.findShapeType(cutter.shapeType);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    CsgResolvedClipCutter resolvedCutter{ .worldToShape = cutterWorldToShape, .workBounds = {}, .shapeType = Move(*shapeType), .cutter = cutter };
    const auto parameterBytes = ResolveCsgCutterParameterBytes(resolvedCutter.shapeType, cutter);
    if(!parameterBytes)
        return MakeUnexpected(Failure{});

    CsgClipWorkBounds receiverBounds;
    if(receiverLocalSpace.boundsCanCull){
        if(const auto worldBounds = BuildCsgReceiverWorldBounds(receiverLocalSpace)){
            StoreFloat(worldBounds->minBounds, receiverBounds.minBounds);
            StoreFloat(worldBounds->maxBounds, receiverBounds.maxBounds);
            receiverBounds.valid = true;
        }
    }
    if(!ResolveCsgClipWorkBounds(shapeRegistry, resolvedCutter.shapeType, cutterShapeToWorld, *parameterBytes, receiverBounds, resolvedCutter.workBounds))
        return MakeUnexpected(Failure{});

    return resolvedCutter;
}

template<typename CutterTransformLoader, typename CutterHandler>
[[nodiscard]] static b8 ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverLocalSpace& receiverLocalSpace,
    CutterTransformLoader&& loadCutterTransforms,
    CutterHandler&& handler
){
    b8 resolved = true;
    receiverLookup.forEachReceiverCutter(receiverDrawState, [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
        if(!resolved)
            return;

        const CsgCutterTransforms cutterTransforms = loadCutterTransforms(cutter);
        const auto resolvedCutter = ResolveReceiverClipCutter(
            shapeRegistry,
            cutter,
            LoadFloat(cutterTransforms.shapeToWorld),
            LoadFloat(cutterTransforms.worldToShape),
            receiverLocalSpace
        );
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


b8 ResolveCsgClipWorkBounds(const CsgShapeRegistry& shapeRegistry, const CsgShapeTypeInfo& shapeType, const SIMDMatrix& shapeToWorld, const BinaryByteView parameterBytes, const CsgClipWorkBounds& receiverBounds, CsgClipWorkBounds& outWorkBounds){
    outWorkBounds = CsgClipWorkBounds{};
    if(!CsgClipCutterRequiresShapeBounds(receiverBounds.valid, static_cast<b8>(shapeType.desc.shaderModule), shapeType.desc.name))
        return true;

    const auto cutterBounds = shapeRegistry.buildShapeBounds(shapeType.id, shapeToWorld, parameterBytes);
    if(!cutterBounds)
        return ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, false, false, Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), outWorkBounds);

    Float4 cutterMinBounds(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 cutterMaxBounds(0.0f, 0.0f, 0.0f, 0.0f);
    StoreFloat(cutterBounds->minBounds, cutterMinBounds);
    StoreFloat(cutterBounds->maxBounds, cutterMaxBounds);
    return ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, true, cutterBounds->finiteBounds, cutterMinBounds, cutterMaxBounds, outWorkBounds);
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
    const CsgReceiverLocalSpace receiverLocalSpace = __hidden_csg_clip_resolve::LoadCsgReceiverLocalSpace(receiverBounds, transform);

    const b8 resolved = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverLocalSpace,
        [](const CsgCutterComponent& cutter)noexcept{
            CsgCutterTransforms transforms;
            transforms.shapeToWorld = cutter.shapeToWorld;
            transforms.worldToShape = cutter.worldToShape;
            return transforms;
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
    const CsgReceiverLocalSpace receiverLocalSpace = __hidden_csg_clip_resolve::LoadCsgReceiverLocalSpace(receiverBounds, transform);

    const auto worldToReceiver = __hidden_csg_clip_resolve::BuildCsgReceiverWorldToLocal(receiverLocalSpace);
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
        receiverLocalSpace,
        [](const CsgCutterComponent& cutter)noexcept{
            CsgCutterTransforms transforms;
            transforms.shapeToWorld = cutter.shapeToWorld;
            transforms.worldToShape = cutter.worldToShape;
            return transforms;
        },
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter){
            CsgCutterGpuData cutterGpuData;
            if(csgFrameData.cutters.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            const SIMDMatrix worldToShape = resolvedCutter.worldToShape;
            cutterGpuData = CsgCutterGpuData{};
            StoreFloat(worldToShape, cutterGpuData.worldToShape);
            __hidden_csg_clip_resolve::BuildResolvedClipCutterGpuData(resolvedCutter, ComputeWorldToShapeScaleBound(cutterGpuData.worldToShape), cutterGpuData);

            if(meshViewReady && resolvedCutter.workBounds.valid)
                receiverWorkRegion.expandWorldBounds(worldToClip, resolvedCutter.workBounds.minBounds, resolvedCutter.workBounds.maxBounds, frameWidth, frameHeight);
            else
                receiverWorkRegion.expandFull();

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

