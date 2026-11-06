// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::prepareSceneTlasResources(Core::Alloc::ScratchArena& scratchArena){
    using namespace RayTracingSoftwareBvhDetail;

    m_preparedSceneContentStamp = {};
    m_rayTracingState.m_refractionCertifiedInstanceIndex = Limit<u32>::s_Max;

    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return false;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();
    BeginLightSpaceCsgGather(m_lightSpaceShadow.m_csg, m_world, candidateCount, true);
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    // Instance descs hold raw BLAS pointers. The graph-owned TLAS retains handles until Shadow Preparation submits.
    Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena> instanceBlases{ scratchArena };
    // Kept parallel to instances for hardware InstanceID lookup.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // All-occluder trace context; draw buffers hold one transparency class.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedConstantRangeMap shadowConstantTypedRanges(0, ECSRenderDetail::MaterialTypedConstantRangeMap::hasher(), ECSRenderDetail::MaterialTypedConstantRangeMap::key_equal(), scratchArena);
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(0, ECSRenderDetail::MaterialTypedByteContentRangeMap::hasher(), ECSRenderDetail::MaterialTypedByteContentRangeMap::key_equal(), scratchArena);
    Vector<RayTracingInstanceWorldBounds, Core::Alloc::ScratchArena> instanceWorldBounds{ scratchArena };
    instances.reserve(candidateCount);
    instanceBlases.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    instanceWorldBounds.reserve(candidateCount);
    shadowConstantTypedRanges.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);
    MeshBufferSlotLookup meshSlotLookup(0, Hasher<NotNull<Core::Buffer*>>(), EqualTo<NotNull<Core::Buffer*>>(), scratchArena);
    meshSlotLookup.reserve(candidateCount);

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware TLAS build requires the descriptor-buffer TLAS heap layout"));
        return false;
    }
    BeginMeshHeapHandleGather(m_rayTracingState.m_hwMeshHeapHandleCache);

    m_rayTracingState.m_shadowMeshIndexBuffers.clear();
    m_rayTracingState.m_shadowMeshAttributeBuffers.clear();
    m_rayTracingState.m_shadowMeshPositionBuffers.clear();
    m_rayTracingState.m_shadowMeshIndexHandles.clear();
    m_rayTracingState.m_shadowMeshAttributeHandles.clear();
    m_rayTracingState.m_shadowMeshPositionHandles.clear();
    m_rayTracingState.m_shadowMeshCount = 0u;
    // Gates transparent-shadow resource preparation.
    m_rayTracingState.m_sceneHasTransparentOccluder = false;
    b8 staticScene = true;
    b8 contentComplete = true;
    b8 allInstanceBoundsValid = true;
    u32 refractiveInstanceCount = 0u;
    u32 singleRefractiveInstanceIndex = Limit<u32>::s_Max;
    RayTracingOpticalSceneGather opticalScene(scratchArena, candidateCount);

    ShadowMaterialSampledTextureCollector sampledTextureCollector(m_preparedShadowTraceMaterialSampledTextures, scratchArena);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        const auto meshResult = RayTracingDetail::ResolveRenderableMeshResources(*meshSystemPtr, m_meshSystem, entity);
        // An entity without a mesh attachment contributes no geometry to the scene.
        if(!meshResult && meshResult.error() == RenderableMeshResolution::Absent)
            continue;
        if(!meshResult || !meshResult->meshResources.blas || !meshResult->meshResources.triangleIndexBuffer || !meshResult->meshResources.attributeBuffer || !meshResult->meshResources.positionBuffer){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        // Runtime mesh updates disable static TLAS reuse.
        if(meshResult->resolvedMesh.runtime || meshResult->meshResources.runtimeMesh)
            staticScene = false;

        // Reuse one table slot for instances sharing geometry.
        const NotNull<Core::Buffer*> meshIndexBuffer(meshResult->meshResources.triangleIndexBuffer.get());
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshIndexBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            const auto indexHandle = AcquireMeshHeapHandle(meshResult->meshResources.triangleIndexBuffer, heap, m_rayTracingState.m_hwMeshHeapHandleCache);
            if(!indexHandle){
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register HW scene mesh buffers in the global descriptor heap"));

                return false;
            }
            const auto attributeHandle = AcquireMeshHeapHandle(meshResult->meshResources.attributeBuffer, heap, m_rayTracingState.m_hwMeshHeapHandleCache);
            if(!attributeHandle){
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register HW scene mesh buffers in the global descriptor heap"));

                return false;
            }
            const auto positionHandle = AcquireMeshHeapHandle(meshResult->meshResources.positionBuffer, heap, m_rayTracingState.m_hwMeshHeapHandleCache);
            if(!positionHandle){
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register HW scene mesh buffers in the global descriptor heap"));

                return false;
            }

            meshSlot = m_rayTracingState.m_shadowMeshCount;
            m_rayTracingState.m_shadowMeshIndexBuffers.push_back(meshIndexBuffer);
            m_rayTracingState.m_shadowMeshAttributeBuffers.push_back(MakeNotNull(meshResult->meshResources.attributeBuffer.get()));
            m_rayTracingState.m_shadowMeshPositionBuffers.push_back(MakeNotNull(meshResult->meshResources.positionBuffer.get()));
            m_rayTracingState.m_shadowMeshIndexHandles.push_back(*indexHandle);
            m_rayTracingState.m_shadowMeshAttributeHandles.push_back(*attributeHandle);
            m_rayTracingState.m_shadowMeshPositionHandles.push_back(*positionHandle);
            if(!meshSlotLookup.emplace(meshIndexBuffer, meshSlot).second){
                NWB_ASSERT(false);
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);
                return false;
            }
            ++m_rayTracingState.m_shadowMeshCount;
        }

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBottomLevelAS(meshResult->meshResources.blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(s_RayTracingAllInstanceMask);

        const NWB::Impl::Scene::TransformComponent* transformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        SIMDMatrix objectToWorld = MatrixIdentity();
        if(transformPtr){
            objectToWorld = MatrixAffineTransformation(LoadFloat(transformPtr->scale), VectorZero(), LoadFloat(transformPtr->rotation), LoadFloat(transformPtr->position));
            StoreFloat(objectToWorld, instanceDesc.transform);
        }

        // Build material context in InstanceID order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        if(!ResolveShadowOccluderMaterial(
            entity,
            renderer.material,
            transformPtr,
            meshInstanceIndex,
            m_materialSystem,
            sampledTextureCollector,
            shadowMaterialTypedBytes,
            shadowConstantTypedRanges,
            shadowMutableTypedRanges,
            opticalScene,
            contentComplete,
            instanceMaterial,
            shadowInstance,
            &m_rayTracingState.m_sceneHasTransparentOccluder
        ))
            return false;
        instanceMaterial.indexSlot = m_rayTracingState.m_shadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = m_rayTracingState.m_shadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = m_rayTracingState.m_shadowMeshPositionHandles[meshSlot].slot();

        const b8 transparent = (instanceMaterial.flags & RtInstanceMaterialFlag::Transparent) != 0u;
        const b8 refractive = (instanceMaterial.flags & RtInstanceMaterialFlag::Refractive) != 0u;
        // Opaque candidates terminate RayQuery; transparent candidates retain material evaluation.
        if(!transparent)
            instanceDesc.setFlags(Core::RayTracingInstanceFlags::ForceOpaque);
        instanceDesc.setInstanceMask(ResolveRayTracingInstanceMask(transparent, refractive));
        const SIMDVectorSingle localMin = LoadFloatInt(meshResult->meshResources.csgLocalBounds.minBounds);
        const SIMDVectorSingle localMax = LoadFloatInt(meshResult->meshResources.csgLocalBounds.maxBounds);
        if(m_lightSpaceShadow.m_csg.gathering){
            SIMDVectorSingle determinant;
            const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);
            AppendLightSpaceCsgReceiver(m_lightSpaceShadow.m_csg, entity, transparent, objectToWorld, worldToObject, localMin, localMax, meshResult->meshResources);
            m_lightSpaceShadow.m_casters.push_back({
                .triangleIndexBuffer = meshResult->meshResources.triangleIndexBuffer,
                .meshletDescBuffer = meshResult->meshResources.meshletDescBuffer,
                .meshletBoundsBuffer = meshResult->meshResources.meshletLocalBoundsBuffer,
                .instanceIndex = meshInstanceIndex,
                .indexCount = meshResult->meshResources.meshletPrimitiveIndexCount,
                .meshletCount = meshResult->meshResources.meshletCount,
                .meshletDescSlot = meshResult->meshResources.meshletCount != 0u ? meshResult->meshResources.meshletDescHeapHandle.slot() : 0u,
                .meshletBoundsSlot = meshResult->meshResources.meshletCount != 0u ? meshResult->meshResources.meshletLocalBoundsHeapHandle.slot() : 0u,
                .transparent = transparent,
                .csg = false,
            });
            AppendLightSpaceSceneBuffers(meshResult->meshResources, true, m_lightSpaceShadow.m_sceneBuffers);
        }
        SIMDVectorSingle opticalMin = VectorZero();
        SIMDVectorSingle opticalMax = VectorZero();
        b8 opticalBoundsValid = false;
        RayTracingInstanceWorldBounds worldBounds;
        if(!meshResult->resolvedMesh.runtime && !meshResult->meshResources.runtimeMesh && meshResult->meshResources.csgLocalBounds.valid()){
            const auto bounds = ComputeOpticalWorldBounds(objectToWorld, localMin, localMax);
            if(bounds){
                StoreFloat(bounds->minimum, worldBounds.minimum);
                StoreFloat(bounds->maximum, worldBounds.maximum);
                if(transparent){
                    opticalMin = bounds->minimum;
                    opticalMax = bounds->maximum;
                    opticalBoundsValid = true;
                }
            }
            else
                allInstanceBoundsValid = false;
        }
        else
            allInstanceBoundsValid = false;
        if(refractive){
            ++refractiveInstanceCount;
            singleRefractiveInstanceIndex = meshInstanceIndex;
        }
        const b8 runtimeOpticalBounds = transparent && (meshResult->resolvedMesh.runtime || meshResult->meshResources.runtimeMesh) && meshResult->meshResources.runtimeLocalBoundsBuffer && meshResult->meshResources.runtimeLocalBoundsHeapHandle.valid();
        if(runtimeOpticalBounds)
            opticalScene.appendRuntime(entity, renderer, meshResult->meshResources.runtimeLocalBoundsBuffer, meshResult->meshResources.runtimeLocalBoundsHeapHandle, instanceDesc.transform);
        else
            opticalScene.append(entity, renderer, transparent, opticalMin, opticalMax, opticalBoundsValid);

        instances.push_back(instanceDesc);
        instanceBlases.push_back(meshResult->meshResources.blas);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
        instanceWorldBounds.push_back(worldBounds);
    }

    if(!FinishLightSpaceCsgGather(m_csgShapeRegistry, m_lightSpaceShadow.m_csg, m_world, scratchArena))
        return false;
    const auto& csg = m_lightSpaceShadow.m_csg.snapshot;
    if(csg.hasCsg){
        if(csg.receiverRanges.size() != instances.size())
            return false;
        for(u32 index = 0u; index < static_cast<u32>(instances.size()); ++index){
            if((csg.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) == 0u)
                continue;
            const b8 transparent = (instanceMaterials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u;
            instances[index].setInstanceMask(ResolveRayTracingCsgInstanceMask(transparent));
            instanceMaterials[index].flags |= NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW;
            m_lightSpaceShadow.m_casters[index].csg = true;
        }
        m_rayTracingState.m_sceneHasTransparentOccluder = true;
    }
    m_lightSpaceShadow.m_sceneEligible = csg.hasCsg && contentComplete;
    if(!csg.hasCsg){
        m_lightSpaceShadow.m_casters.clear();
        m_lightSpaceShadow.m_sceneBuffers.clear();
    }

    LogShadowMeshHeapHighWaterIfNeeded(
        true,
        m_rayTracingState.m_shadowMeshCount,
        m_rayTracingState.m_shadowMeshHeapHighWater
    );
    SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);

    m_rayTracingState.m_tlasInstanceCount = static_cast<u32>(instances.size());
    if(instances.empty()){
        m_rayTracingState.m_tlasStaticSceneHashValid = false;
        m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
        return false;
    }

    if(staticScene && opticalScene.boundsCompleteExceptRuntime && !csg.hasCsg && allInstanceBoundsValid && refractiveInstanceCount == 1u && singleRefractiveInstanceIndex < instances.size()){
        if(IsRefractiveInstanceBoundsIsolated(instanceWorldBounds, singleRefractiveInstanceIndex)){
            instanceMaterials[singleRefractiveInstanceIndex].flags |= RtInstanceMaterialFlag::RefractionEntryCertified;
            m_rayTracingState.m_refractionCertifiedInstanceIndex = singleRefractiveInstanceIndex;
        }
    }

    // TLAS and material context use separate static keys.
    const u64 tlasStaticSceneHash = staticScene ? ComputeTlasStaticSceneHash(instances) : 0u;
    const b8 canReuseTlas =
        staticScene
        && m_rayTracingState.m_tlasStaticSceneHashValid
        && m_rayTracingState.m_tlasStaticSceneHash == tlasStaticSceneHash
        && m_rayTracingState.m_tlas
        && m_rayTracingState.m_tlasMaxInstances >= instances.size()
        && RayTracingDetail::IsHeapHandle(m_rayTracingState.m_tlasHeapHandle, Core::GpuDescriptorClass::AccelStruct)
    ;
    if(!staticScene || !canReuseTlas)
        m_rayTracingState.m_tlasStaticSceneHashValid = false;

    if(!canReuseTlas && (!m_rayTracingState.m_tlas || m_rayTracingState.m_tlasMaxInstances < instances.size())){
        const usize capacity = ::NextGrowingCapacity(m_rayTracingState.m_tlasMaxInstances, instances.size(), s_TlasInitialInstanceCapacity);

        Core::RayTracingAccelStructDesc accelStructDesc(m_arena);
        accelStructDesc.setTopLevelMaxInstances(capacity);
        accelStructDesc.setBuildFlags(Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
        accelStructDesc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        accelStructDesc.setDebugName(s_SceneTlasDebugName);

        auto& device = m_graphics.getDevice();
        auto tlasResult = device.createAccelStruct(accelStructDesc);
        if(!tlasResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene TLAS (capacity {})"), static_cast<u64>(capacity));
            return false;
        }
        Core::RayTracingAccelStructHandle tlas = Move(*tlasResult);
        // Retire the old heap block before replacing the TLAS generation.
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_tlasHeapHandle);
        m_rayTracingState.m_tlas = Move(tlas);
        InitializeCreatedSceneTlasState(
            capacity,
            m_rayTracingState.m_tlasBackingFresh,
            m_rayTracingState.m_tlasMaxInstances
        );
    }

    // Allocate a heap block only for a new TLAS generation.
    if(!RayTracingDetail::IsHeapHandle(m_rayTracingState.m_tlasHeapHandle, Core::GpuDescriptorClass::AccelStruct)){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_tlasHeapHandle);
        if(!RendererDescriptorRegisterDetail::AssignDescriptor(
            heap,
            Core::GpuDescriptorClass::AccelStruct,
            Core::DescriptorWriteItem::RayTracingAccelStruct(0u, m_rayTracingState.m_tlas.get()),
            m_rayTracingState.m_tlasHeapHandle
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register scene TLAS in the descriptor-buffer heap"));
            return false;
        }
    }

    // Preserve a valid typed buffer and hash the descriptor-slot representation.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    const usize materialTypedUploadBytes = ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes);
    const u64 hwMaterialContextHash = ComputeShadowMaterialContextHash(instanceMaterials, shadowInstanceData, shadowMaterialTypedBytes);
    const b8 canReuseHwMaterialContext =
        staticScene
        && !m_rayTracingState.m_sceneHasTransparentOccluder
        && m_rayTracingState.m_hwShadowMaterialContextHashValid
        && m_rayTracingState.m_hwShadowMaterialContextHash == hwMaterialContextHash
        && HasPreparedShadowMaterialContextBuffers(m_rayTracingState, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes)
    ;
    if(!canReuseHwMaterialContext){
        if(
            !ensureShadowInstanceMaterialBuffer(instances.size())
            || !ensureShadowInstanceContextBuffer(shadowInstanceData.size())
            || !ensureShadowMaterialTypedBuffer(materialTypedUploadBytes)
            || !HasPreparedShadowMaterialContextBuffers(m_rayTracingState, instanceMaterials.size(), shadowInstanceData.size(), materialTypedUploadBytes)
        )
            return false;
        if(!capturePreparedShadowMaterialContext(PreparedShadowMaterialContextRoute::Hardware, staticScene, hwMaterialContextHash, instanceMaterials, shadowInstanceData, Span<const u8>(shadowMaterialTypedBytes.data(), materialTypedUploadBytes)))
            return false;
    }
    // Freeze the selected hardware instance stream before recording; accepted preparation publishes its cache identity.
    if(!canReuseTlas){
        if(!m_preparedSceneTlas.capture(m_rayTracingState, staticScene, tlasStaticSceneHash, instances, instanceBlases)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: could not freeze scene TLAS build after preflight"));
            return false;
        }
    }

    // Publish semantic identity from the complete current gather, including cache-hit frames.
    // Missing geometry or an unresolved surface hook must disable temporal consumers without changing acceleration-cache policy.
    if(!m_hardwareOpticalScene.prepare(opticalScene) || !m_hardwareOpticalScene.prepareRuntimeBounds(opticalScene, m_shaderSystem))
        return false;
    m_preparedSceneContentStamp = BuildSwSceneContentStamp(
        tlasStaticSceneHash,
        hwMaterialContextHash,
        opticalScene.contentHash(),
        csg.hasCsg,
        csg.identity,
        staticScene,
        contentComplete
    );
    if(csg.hasCsg){
        u64 captureIdentity = m_preparedSceneContentStamp.material;
        Fnv64AppendValue(captureIdentity, tlasStaticSceneHash);
        Fnv64AppendValue(captureIdentity, csg.identity);
        for(const auto& buffer : m_lightSpaceShadow.m_sceneBuffers)
            Fnv64AppendValue(captureIdentity, buffer.get());
        for(const auto& caster : m_lightSpaceShadow.m_casters)
            UpdateHwLightSpaceCasterCaptureHash(caster.meshletCount, caster.meshletDescSlot, caster.meshletBoundsSlot, captureIdentity);
        b8 captureTrusted = staticScene && contentComplete;
        if(m_lightSpaceShadow.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseOneFrame || m_lightSpaceShadow.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseTwoFrames){
            captureIdentity = BuildLightSpaceCsgCaptureIdentity(m_lightSpaceShadow.m_csg, instanceMaterials, shadowInstanceData, shadowMaterialTypedBytes, m_preparedShadowTraceMaterialSampledTextures);
            captureTrusted = m_lightSpaceShadow.m_csg.captureGeometryTrusted && contentComplete;
        }
        m_lightSpaceShadow.m_captureSceneIdentity = captureIdentity;
        m_lightSpaceShadow.m_captureSceneTrusted = captureTrusted;
        m_lightSpaceShadow.m_sceneTextures.assign(m_preparedShadowTraceMaterialSampledTextures.begin(), m_preparedShadowTraceMaterialSampledTextures.end());
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
