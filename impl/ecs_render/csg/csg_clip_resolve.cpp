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
    Float34 worldToShape = {};
    CsgClipWorkBounds workBounds;
    CsgShapeTypeInfo shapeType;
    Span<const u8> parameterBytes;
};

[[nodiscard]] static CsgClipWorkBounds LoadCsgReceiverWorldBounds(const CsgReceiverCpuBounds& receiverBounds, const Scene::TransformComponent* const transform)noexcept{
    return LoadCsgReceiverWorldBoundsFromTransform(
        receiverBounds,
        transform != nullptr,
        transform ? LoadFloat(transform->position) : VectorZero(),
        transform ? LoadFloat(transform->rotation) : VectorZero(),
        transform ? LoadFloat(transform->scale) : VectorZero()
    );
}

[[nodiscard]] static Expected<CsgResolvedClipCutter> ResolveReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgCutterComponent& cutter,
    const CsgClipWorkBounds& receiverWorldBounds
){
    auto shapeType = shapeRegistry.findShapeType(cutter.shapeType);
    if(!shapeType)
        return MakeUnexpected(Failure{});
    const auto parameterBytes = TryResolveCsgCutterParameterBytes(shapeType->desc.parameterByteSize, shapeType->desc.defaultParameterBytes, cutter.parameterBytes);
    if(!parameterBytes)
        return MakeUnexpected(Failure{});
    CsgResolvedClipCutter resolvedCutter{
        .worldToShape = cutter.worldToShape,
        .workBounds = {},
        .shapeType = Move(*shapeType),
        .parameterBytes = *parameterBytes,
    };

    if(!ResolveCsgClipWorkBounds(shapeRegistry, resolvedCutter.shapeType, LoadFloat(cutter.shapeToWorld), resolvedCutter.parameterBytes, receiverWorldBounds, resolvedCutter.workBounds))
        return MakeUnexpected(Failure{});

    return resolvedCutter;
}

template<typename CutterHandler>
[[nodiscard]] static b8 ForEachReceiverClipCutter(
    const CsgShapeRegistry& shapeRegistry,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgReceiverDrawState& receiverDrawState,
    const CsgClipWorkBounds& receiverWorldBounds,
    CutterHandler&& handler
){
    b8 resolved = true;
    receiverLookup.forEachReceiverCutter(receiverDrawState, [&](const Core::ECS::EntityID, const CsgCutterComponent& cutter){
        if(!resolved)
            return;

        const auto resolvedCutter = ResolveReceiverClipCutter(
            shapeRegistry,
            cutter,
            receiverWorldBounds
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


b8 ResolveCsgClipWorkBounds(const CsgShapeRegistry& shapeRegistry, const CsgShapeTypeInfo& shapeType, const SIMDMatrix& shapeToWorld, const Span<const u8> parameterBytes, const CsgClipWorkBounds& receiverBounds, CsgClipWorkBounds& outWorkBounds){
    outWorkBounds = CsgClipWorkBounds{};
    if(!CsgClipCutterRequiresShapeBounds(receiverBounds.valid, static_cast<b8>(shapeType.desc.shaderModule), shapeType.desc.name))
        return true;

    const auto cutterBounds = shapeRegistry.buildShapeBounds(shapeType.id, shapeToWorld, parameterBytes);
    if(!cutterBounds){
        const Expected<CsgClipWorkBounds> resolved = ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, false, false, VectorZero(), VectorZero());
        if(!resolved)
            return false;
        outWorkBounds = *resolved;
        return true;
    }

    const Expected<CsgClipWorkBounds> resolved = ResolveCsgClipWorkBoundsFromCutterBounds(receiverBounds, true, cutterBounds->finiteBounds, cutterBounds->minBounds, cutterBounds->maxBounds);
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
    const CsgClipWorkBounds receiverWorldBounds = __hidden_csg_clip_resolve::LoadCsgReceiverWorldBounds(receiverBounds, transform);

    const b8 resolved = __hidden_csg_clip_resolve::ForEachReceiverClipCutter(
        m_csgShapeRegistry,
        receiverLookup,
        receiverDrawState,
        receiverWorldBounds,
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter)noexcept{
            return TryAccumulateCsgReceiverClipDrawInfo(resolvedCutter.shapeType.desc.shaderModule, info);
        });
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
    const CsgClipWorkBounds receiverWorldBounds = __hidden_csg_clip_resolve::LoadCsgReceiverWorldBounds(receiverBounds, transform);

    const auto worldToReceiver = TryBuildCsgReceiverWorldToLocal(
        transform != nullptr,
        transform ? LoadFloat(transform->position) : VectorZero(),
        transform ? LoadFloat(transform->rotation) : VectorZero(),
        transform ? LoadFloat(transform->scale) : VectorZero()
    );
    if(!worldToReceiver)
        return MakeUnexpected(Failure{});

    b8 meshViewReady = false;
    SIMDMatrix worldToClipSimd{};
    if(csgWorkRegionMeshViewState){
        worldToClipSimd = LoadFloat(csgWorkRegionMeshViewState->worldToClip);
        meshViewReady = !MatrixIsNaN(worldToClipSimd) && !MatrixIsInfinite(worldToClipSimd);
    }
    else{
        if(const auto acceptedWorldToClip = m_meshSystem.snapshotAcceptedMeshViewWorldToClip()){
            worldToClipSimd = LoadFloat(*acceptedWorldToClip);
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
        receiverWorldBounds,
        [&](const __hidden_csg_clip_resolve::CsgResolvedClipCutter& resolvedCutter){
            if(csgFrameData.cutters.size() >= static_cast<usize>(Limit<u32>::s_Max))
                return false;
            NWB_ASSERT(resolvedCutter.parameterBytes.size() == static_cast<usize>(resolvedCutter.shapeType.desc.parameterByteSize));
            NWB_ASSERT(resolvedCutter.parameterBytes.empty() || resolvedCutter.parameterBytes.data());
            const CsgCutterGpuData cutterGpuData = BuildCsgReceiverCutterGpuDataAndExpandWorkRegion(
                resolvedCutter.shapeType.id,
                LoadFloat(resolvedCutter.worldToShape),
                resolvedCutter.parameterBytes,
                resolvedCutter.workBounds,
                meshViewReady,
                worldToClipSimd,
                frameWidth,
                frameHeight,
                receiverWorkRegion
            );
            csgFrameData.cutters.push_back(cutterGpuData);
            ++range.cutterCount;
            return true;
        });

    FinalizeCsgReceiverRangeWorkRegion(receiverWorkRegion, frameWidth, frameHeight, range, csgFrameData.workRegion);
    if(!appended || range.cutterCount == 0u)
        return MakeUnexpected(Failure{});
    return range;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

