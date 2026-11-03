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
    BinaryByteView parameterBytes;
};

struct CsgReceiverLocalSpace{
    SIMDMatrix localToWorld = MatrixIdentity();
    CsgClipWorkBounds receiverBounds;
    b8 hasLocalToWorld = false;
};

[[nodiscard]] Expected<SIMDMatrix> BuildCsgReceiverWorldToLocal(const CsgReceiverLocalSpace& localSpace)noexcept{
    if(!localSpace.hasLocalToWorld)
        return MatrixIdentity();

    SIMDVectorSingle determinant;
    const SIMDMatrix worldToLocal = MatrixInverse(&determinant, localSpace.localToWorld);
    if(!VectorIsFinite(determinant, VectorComponentMask::s_XYZW) || !Vector4Greater(VectorAbs(determinant), VectorZero()))
        return MakeUnexpected(Failure{});
    return worldToLocal;
}

[[nodiscard]] static CsgReceiverLocalSpace LoadCsgReceiverLocalSpace(const CsgReceiverCpuBounds& receiverBounds, const Scene::TransformComponent* transform)noexcept{
    CsgReceiverLocalSpace localSpace;
    if(transform){
        localSpace.localToWorld = MatrixAffineTransformation(LoadFloat(transform->scale), VectorZero(), LoadFloat(transform->rotation), LoadFloat(transform->position));
        localSpace.hasLocalToWorld = true;
    }
    if(CsgReceiverBoundsCanCull(receiverBounds)){
        const SIMDVectorSingle localMinBounds = LoadFloatInt(receiverBounds.minBounds);
        const SIMDVectorSingle localMaxBounds = LoadFloatInt(receiverBounds.maxBounds);
        if(AabbTests::Valid(localMinBounds, localMaxBounds)){
            if(!localSpace.hasLocalToWorld){
                StoreFloat(localMinBounds, localSpace.receiverBounds.minBounds);
                StoreFloat(localMaxBounds, localSpace.receiverBounds.maxBounds);
                localSpace.receiverBounds.valid = true;
            }
            else if(const auto worldBounds = AabbTests::Transform(localSpace.localToWorld, localMinBounds, localMaxBounds)){
                StoreFloat(worldBounds->minBounds, localSpace.receiverBounds.minBounds);
                StoreFloat(worldBounds->maxBounds, localSpace.receiverBounds.maxBounds);
                localSpace.receiverBounds.valid = true;
            }
        }
    }
    return localSpace;
}

static void BuildResolvedClipCutterGpuData(const CsgResolvedClipCutter& resolvedCutter, const f32 worldToShapeScaleBound, CsgCutterGpuData& outCutter)noexcept{
    NWB_ASSERT(resolvedCutter.parameterBytes.size() == static_cast<usize>(resolvedCutter.shapeType.desc.parameterByteSize));
    NWB_ASSERT(resolvedCutter.parameterBytes.empty() || resolvedCutter.parameterBytes.data());

    if(IsFinite(worldToShapeScaleBound) && worldToShapeScaleBound > 0.0f)
        outCutter.worldToShapeScaleBound = worldToShapeScaleBound;

    outCutter.shapeType = resolvedCutter.shapeType.id;
    CopyCsgCutterInlineParameters(resolvedCutter.parameterBytes, outCutter);
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
    const auto parameterBytes = ResolveCsgCutterParameterBytes(*shapeType, cutter);
    if(!parameterBytes)
        return MakeUnexpected(Failure{});
    CsgResolvedClipCutter resolvedCutter{
        .worldToShape = cutterWorldToShape,
        .workBounds = {},
        .shapeType = Move(*shapeType),
        .parameterBytes = *parameterBytes,
    };

    if(!ResolveCsgClipWorkBounds(shapeRegistry, resolvedCutter.shapeType, cutterShapeToWorld, resolvedCutter.parameterBytes, receiverLocalSpace.receiverBounds, resolvedCutter.workBounds))
        return MakeUnexpected(Failure{});

    return resolvedCutter;
}

template<typename CutterHandler>
[[nodiscard]] static b8 ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgReceiverLocalSpace& receiverLocalSpace,
    CutterHandler&& handler
){
    b8 resolved = true;
    receiverLookup.forEachReceiverCutter(receiverDrawState, [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
        if(!resolved)
            return;

        const auto resolvedCutter = ResolveReceiverClipCutter(
            shapeRegistry,
            cutter,
            LoadFloat(cutter.shapeToWorld),
            LoadFloat(cutter.worldToShape),
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
    if(!cutterBounds){
        const Expected<CsgClipWorkBounds> resolved = ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, false, false, Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f));
        if(!resolved)
            return false;
        outWorkBounds = *resolved;
        return true;
    }

    Float4 cutterMinBounds(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 cutterMaxBounds(0.0f, 0.0f, 0.0f, 0.0f);
    StoreFloat(cutterBounds->minBounds, cutterMinBounds);
    StoreFloat(cutterBounds->maxBounds, cutterMaxBounds);
    const Expected<CsgClipWorkBounds> resolved = ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, true, cutterBounds->finiteBounds, cutterMinBounds, cutterMaxBounds);
    if(!resolved)
        return false;
    outWorkBounds = *resolved;
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
        receiverLocalSpace,
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

