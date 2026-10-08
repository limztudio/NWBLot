// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareCausticEmissionTargetResources(Core::Alloc::ScratchArena& scratchArena){
    // Photon emission targets are world bounds of refractive instances. Freeze the gathered bytes here,
    // preflight still owns capacity/descriptor selection; graph declaration retains only this immutable snapshot.
    m_preparedCausticEmissionTargetBytes.clear();
    m_rayTracingState.m_causticRefractiveInstanceCount = 0u;

    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return true;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    Vector<NwbCausticEmissionTargetGpu, Core::Alloc::ScratchArena> targets{ scratchArena };
    targets.reserve(candidateCount);

    SIMDVector combinedMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVector combinedMax = VectorReplicate(-s_RayTracingFiniteInfinity);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        const auto materialInfoResult = m_materialSystem.findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        MaterialSurfaceInfo* const materialInfo = *materialInfoResult;
        if(!materialInfo || !materialInfo->refractive)
            continue;

        const auto meshResult = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystemPtr,
            m_meshSystem,
            entity
        );
        if(!meshResult || !meshResult->meshResources.csgLocalBounds.valid())
            continue;

        const NWB::Impl::Scene::TransformComponent* transformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = transformPtr
            ? MatrixAffineTransformation(
                LoadFloat(transformPtr->scale),
                VectorZero(),
                LoadFloat(transformPtr->rotation),
                LoadFloat(transformPtr->position)
            )
            : MatrixIdentity()
        ;

        SIMDVector localMin = LoadFloatInt(meshResult->meshResources.csgLocalBounds.minBounds);
        SIMDVector localMax = LoadFloatInt(meshResult->meshResources.csgLocalBounds.maxBounds);
        if(meshResult->resolvedMesh.runtime){
            // Conservative deformation inflation keeps skinned refractors in the emission domain.
            constexpr f32 s_BoundsMidpointWeight = 0.5f;
            const SIMDVector center = VectorMultiply(VectorAdd(localMin, localMax), VectorReplicate(s_BoundsMidpointWeight));
            const SIMDVector half = VectorMultiply(VectorSubtract(localMax, localMin), VectorReplicate(s_BoundsMidpointWeight * s_CausticRuntimeBoundsInflation));
            localMin = VectorSubtract(center, half);
            localMax = VectorAdd(center, half);
        }
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(!worldBounds)
            continue;
        const SIMDVector worldMin = worldBounds->minBounds;
        const SIMDVector worldMax = worldBounds->maxBounds;

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
    NWB_MEMCPY(
        m_preparedCausticEmissionTargetBytes.data(),
        m_preparedCausticEmissionTargetBytes.size(),
        targets.data(),
        targetByteCount
    );

    StoreFloat(combinedMin, m_rayTracingState.m_causticTargetBoundsMin);
    StoreFloat(combinedMax, m_rayTracingState.m_causticTargetBoundsMax);
    m_rayTracingState.m_causticRefractiveInstanceCount = targetCount;

    return true;
}

Expected<Core::GpuUploadBlobId> RendererRayTracingSystem::retainPreparedCausticEmissionTargetUpload(
    Core::GpuTaskGraph& graph
)const{
    Core::GpuUploadBlobId blob;
    const u32 targetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
    if(targetCount == 0u){
        if(!m_preparedCausticEmissionTargetBytes.empty())
            return MakeUnexpected(Failure{});
        return blob;
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

    blob = graph.copyUploadData(
        m_preparedCausticEmissionTargetBytes.data(),
        targetByteCount,
        alignof(NwbCausticEmissionTargetGpu)
    );
    if(!(blob.valid()))
        return MakeUnexpected(Failure{});
    return blob;
}

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
    m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;

    Core::TextureDesc irradianceDesc;
    irradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setInUAV(true)
        // Graphics/async production and the graph-owned lagged-history transfer copy share this target concurrently.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/caustic/irradiance")
    ;
    targets.causticIrradiance = m_graphics.createTexture(irradianceDesc);
    if(!targets.causticIrradiance){
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
    targets.causticAccumulator = m_graphics.createTexture(accumulatorDesc);
    if(!targets.causticAccumulator){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator target"));
        return false;
    }

    Core::TextureDesc historyDesc;
    historyDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/atrous_half_a")
    ;
    targets.causticHistory = m_graphics.createTexture(historyDesc);
    if(!targets.causticHistory){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-A target"));
        return false;
    }

    Core::TextureDesc halfBDesc;
    halfBDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/atrous_half_b")
    ;
    targets.causticResolveHalf = m_graphics.createTexture(halfBDesc);
    if(!targets.causticResolveHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-B target"));
        return false;
    }

    // Half-resolution geometry cache for edge-aware resolve.
    Core::TextureDesc geometryDesc;
    geometryDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/caustic/resolve_geometry")
    ;
    targets.causticResolveGeometry = m_graphics.createTexture(geometryDesc);
    if(!targets.causticResolveGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve geometry cache target"));
        return false;
    }
    if(!prepareCausticResolveActivity(halfWidth, halfHeight))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic activity buffers unavailable; retaining full wavelet filtering"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

