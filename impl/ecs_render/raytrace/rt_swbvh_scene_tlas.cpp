// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/mesh_acceleration_update.h>
#include <impl/ecs_csg/components.h>
#include <global/algorithm.h>
#include <global/hash_utils.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareSceneTlasResources(Core::Alloc::ScratchArena& scratchArena){
    using namespace RayTracingSoftwareBvhDetail;

    m_preparedSceneContentStamp = {};

    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return false;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();
    BeginLightSpaceCsgGather(m_lightSpaceShadow.m_csg, m_world, candidateCount, true);
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    // RayTracingInstanceDesc contains only a raw BLAS pointer. The opaque graph-owned TLAS build retains this parallel handle stream until the accepting Shadow Preparation packet has submitted.
    Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena> instanceBlases{ scratchArena };
    // Kept parallel to instances for hardware InstanceID lookup.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // All-occluder trace context; draw buffers hold one transparency class.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(candidateCount);
    instanceBlases.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);
    MeshBufferSlotLookup meshSlotLookup(
        0,
        Hasher<const Core::Buffer*>(),
        EqualTo<const Core::Buffer*>(),
        scratchArena
    );
    meshSlotLookup.reserve(candidateCount);

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(GLB_TEXT("RendererSystem: hardware TLAS build requires the descriptor-buffer TLAS heap layout"));
        return false;
    }
    BeginMeshHeapHandleGather(m_rayTracingState.m_hwMeshHeapHandleCache);
    const auto resolveMeshHeapHandle = [&](const Core::BufferHandle& buffer, Core::GpuDescriptorHandle& outHandle){
        return AcquireMeshHeapHandle(heap, m_rayTracingState.m_hwMeshHeapHandleCache, buffer, outHandle);
    };
    m_rayTracingState.m_shadowMeshIndexBuffers.clear();
    m_rayTracingState.m_shadowMeshAttributeBuffers.clear();
    m_rayTracingState.m_shadowMeshPositionBuffers.clear();
    m_rayTracingState.m_shadowMeshIndexHandles.clear();
    m_rayTracingState.m_shadowMeshAttributeHandles.clear();
    m_rayTracingState.m_shadowMeshPositionHandles.clear();
    m_rayTracingState.m_shadowMeshCount = 0u;
    // Gates transparent-shadow resource preparation.
    m_rayTracingState.m_sceneHasTransparentOccluder = false;
    bool staticScene = true;
    bool contentComplete = true;
    RayTracingOpticalSceneGather opticalScene(scratchArena, candidateCount);

    ShadowMaterialSampledTextureCollector sampledTextureCollector(m_preparedShadowTraceMaterialSampledTextures, scratchArena);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
        RenderableMeshDesc resolvedMesh;
        const RenderableMeshResolution::Enum meshResolution = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystemPtr,
            m_meshSystem,
            entity,
            resolvedMesh,
            mesh
        );
        // An entity without a mesh attachment contributes no geometry to the scene.
        if(meshResolution == RenderableMeshResolution::Absent)
            continue;
        if(
            meshResolution != RenderableMeshResolution::Ready || !mesh.blas
            || !mesh.triangleIndexBuffer || !mesh.attributeBuffer || !mesh.positionBuffer
        ){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        // Runtime mesh updates disable static TLAS reuse.
        if(resolvedMesh.runtime || mesh.runtimeMesh)
            staticScene = false;

        // Reuse one table slot for instances sharing geometry.
        Core::Buffer* meshIndexBuffer = mesh.triangleIndexBuffer.get();
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshIndexBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            Core::GpuDescriptorHandle indexHandle;
            Core::GpuDescriptorHandle attributeHandle;
            Core::GpuDescriptorHandle positionHandle;
            if(
                !resolveMeshHeapHandle(mesh.triangleIndexBuffer, indexHandle)
                || !resolveMeshHeapHandle(mesh.attributeBuffer, attributeHandle)
                || !resolveMeshHeapHandle(mesh.positionBuffer, positionHandle)
            ){
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);
                NWB_LOGGER_ERROR(GLB_TEXT("RendererSystem: failed to register HW scene mesh buffers in the global descriptor heap"));

                return false;
            }

            meshSlot = m_rayTracingState.m_shadowMeshCount;
            m_rayTracingState.m_shadowMeshIndexBuffers.push_back(meshIndexBuffer);
            m_rayTracingState.m_shadowMeshAttributeBuffers.push_back(mesh.attributeBuffer.get());
            m_rayTracingState.m_shadowMeshPositionBuffers.push_back(mesh.positionBuffer.get());
            m_rayTracingState.m_shadowMeshIndexHandles.push_back(indexHandle);
            m_rayTracingState.m_shadowMeshAttributeHandles.push_back(attributeHandle);
            m_rayTracingState.m_shadowMeshPositionHandles.push_back(positionHandle);
            meshSlotLookup.emplace(meshIndexBuffer, meshSlot);
            ++m_rayTracingState.m_shadowMeshCount;
        }

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBLAS(mesh.blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(s_RayTracingAllInstanceMask);

        const NWB::Impl::Scene::TransformComponent* transformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        if(transformPtr){
            const SIMDMatrix instanceWorld = MatrixAffineTransformation(
                LoadFloat(transformPtr->scale),
                VectorZero(),
                LoadFloat(transformPtr->rotation),
                LoadFloat(transformPtr->position)
            );
            StoreFloat(instanceWorld, instanceDesc.transform);
        }

        // Build material context in InstanceID order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_materialSystem.findMaterialSurfaceInfo(renderer.material, materialInfo)){
            if(materialInfo->transparent)
                m_rayTracingState.m_sceneHasTransparentOccluder = true;
            // The trace surface dispatcher reads this material's Texture2D fields through non-uniform bindless slots. Retain the exact resolved handles during preflight
            if(
                materialInfo->surfaceDispatchId != Limit<u32>::s_Max
                && !appendPreparedShadowTraceMaterialSampledTextures(*materialInfo, sampledTextureCollector)
            )
                return false;
            u32 materialConstantByteOffset = 0u;
            if(!m_materialSystem.appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transformPtr,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = RayTracingDetail::ResolveInstanceShadowMaterial(*materialInfo, materialConstantByteOffset, meshInstanceIndex);
        }
        if(!materialInfo || materialInfo->surfaceDispatchId == Limit<u32>::s_Max)
            contentComplete = false;
        // An opaque surface hook can be unavailable without hiding an optical boundary. Unknown classification or an unevaluable transparent surface cannot support the outside-volume shortcut.
        if(!materialInfo || (materialInfo->transparent && materialInfo->surfaceDispatchId == Limit<u32>::s_Max))
            opticalScene.markIncomplete();
        instanceMaterial.indexSlot = m_rayTracingState.m_shadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = m_rayTracingState.m_shadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = m_rayTracingState.m_shadowMeshPositionHandles[meshSlot].slot();

        // Opaque candidates terminate RayQuery; transparent candidates retain material evaluation.
        if(!(materialInfo && materialInfo->transparent))
            instanceDesc.setFlags(Core::RayTracingInstanceFlags::ForceOpaque);

        const bool transparent = (instanceMaterial.flags & RtInstanceMaterialFlag::Transparent) != 0u;
        instanceDesc.setInstanceMask(NWB_RT_OPTICAL_BASE_INSTANCE_MASK | NWB_RT_SHADOW_BASE_INSTANCE_MASK
            | (transparent ? NWB_RT_OPTICAL_TRANSPARENT_INSTANCE_MASK | NWB_RT_SHADOW_TRANSPARENT_INSTANCE_MASK : 0u));
        if(m_lightSpaceShadow.m_csg.gathering){
            AppendLightSpaceCsgReceiver(m_lightSpaceShadow.m_csg, entity, transparent, LoadFloat(instanceDesc.transform), mesh);
            m_lightSpaceShadow.m_casters.push_back({
                .triangleIndexBuffer = mesh.triangleIndexBuffer,
                .meshletDescBuffer = mesh.meshletDescBuffer,
                .meshletBoundsBuffer = mesh.meshletLocalBoundsBuffer,
                .instanceIndex = meshInstanceIndex,
                .indexCount = mesh.meshletPrimitiveIndexCount,
                .meshletCount = mesh.meshletCount,
                .meshletDescSlot = mesh.meshletCount != 0u ? mesh.meshletDescHeapHandle.slot() : 0u,
                .meshletBoundsSlot = mesh.meshletCount != 0u ? mesh.meshletLocalBoundsHeapHandle.slot() : 0u,
                .transparent = transparent,
                .csg = false,
            });
            m_lightSpaceShadow.m_sceneBuffers.push_back(mesh.positionBuffer);
            m_lightSpaceShadow.m_sceneBuffers.push_back(mesh.triangleIndexBuffer);
            m_lightSpaceShadow.m_sceneBuffers.push_back(mesh.attributeBuffer);
            if(mesh.meshletCount != 0u){
                m_lightSpaceShadow.m_sceneBuffers.push_back(mesh.meshletDescBuffer);
                m_lightSpaceShadow.m_sceneBuffers.push_back(mesh.meshletLocalBoundsBuffer);
            }
            if(mesh.runtimeLocalBoundsBuffer)
                m_lightSpaceShadow.m_sceneBuffers.push_back(mesh.runtimeLocalBoundsBuffer);
            if(mesh.swBvhNodeBuffer)
                m_lightSpaceShadow.m_sceneBuffers.push_back(mesh.swBvhNodeBuffer);
        }
        Float3U opticalLocalMin{};
        Float3U opticalLocalMax{};
        StoreFloat(LoadFloatInt(mesh.csgLocalBounds.minBounds), opticalLocalMin);
        StoreFloat(LoadFloatInt(mesh.csgLocalBounds.maxBounds), opticalLocalMax);
        Float34U opticalWorld{};
        StoreFloat(LoadFloat(instanceDesc.transform), opticalWorld);
        Float3U opticalMin{};
        Float3U opticalMax{};
        const bool opticalBoundsValid =
            transparent && !resolvedMesh.runtime && !mesh.runtimeMesh && mesh.csgLocalBounds.valid()
            && !m_world.tryGetComponent<StaticCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<CsgReceiverComponent>(entity)
            && ComputeOpticalWorldBounds(opticalWorld, opticalLocalMin, opticalLocalMax, opticalMin, opticalMax)
        ;
        const bool runtimeOpticalBounds =
            transparent && (resolvedMesh.runtime || mesh.runtimeMesh) && mesh.runtimeLocalBoundsBuffer
            && mesh.runtimeLocalBoundsHeapHandle.valid()
            && !m_world.tryGetComponent<StaticCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<CsgReceiverComponent>(entity)
        ;
        if(runtimeOpticalBounds)
            opticalScene.appendRuntime(entity, renderer, mesh.runtimeLocalBoundsBuffer, mesh.runtimeLocalBoundsHeapHandle, opticalWorld);
        else
            opticalScene.append(entity, renderer, transparent, opticalMin, opticalMax, opticalBoundsValid);

        instances.push_back(instanceDesc);
        instanceBlases.push_back(mesh.blas);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    if(!FinishLightSpaceCsgGather(m_lightSpaceShadow.m_csg, m_world, m_csgShapeRegistry, scratchArena))
        return false;
    const auto& csg = m_lightSpaceShadow.m_csg.snapshot;
    if(csg.hasCsg){
        if(csg.receiverRanges.size() != instances.size())
            return false;
        for(u32 index = 0u; index < static_cast<u32>(instances.size()); ++index){
            if((csg.receiverRanges[index].flags & NWB_CSG_SHADOW_RECEIVER_ACTIVE) == 0u)
                continue;
            const bool transparent = (instanceMaterials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u;
            instances[index].setInstanceMask(NWB_RT_OPTICAL_BASE_INSTANCE_MASK
                | (transparent ? NWB_RT_OPTICAL_TRANSPARENT_INSTANCE_MASK : 0u));
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

    if(m_rayTracingState.m_shadowMeshCount > m_rayTracingState.m_shadowMeshHeapHighWater){
        m_rayTracingState.m_shadowMeshHeapHighWater = m_rayTracingState.m_shadowMeshCount;
        NWB_LOGGER_INFO(GLB_TEXT("RendererSystem: HW-shadow heap registration high-water: {} distinct meshes -> {} handles")
            , static_cast<u64>(m_rayTracingState.m_shadowMeshCount)
            , static_cast<u64>(m_rayTracingState.m_shadowMeshCount) * s_HardwareRayTracingMeshBufferCount
        );
    }
    SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);

    m_rayTracingState.m_tlasInstanceCount = static_cast<u32>(instances.size());
    if(instances.empty()){
        m_rayTracingState.m_tlasDeviceAddress = 0u;
        m_rayTracingState.m_tlasStaticSceneHashValid = false;
        m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
        return false;
    }

    // TLAS and material context use separate static keys.
    const u64 tlasStaticSceneHash = staticScene ? ComputeTlasStaticSceneHash(instances) : 0u;
    const bool canReuseTlas =
        staticScene
        && m_rayTracingState.m_tlasStaticSceneHashValid
        && m_rayTracingState.m_tlasStaticSceneHash == tlasStaticSceneHash
        && m_rayTracingState.m_tlas
        && m_rayTracingState.m_tlasMaxInstances >= instances.size()
        && IsAccelStructHeapHandle(m_rayTracingState.m_tlasHeapHandle)
    ;
    if(!staticScene || !canReuseTlas)
        m_rayTracingState.m_tlasStaticSceneHashValid = false;

    if(!canReuseTlas && (!m_rayTracingState.m_tlas || m_rayTracingState.m_tlasMaxInstances < instances.size())){
        const usize capacity = ::NextGrowingCapacity(
            m_rayTracingState.m_tlasMaxInstances,
            instances.size(),
            s_TlasInitialInstanceCapacity
        );

        Core::RayTracingAccelStructDesc accelStructDesc(m_arena);
        accelStructDesc.setTopLevelMaxInstances(capacity);
        accelStructDesc.setBuildFlags(Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
        accelStructDesc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        accelStructDesc.setDebugName(Name("scene_tlas"));

        auto& device = m_graphics.getDevice();
        Core::RayTracingAccelStructHandle tlas = device.createAccelStruct(accelStructDesc);
        if(!tlas){
            NWB_LOGGER_ERROR(GLB_TEXT("RendererSystem: failed to create scene TLAS (capacity {})"), static_cast<u64>(capacity));
            return false;
        }
        // Retire the old heap block before replacing the TLAS generation.
        if(m_rayTracingState.m_tlasHeapHandle.valid()){
            heap.free(m_rayTracingState.m_tlasHeapHandle);
            m_rayTracingState.m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        m_rayTracingState.m_tlas = Move(tlas);
        m_rayTracingState.m_tlasBackingFresh = true;
        m_rayTracingState.m_tlasMaxInstances = capacity;
        NWB_LOGGER_INFO(GLB_TEXT("RendererSystem: created scene TLAS (capacity {} instances)"), static_cast<u64>(capacity));
    }

    m_rayTracingState.m_tlasDeviceAddress = m_rayTracingState.m_tlas->getDeviceAddress();

    // Allocate a heap block only for a new TLAS generation.
    if(!IsAccelStructHeapHandle(m_rayTracingState.m_tlasHeapHandle)){
        if(m_rayTracingState.m_tlasHeapHandle.valid()){
            heap.free(m_rayTracingState.m_tlasHeapHandle);
            m_rayTracingState.m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        const Core::GpuDescriptorHandle tlasHeapHandle = heap.allocate(Core::GpuDescriptorClass::AccelStruct);
        if(
            !tlasHeapHandle.valid()
            || !heap.write(tlasHeapHandle, Core::DescriptorWriteItem::rayTracingAccelStruct(0u, m_rayTracingState.m_tlas.get()))
        ){
            NWB_LOGGER_ERROR(GLB_TEXT("RendererSystem: failed to register scene TLAS in the descriptor-buffer heap"));
            if(tlasHeapHandle.valid())
                heap.free(tlasHeapHandle);
            return false;
        }
        m_rayTracingState.m_tlasHeapHandle = tlasHeapHandle;
    }

    // Preserve a valid typed buffer and hash the descriptor-slot representation.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    usize materialTypedUploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes, materialTypedUploadBytes))
        return false;
    const u64 hwMaterialContextHash = ComputeShadowMaterialContextHash(
        instanceMaterials,
        shadowInstanceData,
        shadowMaterialTypedBytes
    );
    u64 gatheredMaterialContentHash = hwMaterialContextHash;
    const bool canReuseHwMaterialContext =
        staticScene
        && !m_rayTracingState.m_sceneHasTransparentOccluder
        && m_rayTracingState.m_hwShadowMaterialContextHashValid
        && m_rayTracingState.m_hwShadowMaterialContextHash == hwMaterialContextHash
        && HasPreparedShadowMaterialContextBuffers(
            m_rayTracingState,
            instanceMaterials.size(),
            shadowInstanceData.size(),
            materialTypedUploadBytes
        )
    ;
    if(!canReuseHwMaterialContext){
        if(
            !ensureShadowInstanceMaterialBuffer(instances.size())
            || !ensureShadowInstanceContextBuffer(shadowInstanceData.size())
            || !ensureShadowMaterialTypedBuffer(materialTypedUploadBytes)
            || !HasPreparedShadowMaterialContextBuffers(
                m_rayTracingState,
                instanceMaterials.size(),
                shadowInstanceData.size(),
                materialTypedUploadBytes
            )
        )
            return false;
        if(!capturePreparedShadowMaterialContext(
            PreparedShadowMaterialContextRoute::Hardware,
            staticScene,
            hwMaterialContextHash,
            instanceMaterials.data(),
            instanceMaterials.size(),
            instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu),
            shadowInstanceData.data(),
            shadowInstanceData.size(),
            shadowInstanceData.size() * sizeof(InstanceGpuData),
            shadowMaterialTypedBytes.data(),
            materialTypedUploadBytes
        ))
            return false;

    }
    // Freeze the selected hardware instance stream before recording; accepted preparation publishes its cache identity.
    if(!canReuseTlas){
        if(!capturePreparedSceneTlasBuild(staticScene, tlasStaticSceneHash, instances, instanceBlases)){
            NWB_LOGGER_ERROR(GLB_TEXT("RendererSystem: could not freeze scene TLAS build after preflight"));
            return false;
        }
    }

    // Publish semantic identity from the complete current gather, including cache-hit frames.
    // Missing geometry or an unresolved surface hook must disable temporal consumers without changing acceleration-cache policy.
    if(!m_hardwareOpticalScene.prepare(opticalScene) || !m_hardwareOpticalScene.prepareRuntimeBounds(opticalScene, m_shaderSystem))
        return false;
    Fnv64AppendValue(gatheredMaterialContentHash, opticalScene.contentHash());
    m_preparedSceneContentStamp = { tlasStaticSceneHash, gatheredMaterialContentHash, staticScene && contentComplete };
    if(csg.hasCsg){
        u64 captureIdentity = gatheredMaterialContentHash;
        Fnv64AppendValue(captureIdentity, tlasStaticSceneHash);
        Fnv64AppendValue(captureIdentity, csg.identity);
        for(const auto& buffer : m_lightSpaceShadow.m_sceneBuffers)
            Fnv64AppendValue(captureIdentity, buffer.get());
        for(const auto& caster : m_lightSpaceShadow.m_casters){
            Fnv64AppendValue(captureIdentity, caster.meshletCount);
            Fnv64AppendValue(captureIdentity, caster.meshletDescSlot);
            Fnv64AppendValue(captureIdentity, caster.meshletBoundsSlot);
        }
        bool captureTrusted = staticScene && contentComplete;
        if(
            m_lightSpaceShadow.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseOneFrame
            || m_lightSpaceShadow.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseTwoFrames
        ){
            captureIdentity = BuildLightSpaceCsgCaptureIdentity(
                m_lightSpaceShadow.m_csg, instanceMaterials.data(), shadowInstanceData.data(), instanceMaterials.size(),
                shadowMaterialTypedBytes.data(), shadowMaterialTypedBytes.size(),
                m_preparedShadowTraceMaterialSampledTextures.data(), m_preparedShadowTraceMaterialSampledTextures.size()
            );
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

