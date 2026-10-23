// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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

bool RendererRayTracingSystem::createCausticTargets(DeferredFrameTargets& targets){
    // Full-res additive targets with half-res temporal resolve ping-pong.
    targets.causticIrradianceFormat = Core::Format::RGBA16_FLOAT;
    targets.causticAccumulatorFormat = Core::Format::R32_UINT;
    targets.causticHistoryFormat = Core::Format::RGBA16_FLOAT;

    // Fresh accumulators reseed the temporal EMA.
    m_rayTracingState.m_causticAccumulatorInitialized = false;

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;

    Core::TextureDesc irradianceDesc;
    irradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setInUAV(true)
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
        .setArraySize(ECSRenderDetail::s_CausticAccumulatorChannelCount)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.causticAccumulatorFormat)
        .setInUAV(true)
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
    historyDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setInUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/atrous_half_a");
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
    halfBDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setInUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/atrous_half_b");
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
    geometryDesc.setWidth(halfWidth).setHeight(halfHeight).setFormat(targets.causticHistoryFormat).setInUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/caustic/resolve_geometry");
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

    const usize targetByteCount = static_cast<usize>(targetCount) * sizeof(NwbCausticEmissionTargetGpu);
    if(
        m_preparedCausticEmissionTargetBytes.size() != targetByteCount
        || !m_rayTracingState.m_causticEmissionTargetBuffer
        || m_rayTracingState.m_causticEmissionTargetCapacity < targetCount
        || !m_rayTracingState.m_causticEmissionTargetHeapHandle.valid()
        || m_rayTracingState.m_causticEmissionTargetHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen caustic emission-target payload no longer matches preflight storage"));
        return MakeUnexpected(Failure{});
    }

    const auto blob = graph.copyUploadData(m_preparedCausticEmissionTargetBytes.data(), targetByteCount, alignof(NwbCausticEmissionTargetGpu));
    if(!blob)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blob};
}

bool RendererRayTracingSystem::prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena){
    // Photon emission targets are world bounds of refractive instances. Freeze the gathered bytes here; preflight still owns capacity/selection; graph declaration retains only this snapshot.
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
        if(renderer.opticalBoundaryMode != OpticalBoundaryMode::ClosedNested && renderer.opticalBoundaryMode != OpticalBoundaryMode::ClosedPriority){
            if(m_unsupportedCausticVolumeCount == 0u)
                m_firstUnsupportedCausticVolumeEntity = entity.id;
            ++m_unsupportedCausticVolumeCount;
            Fnv64AppendValue(m_unsupportedCausticVolumeHash, entity.id);
            Fnv64AppendValue(m_unsupportedCausticVolumeHash, renderer.opticalBoundaryMode);
        }
        if(!meshResult->meshResources.csgLocalBounds.valid())
            continue;

        const NWB::Impl::Scene::TransformComponent* transformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transformPtr ? MatrixAffineTransformation(LoadFloat(transformPtr->scale), VectorZero(), LoadFloat(transformPtr->rotation), LoadFloat(transformPtr->position)) : MatrixIdentity();

        SIMDVectorSingle localMin = LoadFloatInt(meshResult->meshResources.csgLocalBounds.minBounds);
        SIMDVectorSingle localMax = LoadFloatInt(meshResult->meshResources.csgLocalBounds.maxBounds);
        if(meshResult->resolvedMesh.runtime){
            // Conservative deformation inflation keeps skinned refractors in the emission domain.
            constexpr f32 s_BoundsMidpointWeight = 0.5f;
            const SIMDVectorSingle center = VectorMultiply(VectorAdd(localMin, localMax), VectorReplicate(s_BoundsMidpointWeight));
            const SIMDVectorSingle half = VectorMultiply(VectorSubtract(localMax, localMin), VectorReplicate(s_BoundsMidpointWeight * s_CausticRuntimeBoundsInflation));
            localMin = VectorSubtract(center, half);
            localMax = VectorAdd(center, half);
        }
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(!worldBounds)
            continue;
        const SIMDVectorSingle worldMin = worldBounds->minBounds;
        const SIMDVectorSingle worldMax = worldBounds->maxBounds;

        combinedMin = VectorMin(combinedMin, worldMin);
        combinedMax = VectorMax(combinedMax, worldMax);

        NwbCausticEmissionTargetGpu target;
        StoreFloat(VectorSetW(worldMin, 0.0f), target.aabbMin);
        StoreFloat(VectorSetW(worldMax, 0.0f), target.aabbMax);
        targets.push_back(target);
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

    const usize targetByteCount = targets.size() * sizeof(NwbCausticEmissionTargetGpu);
    m_preparedCausticEmissionTargetBytes.resize(targetByteCount);
    NWB_MEMCPY(m_preparedCausticEmissionTargetBytes.data(), m_preparedCausticEmissionTargetBytes.size(), targets.data(), targetByteCount);

    StoreFloat(combinedMin, m_rayTracingState.m_causticTargetBoundsMin);
    StoreFloat(combinedMax, m_rayTracingState.m_causticTargetBoundsMax);
    m_rayTracingState.m_causticRefractiveInstanceCount = targetCount;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

