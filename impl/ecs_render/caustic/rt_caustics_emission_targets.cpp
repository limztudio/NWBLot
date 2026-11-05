// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 ResolveCausticLights(const Span<const f32> causticImportance, const u32 refractiveInstanceCount, Span<SceneLightGpuData> lights)noexcept{
    return AssignCausticLightSlots(causticImportance, refractiveInstanceCount, lights);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::releaseCausticEmissionTargetHeapHandle(){
    if(!m_rayTracingState.m_causticEmissionTargetHeapHandle.valid())
        return;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized())
        heap.free(m_rayTracingState.m_causticEmissionTargetHeapHandle);
    m_rayTracingState.m_causticEmissionTargetHeapHandle = Core::GpuDescriptorHandle::Invalid();
}

b8 RendererRayTracingSystem::createCausticTargets(DeferredFrameTargets& targets){
    // Full-res additive targets with half-res temporal resolve ping-pong.
    targets.causticIrradianceFormat = Core::Format::RGBA16_FLOAT;
    targets.causticAccumulatorFormat = Core::Format::R32_UINT;
    targets.causticHistoryFormat = Core::Format::RGBA16_FLOAT;

    // Fresh accumulators reseed the temporal EMA.
    m_rayTracingState.m_causticAccumulatorInitialized = false;

    const u32 halfWidth = DivideUpWrapping(targets.width, 2u);
    const u32 halfHeight = DivideUpWrapping(targets.height, 2u);

    Core::TextureDesc irradianceDesc;
    irradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setIsUAV(true)
        // Graphics/async production and the graph-owned lagged-history transfer copy share this target concurrently.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setName("engine/caustic/irradiance")
    ;
    auto causticIrradianceResult = m_graphics.createTexture(irradianceDesc);
    if(causticIrradianceResult)
        targets.causticIrradiance = Move(*causticIrradianceResult);
    else
        targets.causticIrradiance.reset();
    if(!causticIrradianceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic irradiance target"));
        return false;
    }

    Core::TextureDesc accumulatorDesc;
    accumulatorDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(s_CausticAccumulatorChannelCount)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.causticAccumulatorFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/accumulator")
    ;
    auto causticAccumulatorResult = m_graphics.createTexture(accumulatorDesc);
    if(causticAccumulatorResult)
        targets.causticAccumulator = Move(*causticAccumulatorResult);
    else
        targets.causticAccumulator.reset();
    if(!causticAccumulatorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator target"));
        return false;
    }

    Core::TextureDesc historyDesc;
    historyDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/atrous_half_a");
    auto causticHistoryResult = m_graphics.createTexture(historyDesc);
    if(causticHistoryResult)
        targets.causticHistory = Move(*causticHistoryResult);
    else
        targets.causticHistory.reset();
    if(!causticHistoryResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-A target"));
        return false;
    }

    Core::TextureDesc halfBDesc;
    halfBDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/atrous_half_b");
    auto causticResolveHalfResult = m_graphics.createTexture(halfBDesc);
    if(causticResolveHalfResult)
        targets.causticResolveHalf = Move(*causticResolveHalfResult);
    else
        targets.causticResolveHalf.reset();
    if(!causticResolveHalfResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-B target"));
        return false;
    }

    // Half-resolution geometry cache for edge-aware resolve.
    Core::TextureDesc geometryDesc;
    geometryDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/resolve_geometry");
    auto causticResolveGeometryResult = m_graphics.createTexture(geometryDesc);
    if(causticResolveGeometryResult)
        targets.causticResolveGeometry = Move(*causticResolveGeometryResult);
    else
        targets.causticResolveGeometry.reset();
    if(!causticResolveGeometryResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve geometry cache target"));
        return false;
    }
    if(!prepareCausticResolveActivity(halfWidth, halfHeight))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic activity buffers unavailable; retaining full wavelet filtering"));
    return true;
}

Expected<Optional<Core::GpuUploadBlobId>> RendererRayTracingSystem::retainPreparedCausticEmissionTargetUpload(Core::GpuTaskGraph& graph)const{
    const u32 targetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
    if(targetCount == 0u){
        if(!m_preparedCausticEmissionTargetBytes.empty())
            return MakeUnexpected(Failure{});
        return Optional<Core::GpuUploadBlobId>{};
    }

    usize targetByteCount = 0u;
    if(!ValidatePreparedCausticEmissionTargetUploadState(
        targetCount,
        m_preparedCausticEmissionTargetBytes.size(),
        static_cast<b8>(m_rayTracingState.m_causticEmissionTargetBuffer),
        m_rayTracingState.m_causticEmissionTargetCapacity,
        m_rayTracingState.m_causticEmissionTargetHeapHandle.valid(),
        m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer,
        targetByteCount
    ))
        return MakeUnexpected(Failure{});

    const auto blob = graph.copyUploadData(m_preparedCausticEmissionTargetBytes.data(), targetByteCount, alignof(NwbCausticEmissionTargetGpu));
    if(!blob)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blob};
}

b8 RendererRayTracingSystem::prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena){
    // Emission targets are refractive world bounds. Freeze bytes here; graph declaration keeps only this snapshot.
    m_preparedCausticEmissionTargetBytes.clear();
    m_rayTracingState.m_causticRefractiveInstanceCount = 0u;
    m_unsupportedCausticVolumeCount = 0u;
    m_unsupportedCausticVolumeHash = s_Fnv64OffsetBasis;

    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return true;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    Vector<NwbCausticEmissionTargetGpu, Core::Alloc::ScratchArena> targets{ scratchArena };
    targets.reserve(candidateCount);

    SIMDVectorSingle combinedMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVectorSingle combinedMax = VectorReplicate(-s_RayTracingFiniteInfinity);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        const auto materialInfoResult = m_materialSystem.findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
        if(!materialInfo->refractive)
            continue;

        const auto meshResult = RayTracingDetail::ResolveRenderableMeshResources(*meshSystemPtr, m_meshSystem, entity);
        if(!meshResult)
            continue;
        RecordUnsupportedCausticVolume(entity.id, renderer.opticalBoundaryMode, m_firstUnsupportedCausticVolumeEntity, m_unsupportedCausticVolumeCount, m_unsupportedCausticVolumeHash);
        if(!meshResult->meshResources.csgLocalBounds.valid())
            continue;

        const NWB::Impl::Scene::TransformComponent* transformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transformPtr ? MatrixAffineTransformation(LoadFloat(transformPtr->scale), VectorZero(), LoadFloat(transformPtr->rotation), LoadFloat(transformPtr->position)) : MatrixIdentity();

        SIMDVectorSingle localMin = LoadFloatInt(meshResult->meshResources.csgLocalBounds.minBounds);
        SIMDVectorSingle localMax = LoadFloatInt(meshResult->meshResources.csgLocalBounds.maxBounds);
        ResolveCausticLocalBounds(meshResult->resolvedMesh.runtime, localMin, localMax);
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(!worldBounds)
            continue;
        targets.push_back(MakeCausticEmissionTarget(worldBounds->minBounds, worldBounds->maxBounds, combinedMin, combinedMax));
    }

    const u32 targetCount = static_cast<u32>(targets.size());
    if(targetCount == 0u){
        m_rayTracingState.m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
        m_rayTracingState.m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
        m_rayTracingState.m_causticRefractiveInstanceCount = 0u;
        return true;
    }

    if(!ensureCausticEmissionTargetBuffer(targetCount))
        return false;

    CaptureCausticEmissionTargetsBytes(
        Span<const NwbCausticEmissionTargetGpu>(targets.data(), targets.size()),
        m_preparedCausticEmissionTargetBytes,
        m_rayTracingState.m_causticRefractiveInstanceCount
    );
    StoreFloat(combinedMin, m_rayTracingState.m_causticTargetBoundsMin);
    StoreFloat(combinedMax, m_rayTracingState.m_causticTargetBoundsMax);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

